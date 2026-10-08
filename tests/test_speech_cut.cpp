#include "test_framework.h"
#include "nairoclip/core/speech_cut.h"
#include "nairoclip/core/editor.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr double kPi = 3.141592653589793;

/// 合成语音轨：在 speechWindows 内为 300Hz 正弦（幅度 0.3），其余静音。
/// 用于验证 VAD 与静音切除。
std::vector<AudioBlock> makeSpeechTrack(
    double seconds, int sampleRate,
    const std::vector<std::pair<double, double>>& speechWindows) {
    const std::size_t totalFrames = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> samples(totalFrames, 0.0f);
    for (const auto& window : speechWindows) {
        const std::size_t start = static_cast<std::size_t>(window.first * sampleRate);
        const std::size_t end = static_cast<std::size_t>(window.second * sampleRate);
        for (std::size_t i = start; i < end && i < totalFrames; ++i) {
            const double t = static_cast<double>(i) / sampleRate;
            samples[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 300.0 * t));
        }
    }
    std::vector<AudioBlock> blocks;
    const std::size_t blockFrames = 1024;
    for (std::size_t offset = 0; offset < totalFrames; offset += blockFrames) {
        const std::size_t count = std::min(blockFrames, totalFrames - offset);
        AudioBlock block;
        block.start = Rational::make(static_cast<std::int64_t>(offset), sampleRate);
        block.sampleRate = sampleRate;
        block.channels = 1;
        block.samples.assign(samples.begin() + static_cast<std::ptrdiff_t>(offset),
                             samples.begin() + static_cast<std::ptrdiff_t>(offset + count));
        blocks.push_back(std::move(block));
    }
    return blocks;
}

/// 全静音轨。
std::vector<AudioBlock> makeSilenceTrack(double seconds, int sampleRate = 22050) {
    const std::size_t totalFrames = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<AudioBlock> blocks;
    const std::size_t blockFrames = 1024;
    for (std::size_t offset = 0; offset < totalFrames; offset += blockFrames) {
        const std::size_t count = std::min(blockFrames, totalFrames - offset);
        AudioBlock block;
        block.start = Rational::make(static_cast<std::int64_t>(offset), sampleRate);
        block.sampleRate = sampleRate;
        block.channels = 1;
        block.samples.assign(count, 0.0f);
        blocks.push_back(std::move(block));
    }
    return blocks;
}

struct SpeechFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    SpeechFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 1920;
        seq.height = 1080;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/audio/speech.wav";
        a.type = MediaType::Audio;
        a.duration = Rational::make(30);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Audio, "A1", &trackId);
    }
    ClipId addClip(double start, double duration, double sourceIn = 0.0) {
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(static_cast<std::int64_t>(start * 1000), 1000);
        c.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
        c.sourceIn = Rational::make(static_cast<std::int64_t>(sourceIn * 1000), 1000);
        ClipId id;
        editor.insertClip(seqId, trackId, c, &id);
        return id;
    }
    const Track* track() const { return findTrack(project, seqId, trackId); }
};

}  // namespace

// =============================================================================
// 语音活动分析（VAD）
// =============================================================================

NC_TEST(SpeechCut, DetectsSpeechWindow) {
    // 5 秒音频，[1,3) 有语音 → 应保留约该区间
    const auto blocks = makeSpeechTrack(5.0, 22050, {{1.0, 3.0}});
    const SpeechActivityResult result = analyzeSpeechActivity(blocks);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.keepSegments.size(), std::size_t(1));
    NC_CHECK(result.keepSegments[0].start.toDouble() <= 1.12);
    NC_CHECK(result.keepSegments[0].end.toDouble() >= 2.88);
    NC_CHECK(result.speechDuration.toDouble() > 1.8);
    NC_CHECK(result.silenceDuration.toDouble() > 1.9);
    NC_CHECK(result.keepRatio < 0.55);
}

NC_TEST(SpeechCut, KeepsShortPausesMerged) {
    // 两段语音间隔 0.15s（< minSilence 0.35）→ 合并为一段
    const auto blocks = makeSpeechTrack(5.0, 22050, {{1.0, 2.0}, {2.15, 3.0}});
    const SpeechActivityResult result = analyzeSpeechActivity(blocks);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.keepSegments.size(), std::size_t(1));
}

NC_TEST(SpeechCut, RemovesLongSilence) {
    // 两段语音间隔 1.5s（> minSilence）→ 保留两段
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}, {3.5, 4.5}});
    const SpeechActivityResult result = analyzeSpeechActivity(blocks);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.keepSegments.size(), std::size_t(2));
    // 中间静音被剪掉
    NC_CHECK(result.silenceDuration.toDouble() > 3.5);
}

NC_TEST(SpeechCut, PaddingExpandsBoundaries) {
    const auto blocks = makeSpeechTrack(5.0, 22050, {{1.0, 2.0}});
    SpeechDetectConfig config;
    config.paddingSeconds = 0.2;
    const SpeechActivityResult result = analyzeSpeechActivity(blocks, config);
    NC_CHECK(result.valid());
    // 起点应早于 1.0（外扩 0.2）
    NC_CHECK(result.keepSegments[0].start.toDouble() < 0.95);
    NC_CHECK(result.keepSegments[0].end.toDouble() > 2.05);
}

NC_TEST(SpeechCut, MaxKeepSplitsLongSpeech) {
    // 10 秒连续语音 + maxKeep 3 秒 → 应切分为 4 段左右
    const auto blocks = makeSpeechTrack(10.0, 22050, {{0.5, 10.0}});
    SpeechDetectConfig config;
    config.maxKeepSeconds = 3.0;
    const SpeechActivityResult result = analyzeSpeechActivity(blocks, config);
    NC_CHECK(result.valid());
    NC_CHECK(result.keepSegments.size() >= std::size_t(3));
    for (const auto& segment : result.keepSegments) {
        NC_CHECK(segment.duration().toDouble() < 3.2);
    }
}

NC_TEST(SpeechCut, EmptyInputInvalid) {
    const SpeechActivityResult result = analyzeSpeechActivity({});
    NC_CHECK(!result.valid());
    NC_CHECK(!result.note.empty());
}

NC_TEST(SpeechCut, SilenceTrackInvalid) {
    const auto blocks = makeSilenceTrack(5.0);
    const SpeechActivityResult result = analyzeSpeechActivity(blocks);
    NC_CHECK(!result.valid());
    NC_CHECK(result.note.find("无有效语音") != std::string::npos);
}

NC_TEST(SpeechCut, TooShortInvalid) {
    const auto blocks = makeSpeechTrack(0.02, 22050, {{0.0, 0.02}});
    const SpeechActivityResult result = analyzeSpeechActivity(blocks);
    NC_CHECK(!result.valid());
}

NC_TEST(SpeechCut, InvalidConfigRejected) {
    SpeechDetectConfig config;
    config.frameSize = -1;
    const auto blocks = makeSpeechTrack(5.0, 22050, {{1.0, 2.0}});
    const SpeechActivityResult result = analyzeSpeechActivity(blocks, config);
    NC_CHECK(!result.valid());
}

// =============================================================================
// 精剪计划
// =============================================================================

NC_TEST(SpeechCut, PlanMapsSegmentsToTimeline) {
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}, {3.5, 4.5}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    NC_CHECK(activity.valid());
    Clip clip;
    clip.id = ClipId{1};
    clip.asset = AssetId{7};
    clip.start = Rational::make(0);
    clip.duration = Rational::make(6);
    clip.sourceIn = Rational::make(0);
    const SpeechCutPlan plan = planSpeechCut(clip, activity);
    NC_CHECK(plan.valid());
    NC_CHECK_EQ(plan.cuts.size(), std::size_t(2));
    // 时间轴连续排布（第一段从 0 开始）
    NC_CHECK_EQ(plan.cuts[0].timelineStart.toDouble(), 0.0);
    NC_CHECK(plan.cuts[1].timelineStart.toDouble() > plan.cuts[0].duration.toDouble() - 0.01);
    NC_CHECK(plan.resultDuration.toDouble() < 2.5);  // 6 秒 → 约 2 秒
    NC_CHECK(plan.removedDuration.toDouble() > 3.5);
    NC_CHECK(plan.summary.find("剪掉") != std::string::npos);
}

NC_TEST(SpeechCut, PlanRespectsClipSourceRange) {
    const auto blocks = makeSpeechTrack(10.0, 22050, {{4.0, 5.0}, {7.0, 8.0}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    NC_CHECK(activity.valid());
    // 片段只使用源 [4.5, 7.5)：应只保留与 [4.5,7.5) 相交的部分
    Clip clip;
    clip.id = ClipId{1};
    clip.asset = AssetId{7};
    clip.start = Rational::make(0);
    clip.duration = Rational::make(3);
    clip.sourceIn = Rational::make(9, 2);
    const SpeechCutPlan plan = planSpeechCut(clip, activity);
    NC_CHECK(plan.valid());
    for (const auto& cut : plan.cuts) {
        NC_CHECK(cut.sourceStart.toDouble() >= 4.5 - 0.01);
        NC_CHECK(cut.sourceStart.toDouble() + cut.duration.toDouble() <= 7.5 + 0.01);
    }
}

NC_TEST(SpeechCut, PlanRejectsSegmentWithoutSpeech) {
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    Clip clip;
    clip.id = ClipId{1};
    clip.asset = AssetId{7};
    clip.start = Rational::make(0);
    clip.duration = Rational::make(2);
    clip.sourceIn = Rational::make(4);  // [4,6) 无语音
    const SpeechCutPlan plan = planSpeechCut(clip, activity);
    NC_CHECK(!plan.valid());
}

NC_TEST(SpeechCut, PlanRejectsInvalidActivity) {
    Clip clip;
    clip.id = ClipId{1};
    clip.asset = AssetId{7};
    clip.start = Rational::make(0);
    clip.duration = Rational::make(2);
    const SpeechActivityResult invalid;
    const SpeechCutPlan plan = planSpeechCut(clip, invalid);
    NC_CHECK(!plan.valid());
    NC_CHECK(!plan.result.message.empty());
}

// =============================================================================
// 落盘（真实修改时间轴 + 撤销）
// =============================================================================

NC_TEST(SpeechCutApply, ReplacesClipWithKeptSegments) {
    SpeechFixture f;
    const ClipId original = f.addClip(0.0, 6.0);
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}, {3.5, 4.5}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    const SpeechCutApplyResult result =
        applySpeechCut(f.editor, f.project, f.seqId, f.trackId, {original}, activity);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.clipsRemoved, std::size_t(1));
    NC_CHECK_EQ(result.clipsInserted, std::size_t(2));
    const Track* track = f.track();
    NC_CHECK(track != nullptr);
    NC_CHECK_EQ(track->clips.size(), std::size_t(2));
    NC_CHECK(track->invariantsHold());
    // 时间轴总时长缩短
    NC_CHECK(track->clips.back().end().toDouble() < 2.5);
}

NC_TEST(SpeechCutApply, UndoRestoresOriginal) {
    SpeechFixture f;
    const ClipId original = f.addClip(0.0, 6.0);
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}, {3.5, 4.5}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    NC_CHECK(applySpeechCut(f.editor, f.project, f.seqId, f.trackId, {original}, activity)
                 .result.isOk());
    // 一次撤销整体回退
    NC_CHECK(f.editor.undo().isOk());
    const Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(1));
    NC_CHECK(track->clips[0].id == original);
    NC_CHECK_EQ(track->clips[0].duration.toDouble(), 6.0);
    // 重做恢复精剪结果
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(2));
}

NC_TEST(SpeechCutApply, HistoryNameIsSpeechCut) {
    SpeechFixture f;
    const ClipId original = f.addClip(0.0, 6.0);
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    applySpeechCut(f.editor, f.project, f.seqId, f.trackId, {original}, activity);
    const auto names = f.editor.undoHistoryNames();
    NC_CHECK(!names.empty());
    NC_CHECK_EQ(names.back(), std::string("口播精剪"));
}

NC_TEST(SpeechCutApply, RejectsLockedTrack) {
    SpeechFixture f;
    const ClipId original = f.addClip(0.0, 6.0);
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    const SpeechCutApplyResult result =
        applySpeechCut(f.editor, f.project, f.seqId, f.trackId, {original}, activity);
    NC_CHECK(!result.result.isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));  // 未改动
}

NC_TEST(SpeechCutApply, RejectsMissingClip) {
    SpeechFixture f;
    f.addClip(0.0, 6.0);
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    const SpeechCutApplyResult result = applySpeechCut(
        f.editor, f.project, f.seqId, f.trackId, {ClipId{9999}}, activity);
    NC_CHECK(!result.result.isOk());
}

NC_TEST(SpeechCutApply, RejectsInvalidActivity) {
    SpeechFixture f;
    const ClipId original = f.addClip(0.0, 6.0);
    const SpeechActivityResult invalid;
    const SpeechCutApplyResult result =
        applySpeechCut(f.editor, f.project, f.seqId, f.trackId, {original}, invalid);
    NC_CHECK(!result.result.isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
}

NC_TEST(SpeechCutApply, ProcessesWholeTrackByDefault) {
    SpeechFixture f;
    f.addClip(0.0, 3.0, 0.0);
    f.addClip(3.0, 3.0, 3.0);  // 两段共 6 秒，源 [0,3) 与 [3,6)
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}, {4.0, 5.0}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    const SpeechCutApplyResult result =
        applySpeechCut(f.editor, f.project, f.seqId, f.trackId, {}, activity);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.clipsRemoved, std::size_t(2));
    NC_CHECK_EQ(result.clipsInserted, std::size_t(2));
    NC_CHECK(f.track()->invariantsHold());
}

// =============================================================================
// 便捷入口（含解码器注入）
// =============================================================================

namespace {
/// Mock 读取器：返回预置音频块序列。
class BlockReader : public MediaReader {
public:
    explicit BlockReader(std::vector<AudioBlock> blocks) : blocks_(std::move(blocks)) {
        probe_.type = MediaType::Audio;
        probe_.sampleRate = 22050;
        probe_.channels = 1;
    }
    Result open(const std::string& p) override {
        if (p.empty()) return Result::fail(ErrorCode::InvalidArgument, "路径为空");
        open_ = true;
        index_ = 0;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time&) override {
        if (!open_) return Result::fail(ErrorCode::InvalidArgument, "未打开");
        index_ = 0;
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame&) override { return false; }
    bool readAudioBlock(AudioBlock& o) override {
        if (!open_ || index_ >= static_cast<int>(blocks_.size())) {
            return false;
        }
        o = blocks_[static_cast<std::size_t>(index_)];
        ++index_;
        return true;
    }

private:
    std::vector<AudioBlock> blocks_;
    MediaProbeResult probe_;
    bool open_ = false;
    int index_ = 0;
};

class BlockFactory : public MediaReaderFactory {
public:
    explicit BlockFactory(std::vector<AudioBlock> blocks) : blocks_(std::move(blocks)) {}
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<BlockReader>(blocks_);
    }

private:
    std::vector<AudioBlock> blocks_;
};
}  // namespace

NC_TEST(SpeechCut, AnalyzeAssetUsesDecoder) {
    const auto blocks = makeSpeechTrack(5.0, 22050, {{1.0, 3.0}});
    BlockFactory factory(blocks);
    MediaAsset asset;
    asset.id = AssetId{1};
    asset.path = "/audio/speech.wav";
    asset.type = MediaType::Audio;
    const SpeechActivityResult result = analyzeAssetSpeech(asset, &factory);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.keepSegments.size(), std::size_t(1));
}

NC_TEST(SpeechCut, AnalyzeAssetWithoutDecoderNotes) {
    MediaAsset asset;
    asset.id = AssetId{1};
    asset.path = "/audio/speech.wav";
    const SpeechActivityResult result = analyzeAssetSpeech(asset, nullptr);
    NC_CHECK(!result.valid());
    NC_CHECK(result.note.find("解码器") != std::string::npos);
}

// =============================================================================
// 批量计划（整轨）
// =============================================================================

NC_TEST(SpeechCut, PlanForTrackKeepsGaps) {
    SpeechFixture f;
    const auto blocks = makeSpeechTrack(6.0, 22050, {{1.0, 2.0}, {4.0, 5.0}});
    const SpeechActivityResult activity = analyzeSpeechActivity(blocks);
    // 手工构造轨道（两片段，中间留 1 秒空隙）
    Track track;
    track.id = TrackId{1};
    track.type = TrackType::Audio;
    Clip a;
    a.id = ClipId{1};
    a.asset = AssetId{7};
    a.start = Rational::make(0);
    a.duration = Rational::make(2);
    a.sourceIn = Rational::make(0);
    Clip b;
    b.id = ClipId{2};
    b.asset = AssetId{7};
    b.start = Rational::make(3);
    b.duration = Rational::make(2);
    b.sourceIn = Rational::make(3);
    track.clips.push_back(a);
    track.clips.push_back(b);

    const SpeechCutPlan plan = planSpeechCutForTrack(track, activity, true);
    NC_CHECK(plan.valid());
    NC_CHECK_EQ(plan.cuts.size(), std::size_t(2));
    // 第二段起点应 >= 第二片段原起点（保留空隙）
    NC_CHECK(plan.cuts[1].timelineStart.toDouble() >= 3.0 - 0.5);
}
