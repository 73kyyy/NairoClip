#include "nairoclip/core/chapter.h"

#include "nairoclip/core/clip.h"
#include "nairoclip/core/commands.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/track.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nairoclip {
namespace core {

namespace {

/// 章节标记颜色（与文档「标记」交互一致；章节主题色）。
constexpr const char* kChapterColor = "#00D084";

/// 单条候选边界（聚类前）。
struct BoundaryCandidate {
    Time time;
    Chapter::Boundary source = Chapter::Boundary::SceneCut;
    /// 强度（用于超限时丢弃最弱边界；场景=直方图差异，静音=静音时长/阈值）。
    double strength = 1.0;
};

double luma(double r, double g, double b) {
    return 0.299 * r + 0.587 * g + 0.114 * b;
}

/// 由语音保留区间推导静音区间（[0, total] 内的补集；已排序、非重叠）。
std::vector<std::pair<Time, Time>> silenceGaps(
    const std::vector<SpeechActivityResult::KeepSegment>& speech, const Time& total) {
    std::vector<std::pair<Time, Time>> gaps;
    if (!total.isPositive()) {
        return gaps;
    }
    // 收集并排序（输入可能来自多段分析，未必有序）
    std::vector<std::pair<Time, Time>> kept;
    kept.reserve(speech.size());
    for (const auto& seg : speech) {
        const Time s = seg.start < Time{} ? Time{} : seg.start;
        const Time e = seg.end > total ? total : seg.end;
        if (e > s) {
            kept.emplace_back(s, e);
        }
    }
    std::sort(kept.begin(), kept.end(),
              [](const std::pair<Time, Time>& a, const std::pair<Time, Time>& b) {
                  return a.first < b.first;
              });
    // 补集：逐段填补
    Time cursor{};
    for (const auto& seg : kept) {
        if (seg.first > cursor) {
            gaps.emplace_back(cursor, seg.first);
        }
        if (seg.second > cursor) {
            cursor = seg.second;
        }
    }
    if (cursor < total) {
        gaps.emplace_back(cursor, total);
    }
    return gaps;
}

/// 统计章节区间内的画面/语音指标（取区间内采样点均值）。
void fillStats(Chapter& chapter, const std::vector<FrameFeature>& features,
               const std::vector<AudioBlock>& audioBlocks) {
    double sumBrightness = 0.0;
    double maxMotion = 0.0;
    std::size_t visualCount = 0;
    for (const auto& f : features) {
        if (f.timestamp < chapter.start || f.timestamp >= chapter.end) {
            continue;
        }
        sumBrightness += luma(f.avgR, f.avgG, f.avgB);
        maxMotion = std::max(maxMotion, std::min(1.0, f.histogramDiff));
        ++visualCount;
    }
    if (visualCount > 0) {
        chapter.averageBrightness = sumBrightness / static_cast<double>(visualCount);
        chapter.averageMotion = maxMotion;
        chapter.hasVisualStats = true;
    }
    // 语音强度：音频块 RMS（仅统计落在章节区间内的块；部分重叠按整块计入，
    // 与波形桶化近似一致，足以支撑标题线索展示）。
    double sumSquares = 0.0;
    std::size_t sampleCount = 0;
    for (const auto& block : audioBlocks) {
        const int channels = std::max(1, block.channels);
        const double blockEnd =
            block.start.toDouble() + static_cast<double>(block.frameCount()) /
                                         static_cast<double>(std::max(1, block.sampleRate));
        if (blockEnd <= chapter.start.toDouble() || block.start >= chapter.end) {
            continue;
        }
        for (std::size_t i = 0; i < block.samples.size(); ++i) {
            const double v = static_cast<double>(block.samples[i]);
            sumSquares += v * v;
        }
        sampleCount += block.samples.size() / static_cast<std::size_t>(channels);
    }
    if (sampleCount > 0) {
        chapter.averageRms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
    }
}

/// 章节标题中的线索词（真实测量，不虚构内容）。
std::string clueText(const Chapter& chapter) {
    std::vector<std::string> clues;
    if (chapter.averageRms >= 0.08) {
        clues.emplace_back("人声清晰");
    } else if (chapter.averageRms > 0.0 && chapter.averageRms < 0.02) {
        clues.emplace_back("近乎静音");
    }
    if (chapter.hasVisualStats) {
        if (chapter.averageBrightness >= 0.65) {
            clues.emplace_back("画面明亮");
        } else if (chapter.averageBrightness <= 0.35) {
            clues.emplace_back("画面偏暗");
        }
        if (chapter.averageMotion >= 0.4) {
            clues.emplace_back("动作丰富");
        } else if (chapter.averageMotion <= 0.08) {
            clues.emplace_back("镜头平稳");
        }
    }
    if (clues.empty()) {
        return std::string();
    }
    std::string out;
    for (std::size_t i = 0; i < clues.size() && i < 2; ++i) {
        if (i > 0) {
            out += " · ";
        }
        out += clues[i];
    }
    return out;
}

}  // namespace

// =============================================================================
// 检测
// =============================================================================

const char* chapterMarkerColor() { return kChapterColor; }

std::string suggestChapterTitle(const Chapter& chapter, const ChapterDetectConfig& config) {
    if (chapter.index <= 0) {
        return std::string();
    }
    std::string title = config.titlePrefix + " " + std::to_string(chapter.index) + " " +
                        config.titleSuffix;
    // 首章（0 起点）不重复线索（通常为片头，线索意义有限）
    if (chapter.boundary == Chapter::Boundary::Start) {
        return title;
    }
    const std::string clue = clueText(chapter);
    if (!clue.empty()) {
        title += " · " + clue;
    }
    return title;
}

bool isAutoGeneratedTitle(const std::string& title, const ChapterDetectConfig& config) {
    // 自动标题形如「第 N 章」或「第 N 章 · 线索」（N 为十进制数字）
    const std::string head = config.titlePrefix + " ";
    if (title.compare(0, head.size(), head) != 0 || title.size() <= head.size()) {
        return false;
    }
    std::size_t pos = head.size();
    bool hasDigit = false;
    while (pos < title.size() && title[pos] >= '0' && title[pos] <= '9') {
        hasDigit = true;
        ++pos;
    }
    if (!hasDigit) {
        return false;
    }
    const std::string tail = " " + config.titleSuffix;
    if (title.compare(pos, tail.size(), tail) != 0) {
        return false;
    }
    pos += tail.size();
    // 允许末尾附加线索（「 · ...」）；其余字符视为手动编辑。
    // 注意：分隔符含非 ASCII 字符（U+00B7），必须按完整字节长度比较。
    if (pos == title.size()) {
        return true;
    }
    static const std::string kClueSeparator = " · ";
    return title.compare(pos, kClueSeparator.size(), kClueSeparator) == 0;
}

ChapterDetectResult detectChapters(const std::vector<ShotBoundary>& shots,
                                   const std::vector<SpeechActivityResult::KeepSegment>& speechSegments,
                                   const Time& totalDuration, const ChapterDetectConfig& config) {
    ChapterDetectResult result;
    if (!config.valid()) {
        result.note = "章节检测配置非法";
        return result;
    }
    if (!totalDuration.isPositive()) {
        result.note = "内容时长为空，无法生成章节";
        return result;
    }
    result.totalDuration = totalDuration;
    const double totalSeconds = totalDuration.toDouble();
    const double minLen = config.minChapterSeconds;
    const double maxLen = config.maxChapterSeconds;

    // ---- 1) 收集候选边界 ----
    std::vector<BoundaryCandidate> candidates;

    if (config.useSceneDetect) {
        for (const auto& shot : shots) {
            // 首镜起点为 0，不是边界；其余镜头的起点即边界
            if (!shot.start.isPositive() || shot.start >= totalDuration) {
                continue;
            }
            BoundaryCandidate c;
            c.time = shot.start;
            c.source = Chapter::Boundary::SceneCut;
            c.strength = std::min(1.0, std::max(0.0, shot.motionScore)) * 0.8 + 0.2;
            candidates.push_back(c);
        }
    }
    if (config.useSilenceDetect && config.silenceBoundarySeconds > 0.0 &&
        !speechSegments.empty()) {
        // 注意：speechSegments 为空表示「无 VAD 数据」（如无音频轨/未分析），
        // 此时跳过静音检测——否则整段视频会被当作一个巨大静音段而产生虚假边界。
        for (const auto& gap : silenceGaps(speechSegments, totalDuration)) {
            const double gapSeconds = (gap.second - gap.first).toDouble();
            if (gapSeconds < config.silenceBoundarySeconds) {
                continue;
            }
            // 边界取静音段中点（语音实际恢复附近，比起点更接近话题切换处）
            const Time mid = gap.first + (gap.second - gap.first) * Rational::make(1, 2);
            if (!mid.isPositive() || mid >= totalDuration) {
                continue;
            }
            BoundaryCandidate c;
            c.time = mid;
            c.source = Chapter::Boundary::Silence;
            c.strength = std::min(1.0, gapSeconds / std::max(1e-9, config.silenceBoundarySeconds) /
                                           4.0);
            candidates.push_back(c);
        }
    }
    result.candidateBoundaries = candidates.size();

    // ---- 2) 聚类：近邻候选合并（同一边界被场景+静音双重命中时归并） ----
    std::sort(candidates.begin(), candidates.end(),
              [](const BoundaryCandidate& a, const BoundaryCandidate& b) { return a.time < b.time; });
    std::vector<BoundaryCandidate> merged;
    for (const auto& c : candidates) {
        if (!merged.empty()) {
            BoundaryCandidate& last = merged.back();
            const double gap = (c.time - last.time).toDouble();
            // 合并窗口：取较短约束（不小于 1 秒，避免过度合并）
            if (gap < std::max(1.0, minLen * 0.25)) {
                const bool lastScene = last.source == Chapter::Boundary::SceneCut ||
                                       last.source == Chapter::Boundary::Both;
                const bool nowScene = c.source == Chapter::Boundary::SceneCut;
                if (lastScene != nowScene) {
                    last.source = Chapter::Boundary::Both;
                }
                // 时间取强度更高者（更可信的边界位置；先比较再更新强度）
                if (c.strength > last.strength) {
                    last.time = c.time;
                    last.strength = c.strength;
                }
                continue;
            }
        }
        merged.push_back(c);
    }

    // ---- 3) 时长约束：过短间隔并入上一章 ----
    std::vector<BoundaryCandidate> filtered;
    Time lastBoundary{};
    for (const auto& c : merged) {
        const double since = (c.time - lastBoundary).toDouble();
        if (since < minLen) {
            // 与上一章边界过近：丢弃（保留更靠后的边界由下一轮处理）
            continue;
        }
        filtered.push_back(c);
        lastBoundary = c.time;
    }

    // ---- 4) 最大章节数：按强度丢弃最弱边界 ----
    if (config.maxChapters > 0 && filtered.size() + 1 > config.maxChapters) {
        std::vector<std::size_t> order(filtered.size());
        for (std::size_t i = 0; i < order.size(); ++i) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            return filtered[a].strength > filtered[b].strength;
        });
        std::vector<bool> drop(filtered.size(), true);
        const std::size_t keepCount = config.maxChapters - 1;
        for (std::size_t i = 0; i < keepCount && i < order.size(); ++i) {
            drop[order[i]] = false;
        }
        std::vector<BoundaryCandidate> kept;
        for (std::size_t i = 0; i < filtered.size(); ++i) {
            if (!drop[i]) {
                kept.push_back(filtered[i]);
            }
        }
        filtered.swap(kept);
        // 丢弃后重新保证最短时长
        std::vector<BoundaryCandidate> rechecked;
        Time cursor{};
        for (const auto& c : filtered) {
            if ((c.time - cursor).toDouble() >= minLen) {
                rechecked.push_back(c);
                cursor = c.time;
            }
        }
        filtered.swap(rechecked);
        result.warnings.push_back("章节数达到上限，已合并较弱边界");
    }

    // ---- 5) 组装章节（含超长硬切） ----
    std::vector<Time> boundaries;
    for (const auto& c : filtered) {
        boundaries.push_back(c.time);
    }
    std::vector<Chapter> chapters;
    auto pushChapter = [&](const Time& start, const Time& end, Chapter::Boundary source) {
        Chapter ch;
        ch.index = static_cast<int>(chapters.size()) + 1;
        ch.start = start;
        ch.end = end;
        ch.boundary = source;
        chapters.push_back(std::move(ch));
    };
    Time chapterStart{};
    Chapter::Boundary chapterSource = Chapter::Boundary::Start;
    for (std::size_t i = 0; i <= boundaries.size(); ++i) {
        const Time chapterEnd = i < boundaries.size() ? boundaries[i] : totalDuration;
        // 超长章节硬切（等分，避免最后一个子章过短）
        if (maxLen > 0.0) {
            double remain = (chapterEnd - chapterStart).toDouble();
            while (remain > maxLen + 1e-9) {
                const int pieces = static_cast<int>(std::ceil(remain / maxLen));
                const Time piece = chapterStart +
                                   (chapterEnd - chapterStart) * Rational::make(1, pieces);
                pushChapter(chapterStart, piece, chapterSource);
                chapterStart = piece;
                chapterSource = Chapter::Boundary::Forced;
                remain = (chapterEnd - chapterStart).toDouble();
            }
        }
        pushChapter(chapterStart, chapterEnd, chapterSource);
        chapterStart = chapterEnd;
        chapterSource = i < boundaries.size() ? filtered[i].source : Chapter::Boundary::Unknown;
    }
    // 去除零长度/负数章节（数值边界情形）
    chapters.erase(std::remove_if(chapters.begin(), chapters.end(),
                                  [](const Chapter& c) { return !(c.end > c.start); }),
                   chapters.end());
    if (chapters.empty()) {
        result.note = "未能生成有效章节";
        return result;
    }
    // 重排序号
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        chapters[i].index = static_cast<int>(i) + 1;
    }
    result.chapters = std::move(chapters);

    // 自动标题（本地规则化；此时尚无画面/语音统计时只输出「第 N 章」）
    suggestChapterTitles(result, config);

    // ---- 6) 摘要 ----
    std::size_t sceneCount = 0;
    std::size_t silenceCount = 0;
    std::size_t bothCount = 0;
    std::size_t forcedCount = 0;
    for (const auto& c : result.chapters) {
        switch (c.boundary) {
            case Chapter::Boundary::SceneCut: ++sceneCount; break;
            case Chapter::Boundary::Silence: ++silenceCount; break;
            case Chapter::Boundary::Both: ++bothCount; break;
            case Chapter::Boundary::Forced: ++forcedCount; break;
            default: break;
        }
    }
    std::string summary = "检测到 " + std::to_string(result.chapters.size()) + " 个章节";
    summary += "（场景 " + std::to_string(sceneCount) + " / 静音 " + std::to_string(silenceCount) +
               " / 双重 " + std::to_string(bothCount) + " / 硬切 " + std::to_string(forcedCount) +
               "，总时长 " + std::to_string(static_cast<int>(totalSeconds + 0.5)) + " 秒）";
    result.summary = summary;
    return result;
}

ChapterDetectResult detectChaptersFromFeatures(const std::vector<FrameFeature>& features,
                                               const std::vector<AudioBlock>& audioBlocks,
                                               const Time& totalDuration,
                                               const ChapterDetectConfig& config) {
    // 1) 场景检测（复用既有算法）
    std::vector<ShotBoundary> shots;
    if (!features.empty()) {
        SceneDetectConfig sceneConfig;
        sceneConfig.threshold = config.sceneThreshold;
        sceneConfig.minShotDurationSeconds = std::min(2.0, config.minChapterSeconds / 4.0);
        shots = detectShots(features, totalDuration, sceneConfig);
    }
    // 2) 语音活动（复用既有 VAD）
    std::vector<SpeechActivityResult::KeepSegment> speechSegments;
    if (!audioBlocks.empty()) {
        SpeechDetectConfig vadConfig;
        const SpeechActivityResult activity = analyzeSpeechActivity(audioBlocks, vadConfig);
        speechSegments = activity.keepSegments;
    }
    // 3) 章节检测
    ChapterDetectResult result = detectChapters(shots, speechSegments, totalDuration, config);
    // 4) 统计与自动标题（使用真实采样数据）
    for (auto& chapter : result.chapters) {
        fillStats(chapter, features, audioBlocks);
    }
    suggestChapterTitles(result, config);
    return result;
}

void suggestChapterTitles(ChapterDetectResult& result, const ChapterDetectConfig& config) {
    for (auto& chapter : result.chapters) {
        if (!isAutoGeneratedTitle(chapter.title, config)) {
            // 手动编辑过的标题不覆盖；但若为空（尚未生成过）则补全
            if (!chapter.title.empty()) {
                continue;
            }
        }
        chapter.title = suggestChapterTitle(chapter, config);
    }
}

bool renameChapter(ChapterDetectResult& result, std::size_t zeroBasedIndex,
                   const std::string& newTitle) {
    if (zeroBasedIndex >= result.chapters.size()) {
        return false;
    }
    std::string trimmed = newTitle;
    // 去除首尾空白（避免纯空白标题）
    const std::size_t begin = trimmed.find_first_not_of(" \t\r\n");
    const std::size_t end = trimmed.find_last_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return false;
    }
    trimmed = trimmed.substr(begin, end - begin + 1);
    result.chapters[zeroBasedIndex].title = trimmed;
    return true;
}

// =============================================================================
// 真实素材采样
// =============================================================================

SequenceChapterSamples sampleSequenceForChapters(const Project& project, SequenceId sequenceId,
                                                 MediaReaderFactory* readerFactory,
                                                 const ChapterSampleConfig& config) {
    SequenceChapterSamples out;
    const Sequence* seq = project.findSequence(sequenceId);
    if (seq == nullptr) {
        out.note = "序列不存在";
        return out;
    }
    out.timelineDuration = seq->duration();
    if (!out.timelineDuration.isPositive()) {
        out.note = "序列内容时长为空";
        return out;
    }
    if (readerFactory == nullptr) {
        out.note = "未提供解码器，无法采样素材";
        return out;
    }
    if (!config.valid()) {
        out.note = "采样配置非法";
        return out;
    }

    // ---- 目标轨道选择 ----
    auto findTrackIndex = [&](TrackType type, int preferred, bool requireVisible) -> int {
        if (preferred >= 0 && static_cast<std::size_t>(preferred) < seq->tracks.size()) {
            const Track& t = seq->tracks[static_cast<std::size_t>(preferred)];
            if (t.type == type && !t.clips.empty() &&
                (!requireVisible || !t.hidden)) {
                return preferred;
            }
        }
        for (std::size_t i = 0; i < seq->tracks.size(); ++i) {
            const Track& t = seq->tracks[i];
            if (t.type == type && !t.clips.empty() && (!requireVisible || !t.hidden)) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int videoIndex = findTrackIndex(TrackType::Video, config.videoTrackIndex, true);
    const int audioIndex = findTrackIndex(TrackType::Audio, config.audioTrackIndex, false);

    // ---- 视频采样（时间轴坐标系） ----
    if (videoIndex >= 0) {
        const Track& track = seq->tracks[static_cast<std::size_t>(videoIndex)];
        // 自适应采样步长（显式配置优先；否则按内容时长估算）
        const double stepSeconds = config.effectiveVideoStepSeconds(out.timelineDuration);
        std::vector<std::uint8_t> prevLuma;
        double prevR = 0.0;
        double prevG = 0.0;
        double prevB = 0.0;
        bool truncated = false;
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !clip.asset.isValid() || clip.isCompound()) {
                continue;
            }
            const MediaAsset* asset = project.findAsset(clip.asset);
            if (asset == nullptr || asset->type == MediaType::Audio) {
                continue;
            }
            auto reader = readerFactory->create();
            if (reader == nullptr || !reader->open(asset->path).isOk()) {
                out.warnings.push_back("素材无法解码：" + asset->name);
                continue;
            }
            const double clipSeconds = clip.duration.toDouble();
            for (double local = 0.0; local < clipSeconds; local += stepSeconds) {
                if (out.features.size() >= config.maxVideoSamples) {
                    truncated = true;
                    break;
                }
                // 片段本地偏移 → 源素材时间（速度 1 时 local 即源偏移）
                const Time localTime =
                    Rational::make(static_cast<std::int64_t>(local * 1000.0), 1000);
                const Time sourceTime = clip.sourceIn + localTime * clip.speed;
                if (!reader->seek(sourceTime).isOk()) {
                    break;
                }
                VideoFrame frame;
                if (!reader->readVideoFrame(frame)) {
                    break;
                }
                // 均值与 4x4 亮度网格（与参考仿剪的采样约定一致）
                double sumR = 0.0;
                double sumG = 0.0;
                double sumB = 0.0;
                std::size_t count = 0;
                const int stride = frame.stride > 0 ? frame.stride : frame.width * 4;
                for (int y = 0; y < frame.height; y += 4) {
                    const std::uint8_t* row =
                        frame.pixels.data() +
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
                    for (int x = 0; x < frame.width; x += 4) {
                        const std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
                        sumR += px[0];
                        sumG += px[1];
                        sumB += px[2];
                        ++count;
                    }
                }
                const double avgR = count > 0 ? sumR / static_cast<double>(count) / 255.0 : 0.0;
                const double avgG = count > 0 ? sumG / static_cast<double>(count) / 255.0 : 0.0;
                const double avgB = count > 0 ? sumB / static_cast<double>(count) / 255.0 : 0.0;
                // 与上一采样点的直方图差异（4x4 网格亮度直方图 + 均值距离）
                std::vector<std::uint8_t> lumaGrid;
                std::vector<int> hist(8, 0);
                for (int gy = 0; gy < 4; ++gy) {
                    for (int gx = 0; gx < 4; ++gx) {
                        const int sx = frame.width > 0 ? gx * frame.width / 4 : 0;
                        const int sy = frame.height > 0 ? gy * frame.height / 4 : 0;
                        const std::uint8_t* px =
                            frame.pixels.data() +
                            static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride) +
                            static_cast<std::size_t>(sx) * 4;
                        const int lumaValue =
                            (px[0] * 299 + px[1] * 587 + px[2] * 114) / 1000;
                        lumaGrid.push_back(static_cast<std::uint8_t>(std::min(255, lumaValue)));
                        hist[std::min(7, lumaValue / 32)]++;
                    }
                }
                double diff = 0.0;
                if (!prevLuma.empty()) {
                    std::vector<int> prevHist(8, 0);
                    for (const auto v : prevLuma) {
                        prevHist[v / 32]++;
                    }
                    int sumAbs = 0;
                    int total = 0;
                    for (int i = 0; i < 8; ++i) {
                        sumAbs += std::abs(hist[static_cast<std::size_t>(i)] -
                                           prevHist[static_cast<std::size_t>(i)]);
                        total += std::max(hist[static_cast<std::size_t>(i)],
                                          prevHist[static_cast<std::size_t>(i)]);
                    }
                    diff = total > 0 ? static_cast<double>(sumAbs) / (2.0 * total) : 0.0;
                }
                const double colorDelta =
                    (std::abs(avgR - prevR) + std::abs(avgG - prevG) + std::abs(avgB - prevB)) /
                    3.0;
                diff = std::max(diff, colorDelta * 3.0);
                prevR = avgR;
                prevG = avgG;
                prevB = avgB;
                prevLuma = std::move(lumaGrid);

                FrameFeature ff;
                // 时间轴时间（采样点是片段本地偏移，直接落在时间轴刻度上）
                ff.timestamp = clip.start + Rational::make(
                                               static_cast<std::int64_t>(local * 1000.0), 1000);
                ff.avgR = avgR;
                ff.avgG = avgG;
                ff.avgB = avgB;
                ff.histogramDiff = std::min(1.0, diff);
                out.features.push_back(ff);
            }
            reader->close();
            if (truncated) {
                break;
            }
        }
        if (truncated) {
            out.warnings.push_back("视频采样点达到上限，后段未参与章节分析");
        }
    } else {
        out.warnings.push_back("未找到可用视频轨，章节仅依据音频边界");
    }

    // ---- 音频采样（时间轴坐标系） ----
    if (audioIndex >= 0) {
        const Track& track = seq->tracks[static_cast<std::size_t>(audioIndex)];
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !clip.asset.isValid() || clip.isCompound()) {
                continue;
            }
            const MediaAsset* asset = project.findAsset(clip.asset);
            if (asset == nullptr) {
                continue;
            }
            auto reader = readerFactory->create();
            if (reader == nullptr || !reader->open(asset->path).isOk()) {
                continue;
            }
            // 只解码 [sourceIn, sourceOut) 内的块，并映射到时间轴坐标
            if (!reader->seek(clip.sourceIn).isOk()) {
                reader->close();
                continue;
            }
            const Time sourceEnd = clip.sourceIn + clip.duration * clip.speed;
            AudioBlock block;
            std::size_t guard = 0;
            while (guard < config.maxAudioBlocks && reader->readAudioBlock(block)) {
                ++guard;
                if (block.start >= sourceEnd) {
                    break;
                }
                if (block.start + Rational::make(
                                      static_cast<std::int64_t>(block.frameCount()),
                                      std::max(1, block.sampleRate)) < clip.sourceIn) {
                    continue;
                }
                AudioBlock mapped = block;
                // 映射：源时间偏移 / 速度 → 时间轴偏移（速度 1 时等价）
                const Rational local =
                    (block.start - clip.sourceIn) / (clip.speed.isPositive() ? clip.speed
                                                                             : Rational::make(1));
                mapped.start = clip.start + (local.isNegative() ? Time{} : local);
                out.audio.push_back(std::move(mapped));
            }
            reader->close();
            if (guard >= config.maxAudioBlocks) {
                out.warnings.push_back("音频采样块达到上限，后段未参与章节分析");
                break;
            }
        }
    } else if (videoIndex >= 0) {
        out.warnings.push_back("未找到音频轨，章节仅依据画面边界");
    }

    if (!out.usable()) {
        out.note = "未采集到任何可分析数据";
    }
    return out;
}

// =============================================================================
// 落盘与重建
// =============================================================================

ChapterApplyResult applyChapters(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                                 ChapterDetectResult& result,
                                 const std::string& transactionName) {
    ChapterApplyResult out;
    Sequence* seq = project.findSequence(sequenceId);
    if (seq == nullptr) {
        out.result = Result::fail(ErrorCode::NotFound, "序列不存在");
        return out;
    }
    if (result.chapters.empty()) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "没有可落盘的章节");
        return out;
    }
    // 前置校验：全部章节参数合法（失败不产生副作用）
    for (auto& chapter : result.chapters) {
        if (!chapter.end.isPositive() || chapter.end < chapter.start) {
            out.result = Result::fail(ErrorCode::InvalidArgument, "章节时间区间非法");
            return out;
        }
        if (chapter.title.empty()) {
            chapter.title = suggestChapterTitle(chapter);
            if (chapter.title.empty()) {
                out.result = Result::fail(ErrorCode::InvalidArgument, "章节标题为空");
                return out;
            }
        }
    }

    editor.beginTransaction(transactionName);
    for (auto& chapter : result.chapters) {
        // 幂等：优先按 markerId 匹配；否则按「章节颜色 + 相同起点」匹配
        const Marker* existing = nullptr;
        if (chapter.markerId.isValid()) {
            for (const auto& m : seq->markers) {
                if (m.id == chapter.markerId) {
                    existing = &m;
                    break;
                }
            }
        }
        if (existing == nullptr) {
            for (const auto& m : seq->markers) {
                if (m.color == kChapterColor && m.time == chapter.start) {
                    existing = &m;
                    break;
                }
            }
        }
        Marker marker;
        if (existing != nullptr) {
            marker = *existing;
            marker.time = chapter.start;
            marker.name = chapter.title;
            marker.color = kChapterColor;
        } else {
            // 预分配 ID（保证 redo 确定性：重做时沿用同一 ID）
            marker.id = MarkerId{project.ids.next()};
            marker.time = chapter.start;
            marker.name = chapter.title;
            marker.color = kChapterColor;
        }
        const Result r = editor.upsertMarker(sequenceId, marker);
        if (!r.isOk()) {
            out.warnings.push_back("章节「" + chapter.title + "」落盘失败：" + r.message);
            continue;
        }
        chapter.markerId = marker.id;
        if (existing != nullptr) {
            ++out.markersUpdated;
        } else {
            ++out.markersAdded;
        }
        // 每轮后重新读取序列（前一轮可能重排标记列表）
        seq = project.findSequence(sequenceId);
        if (seq == nullptr) {
            break;
        }
    }
    editor.endTransaction();
    if (out.markersAdded == 0 && out.markersUpdated == 0) {
        out.result = Result::fail(ErrorCode::Internal, "章节标记未能写入");
        return out;
    }
    out.result = Result::ok();
    return out;
}

std::vector<Chapter> readChaptersFromSequence(const Sequence& sequence) {
    // 收集章节标记（章节颜色）
    std::vector<const Marker*> chapterMarkers;
    for (const auto& m : sequence.markers) {
        if (m.color == kChapterColor) {
            chapterMarkers.push_back(&m);
        }
    }
    std::sort(chapterMarkers.begin(), chapterMarkers.end(),
              [](const Marker* a, const Marker* b) { return a->time < b->time; });
    std::vector<Chapter> chapters;
    if (chapterMarkers.empty()) {
        return chapters;
    }
    const Time contentEnd = sequence.duration();
    for (std::size_t i = 0; i < chapterMarkers.size(); ++i) {
        Chapter ch;
        ch.index = static_cast<int>(i) + 1;
        ch.start = chapterMarkers[i]->time;
        ch.end = i + 1 < chapterMarkers.size() ? chapterMarkers[i + 1]->time : contentEnd;
        ch.title = chapterMarkers[i]->name;
        ch.markerId = chapterMarkers[i]->id;
        ch.boundary = i == 0 ? Chapter::Boundary::Start : Chapter::Boundary::Unknown;
        if (ch.end > ch.start) {
            chapters.push_back(std::move(ch));
        }
    }
    // 重排序号（剔除非法区间后可能缺号）
    for (std::size_t i = 0; i < chapters.size(); ++i) {
        chapters[i].index = static_cast<int>(i) + 1;
    }
    return chapters;
}

}  // namespace core
}  // namespace nairoclip
