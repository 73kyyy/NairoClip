#include "nairoclip/core/speech_cut.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

/// 秒 → Time（微秒精度）。
Time secondsToTime(double seconds) {
    return Rational::make(static_cast<std::int64_t>(seconds * 1000000.0 + 0.5), 1000000);
}

/// Time → 秒。
double timeToSeconds(const Time& t) { return t.toDouble(); }

/// 计算帧 RMS（单声道下混平均值）。
double frameRms(const std::vector<float>& mono, std::int64_t start, std::int64_t count) {
    if (count <= 0 || start < 0 || start >= static_cast<std::int64_t>(mono.size())) {
        return 0.0;
    }
    const std::int64_t end = std::min(start + count, static_cast<std::int64_t>(mono.size()));
    if (end <= start) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::int64_t i = start; i < end; ++i) {
        const double v = static_cast<double>(mono[static_cast<std::size_t>(i)]);
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(end - start));
}

}  // namespace

// =============================================================================
// 语音活动分析
// =============================================================================

SpeechActivityResult analyzeSpeechActivity(const std::vector<AudioBlock>& blocks,
                                           const SpeechDetectConfig& config) {
    SpeechActivityResult result;
    if (!config.valid()) {
        result.note = "语音检测参数非法";
        return result;
    }
    if (blocks.empty()) {
        result.note = "无音频输入";
        return result;
    }
    const int sampleRate = blocks.front().sampleRate > 0 ? blocks.front().sampleRate : 48000;
    result.frameSeconds =
        static_cast<double>(std::max(16, config.hopSize)) / static_cast<double>(sampleRate);

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
    const std::int64_t totalFrames = static_cast<std::int64_t>(mono.size());
    if (totalFrames < config.frameSize) {
        result.note = "音频过短，无法分析";
        return result;
    }
    result.sourceDuration =
        blocks.front().start + Rational::make(totalFrames, sampleRate);

    // ---- 2) 逐帧 RMS → 语音活动布尔序列 ----
    const int frameSize = std::max(64, config.frameSize);
    const int hopSize = std::max(16, std::min(config.hopSize, frameSize));
    const std::int64_t frameCount = (totalFrames - frameSize) / hopSize + 1;
    result.rmsByFrame.resize(static_cast<std::size_t>(frameCount), 0.0);
    std::vector<bool> active(static_cast<std::size_t>(frameCount), false);
    std::size_t activeCount = 0;
    for (std::int64_t f = 0; f < frameCount; ++f) {
        const double rms = frameRms(mono, f * hopSize, frameSize);
        result.rmsByFrame[static_cast<std::size_t>(f)] = rms;
        if (rms >= config.silenceRmsThreshold) {
            active[static_cast<std::size_t>(f)] = true;
            ++activeCount;
        }
    }
    if (activeCount == 0) {
        result.note = "素材无有效语音（静音阈值可调低）";
        return result;
    }

    // ---- 3) 活动帧 → 初步区间（gap > minSilence 才切断）----
    struct Span {
        std::int64_t startFrame;
        std::int64_t endFrame;  // 闭区间
    };
    const std::int64_t minSilenceFrames = static_cast<std::int64_t>(
        config.minSilenceSeconds / std::max(1e-9, result.frameSeconds) + 0.5);
    std::vector<Span> spans;
    std::int64_t spanStart = -1;
    std::int64_t lastActive = -1;
    for (std::int64_t f = 0; f < frameCount; ++f) {
        if (!active[static_cast<std::size_t>(f)]) {
            continue;
        }
        if (spanStart < 0) {
            spanStart = f;
        } else if (f - lastActive > minSilenceFrames + 1) {
            spans.push_back(Span{spanStart, lastActive});
            spanStart = f;
        }
        lastActive = f;
    }
    if (spanStart >= 0) {
        spans.push_back(Span{spanStart, lastActive});
    }
    if (spans.empty()) {
        result.note = "未形成有效语音段";
        return result;
    }

    // ---- 4) 边界处理：padding 外扩 + minKeep 合并 ----
    const double frameSeconds = result.frameSeconds;
    struct RawSegment {
        double start;
        double end;
    };
    std::vector<RawSegment> raws;
    for (const auto& span : spans) {
        RawSegment segment;
        segment.start = static_cast<double>(span.startFrame) * frameSeconds - config.paddingSeconds;
        segment.end =
            static_cast<double>(span.endFrame + 1) * frameSeconds + config.paddingSeconds;
        segment.start = std::max(0.0, segment.start);
        segment.end = std::min(static_cast<double>(totalFrames) / sampleRate, segment.end);
        if (segment.end > segment.start) {
            raws.push_back(segment);
        }
    }
    // 合并（padding 可能使相邻区间重叠/过近）+ minKeep 过滤
    std::vector<RawSegment> merged;
    for (const auto& raw : raws) {
        if (!merged.empty() && raw.start - merged.back().end <= config.minKeepSeconds) {
            merged.back().end = std::max(merged.back().end, raw.end);
        } else {
            merged.push_back(raw);
        }
    }
    // maxKeep 切分（超长段等分）
    std::vector<RawSegment> finalSegments;
    for (const auto& segment : merged) {
        const double length = segment.end - segment.start;
        if (config.maxKeepSeconds > 0.0 && length > config.maxKeepSeconds) {
            const int pieces = static_cast<int>(length / config.maxKeepSeconds) + 1;
            const double piece = length / pieces;
            for (int p = 0; p < pieces; ++p) {
                RawSegment pieceSegment;
                pieceSegment.start = segment.start + piece * p;
                pieceSegment.end = (p == pieces - 1) ? segment.end : segment.start + piece * (p + 1);
                finalSegments.push_back(pieceSegment);
            }
        } else {
            finalSegments.push_back(segment);
        }
    }

    // ---- 5) 输出（含平均 RMS）----
    const Time origin = blocks.front().start;
    double speechSeconds = 0.0;
    for (const auto& segment : finalSegments) {
        SpeechActivityResult::KeepSegment keep;
        keep.start = origin + secondsToTime(segment.start);
        keep.end = origin + secondsToTime(segment.end);
        // 区间内平均 RMS（按帧采样）
        const std::int64_t firstFrame =
            std::max<std::int64_t>(0, static_cast<std::int64_t>(segment.start / frameSeconds));
        const std::int64_t lastFrame = std::min<std::int64_t>(
            frameCount - 1, static_cast<std::int64_t>(segment.end / frameSeconds));
        double sum = 0.0;
        int count = 0;
        for (std::int64_t f = firstFrame; f <= lastFrame; ++f) {
            sum += result.rmsByFrame[static_cast<std::size_t>(f)];
            ++count;
        }
        keep.averageRms = count > 0 ? sum / static_cast<double>(count) : 0.0;
        speechSeconds += segment.end - segment.start;
        result.keepSegments.push_back(keep);
    }
    result.speechDuration = secondsToTime(speechSeconds);
    const double totalSeconds = static_cast<double>(totalFrames) / sampleRate;
    result.silenceDuration = secondsToTime(std::max(0.0, totalSeconds - speechSeconds));
    result.keepRatio = totalSeconds > 0.0 ? speechSeconds / totalSeconds : 1.0;
    return result;
}

// =============================================================================
// 精剪计划
// =============================================================================

namespace {

/// 单个片段的精剪映射（共用逻辑）。
/// 保留区间（源坐标）裁剪到 [rangeStart, rangeEnd)，再平移为源起点。
std::vector<SpeechCutPlan::Cut> mapSegmentsToCuts(
    AssetId asset, const Time& rangeStart, const Time& rangeEnd,
    const std::vector<SpeechActivityResult::KeepSegment>& segments, Time& cursor,
    double& keptSeconds, const Time& speed) {
    std::vector<SpeechCutPlan::Cut> cuts;
    for (const auto& segment : segments) {
        // 区间与片段范围求交
        const Time start = std::max(segment.start, rangeStart);
        const Time end = std::min(segment.end, rangeEnd);
        if (!(end > start)) {
            continue;
        }
        SpeechCutPlan::Cut cut;
        cut.asset = asset;
        cut.sourceStart = start;
        cut.duration = end - start;
        // speed 映射：源时长 → 时间轴时长
        if (speed != Rational::make(1)) {
            cut.duration = cut.duration / speed;
        }
        cut.timelineStart = cursor;
        cursor = cursor + cut.duration;
        keptSeconds += (end - start).toDouble();
        cuts.push_back(cut);
    }
    return cuts;
}

}  // namespace

SpeechCutPlan planSpeechCut(const Clip& targetClip, const SpeechActivityResult& activity,
                            bool keepGaps) {
    SpeechCutPlan plan;
    (void)keepGaps;
    if (!activity.valid()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument,
                                   activity.note.empty() ? "语音分析无效" : activity.note);
        return plan;
    }
    if (!targetClip.asset.isValid()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument, "片段未引用素材");
        return plan;
    }
    const Time rangeStart = targetClip.sourceIn;
    const Time rangeEnd = targetClip.sourceOut();
    Time cursor = targetClip.start;
    double keptSeconds = 0.0;
    plan.cuts = mapSegmentsToCuts(targetClip.asset, rangeStart, rangeEnd, activity.keepSegments,
                                  cursor, keptSeconds, targetClip.speed);
    if (plan.cuts.empty()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument, "片段范围内无有效语音");
        return plan;
    }
    plan.originalDuration = targetClip.duration;
    plan.resultDuration = cursor - targetClip.start;
    plan.removedDuration = plan.originalDuration - plan.resultDuration;
    if (plan.removedDuration.isNegative()) {
        plan.removedDuration = Rational::make(0);
    }
    const double originalSeconds = std::max(1e-9, plan.originalDuration.toDouble());
    const double savedRatio = plan.removedDuration.toDouble() / originalSeconds;
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "剪掉 %.1f 秒静音（缩短 %.0f%%），保留 %.1f 秒",
                  plan.removedDuration.toDouble(), savedRatio * 100.0,
                  plan.resultDuration.toDouble());
    plan.summary = buffer;
    plan.result = Result::ok();
    return plan;
}

SpeechCutPlan planSpeechCutForTrack(const Track& track, const SpeechActivityResult& activity,
                                    bool keepGaps) {
    SpeechCutPlan plan;
    if (!activity.valid()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument,
                                   activity.note.empty() ? "语音分析无效" : activity.note);
        return plan;
    }
    if (track.clips.empty()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument, "轨道无片段");
        return plan;
    }
    // 按片段顺序处理：保留片段间原空隙（keepGaps）或紧凑排布
    Time cursor = track.clips.front().start;
    double keptSeconds = 0.0;
    double originalSeconds = 0.0;
    for (const auto& clip : track.clips) {
        if (!clip.enabled || !clip.asset.isValid()) {
            continue;
        }
        if (keepGaps) {
            cursor = std::max(cursor, clip.start);
        }
        const std::size_t before = plan.cuts.size();
        const Time cursorBefore = cursor;
        auto cuts = mapSegmentsToCuts(clip.asset, clip.sourceIn, clip.sourceOut(),
                                      activity.keepSegments, cursor, keptSeconds, clip.speed);
        // 修正 timelineStart：mapSegmentsToCuts 从传入 cursor 起排，保持正确
        plan.cuts.insert(plan.cuts.end(), cuts.begin(), cuts.end());
        originalSeconds += clip.duration.toDouble();
        if (plan.cuts.size() == before) {
            // 该片段无有效语音：若无空隙模式则跳过（片段被剪掉），否则跳过并保持 cursor
            cursor = cursorBefore;
        }
        if (keepGaps) {
            cursor = std::max(cursor, clip.end());
        }
    }
    if (plan.cuts.empty()) {
        plan.result = Result::fail(ErrorCode::InvalidArgument, "轨道范围内无有效语音");
        return plan;
    }
    plan.originalDuration = secondsToTime(originalSeconds);
    plan.resultDuration = plan.cuts.back().timelineStart + plan.cuts.back().duration -
                          plan.cuts.front().timelineStart;
    plan.removedDuration = plan.originalDuration - plan.resultDuration;
    if (plan.removedDuration.isNegative()) {
        plan.removedDuration = Rational::make(0);
    }
    const double original = std::max(1e-9, plan.originalDuration.toDouble());
    char buffer[128];
    std::snprintf(buffer, sizeof(buffer), "剪掉 %.1f 秒静音（缩短 %.0f%%），保留 %.1f 秒",
                  plan.removedDuration.toDouble(),
                  plan.removedDuration.toDouble() / original * 100.0,
                  plan.resultDuration.toDouble());
    plan.summary = buffer;
    plan.result = Result::ok();
    return plan;
}

// =============================================================================
// 落盘
// =============================================================================

SpeechCutApplyResult applySpeechCut(TimelineEditor& editor, Project& project,
                                    SequenceId sequenceId, TrackId trackId,
                                    const std::vector<ClipId>& targetClipIds,
                                    const SpeechActivityResult& activity,
                                    const std::string& transactionName) {
    SpeechCutApplyResult result;
    // 1) 前置校验
    Track* track = findTrack(project, sequenceId, trackId);
    if (track == nullptr) {
        result.result = Result::fail(ErrorCode::NotFound, "目标轨道不存在");
        return result;
    }
    if (track->locked) {
        result.result = Result::fail(ErrorCode::Locked, "目标轨道已锁定");
        return result;
    }
    // 收集待替换片段（指定的或全部）
    std::vector<Clip> targets;
    if (targetClipIds.empty()) {
        targets = track->clips;
    } else {
        for (const auto& clipId : targetClipIds) {
            const Clip* clip = track->findClip(clipId);
            if (clip == nullptr) {
                result.result = Result::fail(ErrorCode::NotFound, "待处理片段不存在");
                return result;
            }
            targets.push_back(*clip);
        }
    }
    if (targets.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "轨道无片段可处理");
        return result;
    }
    // 2) 事务外先生成计划（失败不产生副作用）
    // 单片段复用 planSpeechCut；多片段逐段生成
    std::vector<SpeechCutPlan::Cut> allCuts;
    Time cursor = targets.front().start;
    double keptSeconds = 0.0;
    for (const auto& clip : targets) {
        if (!clip.enabled || !clip.asset.isValid()) {
            continue;
        }
        const std::size_t before = allCuts.size();
        auto cuts = mapSegmentsToCuts(clip.asset, clip.sourceIn, clip.sourceOut(),
                                      activity.keepSegments, cursor, keptSeconds, clip.speed);
        allCuts.insert(allCuts.end(), cuts.begin(), cuts.end());
        (void)before;
    }
    if (allCuts.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument,
                                     activity.note.empty() ? "无保留区间可落盘" : activity.note);
        return result;
    }

    // 3) 事务执行：先删旧片段，再插入保留段
    editor.beginTransaction(transactionName);
    for (const auto& clip : targets) {
        const Result r = editor.removeClip(sequenceId, trackId, clip.id);
        if (r.isOk()) {
            ++result.clipsRemoved;
        }
    }
    for (const auto& cut : allCuts) {
        Clip clip;
        clip.asset = cut.asset;
        clip.start = cut.timelineStart;
        clip.duration = cut.duration;
        clip.sourceIn = cut.sourceStart;
        const Result r = editor.insertClip(sequenceId, trackId, clip);
        if (r.isOk()) {
            ++result.clipsInserted;
        }
    }
    const bool grouped = editor.endTransaction();
    (void)grouped;
    if (result.clipsInserted == 0) {
        result.result = Result::fail(ErrorCode::Internal, "精剪未插入任何片段");
        return result;
    }
    result.result = Result::ok();
    return result;
}

// =============================================================================
// 便捷入口
// =============================================================================

SpeechActivityResult analyzeAssetSpeech(const MediaAsset& asset, MediaReaderFactory* readerFactory,
                                        const SpeechDetectConfig& config) {
    SpeechActivityResult result;
    if (readerFactory == nullptr) {
        result.note = "未提供解码器，无法分析语音活动";
        return result;
    }
    if (asset.path.empty()) {
        result.note = "素材路径为空";
        return result;
    }
    auto reader = readerFactory->create();
    if (reader == nullptr || !reader->open(asset.path).isOk()) {
        result.note = "音频素材无法解码：" + asset.name;
        return result;
    }
    std::vector<AudioBlock> blocks;
    AudioBlock block;
    // 上限保护（≈ 70 分钟 @48kHz/1024）
    const std::size_t hardCapBlocks = 200000;
    while (blocks.size() < hardCapBlocks && reader->readAudioBlock(block)) {
        blocks.push_back(block);
    }
    reader->close();
    return analyzeSpeechActivity(blocks, config);
}

}  // namespace core
}  // namespace nairoclip
