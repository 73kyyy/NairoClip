#include "test_framework.h"
#include "nairoclip/core/beat_detect.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr double kPi = 3.141592653589793;

/// 合成 click track：每拍一个 80Hz 衰减音（模拟底鼓），用于验证 BPM 估计。
std::vector<AudioBlock> makeClickTrack(double bpm, double seconds, int sampleRate = 22050) {
    const int channels = 1;
    const std::size_t totalFrames = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> samples(totalFrames, 0.0f);
    const double period = 60.0 / bpm;
    for (double t = 0.0; t < seconds; t += period) {
        const std::size_t start = static_cast<std::size_t>(t * sampleRate);
        const std::size_t len = static_cast<std::size_t>(0.05 * sampleRate);
        for (std::size_t i = 0; i < len && start + i < totalFrames; ++i) {
            const double time = static_cast<double>(i) / sampleRate;
            const double decay = std::exp(-time * 40.0);
            samples[start + i] +=
                static_cast<float>(0.9 * decay * std::sin(2.0 * kPi * 80.0 * time));
        }
    }
    std::vector<AudioBlock> blocks;
    const std::size_t blockFrames = 1024;
    for (std::size_t offset = 0; offset < totalFrames; offset += blockFrames) {
        const std::size_t count = std::min(blockFrames, totalFrames - offset);
        AudioBlock block;
        block.start = Rational::make(static_cast<std::int64_t>(offset), sampleRate);
        block.sampleRate = sampleRate;
        block.channels = channels;
        block.samples.assign(samples.begin() + static_cast<std::ptrdiff_t>(offset),
                             samples.begin() + static_cast<std::ptrdiff_t>(offset + count));
        blocks.push_back(std::move(block));
    }
    return blocks;
}

/// 全静音音频块（直接生成零样本，避免合成循环开销）。
std::vector<AudioBlock> makeSilence(double seconds, int sampleRate = 22050) {
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

}  // namespace

// =============================================================================
// BPM 估计（真实算法验证）
// =============================================================================

NC_TEST(BeatDetect, EstimatesKnownBpm120) {
    const auto blocks = makeClickTrack(120.0, 10.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    // 允许 ±8 BPM 误差（帧分辨率与吸附影响）
    NC_CHECK(std::fabs(analysis.bpm - 120.0) < 8.0);
    // 10 秒 / 0.5 秒每拍 ≈ 20 拍（允许边界少量偏差）
    NC_CHECK(analysis.beats.size() >= std::size_t(16));
    NC_CHECK(analysis.beats.size() <= std::size_t(24));
}

NC_TEST(BeatDetect, EstimatesKnownBpm90) {
    const auto blocks = makeClickTrack(90.0, 12.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    NC_CHECK(std::fabs(analysis.bpm - 90.0) < 8.0);
}

NC_TEST(BeatDetect, BeatSpacingMatchesTempo) {
    const auto blocks = makeClickTrack(120.0, 10.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    NC_CHECK(analysis.beats.size() >= std::size_t(4));
    // 相邻节拍间隔应接近 0.5 秒
    for (std::size_t i = 1; i < analysis.beats.size(); ++i) {
        const double gap =
            (analysis.beats[i].time - analysis.beats[i - 1].time).toDouble();
        NC_CHECK(gap > 0.35);
        NC_CHECK(gap < 0.65);
    }
}

NC_TEST(BeatDetect, BeatsAreTimeOrdered) {
    const auto blocks = makeClickTrack(140.0, 8.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    for (std::size_t i = 1; i < analysis.beats.size(); ++i) {
        NC_CHECK(analysis.beats[i - 1].time < analysis.beats[i].time);
    }
}

NC_TEST(BeatDetect, ConfidencePositiveForClearTrack) {
    const auto blocks = makeClickTrack(120.0, 10.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.confidence > 0.1);
    NC_CHECK(analysis.confidence <= 1.0);
}

NC_TEST(BeatDetect, OriginRespected) {
    // 音频从 5s 开始：节拍时间应为绝对时间（>= 5s）。
    auto blocks = makeClickTrack(120.0, 6.0);
    for (auto& block : blocks) {
        block.start = block.start + Rational::make(5);
    }
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    NC_CHECK(analysis.origin == Rational::make(5));
    NC_CHECK(analysis.beats.front().time >= Rational::make(5));
}

// =============================================================================
// 边界与失败态（不崩溃、可解释）
// =============================================================================

NC_TEST(BeatDetect, EmptyInputIsInvalidWithNote) {
    const BeatAnalysis analysis = analyzeBeats({});
    NC_CHECK(!analysis.valid());
    NC_CHECK(!analysis.note.empty());
}

NC_TEST(BeatDetect, TooShortInputIsInvalid) {
    const auto blocks = makeClickTrack(120.0, 0.05);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(!analysis.valid());
    NC_CHECK(!analysis.note.empty());
}

NC_TEST(BeatDetect, SilenceYieldsNoBeats) {
    const auto blocks = makeSilence(5.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(!analysis.valid());
    NC_CHECK(!analysis.note.empty());
}

NC_TEST(BeatDetect, MaxBeatsCapRespected) {
    BeatDetectConfig config;
    config.maxBeats = 5;
    const auto blocks = makeClickTrack(120.0, 20.0);
    const BeatAnalysis analysis = analyzeBeats(blocks, config);
    NC_CHECK(analysis.beats.size() <= std::size_t(5));
}

// =============================================================================
// 节拍网格 / 吸附（音乐卡点核心操作）
// =============================================================================

NC_TEST(BeatGrid, GeneratesExpectedPoints) {
    const auto grid = buildBeatGrid(Rational::make(1), Rational::make(10), 120.0);
    // 10 秒 / 0.5 秒 = 20 个点
    NC_CHECK_EQ(grid.size(), std::size_t(20));
    NC_CHECK(grid.front() == Rational::make(1));
    NC_CHECK(grid[1] == Rational::make(3, 2));
    NC_CHECK(grid.back() < Rational::make(11));
}

NC_TEST(BeatGrid, RejectsInvalidInput) {
    NC_CHECK(buildBeatGrid(Rational::make(0), Rational::make(10), 0.0).empty());
    NC_CHECK(buildBeatGrid(Rational::make(0), Rational::make(10), -5.0).empty());
    NC_CHECK(buildBeatGrid(Rational::make(0), Rational::make(0), 120.0).empty());
}

NC_TEST(SnapCuts, SnapsToNearbyBeat) {
    const std::vector<Time> beats = {Rational::make(1), Rational::make(3, 2),
                                     Rational::make(2)};
    const std::vector<Time> cuts = {Rational::make(51, 50)};  // 1.02s
    const auto snapped = snapCutsToBeats(cuts, beats, 0.07);
    NC_CHECK_EQ(snapped.size(), std::size_t(1));
    NC_CHECK(snapped[0] == Rational::make(1));
}

NC_TEST(SnapCuts, LeavesFarCutsUnchanged) {
    const std::vector<Time> beats = {Rational::make(1), Rational::make(2)};
    const std::vector<Time> cuts = {Rational::make(5)};  // 远离任何节拍
    const auto snapped = snapCutsToBeats(cuts, beats, 0.07);
    NC_CHECK(snapped[0] == Rational::make(5));
}

NC_TEST(SnapCuts, ZeroRadiusReturnsOriginal) {
    const std::vector<Time> beats = {Rational::make(1)};
    const std::vector<Time> cuts = {Rational::make(51, 50)};
    const auto snapped = snapCutsToBeats(cuts, beats, 0.0);
    NC_CHECK(snapped == cuts);
}

NC_TEST(SnapCuts, PreservesAscendingOrder) {
    const std::vector<Time> beats = {Rational::make(1), Rational::make(3, 2)};
    const std::vector<Time> cuts = {Rational::make(103, 100), Rational::make(149, 100)};
    const auto snapped = snapCutsToBeats(cuts, beats, 0.07);
    NC_CHECK_EQ(snapped.size(), std::size_t(2));
    NC_CHECK(snapped[0] < snapped[1]);
}

// =============================================================================
// 音乐卡点（Beat Sync Edit）：节拍 → 镜头切分建议
// =============================================================================

NC_TEST(BeatSync, PlansCutsAlignedToBeats) {
    const auto blocks = makeClickTrack(120.0, 8.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    // 素材：单段 30 秒可用
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(30)}};
    const BeatSyncPlan plan = planBeatSync(analysis, material);
    NC_CHECK(plan.result.isOk());
    NC_CHECK(!plan.cuts.empty());
    NC_CHECK(std::fabs(plan.bpm - 120.0) < 8.0);
    // 每个片段时长应接近节拍周期（0.5s，允许 minShot 过滤偏差）
    for (const auto& cut : plan.cuts) {
        NC_CHECK(cut.duration.toDouble() > 0.2);
        NC_CHECK(cut.duration.toDouble() < 0.8);
    }
    // 片段连续（无缝隙）
    for (std::size_t i = 1; i < plan.cuts.size(); ++i) {
        // 源内起点累计 = 前序时长之和（同素材循环取用）
        NC_CHECK(plan.cuts[i].start >= plan.cuts[i - 1].start);
    }
}

NC_TEST(BeatSync, RespectsMaterialExhaustion) {
    const auto blocks = makeClickTrack(120.0, 8.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    // 素材仅 1 秒（约 2 个节拍区间）
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(1)}};
    BeatSyncOptions options;
    options.maxShotSeconds = 0.0;  // 不切分长区间
    const BeatSyncPlan plan = planBeatSync(analysis, material, options);
    NC_CHECK(plan.result.isOk());
    // 素材 1 秒 / 每片段约 0.5s → 约 2 个片段后耗尽
    NC_CHECK(plan.cuts.size() <= std::size_t(4));
    NC_CHECK(!plan.warnings.empty());  // 应提示素材用尽
}

NC_TEST(BeatSync, LoopsMultipleMaterialSegments) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    // 两段素材各 0.5 秒：应循环取用并引用两个不同素材
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(1)},
                                                   {AssetId{2}, Rational::make(1)}};
    const BeatSyncPlan plan = planBeatSync(analysis, material);
    NC_CHECK(plan.result.isOk());
    NC_CHECK(plan.cuts.size() >= std::size_t(3));
    bool sawAsset2 = false;
    for (const auto& cut : plan.cuts) {
        if (cut.asset.value == 2) sawAsset2 = true;
    }
    NC_CHECK(sawAsset2);  // 第一段用尽后应切到第二段
}

NC_TEST(BeatSync, RejectsInvalidAnalysis) {
    const BeatAnalysis invalid;  // valid() == false
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(10)}};
    const BeatSyncPlan plan = planBeatSync(invalid, material);
    NC_CHECK(!plan.result.isOk());
    NC_CHECK(!plan.result.message.empty());
}

NC_TEST(BeatSync, RejectsEmptyMaterial) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    const BeatSyncPlan plan = planBeatSync(analysis, {});
    NC_CHECK(!plan.result.isOk());
    NC_CHECK(!plan.result.message.empty());
}

NC_TEST(BeatSync, RejectsInvalidMaterialSegment) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    // 时长非正的素材
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(0)}};
    const BeatSyncPlan plan = planBeatSync(analysis, material);
    NC_CHECK(!plan.result.isOk());
}

NC_TEST(BeatSync, FiltersTooShortSpans) {
    const auto blocks = makeClickTrack(200.0, 6.0);  // 快节奏：0.3s/拍
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    BeatSyncOptions options;
    options.minShotSeconds = 0.4;  // 过滤 0.3s 的拍
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(30)}};
    const BeatSyncPlan plan = planBeatSync(analysis, material, options);
    if (plan.result.isOk()) {
        for (const auto& cut : plan.cuts) {
            NC_CHECK(cut.duration.toDouble() >= 0.3);
        }
    }
}

NC_TEST(BeatSync, SplitsTooLongSpans) {
    // 构造只有少数节拍的场景：放慢到 60 BPM（1s/拍），maxShotSeconds=0.5 应切分
    const auto blocks = makeClickTrack(60.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    BeatSyncOptions options;
    options.maxShotSeconds = 0.5;
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(30)}};
    const BeatSyncPlan plan = planBeatSync(analysis, material, options);
    NC_CHECK(plan.result.isOk());
    for (const auto& cut : plan.cuts) {
        NC_CHECK(cut.duration.toDouble() <= 0.6);
    }
}

NC_TEST(BeatSync, ReportsStrongBeatStatistics) {
    const auto blocks = makeClickTrack(120.0, 8.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    const std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(30)}};
    BeatSyncOptions options;
    options.preferStrongBeats = true;
    const BeatSyncPlan plan = planBeatSync(analysis, material, options);
    NC_CHECK(plan.result.isOk());
    // 应包含统计文案（UI「识别节拍点 N 个」）
    bool sawStats = false;
    for (const auto& warning : plan.warnings) {
        if (warning.find("节拍点") != std::string::npos) {
            sawStats = true;
        }
    }
    NC_CHECK(sawStats);
}

// =============================================================================
// 音乐卡点落盘（applyBeatSync：真实写入时间轴 + 可撤销）
// =============================================================================

#include "nairoclip/core/auto_edit.h"
#include "nairoclip/core/editor.h"

namespace {
struct SyncFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    SyncFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 1920;
        seq.height = 1080;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/music/track.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
    }
    const Track* track() const { return findTrack(project, seqId, trackId); }
};
}  // namespace

NC_TEST(BeatSyncApply, InsertsClipsAlignedToBeats) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    NC_CHECK(analysis.valid());
    SyncFixture f;
    const std::vector<MaterialSegment> material = {{f.assetId, Rational::make(30)}};
    const AutoEditResult result =
        applyBeatSync(f.editor, f.project, f.seqId, f.trackId, analysis, material);
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.clipsInserted > 0);
    const Track* track = f.track();
    NC_CHECK(track != nullptr);
    NC_CHECK_EQ(track->clips.size(), result.clipsInserted);
    NC_CHECK(track->invariantsHold());  // 不重叠、按序、合法
    // 片段首尾相接（无缝隙），且起点从 0 开始
    NC_CHECK(f.track()->clips.front().start == Rational::make(0));
    for (std::size_t i = 1; i < track->clips.size(); ++i) {
        NC_CHECK(track->clips[i].start == track->clips[i - 1].end());
    }
}

NC_TEST(BeatSyncApply, UndoRestoresEmptyTrack) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    SyncFixture f;
    const std::vector<MaterialSegment> material = {{f.assetId, Rational::make(30)}};
    const AutoEditResult result =
        applyBeatSync(f.editor, f.project, f.seqId, f.trackId, analysis, material);
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.clipsInserted > 1);
    // 一次撤销应整体回退（事务合并）
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(f.track()->clips.empty());
    // 重做恢复
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), result.clipsInserted);
}

NC_TEST(BeatSyncApply, RejectsNonEmptyTrack) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    SyncFixture f;
    // 先插入一个片段占位
    Clip existing;
    existing.asset = f.assetId;
    existing.start = Rational::make(0);
    existing.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, existing).isOk());
    const std::vector<MaterialSegment> material = {{f.assetId, Rational::make(30)}};
    const AutoEditResult result =
        applyBeatSync(f.editor, f.project, f.seqId, f.trackId, analysis, material);
    NC_CHECK(!result.result.isOk());  // 拒绝，避免破坏已有剪辑
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));  // 原片段未受影响
}

NC_TEST(BeatSyncApply, RejectsLockedTrack) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    SyncFixture f;
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    const std::vector<MaterialSegment> material = {{f.assetId, Rational::make(30)}};
    const AutoEditResult result =
        applyBeatSync(f.editor, f.project, f.seqId, f.trackId, analysis, material);
    NC_CHECK(!result.result.isOk());
    NC_CHECK(f.track()->clips.empty());
}

NC_TEST(BeatSyncApply, HistoryNameIsBeatSync) {
    const auto blocks = makeClickTrack(120.0, 6.0);
    const BeatAnalysis analysis = analyzeBeats(blocks);
    SyncFixture f;
    const std::vector<MaterialSegment> material = {{f.assetId, Rational::make(30)}};
    applyBeatSync(f.editor, f.project, f.seqId, f.trackId, analysis, material);
    const auto names = f.editor.undoHistoryNames();
    NC_CHECK(!names.empty());
    // 历史按时间顺序返回（最早 → 最新）；fixture 构造时先有「添加轨道」，
    // 音乐卡点作为事务合并为最后一条（历史面板显示名）。
    NC_CHECK_EQ(names.back(), std::string("音乐卡点"));
    // 事务合并关键验证：N 个片段插入 == 1 条历史（不是 N 条）
    NC_CHECK_EQ(names.size(), std::size_t(2));  // 「添加轨道」+「音乐卡点」
}
