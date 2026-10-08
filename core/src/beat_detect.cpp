#include "nairoclip/core/beat_detect.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.141592653589793;

/// 单帧多频带能量。
struct FrameEnergy {
    double low = 0.0;   // 低频（约 < 200Hz）
    double mid = 0.0;   // 中频
    double high = 0.0;  // 高频
};

/// 计算帧的频带能量（一阶 IIR 低通近似分频：零 FFT 依赖、跨平台确定性）。
FrameEnergy frameEnergy(const std::vector<float>& mono, int start, int count, int sampleRate,
                        double& lowState) {
    FrameEnergy energy;
    if (count <= 0 || start < 0 || start >= static_cast<int>(mono.size())) {
        return energy;
    }
    const int end = std::min(start + count, static_cast<int>(mono.size()));
    if (end <= start) {
        return energy;
    }
    const double cutoff = 200.0;
    const double dt = 1.0 / static_cast<double>(std::max(1, sampleRate));
    const double rc = 1.0 / (2.0 * kPi * cutoff);
    const double alpha = dt / (rc + dt);
    double lowSum = 0.0;
    double midSum = 0.0;
    double highSum = 0.0;
    for (int i = start; i < end; ++i) {
        const double x = static_cast<double>(mono[static_cast<std::size_t>(i)]);
        lowState += alpha * (x - lowState);
        const double low = lowState;
        const double high = x - low;
        const double mid = x - low * 0.5 - high * 0.5;
        lowSum += low * low;
        midSum += mid * mid;
        highSum += high * high;
    }
    const double n = static_cast<double>(end - start);
    energy.low = std::sqrt(lowSum / n);
    energy.mid = std::sqrt(midSum / n);
    energy.high = std::sqrt(highSum / n);
    return energy;
}

/// 半波整流差分（onset 强度）：仅保留能量上升部分。
double positiveDiff(double current, double previous) {
    const double diff = current - previous;
    return diff > 0.0 ? diff : 0.0;
}

/// 归一化到 [0,1]（全零时保持全零）。
void normalizeInPlace(std::vector<double>& values) {
    double maxValue = 0.0;
    for (const double v : values) {
        maxValue = std::max(maxValue, v);
    }
    if (maxValue <= 0.0) {
        return;
    }
    for (double& v : values) {
        v /= maxValue;
    }
}

// ---------------------------------------------------------------------------
// 周期估计：精细扫描 + 梳状（comb）对齐评分
// ---------------------------------------------------------------------------
// 为什么不用「整数帧滞后自相关」直接定周期（此前的实现缺陷）：
//   帧跳距（如 512/44100 ≈ 11.6ms）量化后的网格与真实节拍周期存在系统性
//   误差，逐拍累积后远超容差，会把正确周期误判（实测 120BPM 被误判为
//   60BPM 的半速歧义）。因此改为：
//     1. 以 1ms 步进精细扫描周期（不受帧量化限制）；
//     2. 每个候选周期计算「三角核梳状对齐分」：
//          coverage  = 各 onset 到最近网格点的加权命中率（防漏拍）
//          precision = 各网格点到最近 onset 的加权命中率（防空拍）
//          align     = coverage × precision
//     3. 倍频消解：对齐度达到最佳 92% 的候选中取最短周期（基频）；
//     4. 轻微节拍偏好（对数域 120BPM 附近，权重 8%），仅消解同等证据争议。

/// onset 强度表：固定时间桶聚合，供梳状评分邻域查询。
struct OnsetTable {
    double binSeconds = 0.01;
    std::vector<double> strength;
    std::vector<double> centers;
    double minTime = 0.0;
    double maxTime = 0.0;
};

OnsetTable buildOnsetTable(const std::vector<Onset>& onsets, const Time& origin) {
    OnsetTable table;
    if (onsets.empty()) {
        return table;
    }
    double minT = 1e18;
    double maxT = -1e18;
    for (const auto& onset : onsets) {
        const double t = (onset.time - origin).toDouble();
        minT = std::min(minT, t);
        maxT = std::max(maxT, t);
    }
    table.minTime = minT;
    table.maxTime = maxT;
    const int binCount = static_cast<int>(maxT / table.binSeconds) + 2;
    table.strength.assign(static_cast<std::size_t>(std::max(1, binCount)), 0.0);
    table.centers.resize(table.strength.size());
    for (std::size_t i = 0; i < table.centers.size(); ++i) {
        table.centers[i] = (static_cast<double>(i) + 0.5) * table.binSeconds;
    }
    for (const auto& onset : onsets) {
        const double t = (onset.time - origin).toDouble();
        const int index = static_cast<int>(t / table.binSeconds);
        if (index >= 0 && index < static_cast<int>(table.strength.size())) {
            table.strength[static_cast<std::size_t>(index)] += onset.strength;
        }
    }
    return table;
}

/// 三角核查询：时间 t 邻域内的 onset 强度加权和（σ 内线性衰减）。
double kernelAt(const OnsetTable& table, double t, double sigma) {
    if (sigma <= 0.0 || table.strength.empty()) {
        return 0.0;
    }
    const int lo = std::max(0, static_cast<int>((t - sigma) / table.binSeconds));
    const int hi = std::min(static_cast<int>(table.strength.size()) - 1,
                            static_cast<int>((t + sigma) / table.binSeconds) + 1);
    double sum = 0.0;
    for (int i = lo; i <= hi; ++i) {
        const double distance = std::fabs(table.centers[static_cast<std::size_t>(i)] - t);
        if (distance < sigma) {
            sum += table.strength[static_cast<std::size_t>(i)] * (1.0 - distance / sigma);
        }
    }
    return sum;
}

/// 梳状对齐评分结果。
struct CombScore {
    double coverage = 0.0;
    double precision = 0.0;
    double align = 0.0;
};

/// 计算候选周期的梳状对齐分。
CombScore combScore(double period, const OnsetTable& table,
                    const std::vector<double>& onsetTimes,
                    const std::vector<double>& onsetWeights, double anchor, double sigma) {
    CombScore score;
    if (period <= 0.0 || onsetTimes.empty() || sigma <= 0.0) {
        return score;
    }
    // coverage：每个 onset 到最近网格点的加权命中率
    double weightSum = 0.0;
    double coveredSum = 0.0;
    for (std::size_t i = 0; i < onsetTimes.size(); ++i) {
        const double w = onsetWeights[i];
        if (w <= 0.0) {
            continue;
        }
        weightSum += w;
        const double offset = (onsetTimes[i] - anchor) / period;
        const double nearestK = std::floor(offset + 0.5);
        const double distance = std::fabs(onsetTimes[i] - (anchor + nearestK * period));
        if (distance < sigma) {
            coveredSum += w * (1.0 - distance / sigma);
        }
    }
    score.coverage = weightSum > 0.0 ? coveredSum / weightSum : 0.0;

    // precision：每个网格点到最近 onset 的加权命中率
    const double gridStart =
        anchor - std::ceil((anchor - (table.minTime - sigma)) / period) * period;
    const double gridEnd = anchor + std::floor(((table.maxTime + sigma) - anchor) / period) * period;
    int gridCount = 0;
    double gridCovered = 0.0;
    const int maxGridPoints = 20000;  // 上限保护（长音频 + 极短周期）
    for (double t = gridStart; t <= gridEnd + 1e-9 && gridCount < maxGridPoints; t += period) {
        ++gridCount;
        const double hit = kernelAt(table, t, sigma);
        if (hit > 0.0) {
            gridCovered += std::min(1.0, hit);
        }
    }
    score.precision = gridCount > 0 ? gridCovered / static_cast<double>(gridCount) : 0.0;
    score.align = score.coverage * score.precision;
    return score;
}

/// 轻微节拍偏好曲线：对数域以 120BPM 为中心的高斯（权重 8%）。
double tempoPreference(double bpm) {
    if (bpm <= 0.0) {
        return 0.0;
    }
    const double x = std::log(bpm / 120.0) / std::log(2.0);
    return 0.92 + 0.08 * std::exp(-(x * x) / 2.0);
}

}  // namespace

BeatAnalysis analyzeBeats(const std::vector<AudioBlock>& blocks, const BeatDetectConfig& config) {
    BeatAnalysis result;
    if (blocks.empty()) {
        result.note = "无音频输入";
        return result;
    }
    const int sampleRate = blocks.front().sampleRate > 0 ? blocks.front().sampleRate : 48000;
    result.origin = blocks.front().start;

    // ---- 1) 下混为单声道（跨块连续）----
    std::vector<float> mono;
    for (const auto& block : blocks) {
        const std::size_t frames = block.frameCount();
        const int blockChannels = std::max(1, block.channels);
        mono.reserve(mono.size() + frames);
        for (std::size_t f = 0; f < frames; ++f) {
            double sum = 0.0;
            for (int c = 0; c < blockChannels; ++c) {
                const std::size_t idx = f * static_cast<std::size_t>(blockChannels) +
                                        static_cast<std::size_t>(c);
                if (idx < block.samples.size()) {
                    sum += static_cast<double>(block.samples[idx]);
                }
            }
            mono.push_back(static_cast<float>(sum / static_cast<double>(blockChannels)));
        }
    }
    if (mono.size() < static_cast<std::size_t>(std::max(1, config.frameSize)) * 2) {
        result.note = "音频过短，无法分析节拍";
        return result;
    }

    const int frameSize = std::max(64, config.frameSize);
    const int hopSize = std::max(16, std::min(config.hopSize, frameSize));
    const int frameCount =
        static_cast<int>((static_cast<int>(mono.size()) - frameSize) / hopSize) + 1;
    if (frameCount < 4) {
        result.note = "音频过短，无法分析节拍";
        return result;
    }
    const double frameSeconds = static_cast<double>(hopSize) / static_cast<double>(sampleRate);
    result.analyzedDuration = Rational::make(static_cast<std::int64_t>(mono.size()), sampleRate);

    // ---- 2) 逐帧频带能量 → 多频带 onset 强度 ----
    std::vector<double> lowEnergy(static_cast<std::size_t>(frameCount));
    std::vector<double> midEnergy(static_cast<std::size_t>(frameCount));
    std::vector<double> highEnergy(static_cast<std::size_t>(frameCount));
    double lowState = 0.0;
    for (int f = 0; f < frameCount; ++f) {
        const FrameEnergy energy = frameEnergy(mono, f * hopSize, frameSize, sampleRate, lowState);
        lowEnergy[static_cast<std::size_t>(f)] = energy.low;
        midEnergy[static_cast<std::size_t>(f)] = energy.mid;
        highEnergy[static_cast<std::size_t>(f)] = energy.high;
    }
    std::vector<double> strength(static_cast<std::size_t>(frameCount), 0.0);
    for (int f = 1; f < frameCount; ++f) {
        const double lowD = positiveDiff(lowEnergy[static_cast<std::size_t>(f)],
                                         lowEnergy[static_cast<std::size_t>(f - 1)]);
        const double midD = positiveDiff(midEnergy[static_cast<std::size_t>(f)],
                                         midEnergy[static_cast<std::size_t>(f - 1)]);
        const double highD = positiveDiff(highEnergy[static_cast<std::size_t>(f)],
                                          highEnergy[static_cast<std::size_t>(f - 1)]);
        strength[static_cast<std::size_t>(f)] = lowD * 1.0 + midD * 0.6 + highD * 0.3;
    }
    normalizeInPlace(strength);

    // ---- 3) 自适应阈值 + 局部峰值 → onset ----
    const int adaptiveWindow =
        std::max(1, static_cast<int>(config.adaptiveWindowSeconds / std::max(1e-9, frameSeconds)));
    const int localWindow = std::max(1, adaptiveWindow / 2);
    const double sensitivity = config.sensitivity > 0.0 ? config.sensitivity : 1.0;
    std::vector<Onset> onsets;
    for (int f = 1; f + 1 < frameCount; ++f) {
        const double value = strength[static_cast<std::size_t>(f)];
        if (value <= 0.0) {
            continue;
        }
        double sum = 0.0;
        int count = 0;
        for (int k = std::max(0, f - localWindow); k <= std::min(frameCount - 1, f + localWindow);
             ++k) {
            sum += strength[static_cast<std::size_t>(k)];
            ++count;
        }
        const double mean = count > 0 ? sum / static_cast<double>(count) : 0.0;
        if (value < mean * sensitivity) {
            continue;
        }
        const double previous = strength[static_cast<std::size_t>(f - 1)];
        const double next = strength[static_cast<std::size_t>(f + 1)];
        if (value < previous || value < next) {
            continue;
        }
        Onset onset;
        // 帧中心时间（补偿帧对齐导致的系统性偏差）
        const double seconds = (static_cast<double>(f) + 0.5) * frameSeconds;
        onset.time = blocks.front().start +
                     Rational::make(static_cast<std::int64_t>(seconds * 1000000.0 + 0.5), 1000000);
        onset.strength = value;
        onsets.push_back(onset);
    }
    result.onsets = onsets;
    if (onsets.size() < 4) {
        result.note = "起音点过少，无法估计节拍（可调低灵敏度）";
        return result;
    }

    // ---- 4) 精细周期扫描 + 梳状对齐 → BPM（含倍频消解）----
    const double minBpm = std::max(20.0, config.minBpm);
    const double maxBpm = std::max(minBpm + 1.0, config.maxBpm);
    const double minPeriod = 60.0 / maxBpm;
    const double maxPeriod = 60.0 / minBpm;

    const OnsetTable table = buildOnsetTable(onsets, blocks.front().start);
    std::vector<double> onsetTimes;
    std::vector<double> onsetWeights;
    onsetTimes.reserve(onsets.size());
    onsetWeights.reserve(onsets.size());
    double anchor = 0.0;
    double anchorStrength = -1.0;
    for (const auto& onset : onsets) {
        const double t = (onset.time - blocks.front().start).toDouble();
        onsetTimes.push_back(t);
        onsetWeights.push_back(onset.strength);
        if (onset.strength > anchorStrength) {
            anchorStrength = onset.strength;
            anchor = t;
        }
    }
    // 命中容差：略宽于 2 个帧跳距（吸收帧量化抖动）。
    const double sigma = std::max(0.03, frameSeconds * 2.0);

    struct Candidate {
        double period = 0.0;
        CombScore comb;
        double score = 0.0;
    };
    std::vector<Candidate> candidates;
    const double stepSeconds = 0.001;  // 1ms 步进
    for (double period = minPeriod; period <= maxPeriod + 1e-9; period += stepSeconds) {
        Candidate candidate;
        candidate.period = period;
        candidate.comb = combScore(period, table, onsetTimes, onsetWeights, anchor, sigma);
        if (candidate.comb.align <= 0.0) {
            continue;
        }
        candidate.score = candidate.comb.align * tempoPreference(60.0 / period);
        candidates.push_back(candidate);
    }
    if (candidates.empty()) {
        result.note = "无法估计节拍周期";
        return result;
    }
    const auto bestIt = std::max_element(
        candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.score < b.score; });
    const double bestAlign = bestIt->comb.align;
    // 倍频消解：对齐度达到最佳 92% 的候选中取最短周期（基频）。
    double chosenPeriod = bestIt->period;
    for (const auto& candidate : candidates) {
        if (candidate.comb.align >= 0.92 * bestAlign && candidate.period < chosenPeriod) {
            chosenPeriod = candidate.period;
        }
    }
    result.bpm = 60.0 / chosenPeriod;
    result.confidence = std::min(1.0, std::max(0.0, (bestAlign - 0.20) / 0.75));

    // ---- 5) 生成节拍网格，并吸附到最近 onset 峰值 ----
    const double totalSeconds = static_cast<double>(mono.size()) / static_cast<double>(sampleRate);
    const double snapRadius = std::max(0.0, config.snapRadiusSeconds);
    auto snapToOnset = [&](double seconds) {
        Onset beat;
        double bestDelta = snapRadius;
        double snapped = seconds;
        double strengthAt = 0.0;
        for (const auto& onset : onsets) {
            const double onsetSeconds = (onset.time - blocks.front().start).toDouble();
            const double delta = std::fabs(onsetSeconds - seconds);
            if (delta < bestDelta) {
                bestDelta = delta;
                snapped = onsetSeconds;
                strengthAt = onset.strength;
            }
        }
        beat.time = blocks.front().start +
                    Rational::make(static_cast<std::int64_t>(snapped * 1000000.0 + 0.5), 1000000);
        beat.strength = strengthAt;
        return beat;
    };
    std::vector<Onset> beats;
    for (double t = anchor; t >= 0.0; t -= chosenPeriod) {
        beats.push_back(snapToOnset(t));
    }
    for (double t = anchor + chosenPeriod; t < totalSeconds; t += chosenPeriod) {
        beats.push_back(snapToOnset(t));
    }
    std::sort(beats.begin(), beats.end(),
              [](const Onset& a, const Onset& b) { return a.time < b.time; });
    std::vector<Onset> deduped;
    Time lastTime;
    bool hasLast = false;
    for (const auto& beat : beats) {
        if (hasLast && beat.time == lastTime) {
            continue;
        }
        deduped.push_back(beat);
        lastTime = beat.time;
        hasLast = true;
    }
    beats = std::move(deduped);
    if (config.maxBeats > 0 && static_cast<int>(beats.size()) > config.maxBeats) {
        beats.resize(static_cast<std::size_t>(config.maxBeats));
    }
    result.beats = std::move(beats);
    if (result.beats.empty()) {
        result.note = "未生成有效节拍";
    }
    return result;
}

std::vector<Time> buildBeatGrid(const Time& startTime, const Time& duration, double bpm) {
    std::vector<Time> grid;
    if (bpm <= 0.0 || !duration.isPositive()) {
        return grid;
    }
    const double periodSeconds = 60.0 / bpm;
    const std::int64_t periodMicros = static_cast<std::int64_t>(periodSeconds * 1000000.0 + 0.5);
    if (periodMicros <= 0) {
        return grid;
    }
    const std::int64_t totalMicros = toMicroseconds(duration);
    for (std::int64_t t = 0; t < totalMicros; t += periodMicros) {
        grid.push_back(startTime + fromMicroseconds(t));
    }
    return grid;
}

std::vector<Time> snapCutsToBeats(const std::vector<Time>& cutPoints,
                                  const std::vector<Time>& beats, double radiusSeconds) {
    std::vector<Time> snapped;
    snapped.reserve(cutPoints.size());
    if (radiusSeconds <= 0.0 || beats.empty()) {
        return cutPoints;
    }
    const std::int64_t radiusMicros = static_cast<std::int64_t>(radiusSeconds * 1000000.0 + 0.5);
    for (const auto& cut : cutPoints) {
        const std::int64_t cutMicros = toMicroseconds(cut);
        std::int64_t bestDistance = radiusMicros + 1;
        std::int64_t bestMicros = cutMicros;
        for (const auto& beat : beats) {
            const std::int64_t beatMicros = toMicroseconds(beat);
            const std::int64_t distance =
                beatMicros > cutMicros ? beatMicros - cutMicros : cutMicros - beatMicros;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestMicros = beatMicros;
            }
        }
        snapped.push_back(fromMicroseconds(bestMicros));
    }
    std::sort(snapped.begin(), snapped.end());
    return snapped;
}

// =============================================================================
// 音乐卡点（Beat Sync Edit）
// =============================================================================
// 产品对应：「AI成片 → 音乐卡点」与「🎵音频 → 节拍 → AI 自动打点」。
// 输入：analyzeBeats 的节拍结果 + 用户素材；
// 输出：按节拍切分的剪辑建议（CutSuggestion 列表，可交给自动剪辑落盘）。
// 合规：只分析用户本机已导入的音乐（不内置曲库、不上传音频内容）。

BeatSyncPlan planBeatSync(const BeatAnalysis& analysis,
                          const std::vector<MaterialSegment>& material,
                          const BeatSyncOptions& options) {
    BeatSyncPlan plan;
    if (!analysis.valid()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument,
                                   analysis.note.empty() ? "节拍分析无效" : analysis.note);
        return plan;
    }
    if (material.empty()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument, "用户素材为空");
        return plan;
    }
    for (const auto& segment : material) {
        if (!segment.asset.isValid() || !segment.usableDuration.isPositive()) {
            plan.result = Result::fail(ErrorCode::InvalidArgument, "素材片段无效（ID 或时长）");
            return plan;
        }
    }
    plan.bpm = analysis.bpm;

    // ---- 1) 建立节拍区间（当前节拍 → 下一节拍；末段用周期收尾）----
    struct BeatSpan {
        Time start;
        Time duration;
        double strength = 0.0;
    };
    std::vector<BeatSpan> spans;
    const double fallbackPeriod = analysis.bpm > 0.0 ? 60.0 / analysis.bpm : 0.5;
    for (std::size_t i = 0; i < analysis.beats.size(); ++i) {
        const Time start = analysis.beats[i].time;
        Time end;
        if (i + 1 < analysis.beats.size()) {
            end = analysis.beats[i + 1].time;
        } else {
            end = start + Rational::make(
                              static_cast<std::int64_t>(fallbackPeriod * 1000000.0 + 0.5), 1000000);
        }
        const Time duration = end - start;
        if (!duration.isPositive()) {
            continue;
        }
        // 过滤过短区间（避免碎片化；仍保留节奏感）
        if (options.minShotSeconds > 0.0 && duration.toDouble() < options.minShotSeconds) {
            continue;
        }
        // 过长的区间（如静音段被吸附到同一节拍）切成等长多段
        const double maxSeconds = options.maxShotSeconds;
        if (maxSeconds > 0.0 && duration.toDouble() > maxSeconds) {
            const int pieces = static_cast<int>(duration.toDouble() / maxSeconds) + 1;
            const Time piece = duration / Rational::make(pieces);
            for (int p = 0; p < pieces; ++p) {
                BeatSpan span;
                span.start = start + piece * Rational::make(p);
                span.duration = piece;
                span.strength = analysis.beats[i].strength;
                spans.push_back(span);
            }
        } else {
            BeatSpan span;
            span.start = start;
            span.duration = duration;
            span.strength = analysis.beats[i].strength;
            spans.push_back(span);
        }
    }
    if (spans.empty()) {
        plan.result =
            Result::fail(ErrorCode::InvalidArgument, "节拍区间为空（可调低最小片段时长）");
        return plan;
    }

    // ---- 2) 素材循环取用（保持素材内部连续性）----
    std::size_t materialIndex = 0;
    Time materialCursor;  // 当前素材内已用位置
    for (const auto& span : spans) {
        if (materialIndex >= material.size()) {
            plan.warnings.push_back("素材已用尽，剩余节拍区间未生成片段");
            break;
        }
        Time remaining = material[materialIndex].usableDuration - materialCursor;
        if (remaining.toDouble() <= 0.001) {
            ++materialIndex;
            materialCursor = Rational::make(0);
            if (materialIndex >= material.size()) {
                plan.warnings.push_back("素材已用尽，剩余节拍区间未生成片段");
                break;
            }
            remaining = material[materialIndex].usableDuration;
        }
        CutSuggestion cut;
        cut.asset = material[materialIndex].asset;
        cut.start = materialCursor;
        cut.duration = span.duration;
        cut.targetIndex = plan.cuts.size();
        plan.cuts.push_back(cut);
        materialCursor = materialCursor + span.duration;
        ++plan.beatsUsed;
    }
    if (plan.cuts.empty()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument, "未能生成任何片段（素材时长不足）");
        return plan;
    }
    plan.result = Result::ok();

    // ---- 3) 统计信息（供 UI 文案：「识别节拍点 N 个 ✅」）----
    if (options.preferStrongBeats) {
        double maxStrength = 0.0;
        for (const auto& beat : analysis.beats) {
            maxStrength = std::max(maxStrength, beat.strength);
        }
        std::size_t strongCount = 0;
        if (maxStrength > 0.0) {
            for (const auto& beat : analysis.beats) {
                if (beat.strength >= maxStrength * options.strongBeatThreshold) {
                    ++strongCount;
                }
            }
        }
        plan.warnings.push_back("节拍点 " + std::to_string(analysis.beats.size()) + " 个，强拍 " +
                                std::to_string(strongCount) + " 个");
    }
    return plan;
}

}  // namespace core
}  // namespace nairoclip
