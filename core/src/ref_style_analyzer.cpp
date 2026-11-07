#include "nairoclip/core/ref_style_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#endif

namespace nairoclip {
namespace core {

namespace {

double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

double medianOf(std::vector<double> v) {
    if (v.empty()) return 0.0;
    const std::size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(mid), v.end());
    return v[mid];
}

/// 一次「切点事件」：连续若干帧差异都超过阈值。
struct EventSpan {
    std::size_t first = 0;   // 第一个超阈值样本
    std::size_t last = 0;    // 最后一个
    double peakDiff = 0.0;
};

/// 判断一段事件属于哪种转场。
///
/// 这里的判断全部基于可测量的几何量（差异幅度、差异重心、细节能量、
/// 亮度曲线），不是「认出了某个软件的特效」。所以输出是**物理类型**
/// 加一个置信度，UI 可以据此显示「确定 / 疑似」。
RefTransition classifyEvent(const std::vector<FrameSample>& s, const EventSpan& span,
                            double baselineLuma) {
    RefTransition t;
    t.atSeconds = s[span.first].timeSeconds;
    t.durationSeconds = std::max(0.0, s[span.last].timeSeconds - s[span.first].timeSeconds);

    const std::size_t n = span.last - span.first + 1;
    const double before = span.first > 0 ? s[span.first - 1].luma : baselineLuma;

    // 事件窗口内的亮度极值
    double maxLuma = 0.0;
    double minLuma = 1.0;
    double lumaDelta = 0.0;
    double detailMin = 1.0;
    double detailMax = 0.0;
    double concentrationMax = 0.0;
    for (std::size_t i = span.first; i <= span.last; ++i) {
        maxLuma = std::max(maxLuma, s[i].luma);
        minLuma = std::min(minLuma, s[i].luma);
        detailMin = std::min(detailMin, s[i].detailEnergy);
        detailMax = std::max(detailMax, s[i].detailEnergy);
        concentrationMax = std::max(concentrationMax, s[i].diffConcentration);
    }
    lumaDelta = std::fabs(s[span.last].luma - before);

    // ---- 闪白 / 闪黑：亮度尖峰后回落 ----
    // 判据：窗口内亮度峰值明显高于「事件前后」的基线，且末尾已经回落。
    const double after = span.last + 1 < s.size() ? s[span.last + 1].luma : s[span.last].luma;
    const bool lumaSpike = (maxLuma - before > 0.30 || before - minLuma > 0.30) &&
                           std::fabs(after - before) < 0.18;
    if (lumaSpike) {
        t.kind = RefTransitionKind::Flash;
        t.confidence = clamp01(0.55 + (maxLuma - before) * 0.8);
        return t;
    }

    // ---- 单帧跳变 = 硬切 ----
    if (n == 1) {
        t.kind = RefTransitionKind::HardCut;
        // 跳变越剧烈越确定；极轻微的变化只给到「疑似」
        t.confidence = clamp01(0.45 + span.peakDiff * 1.6);
        return t;
    }

    // ---- 差异集中成窄带 = 擦除 ----
    if (concentrationMax > 0.55) {
        t.kind = RefTransitionKind::Wipe;
        t.confidence = clamp01(0.5 + (concentrationMax - 0.55) * 1.5);
        return t;
    }

    // ---- 差异重心持续朝一个方向移动 = 滑动 / 甩镜 ----
    double dx = 0.0;
    double dy = 0.0;
    std::size_t moved = 0;
    for (std::size_t i = span.first + 1; i <= span.last; ++i) {
        if (s[i].diffCentroidX < 0.0 || s[i - 1].diffCentroidX < 0.0) continue;
        dx += s[i].diffCentroidX - s[i - 1].diffCentroidX;
        dy += s[i].diffCentroidY - s[i - 1].diffCentroidY;
        ++moved;
    }
    if (moved >= 2) {
        const double travel = std::sqrt(dx * dx + dy * dy);
        if (travel > 0.12) {
            // 甩镜的特征：位移大，同时画面高频细节被方向性模糊吃掉
            const double detailDrop = detailMax > 1e-6 ? (detailMax - detailMin) / detailMax : 0.0;
            if (detailDrop > 0.30) {
                t.kind = RefTransitionKind::WhipPan;
                t.confidence = clamp01(0.5 + detailDrop * 0.7);
            } else {
                t.kind = RefTransitionKind::Slide;
                t.confidence = clamp01(0.5 + travel * 1.2);
            }
            return t;
        }
    }

    // ---- 差异从中心向外扩散 = 缩放 ----
    // 判据：窗口首尾的「离中心距离」在增大，且差异不集中（不是擦除）。
    if (n >= 2 && s[span.first].diffCentroidX >= 0.0 && s[span.last].diffCentroidX >= 0.0) {
        const auto radius = [](const FrameSample& f) {
            const double cx = f.diffCentroidX - 0.5;
            const double cy = f.diffCentroidY - 0.5;
            return std::sqrt(cx * cx + cy * cy);
        };
        const double r0 = radius(s[span.first]);
        const double r1 = radius(s[span.last]);
        if (r1 - r0 > 0.06 && concentrationMax < 0.40) {
            t.kind = RefTransitionKind::Zoom;
            t.confidence = clamp01(0.45 + (r1 - r0) * 2.0);
            return t;
        }
    }

    // ---- 细节能量突增（条带错位会打碎梯度）= 故障 ----
    if (detailMax > 0.02 && detailMin > 1e-6 && detailMax / detailMin > 2.2) {
        t.kind = RefTransitionKind::Glitch;
        t.confidence = 0.35;   // 弱判据：只能算「疑似」
        return t;
    }

    // ---- 其余多帧渐变 = 溶解 ----
    t.kind = RefTransitionKind::Dissolve;
    // 渐变越长越平缓越像叠化；伴随大亮度变化时置信度降低（可能是别的）
    t.confidence = clamp01(0.55 + (0.25 - std::fabs(lumaDelta)) * 0.8);
    return t;
}

}  // namespace

const char* refTransitionName(RefTransitionKind kind) {
    switch (kind) {
        case RefTransitionKind::HardCut: return "硬切";
        case RefTransitionKind::Dissolve: return "溶解";
        case RefTransitionKind::Slide: return "滑动";
        case RefTransitionKind::WhipPan: return "甩镜";
        case RefTransitionKind::Zoom: return "缩放";
        case RefTransitionKind::Rotate: return "旋转";
        case RefTransitionKind::Flash: return "闪光";
        case RefTransitionKind::Glitch: return "故障";
        case RefTransitionKind::Wipe: return "擦除";
        default: return "未知";
    }
}

void analyzeFrameSamples(const std::vector<FrameSample>& samples, RefStyleProfile& out) {
    out = RefStyleProfile{};
    if (samples.empty()) return;

    // ---------- 1) 全片色彩画像 ----------
    double sr = 0.0, sg = 0.0, sb = 0.0, ssat = 0.0, scon = 0.0;
    for (const auto& f : samples) {
        sr += f.meanR;
        sg += f.meanG;
        sb += f.meanB;
        ssat += f.saturation;
        scon += f.contrast;
    }
    const double n = static_cast<double>(samples.size());
    out.avgR = sr / n;
    out.avgG = sg / n;
    out.avgB = sb / n;
    out.avgSaturation = ssat / n;
    out.avgContrast = scon / n;
    out.totalSeconds = samples.back().timeSeconds;

    // ---------- 2) 切点检测 ----------
    // 用中位数做基线，避免全片都是快切时阈值被拉高（那样会漏掉慢切）
    std::vector<double> diffs;
    diffs.reserve(samples.size());
    for (std::size_t i = 1; i < samples.size(); ++i) diffs.push_back(samples[i].diff);
    const double base = medianOf(diffs);
    const double threshold = std::max(0.10, base * 2.5 + 0.02);

    std::vector<EventSpan> spans;
    std::size_t i = 1;
    while (i < samples.size()) {
        if (samples[i].diff < threshold) {
            ++i;
            continue;
        }
        EventSpan span;
        span.first = i;
        span.last = i;
        span.peakDiff = samples[i].diff;
        // 允许中间有 1 个样本回落到阈值以下（解码抖动会让单帧差异忽高忽低）
        std::size_t j = i + 1;
        while (j < samples.size()) {
            if (samples[j].diff >= threshold) {
                span.last = j;
                span.peakDiff = std::max(span.peakDiff, samples[j].diff);
                ++j;
                continue;
            }
            // 看下一帧是否又起来：连上
            if (j + 1 < samples.size() && samples[j + 1].diff >= threshold &&
                samples[j].diff >= threshold * 0.6) {
                span.last = j;
                ++j;
                continue;
            }
            break;
        }
        spans.push_back(span);
        i = span.last + 1;
    }

    // 单帧事件过多时说明阈值偏低（噪声），把明显弱于平均峰值的丢掉
    if (!spans.empty()) {
        double peakSum = 0.0;
        for (const auto& sp : spans) peakSum += sp.peakDiff;
        const double peakAvg = peakSum / static_cast<double>(spans.size());
        spans.erase(std::remove_if(spans.begin(), spans.end(),
                                   [&](const EventSpan& sp) {
                                       return sp.peakDiff < peakAvg * 0.45;
                                   }),
                    spans.end());
    }

    const double baselineLuma = samples.front().luma;
    for (const auto& sp : spans) {
        out.transitions.push_back(classifyEvent(samples, sp, baselineLuma));
    }

    // ---------- 3) 按切点切镜头 ----------
    std::vector<double> cutTimes;
    cutTimes.reserve(out.transitions.size());
    for (const auto& t : out.transitions) cutTimes.push_back(t.atSeconds);
    std::sort(cutTimes.begin(), cutTimes.end());

    double shotStart = 0.0;
    for (std::size_t k = 0; k <= cutTimes.size(); ++k) {
        const double shotEnd = (k < cutTimes.size()) ? cutTimes[k] : out.totalSeconds;
        if (shotEnd - shotStart > 1e-3) {
            RefShot shot;
            shot.startSeconds = shotStart;
            shot.endSeconds = shotEnd;
            double m = 0.0, b = 0.0, sat = 0.0, r = 0.0, g = 0.0, bl = 0.0;
            std::size_t cnt = 0;
            for (const auto& f : samples) {
                if (f.timeSeconds < shot.startSeconds || f.timeSeconds > shot.endSeconds) continue;
                m += f.diff;
                b += f.luma;
                sat += f.saturation;
                r += f.meanR;
                g += f.meanG;
                bl += f.meanB;
                ++cnt;
            }
            if (cnt > 0) {
                const double c = static_cast<double>(cnt);
                shot.motion = clamp01((m / c) / 0.25);
                shot.brightness = b / c;
                shot.saturation = sat / c;
                shot.meanR = r / c;
                shot.meanG = g / c;
                shot.meanB = bl / c;
            }
            out.shots.push_back(shot);
        }
        shotStart = shotEnd;
    }
    out.shotCount = static_cast<int>(out.shots.size());

    // ---------- 4) 抖动估计 ----------
    /* 抖动的振幅与频率本来就写在像素里：位移重心的往复就是抖。
       做法：在每个镜头内部（排除转场窗口）取差异重心的偏移序列，
       去均值后算 RMS 作为振幅、算方向反转次数作为频率。

       只在「运动量足够」的镜头上估 —— 静止镜头算出来的 RMS 是噪声，
       会给出一个假的「有抖动」结论。 */
    double bestAmp = 0.0;
    double bestFreq = 0.0;
    double bestConf = 0.0;
    for (const auto& shot : out.shots) {
        std::vector<double> xs;
        std::vector<double> ys;
        for (const auto& f : samples) {
            if (f.timeSeconds < shot.startSeconds || f.timeSeconds > shot.endSeconds) continue;
            if (f.diffCentroidX < 0.0) continue;
            if (f.diff < 0.02 || f.diff > 0.35) continue;   // 排除静止与转场
            xs.push_back(f.diffCentroidX);
            ys.push_back(f.diffCentroidY);
        }
        if (xs.size() < 6) continue;

        const auto stats = [](const std::vector<double>& v, double& mean, double& rms) {
            mean = 0.0;
            for (double x : v) mean += x;
            mean /= static_cast<double>(v.size());
            double acc = 0.0;
            for (double x : v) acc += (x - mean) * (x - mean);
            rms = std::sqrt(acc / static_cast<double>(v.size()));
        };
        double mx = 0.0, my = 0.0, rx = 0.0, ry = 0.0;
        stats(xs, mx, rx);
        stats(ys, my, ry);

        // 振幅取两轴较大者；乘 2 是把 RMS 换成「峰峰值」量级
        const double amp = std::min(0.2, std::max(rx, ry) * 2.0);
        const double dur = shot.duration();
        if (dur < 0.5) continue;

        // 频率：x 方向穿越均值的次数 / (2 * 时长) ≈ 每秒完整周期数
        int crossings = 0;
        for (std::size_t k = 1; k < xs.size(); ++k) {
            if ((xs[k - 1] - mx) * (xs[k] - mx) < 0.0) ++crossings;
        }
        const double freq = std::min(20.0, static_cast<double>(crossings) / (2.0 * dur));

        // 置信度：样本越多、振幅越大越可信
        const double conf = clamp01(static_cast<double>(xs.size()) / 24.0) *
                            clamp01(amp / 0.02);
        if (conf > bestConf) {
            bestConf = conf;
            bestAmp = amp;
            bestFreq = freq;
        }
    }
    out.shakeAmplitude = bestAmp;
    out.shakeFrequencyHz = bestFreq;
    out.shakeConfidence = bestConf;

    // ---------- 5) 节奏 ----------
    double motionSum = 0.0;
    for (const auto& f : samples) motionSum += f.diff;
    out.motionIntensity = clamp01((motionSum / static_cast<double>(samples.size())) / 0.25);
    out.cutsPerMinute =
        out.totalSeconds > 1.0
            ? static_cast<double>(out.transitions.size()) / (out.totalSeconds / 60.0)
            : 0.0;
}

std::string RefStyleProfile::toJson() const {
    std::ostringstream os;
    os.setf(std::ios::fixed);
    os.precision(3);

    os << "{";
    os << "\"shot_count\":" << shotCount;
    os << ",\"total_seconds\":" << totalSeconds;
    os << ",\"avg_r\":" << avgR << ",\"avg_g\":" << avgG << ",\"avg_b\":" << avgB;
    os << ",\"avg_saturation\":" << avgSaturation << ",\"avg_contrast\":" << avgContrast;
    os << ",\"shake_amplitude\":" << shakeAmplitude;
    os << ",\"shake_frequency_hz\":" << shakeFrequencyHz;
    os << ",\"shake_confidence\":" << shakeConfidence;
    os << ",\"motion_intensity\":" << motionIntensity;
    os << ",\"cuts_per_minute\":" << cutsPerMinute;

    os << ",\"shots\":[";
    for (std::size_t i = 0; i < shots.size(); ++i) {
        if (i) os << ",";
        os << "{\"start\":" << shots[i].startSeconds << ",\"end\":" << shots[i].endSeconds
           << ",\"duration\":" << shots[i].duration() << ",\"motion\":" << shots[i].motion
           << ",\"brightness\":" << shots[i].brightness
           << ",\"saturation\":" << shots[i].saturation << "}";
    }
    os << "],";

    os << "\"transitions\":[";
    for (std::size_t i = 0; i < transitions.size(); ++i) {
        if (i) os << ",";
        os << "{\"kind\":" << static_cast<int>(transitions[i].kind) << ",\"name\":\""
           << refTransitionName(transitions[i].kind) << "\",\"at\":" << transitions[i].atSeconds
           << ",\"duration\":" << transitions[i].durationSeconds
           << ",\"confidence\":" << transitions[i].confidence << "}";
    }
    os << "]}";
    return os.str();
}

// ---------------------------------------------------------------------------
// 真实解码采样
// ---------------------------------------------------------------------------
#ifdef NAIROCLIP_HAS_FFMPEG

namespace {

/// 降采样格子的统计结果（每帧算一次）。
struct GridStats {
    double meanR = 0.0, meanG = 0.0, meanB = 0.0;
    double luma = 0.0;
    double saturation = 0.0;
    double contrast = 0.0;
    double detailEnergy = 0.0;
    std::vector<double> cells;   // 每格亮度（行优先）
    int cols = 0, rows = 0;
};

}  // namespace

bool sampleReferenceVideo(const std::string& path, double maxFps, int maxSamples,
                          std::vector<FrameSample>& out, std::string& error) {
    out.clear();
    FFmpegMediaReader reader;
    const Result opened = reader.open(path);
    if (!opened.isOk()) {
        error = "打不开参考视频：" + path;
        return false;
    }
    const double duration = reader.probe().duration.toDouble();
    if (duration <= 0.0) {
        error = "参考视频时长为 0";
        return false;
    }
    const double fps = maxFps > 0.0 ? maxFps : 6.0;
    const int cap = maxSamples > 0 ? maxSamples : 600;
    double step = 1.0 / fps;
    if (duration / step > static_cast<double>(cap)) {
        step = duration / static_cast<double>(cap);
    }
    if (step < 1.0 / 60.0) step = 1.0 / 60.0;

    std::vector<double> prevCells;
    int prevCols = 0;
    int prevRows = 0;
    double prevTime = 0.0;

    VideoFrame frame;
    for (double t = 0.0; t < duration; t += step) {
        if (!reader.seek(fromMicroseconds(static_cast<std::int64_t>(t * 1e6))).isOk()) break;
        if (!reader.readVideoFrame(frame)) continue;
        if (frame.width <= 0 || frame.height <= 0) continue;

        // 固定网格：64×36。太小丢结构，太大手机算不动。
        const int cols = 64;
        const int rows = 36;
        GridStats g;
        g.cols = cols;
        g.rows = rows;
        g.cells.assign(static_cast<std::size_t>(cols) * rows, 0.0);

        double sumR = 0.0, sumG = 0.0, sumB = 0.0, sumL = 0.0, sumL2 = 0.0, sumSat = 0.0;
        double sumDetail = 0.0;
        std::size_t count = 0;
        std::vector<double> rowLuma(static_cast<std::size_t>(cols), 0.0);

        for (int ry = 0; ry < rows; ++ry) {
            const int y0 = ry * frame.height / rows;
            const int y1 = std::max(y0 + 1, (ry + 1) * frame.height / rows);
            for (int cx = 0; cx < cols; ++cx) {
                const int x0 = cx * frame.width / cols;
                const int x1 = std::max(x0 + 1, (cx + 1) * frame.width / cols);
                // 每格取中心点即可：目的是统计，不是重建画面
                const int px = std::min(frame.width - 1, (x0 + x1) / 2);
                const int py = std::min(frame.height - 1, (y0 + y1) / 2);
                const std::size_t off =
                    static_cast<std::size_t>(py) * static_cast<std::size_t>(frame.stride) +
                    static_cast<std::size_t>(px) * 4;
                if (off + 3 >= frame.pixels.size()) continue;
                const double r = frame.pixels[off] / 255.0;
                const double gg = frame.pixels[off + 1] / 255.0;
                const double b = frame.pixels[off + 2] / 255.0;
                const double l = 0.299 * r + 0.587 * gg + 0.114 * b;
                const double mx = std::max(r, std::max(gg, b));
                const double mn = std::min(r, std::min(gg, b));
                const double sat = mx > 1e-6 ? (mx - mn) / mx : 0.0;

                g.cells[static_cast<std::size_t>(ry) * cols + cx] = l;
                rowLuma[static_cast<std::size_t>(cx)] = l;
                sumR += r;
                sumG += gg;
                sumB += b;
                sumL += l;
                sumL2 += l * l;
                sumSat += sat;
                ++count;
            }
            for (int cx = 1; cx < cols; ++cx) {
                sumDetail += std::fabs(rowLuma[static_cast<std::size_t>(cx)] -
                                       rowLuma[static_cast<std::size_t>(cx - 1)]);
            }
        }
        if (count == 0) continue;

        const double dn = static_cast<double>(count);
        g.meanR = sumR / dn;
        g.meanG = sumG / dn;
        g.meanB = sumB / dn;
        g.luma = sumL / dn;
        g.saturation = sumSat / dn;
        const double var = std::max(0.0, sumL2 / dn - g.luma * g.luma);
        g.contrast = std::sqrt(var);
        g.detailEnergy = std::min(1.0, (sumDetail / (dn * 0.5)));

        FrameSample s;
        s.timeSeconds = t;
        s.width = frame.width;
        s.height = frame.height;
        s.meanR = g.meanR;
        s.meanG = g.meanG;
        s.meanB = g.meanB;
        s.luma = g.luma;
        s.saturation = g.saturation;
        s.contrast = g.contrast;
        s.detailEnergy = g.detailEnergy;

        if (!prevCells.empty() && prevCols == cols && prevRows == rows) {
            double sumDiff = 0.0;
            double wsum = 0.0;
            double cxWeighted = 0.0;
            double cyWeighted = 0.0;
            double maxCell = 0.0;
            std::size_t cnt = 0;
            for (int ry = 0; ry < rows; ++ry) {
                for (int cx = 0; cx < cols; ++cx) {
                    const std::size_t idx = static_cast<std::size_t>(ry) * cols + cx;
                    const double d = std::fabs(g.cells[idx] - prevCells[idx]);
                    sumDiff += d;
                    if (d > maxCell) maxCell = d;
                    // 重心只统计「确实变了」的格子，否则全屏均匀噪声会把重心拉到中间
                    if (d > 0.02) {
                        cxWeighted += d * (static_cast<double>(cx) + 0.5) / cols;
                        cyWeighted += d * (static_cast<double>(ry) + 0.5) / rows;
                        wsum += d;
                    }
                    ++cnt;
                }
            }
            if (cnt > 0) s.diff = std::min(1.0, sumDiff / static_cast<double>(cnt) * 2.0);
            if (wsum > 1e-9) {
                s.diffCentroidX = cxWeighted / wsum;
                s.diffCentroidY = cyWeighted / wsum;
            }
            // 集中度：最大格差异 / 平均差异（归一化到 0..1）
            const double meanCell = cnt > 0 ? sumDiff / static_cast<double>(cnt) : 0.0;
            if (meanCell > 1e-9) {
                s.diffConcentration = clamp01((maxCell / meanCell - 1.0) / 8.0);
            }
        }

        prevCells = g.cells;
        prevCols = cols;
        prevRows = rows;
        prevTime = t;
        out.push_back(s);
        if (static_cast<int>(out.size()) >= cap) break;
    }
    (void)prevTime;

    if (out.size() < 2) {
        error = "参考视频解码出的帧太少";
        return false;
    }
    return true;
}

bool analyzeReferenceVideo(const std::string& path, RefStyleProfile& out, std::string& error) {
    std::vector<FrameSample> samples;
    if (!sampleReferenceVideo(path, 6.0, 600, samples, error)) return false;
    analyzeFrameSamples(samples, out);
    return true;
}

#else

bool sampleReferenceVideo(const std::string&, double, int, std::vector<FrameSample>&,
                          std::string& error) {
    error = "本构建未启用 FFmpeg，无法分析参考视频";
    return false;
}

bool analyzeReferenceVideo(const std::string&, RefStyleProfile&, std::string& error) {
    error = "本构建未启用 FFmpeg，无法分析参考视频";
    return false;
}

#endif

}  // namespace core
}  // namespace nairoclip
