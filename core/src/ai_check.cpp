#include "nairoclip/core/ai_check.h"

#include "nairoclip/core/audio_mixer.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace nairoclip {
namespace core {

namespace {

/// 秒 → Time（微秒精度，避免浮点累计误差）。
Time secondsToTime(double seconds) {
    return Rational::make(static_cast<std::int64_t>(seconds * 1000000.0 + 0.5), 1000000);
}

/// 字幕重叠检测：同一时刻可见多条字幕（按时间扫描）。
void checkSubtitleOverlaps(const Sequence& sequence, const AiCheckConfig& config,
                           AiCheckReport& report) {
    const auto& cues = sequence.subtitles.cues();
    if (cues.size() < 2) {
        return;
    }
    const Time tolerance = secondsToTime(config.subtitleOverlapToleranceSeconds);
    // 输入已按 start 升序（SubtitleTrack 不变量）；逐对比较邻接项。
    // 注意：允许双语叠放（设计上同刻两条不同语言），但完全重叠且文本近似
    // 视为误操作（同文案重复输入是最常见的真实场景）。
    for (std::size_t i = 0; i + 1 < cues.size(); ++i) {
        const SubtitleCue& a = cues[i];
        if (!a.enabled) {
            continue;
        }
        for (std::size_t j = i + 1; j < cues.size(); ++j) {
            const SubtitleCue& b = cues[j];
            if (!b.enabled) {
                continue;
            }
            // b.start >= a.start（有序）；若 b.start >= a.end 则后续都不可能重叠
            if (!(b.start < a.end())) {
                break;
            }
            const Time overlap = a.end() - b.start;
            if (overlap <= tolerance) {
                continue;
            }
            AiCheckIssue issue;
            issue.type = AiCheckIssueType::SubtitleOverlap;
            issue.severity = AiCheckSeverity::Warning;
            issue.time = b.start;
            // 关联后一条字幕（修复时调整它）
            issue.entityId = b.id.value;
            issue.message = "字幕重叠：" + std::to_string(static_cast<int>(overlap.toDouble() * 1000)) +
                            "ms（「" + b.text + "」与「" + a.text + "」）";
            issue.autoFixable = true;
            // 携实测重叠秒数（供 UI 展示与修复策略判断，不使用估算值）
            issue.measuredValue = overlap.toDouble();
            report.issues.push_back(std::move(issue));
        }
    }
}

/// 字幕过短 / 空字幕检测。
void checkSubtitleQuality(const Sequence& sequence, const AiCheckConfig& config,
                          AiCheckReport& report) {
    const Time minDuration = secondsToTime(config.subtitleMinDurationSeconds);
    for (const auto& cue : sequence.subtitles.cues()) {
        if (!cue.enabled) {
            continue;
        }
        // 空字幕（无文本）
        bool blank = true;
        for (const char c : cue.text) {
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                blank = false;
                break;
            }
        }
        if (blank) {
            AiCheckIssue issue;
            issue.type = AiCheckIssueType::SubtitleEmpty;
            issue.severity = AiCheckSeverity::Info;
            issue.time = cue.start;
            issue.entityId = cue.id.value;
            issue.message = "空字幕（无文本内容）";
            issue.autoFixable = false;  // 删除需用户确认（避免误删占位）
            report.issues.push_back(std::move(issue));
            continue;
        }
        // 过短
        if (minDuration.isPositive() && cue.duration < minDuration) {
            AiCheckIssue issue;
            issue.type = AiCheckIssueType::SubtitleTooShort;
            issue.severity = AiCheckSeverity::Info;
            issue.time = cue.start;
            issue.entityId = cue.id.value;
            issue.message = "字幕过短（" +
                            std::to_string(static_cast<int>(cue.duration.toDouble() * 1000)) +
                            "ms，建议 ≥" +
                            std::to_string(static_cast<int>(minDuration.toDouble() * 1000)) +
                            "ms）：「" + cue.text + "」";
            issue.autoFixable = false;
            report.issues.push_back(std::move(issue));
        }
    }
}

/// 音频峰值过高检测：真实解码各音频片段样本，统计窗口内峰值。
/// 解码器由 readerFactory 提供；为空/打开失败时跳过并记录 note。
void checkAudioPeaks(const Project& project, const Sequence& sequence,
                     MediaReaderFactory* readerFactory, const AiCheckConfig& config,
                     AiCheckReport& report) {
    if (readerFactory == nullptr) {
        report.notes.push_back("未提供解码器，已跳过音频峰值检查");
        return;
    }
    // 按素材聚合统计（同一素材多次引用只解码一次）
    std::map<std::uint64_t, std::vector<double>> peaksByAsset;  // assetId → 每秒窗峰值
    // 预先聚合每个素材被引用的最大源终点（避免只解码到第一个片段的长度；
    // 同一素材可能被多个片段引用到不同区间）。
    std::map<std::uint64_t, double> maxSourceOutByAsset;
    for (const auto& track : sequence.tracks) {
        if (track.type != TrackType::Audio || track.muted) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !clip.asset.isValid()) {
                continue;
            }
            const double sourceOut = clip.sourceOut().toDouble();
            auto it = maxSourceOutByAsset.find(clip.asset.value);
            if (it == maxSourceOutByAsset.end() || sourceOut > it->second) {
                maxSourceOutByAsset[clip.asset.value] = sourceOut;
            }
        }
    }
    for (const auto& track : sequence.tracks) {
        if (track.type != TrackType::Audio || track.muted) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !clip.asset.isValid()) {
                continue;
            }
            const MediaAsset* asset = project.findAsset(clip.asset);
            if (asset == nullptr || asset->offline) {
                continue;
            }
            auto it = peaksByAsset.find(clip.asset.value);
            if (it != peaksByAsset.end()) {
                continue;  // 已解码过
            }
            auto reader = readerFactory->create();
            if (reader == nullptr || !reader->open(asset->path).isOk()) {
                report.notes.push_back("音频素材无法解码：" + asset->name);
                peaksByAsset.emplace(clip.asset.value, std::vector<double>{});
                continue;
            }
            // 逐块读取，按窗口统计峰值（窗口长度按素材实际采样率换算）
            const int sampleRate =
                reader->probe().sampleRate > 0 ? reader->probe().sampleRate : 48000;
            const std::size_t windowSamples = std::max<std::size_t>(
                1, static_cast<std::size_t>(static_cast<double>(sampleRate) *
                                            config.audioPeakWindowSeconds));
            std::vector<double> windowPeaks;
            AudioBlock block;
            std::size_t windowCount = 0;
            double windowPeak = 0.0;
            // 上限保护：只解码该素材被引用的最大源终点（+ 少量余量），避免长素材
            // 全量解码（真实场景：素材可能数小时，而片段只用了其中一小段）。
            // 同时设绝对上限块数，防止异常素材（duration 探测失真）导致长时间占用。
            const double maxSourceOut = maxSourceOutByAsset.count(clip.asset.value) > 0
                                            ? maxSourceOutByAsset[clip.asset.value]
                                            : clip.sourceOut().toDouble();
            const double blockSeconds =
                block.sampleRate > 0 ? 1024.0 / static_cast<double>(block.sampleRate)
                                     : 1024.0 / 48000.0;
            const std::size_t maxBlocks = static_cast<std::size_t>(
                std::max<long long>(1, static_cast<long long>(
                                           std::ceil((maxSourceOut + 0.5) / blockSeconds))));
            const std::size_t hardCapBlocks = 200000;  // ≈ 70 分钟 @48kHz/1024
            std::size_t blocksRead = 0;
            while (blocksRead < std::min(maxBlocks, hardCapBlocks) &&
                   reader->readAudioBlock(block)) {
                ++blocksRead;
                for (const float sample : block.samples) {
                    const double magnitude = std::fabs(static_cast<double>(sample));
                    windowPeak = std::max(windowPeak, magnitude);
                    ++windowCount;
                    if (windowCount >= windowSamples) {
                        windowPeaks.push_back(windowPeak);
                        windowPeak = 0.0;
                        windowCount = 0;
                    }
                }
            }
            if (windowCount > 0) {
                windowPeaks.push_back(windowPeak);
            }
            reader->close();
            peaksByAsset.emplace(clip.asset.value, std::move(windowPeaks));
        }
    }
    // 生成问题（每处：片段 + 窗口序号 → 时间点 = 片段起点 + 窗口偏移）
    for (const auto& track : sequence.tracks) {
        if (track.type != TrackType::Audio || track.muted) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !clip.asset.isValid()) {
                continue;
            }
            const auto it = peaksByAsset.find(clip.asset.value);
            if (it == peaksByAsset.end()) {
                continue;
            }
            const auto& windowPeaks = it->second;
            for (std::size_t w = 0; w < windowPeaks.size(); ++w) {
                if (windowPeaks[w] < config.audioPeakThreshold) {
                    continue;
                }
                AiCheckIssue issue;
                issue.type = AiCheckIssueType::AudioPeakClipping;
                issue.severity = AiCheckSeverity::Error;
                // 窗口序号 → 片段内偏移（源时间；速度 1 时与时间轴一致）
                const Time windowOffset =
                    secondsToTime(static_cast<double>(w) * config.audioPeakWindowSeconds);
                const Time timelineOffset = windowOffset / clip.speed;
                issue.time = clip.start + timelineOffset;
                if (issue.time >= clip.end()) {
                    continue;  // 超出片段范围（末窗口保护）
                }
                issue.entityId = clip.id.value;
                issue.trackId = track.id;
                issue.message = "音频峰值过高（" +
                                std::to_string(static_cast<int>(windowPeaks[w] * 100.0)) +
                                "% 满刻度，建议降低增益）";
                issue.autoFixable = true;
                // 携带真实峰值：修复时据此精确计算安全增益（不估算）
                issue.measuredValue = windowPeaks[w];
                report.issues.push_back(std::move(issue));
            }
        }
    }
}

/// 静音片段检测（可能素材问题）：整段 RMS 低于阈值的音频片段。
void checkSilentClips(const Project& project, const Sequence& sequence,
                      MediaReaderFactory* readerFactory, const AiCheckConfig& config,
                      AiCheckReport& report) {
    if (readerFactory == nullptr) {
        return;  // 已在峰值检查中记录 note
    }
    for (const auto& track : sequence.tracks) {
        if (track.type != TrackType::Audio || track.muted) {
            continue;
        }
        for (const auto& clip : track.clips) {
            if (!clip.enabled || !clip.asset.isValid()) {
                continue;
            }
            const MediaAsset* asset = project.findAsset(clip.asset);
            if (asset == nullptr || asset->offline) {
                continue;
            }
            auto reader = readerFactory->create();
            if (reader == nullptr || !reader->open(asset->path).isOk()) {
                continue;
            }
            double sumSquares = 0.0;
            std::size_t sampleCount = 0;
            AudioBlock block;
            int blocksRead = 0;
            // 上限：最多读 100 块（约 2 秒 @48kHz/1024），静音判定足够且快速
            while (blocksRead < 100 && reader->readAudioBlock(block)) {
                for (const float sample : block.samples) {
                    const double v = static_cast<double>(sample);
                    sumSquares += v * v;
                    ++sampleCount;
                }
                ++blocksRead;
            }
            reader->close();
            if (sampleCount == 0) {
                continue;
            }
            const double rms = std::sqrt(sumSquares / static_cast<double>(sampleCount));
            if (rms >= config.silenceRmsThreshold) {
                continue;
            }
            AiCheckIssue issue;
            issue.type = AiCheckIssueType::AudioSilentClip;
            issue.severity = AiCheckSeverity::Warning;
            issue.time = clip.start;
            issue.entityId = clip.id.value;
            issue.trackId = track.id;
            issue.message = "音频片段近乎静音（RMS " + std::to_string(rms) +
                            "，检查素材或增益设置）";
            issue.autoFixable = false;
            report.issues.push_back(std::move(issue));
        }
    }
}

/// 视频轨空隙检测（黑场跳变，多为误删）。
void checkClipGaps(const Sequence& sequence, const AiCheckConfig& config, AiCheckReport& report) {
    const Time threshold = secondsToTime(config.clipGapThresholdSeconds);
    for (const auto& track : sequence.tracks) {
        if (track.type != TrackType::Video || track.hidden) {
            continue;
        }
        Time cursor;  // 已覆盖区间终点
        bool first = true;
        for (const auto& clip : track.clips) {
            if (!clip.enabled) {
                continue;
            }
            if (!first && clip.start > cursor) {
                const Time gap = clip.start - cursor;
                if (gap >= threshold) {
                    AiCheckIssue issue;
                    issue.type = AiCheckIssueType::ClipGap;
                    issue.severity = AiCheckSeverity::Info;
                    issue.time = cursor;
                    issue.trackId = track.id;
                    issue.message = "视频空隙（" +
                                    std::to_string(static_cast<int>(gap.toDouble() * 1000)) +
                                    "ms 黑场）：「" + track.name + "」";
                    issue.autoFixable = false;  // 自动吸附会改变用户节奏，交由手动
                    report.issues.push_back(std::move(issue));
                }
            }
            cursor = first ? clip.end() : std::max(cursor, clip.end());
            first = false;
        }
    }
}

}  // namespace

// =============================================================================
// 报告与主入口
// =============================================================================

std::size_t AiCheckReport::countOf(AiCheckIssueType type) const {
    const std::size_t index = static_cast<std::size_t>(type);
    return index < countsByType.size() ? countsByType[index] : 0;
}

bool AiCheckReport::hasAutoFixable() const {
    for (const auto& issue : issues) {
        if (issue.autoFixable) {
            return true;
        }
    }
    return false;
}

std::string AiCheckReport::summary() const {
    if (issues.empty()) {
        return std::string();
    }
    std::string text;
    const AiCheckIssueType types[] = {
        AiCheckIssueType::SubtitleOverlap,    AiCheckIssueType::SubtitleTooShort,
        AiCheckIssueType::SubtitleEmpty,      AiCheckIssueType::AudioPeakClipping,
        AiCheckIssueType::AudioSilentClip,    AiCheckIssueType::ClipGap,
    };
    for (const auto type : types) {
        const std::size_t count = countOf(type);
        if (count == 0) {
            continue;
        }
        if (!text.empty()) {
            text += "；";
        }
        text += aiCheckIssueName(type);
        text += " " + std::to_string(count) + " 处";
    }
    return text;
}

const char* aiCheckIssueName(AiCheckIssueType type) {
    switch (type) {
        case AiCheckIssueType::SubtitleOverlap:
            return "字幕重叠";
        case AiCheckIssueType::SubtitleTooShort:
            return "字幕过短";
        case AiCheckIssueType::SubtitleEmpty:
            return "空字幕";
        case AiCheckIssueType::AudioPeakClipping:
            return "音频峰值过高";
        case AiCheckIssueType::AudioSilentClip:
            return "音频片段静音";
        case AiCheckIssueType::ClipGap:
            return "视频空隙";
    }
    return "未知问题";
}

AiCheckReport checkSequence(const Project& project, SequenceId sequenceId,
                            MediaReaderFactory* readerFactory, const AiCheckConfig& config) {
    AiCheckReport report;
    report.countsByType.assign(6, 0);
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        report.notes.push_back("序列不存在");
        return report;
    }
    if (!config.valid()) {
        report.notes.push_back("检查配置非法");
        return report;
    }
    report.scannedDuration = sequence->duration();

    if (config.checkSubtitles) {
        if (sequence->subtitles.empty()) {
            report.notes.push_back("未检测到字幕");
        } else {
            checkSubtitleOverlaps(*sequence, config, report);
            checkSubtitleQuality(*sequence, config, report);
        }
    }
    if (config.checkAudio) {
        checkAudioPeaks(project, *sequence, readerFactory, config, report);
        checkSilentClips(project, *sequence, readerFactory, config, report);
    }
    if (config.checkGaps) {
        checkClipGaps(*sequence, config, report);
    }

    // 排序：严重级别降序 → 时间升序（UI 列表顺序：先看严重问题）
    std::stable_sort(report.issues.begin(), report.issues.end(),
                     [](const AiCheckIssue& a, const AiCheckIssue& b) {
                         if (a.severity != b.severity) {
                             return static_cast<int>(a.severity) > static_cast<int>(b.severity);
                         }
                         return a.time < b.time;
                     });
    // 统计
    for (const auto& issue : report.issues) {
        const std::size_t index = static_cast<std::size_t>(issue.type);
        if (index < report.countsByType.size()) {
            ++report.countsByType[index];
        }
    }
    return report;
}

// =============================================================================
// 修复（经编辑器命令，可撤销）
// =============================================================================

Result fixIssue(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                const AiCheckIssue& issue, const AiCheckConfig& config) {
    const Sequence* sequence = project.findSequence(sequenceId);
    if (sequence == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    switch (issue.type) {
        case AiCheckIssueType::SubtitleOverlap: {
            // 修复策略：把后一条字幕起点移到前一条结束处；若导致时长非法则缩短原时长
            const SubtitleCue* later = sequence->subtitles.findCue(SubtitleId{issue.entityId});
            if (later == nullptr) {
                return Result::fail(ErrorCode::NotFound, "字幕不存在");
            }
            // 找与它重叠的前一条（同刻可见且更早开始）
            const SubtitleCue* previous = nullptr;
            for (const auto& cue : sequence->subtitles.cues()) {
                if (cue.id == later->id || !cue.enabled) {
                    continue;
                }
                if (cue.start < later->start && later->start < cue.end()) {
                    previous = &cue;
                    break;
                }
            }
            if (previous == nullptr) {
                return Result::fail(ErrorCode::NotFound, "未找到重叠的前一条字幕");
            }
            const Time newStart = previous->end();
            const Time newEnd = later->end() > newStart + secondsToTime(0.2)
                                    ? later->end()
                                    : newStart + secondsToTime(0.2);
            SubtitleCue fixed = *later;
            fixed.start = newStart;
            fixed.duration = newEnd - newStart;
            return editor.upsertSubtitle(sequenceId, std::move(fixed));
        }
        case AiCheckIssueType::AudioPeakClipping: {
            // 修复策略：找到片段，按峰值超限比例施加安全增益（留 5% 余量）
            if (!issue.trackId.isValid()) {
                return Result::fail(ErrorCode::InvalidArgument, "缺少轨道信息");
            }
            const Track* track = findTrack(project, sequenceId, issue.trackId);
            if (track == nullptr) {
                return Result::fail(ErrorCode::NotFound, "轨道不存在");
            }
            const Clip* clip = track->findClip(ClipId{issue.entityId});
            if (clip == nullptr) {
                return Result::fail(ErrorCode::NotFound, "片段不存在");
            }
            // 精确修复：用检测到的真实峰值算增益，把峰值压到目标线
            // （目标 = 阈值 × 0.95，约留 0.45dB 余量，避免修复后仍贴顶）。
            const double currentPeak =
                issue.measuredValue > 0.0 ? issue.measuredValue : config.audioPeakThreshold;
            const double target = config.audioPeakThreshold * 0.95;
            // 防过冲：峰值不可能低于正常电平，ratio 上限 1（不提升音量）
            const double ratio = std::min(1.0, target / std::max(1e-9, currentPeak));
            const double gainDb = 20.0 * std::log10(std::max(1e-6, ratio));
            const double newGain = clip->audioGainDb + gainDb;
            // 增益下限保护（不低于 -20dB，避免过度压制改变听感）
            const double clampedGain = std::max(-20.0, newGain);
            return editor.setClipAudio(sequenceId, issue.trackId, ClipId{issue.entityId},
                                       clampedGain, clip->audioFadeIn, clip->audioFadeOut);
        }
        case AiCheckIssueType::SubtitleTooShort:
        case AiCheckIssueType::SubtitleEmpty:
        case AiCheckIssueType::AudioSilentClip:
        case AiCheckIssueType::ClipGap:
            return Result::fail(ErrorCode::Unsupported, "该问题不支持自动修复");
    }
    return Result::fail(ErrorCode::Unsupported, "未知问题类型");
}

AiCheckFixResult fixAllIssues(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                              const std::vector<AiCheckIssue>& issues,
                              const AiCheckConfig& config) {
    AiCheckFixResult result;
    editor.beginTransaction("AI 检查自动修复");
    for (const auto& issue : issues) {
        if (!issue.autoFixable) {
            ++result.skipped;
            continue;
        }
        const Result r = fixIssue(editor, project, sequenceId, issue, config);
        if (r.isOk()) {
            ++result.fixed;
        } else if (r.code == ErrorCode::Unsupported) {
            ++result.skipped;
        } else {
            ++result.failed;
            result.failedIssues.push_back(issue);
        }
    }
    const bool grouped = editor.endTransaction();
    (void)grouped;
    return result;
}

}  // namespace core
}  // namespace nairoclip
