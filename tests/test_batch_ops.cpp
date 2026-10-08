#include "test_framework.h"
#include "nairoclip/core/batch_ops.h"
#include "nairoclip/core/editor.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

struct BatchFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId v1;
    TrackId v2;
    AssetId assetId;
    BatchFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 1920;
        seq.height = 1080;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/videos/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(120);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &v1);
        editor.addTrack(seqId, TrackType::Video, "V2", &v2);
    }
    ClipId addClip(TrackId track, double start, double duration) {
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(static_cast<std::int64_t>(start * 1000), 1000);
        c.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
        ClipId id;
        editor.insertClip(seqId, track, c, &id);
        return id;
    }
    const Track* track(TrackId id) const { return findTrack(project, seqId, id); }
};

}  // namespace

// =============================================================================
// 批量删除（文档「批量删除」）
// =============================================================================

NC_TEST(BatchOps, DeleteRemovesAllSelected) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.0, 1.0);
    const ClipId c = f.addClip(f.v1, 2.0, 1.0);
    const BatchResult result = batchDelete(f.editor, f.project, f.seqId,
                                           {{f.v1, a}, {f.v1, c}});
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(2));
    NC_CHECK_EQ(f.track(f.v1)->clips.size(), std::size_t(1));
    NC_CHECK(f.track(f.v1)->findClip(b) != nullptr);
    NC_CHECK(f.track(f.v1)->invariantsHold());
}

NC_TEST(BatchOps, DeleteCrossTrack) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v2, 0.0, 1.0);
    const BatchResult result = batchDelete(f.editor, f.project, f.seqId,
                                           {{f.v1, a}, {f.v2, b}});
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(2));
    NC_CHECK_EQ(f.track(f.v1)->clips.size(), std::size_t(0));
    NC_CHECK_EQ(f.track(f.v2)->clips.size(), std::size_t(0));
}

NC_TEST(BatchOps, DeleteRippleShiftsFollowing) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);   // [0,1)
    const ClipId b = f.addClip(f.v1, 1.0, 1.0);   // [1,2)
    const ClipId c = f.addClip(f.v1, 2.0, 1.0);   // [2,3)
    const BatchResult result =
        batchDelete(f.editor, f.project, f.seqId, {{f.v1, a}}, true);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(1));
    // 波纹删除：后续片段前移 1 秒
    NC_CHECK(f.track(f.v1)->findClip(b)->start == Rational::make(0));
    NC_CHECK(f.track(f.v1)->findClip(c)->start == Rational::make(1));
    NC_CHECK(f.track(f.v1)->invariantsHold());
}

NC_TEST(BatchOps, DeleteUndoRestoresAll) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.0, 1.0);
    NC_CHECK(batchDelete(f.editor, f.project, f.seqId, {{f.v1, a}, {f.v1, b}}).valid());
    NC_CHECK_EQ(f.track(f.v1)->clips.size(), std::size_t(0));
    // 一次撤销整体回退
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.track(f.v1)->clips.size(), std::size_t(2));
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.track(f.v1)->clips.size(), std::size_t(0));
}

NC_TEST(BatchOps, DeletePartialFailureReported) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    // 不存在的片段混入：应部分成功并说明
    const BatchResult result = batchDelete(f.editor, f.project, f.seqId,
                                           {{f.v1, a}, {f.v1, ClipId{9999}}});
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(1));
    NC_CHECK_EQ(result.failed, std::size_t(1));
    NC_CHECK(!result.messages.empty());
}

NC_TEST(BatchOps, DeleteEmptySelectionRejected) {
    BatchFixture f;
    const BatchResult result = batchDelete(f.editor, f.project, f.seqId, {});
    NC_CHECK(!result.valid());
}

NC_TEST(BatchOps, DeleteLockedTrackFails) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.v1, true).isOk());
    const BatchResult result = batchDelete(f.editor, f.project, f.seqId, {{f.v1, a}});
    NC_CHECK(!result.valid());
    NC_CHECK_EQ(f.track(f.v1)->clips.size(), std::size_t(1));  // 未改动
}

// =============================================================================
// 批量应用转场（文档「批量应用转场」）
// =============================================================================

NC_TEST(BatchOps, ApplyTransitionsBetweenAdjacent) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);   // [0,1)
    const ClipId b = f.addClip(f.v1, 1.5, 1.0);   // [1.5,2.5) → 间隙 0.5s
    const ClipId c = f.addClip(f.v1, 3.5, 1.0);   // [3.5,4.5) → 间隙 1.0s
    BatchTransitionOptions options;
    options.duration = Rational::make(3, 10);  // 0.3s（能放进两个间隙）
    const BatchResult result =
        batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b, c}, options);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(2));  // a→b, b→c
    NC_CHECK_EQ(f.track(f.v1)->transitions.size(), std::size_t(2));
}

NC_TEST(BatchOps, ApplyTransitionsShrinksToGap) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.1, 1.0);  // 间隙 0.1s
    BatchTransitionOptions options;
    options.duration = Rational::make(1, 2);  // 0.5s > 间隙
    options.allowShrink = true;
    const BatchResult result =
        batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b}, options);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(1));
    // 转场时长被缩短到间隙
    const Track* track = f.track(f.v1);
    NC_CHECK_EQ(track->transitions.size(), std::size_t(1));
    NC_CHECK(track->transitions[0].duration.toDouble() <= 0.11);
    // 应有说明
    bool sawShrinkNote = false;
    for (const auto& message : result.messages) {
        if (message.find("缩短") != std::string::npos) sawShrinkNote = true;
    }
    NC_CHECK(sawShrinkNote);
}

NC_TEST(BatchOps, ApplyTransitionsSkipsZeroGapWithoutShift) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.0, 1.0);  // 首尾相接（间隙 0）
    BatchTransitionOptions options;
    options.allowShrink = true;
    options.shiftToMakeRoom = false;
    const BatchResult result =
        batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b}, options);
    NC_CHECK(!result.valid());  // 无成功项
    NC_CHECK_EQ(f.track(f.v1)->transitions.size(), std::size_t(0));
    NC_CHECK(!result.messages.empty());
}

NC_TEST(BatchOps, ApplyTransitionsShiftsToMakeRoom) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.0, 1.0);  // 首尾相接
    BatchTransitionOptions options;
    options.duration = Rational::make(3, 10);
    options.shiftToMakeRoom = true;
    const BatchResult result =
        batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b}, options);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(1));
    // b 被后移 0.3 秒
    NC_CHECK(f.track(f.v1)->findClip(b)->start == Rational::make(13, 10));
    NC_CHECK_EQ(f.track(f.v1)->transitions.size(), std::size_t(1));
    NC_CHECK(f.track(f.v1)->invariantsHold());
}

NC_TEST(BatchOps, ApplyTransitionsSkipsOccupied) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.5, 1.0);
    // 预置转场占用 a
    Transition tr;
    tr.id = TransitionId{f.project.ids.next()};
    tr.type = TransitionType::CrossDissolve;
    tr.duration = Rational::make(2, 10);
    tr.fromClip = a;
    tr.toClip = b;
    NC_CHECK(f.editor.addTransition(f.seqId, f.v1, tr).isOk());
    BatchTransitionOptions options;
    options.duration = Rational::make(2, 10);
    const BatchResult result =
        batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b}, options);
    NC_CHECK(!result.valid());  // 已占用 → 跳过
    NC_CHECK_EQ(f.track(f.v1)->transitions.size(), std::size_t(1));
}

NC_TEST(BatchOps, ApplyTransitionsRejectsTooFew) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const BatchResult result = batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a});
    NC_CHECK(!result.valid());
}

NC_TEST(BatchOps, ApplyTransitionsMaxCount) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.5, 1.0);
    const ClipId c = f.addClip(f.v1, 3.0, 1.0);
    BatchTransitionOptions options;
    options.duration = Rational::make(2, 10);
    options.maxCount = 1;
    const BatchResult result =
        batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b, c}, options);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(1));
    NC_CHECK_EQ(f.track(f.v1)->transitions.size(), std::size_t(1));
}

NC_TEST(BatchOps, ApplyTransitionsUndoRestores) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.5, 1.0);
    BatchTransitionOptions options;
    options.duration = Rational::make(2, 10);
    NC_CHECK(batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b}, options).valid());
    NC_CHECK_EQ(f.track(f.v1)->transitions.size(), std::size_t(1));
    // 一次撤销整体回退
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.track(f.v1)->transitions.size(), std::size_t(0));
}

// =============================================================================
// 批量调整音量（文档「批量调整音量」）
// =============================================================================

namespace {
struct AudioBatchFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId a1;
    AssetId assetId;
    AudioBatchFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/audio/a.wav";
        a.type = MediaType::Audio;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Audio, "A1", &a1);
    }
    ClipId addClip(double start, double duration) {
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(static_cast<std::int64_t>(start * 1000), 1000);
        c.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
        ClipId id;
        editor.insertClip(seqId, a1, c, &id);
        return id;
    }
    const Track* track() const { return findTrack(project, seqId, a1); }
};
}  // namespace

NC_TEST(BatchOps, SetAudioGainAll) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 1.0);
    const ClipId b = f.addClip(1.0, 1.0);
    BatchAudioOptions options;
    options.setGain = true;
    options.gainDb = -6.0;
    const BatchResult result =
        batchSetAudio(f.editor, f.project, f.seqId, {{f.a1, a}, {f.a1, b}}, options);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(2));
    NC_CHECK(std::fabs(f.track()->findClip(a)->audioGainDb + 6.0) < 1e-9);
    NC_CHECK(std::fabs(f.track()->findClip(b)->audioGainDb + 6.0) < 1e-9);
}

NC_TEST(BatchOps, SetAudioClampsOutOfRange) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 2.0);
    BatchAudioOptions options;
    options.setGain = true;
    options.gainDb = 20.0;  // 超范围
    options.clampToRange = true;
    const BatchResult result =
        batchSetAudio(f.editor, f.project, f.seqId, {{f.a1, a}}, options);
    NC_CHECK(result.valid());
    NC_CHECK(std::fabs(f.track()->findClip(a)->audioGainDb - 12.0) < 1e-9);  // 钳制到上限
}

NC_TEST(BatchOps, SetAudioRejectsOutOfRangeWithoutClamp) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 2.0);
    BatchAudioOptions options;
    options.setGain = true;
    options.gainDb = 30.0;
    options.clampToRange = false;
    const BatchResult result =
        batchSetAudio(f.editor, f.project, f.seqId, {{f.a1, a}}, options);
    NC_CHECK(!result.valid());
    NC_CHECK_EQ(f.track()->findClip(a)->audioGainDb, 0.0);  // 未改动
}

NC_TEST(BatchOps, SetAudioFadesScaleToFit) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 1.0);  // 1 秒片段
    BatchAudioOptions options;
    options.setFades = true;
    options.fadeIn = Rational::make(1);   // 1s
    options.fadeOut = Rational::make(1);  // 1s → 总 2s > 1s
    const BatchResult result =
        batchSetAudio(f.editor, f.project, f.seqId, {{f.a1, a}}, options);
    NC_CHECK(result.valid());
    const Clip* clip = f.track()->findClip(a);
    NC_CHECK(clip->audioFadeIn + clip->audioFadeOut <= clip->duration);
    NC_CHECK(clip->audioValid());
    // 应说明缩放
    bool sawScale = false;
    for (const auto& message : result.messages) {
        if (message.find("缩放") != std::string::npos) sawScale = true;
    }
    NC_CHECK(sawScale);
}

NC_TEST(BatchOps, SetAudioRequiresSomeOption) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 1.0);
    const BatchResult result = batchSetAudio(f.editor, f.project, f.seqId, {{f.a1, a}}, {});
    NC_CHECK(!result.valid());
}

NC_TEST(BatchOps, SetAudioUndoRestores) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 1.0);
    BatchAudioOptions options;
    options.setGain = true;
    options.gainDb = -9.0;
    NC_CHECK(batchSetAudio(f.editor, f.project, f.seqId, {{f.a1, a}}, options).valid());
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.track()->findClip(a)->audioGainDb, 0.0);
}

// =============================================================================
// 批量变速（文档移动端「批量变速」）
// =============================================================================

NC_TEST(BatchOps, SetSpeedAll) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 2.0);
    const ClipId b = f.addClip(3.0, 2.0);
    const BatchResult result = batchSetSpeed(f.editor, f.project, f.seqId,
                                             {{f.a1, a}, {f.a1, b}}, Rational::make(2));
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(2));
    // 2 倍速 → 时长减半
    NC_CHECK_EQ(f.track()->findClip(a)->duration.toDouble(), 1.0);
    NC_CHECK_EQ(f.track()->findClip(b)->duration.toDouble(), 1.0);
}

NC_TEST(BatchOps, SetSpeedRejectsInvalid) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 2.0);
    const BatchResult result =
        batchSetSpeed(f.editor, f.project, f.seqId, {{f.a1, a}}, Rational::make(20));
    NC_CHECK(!result.valid());
    NC_CHECK_EQ(f.track()->findClip(a)->duration.toDouble(), 2.0);  // 未改动
}

NC_TEST(BatchOps, SetSpeedReportsOverlap) {
    AudioBatchFixture f;
    const ClipId a = f.addClip(0.0, 2.0);   // [0,2)
    const ClipId b = f.addClip(2.0, 2.0);   // [2,4)
    // 变速 0.5x → a 时长变为 4 秒，与 b 重叠 → a 失败
    const BatchResult result = batchSetSpeed(f.editor, f.project, f.seqId,
                                             {{f.a1, a}, {f.a1, b}}, Rational::make(1, 2));
    // b 先处理（降序）不重叠成功；a 失败
    NC_CHECK_EQ(result.succeeded, std::size_t(1));
    NC_CHECK_EQ(result.failed, std::size_t(1));
    NC_CHECK(!result.messages.empty());
}

// =============================================================================
// 对齐到同一起点（文档「对齐到同一起点」）
// =============================================================================

NC_TEST(BatchOps, AlignToEarliestStart) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 1.0, 1.0);
    const ClipId b = f.addClip(f.v2, 3.0, 1.0);
    const BatchResult result =
        batchAlignStart(f.editor, f.project, f.seqId, {{f.v1, a}, {f.v2, b}});
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(2));
    // 两轨都对齐到 1.0
    NC_CHECK(f.track(f.v1)->findClip(a)->start == Rational::make(1));
    NC_CHECK(f.track(f.v2)->findClip(b)->start == Rational::make(1));
}

NC_TEST(BatchOps, AlignToExplicitTarget) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v2, 5.0, 1.0);
    BatchAlignOptions options;
    options.useExplicitTarget = true;
    options.targetStart = Rational::make(2);
    const BatchResult result =
        batchAlignStart(f.editor, f.project, f.seqId, {{f.v1, a}, {f.v2, b}}, options);
    NC_CHECK(result.valid());
    NC_CHECK(f.track(f.v1)->findClip(a)->start == Rational::make(2));
    NC_CHECK(f.track(f.v2)->findClip(b)->start == Rational::make(2));
}

NC_TEST(BatchOps, AlignRejectsNegativeTarget) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 1.0, 1.0);
    BatchAlignOptions options;
    options.useExplicitTarget = true;
    options.targetStart = Rational::make(-1);
    const BatchResult result =
        batchAlignStart(f.editor, f.project, f.seqId, {{f.v1, a}}, options);
    NC_CHECK(!result.valid());
    NC_CHECK(f.track(f.v1)->findClip(a)->start == Rational::make(1));  // 未改动
}

NC_TEST(BatchOps, AlignReportsSameTrackCollision) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);  // [0,1)
    const ClipId b = f.addClip(f.v1, 2.0, 1.0);  // [2,3)
    // 都对齐到 2.0：a 会与 b 重叠 → a 失败，b 幂等成功
    BatchAlignOptions options;
    options.useExplicitTarget = true;
    options.targetStart = Rational::make(2);
    const BatchResult result =
        batchAlignStart(f.editor, f.project, f.seqId, {{f.v1, a}, {f.v1, b}}, options);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.succeeded, std::size_t(1));
    NC_CHECK_EQ(result.failed, std::size_t(1));
    NC_CHECK(f.track(f.v1)->invariantsHold());
}

NC_TEST(BatchOps, AlignUndoRestores) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 1.0, 1.0);
    const ClipId b = f.addClip(f.v2, 3.0, 1.0);
    NC_CHECK(batchAlignStart(f.editor, f.project, f.seqId, {{f.v1, a}, {f.v2, b}}).valid());
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(f.track(f.v1)->findClip(a)->start == Rational::make(1));
    NC_CHECK(f.track(f.v2)->findClip(b)->start == Rational::make(3));
}

NC_TEST(BatchOps, HistoryNamesPopulated) {
    BatchFixture f;
    const ClipId a = f.addClip(f.v1, 0.0, 1.0);
    const ClipId b = f.addClip(f.v1, 1.5, 1.0);
    batchApplyTransitions(f.editor, f.project, f.seqId, f.v1, {a, b},
                          BatchTransitionOptions{Rational::make(1, 10) == Rational::make(1, 10)
                                                     ? TransitionType::CrossDissolve
                                                     : TransitionType::CrossDissolve,
                                                 Rational::make(1, 10)});
    const auto names = f.editor.undoHistoryNames();
    NC_CHECK(!names.empty());
    NC_CHECK_EQ(names.back(), std::string("批量应用转场"));
}
