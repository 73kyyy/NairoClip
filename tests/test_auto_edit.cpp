#include "test_framework.h"
#include "nairoclip/core/auto_edit.h"
#include <cmath>
using namespace nairoclip::core;
namespace {
ShotBoundary shot(double s, double e) {
    ShotBoundary b;
    b.start = Rational::make(static_cast<std::int64_t>(s * 1000), 1000);
    b.end = Rational::make(static_cast<std::int64_t>(e * 1000), 1000);
    return b;
}
struct Fixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/video/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
    }
};
StyleProfile makeProfile() {
    StyleProfile p;
    // 3 镜：1s / 1s / 1s → 中位数 1s
    p.rhythm = analyzeRhythm({shot(0, 1), shot(1, 2), shot(2, 3)});
    p.color = analyzeColor(0.8, 0.5, 0.3, 0.8, 0.7);  // 暖色饱和
    return p;
}
}  // namespace
NC_TEST(AutoEdit, InsertsClipsInTransaction) {
    Fixture f;
    // 记录 AutoEdit 前的撤销计数（addTrack 自身占 1 条）
    const std::size_t undoBefore = f.editor.undoCount();
    const auto result = applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, makeProfile(),
                                      {{f.assetId, Rational::make(10)}});
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.clipsInserted, std::size_t(3));
    const Track* track = findTrack(f.project, f.seqId, f.trackId);
    NC_CHECK_EQ(track->clips.size(), std::size_t(3));
    NC_CHECK(track->invariantsHold());
    // 一连串片段时间上连续
    NC_CHECK(track->clips[0].start == Rational::make(0));
    NC_CHECK(track->clips[1].start == Rational::make(1));
    NC_CHECK(track->clips[2].start == Rational::make(2));
    // AutoEdit 相对增量 = 1 条撤销记录（事务合并）
    NC_CHECK_EQ(f.editor.undoCount() - undoBefore, std::size_t(1));
    const auto names = f.editor.undoHistoryNames();
    NC_CHECK_EQ(names.back(), std::string("参考风格生成"));
}
NC_TEST(AutoEdit, UndoRevertsWholeAutoEdit) {
    Fixture f;
    applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, makeProfile(),
                  {{f.assetId, Rational::make(10)}});
    NC_CHECK_EQ(findTrack(f.project, f.seqId, f.trackId)->clips.size(), std::size_t(3));
    // 一次撤销全部回退
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, f.trackId)->clips.size(), std::size_t(0));
    // 重做恢复
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, f.trackId)->clips.size(), std::size_t(3));
}
NC_TEST(AutoEdit, AppliesColorEffects) {
    Fixture f;
    const auto result = applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, makeProfile(),
                                      {{f.assetId, Rational::make(10)}});
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.effectsAdded > 0);
    const Track* track = findTrack(f.project, f.seqId, f.trackId);
    for (const auto& clip : track->clips) {
        NC_CHECK(!clip.effects.empty());
    }
}
NC_TEST(AutoEdit, RejectsNonEmptyTrack) {
    Fixture f;
    // 先插一个片段
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c).isOk());
    const auto result = applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, makeProfile(),
                                      {{f.assetId, Rational::make(10)}});
    NC_CHECK(!result.result.isOk());
    NC_CHECK(result.result.code == ErrorCode::InvalidArgument);
}
NC_TEST(AutoEdit, RejectsInvalidProfile) {
    Fixture f;
    const auto result = applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, StyleProfile{},
                                      {{f.assetId, Rational::make(10)}});
    NC_CHECK(!result.result.isOk());
}
NC_TEST(AutoEdit, RejectsEmptyMaterial) {
    Fixture f;
    const auto result = applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, makeProfile(), {});
    NC_CHECK(!result.result.isOk());
}
NC_TEST(AutoEdit, RejectsLockedTrack) {
    Fixture f;
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    const auto result = applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, makeProfile(),
                                      {{f.assetId, Rational::make(10)}});
    NC_CHECK(!result.result.isOk());
    NC_CHECK(result.result.code == ErrorCode::Locked);
}
NC_TEST(AutoEdit, NoColorWhenDisabled) {
    Fixture f;
    AutoEditOptions options;
    options.applyColor = false;
    const auto result = applyAutoEdit(f.editor, f.project, f.seqId, f.trackId, makeProfile(),
                                      {{f.assetId, Rational::make(10)}}, options);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.effectsAdded, std::size_t(0));
    const Track* track = findTrack(f.project, f.seqId, f.trackId);
    for (const auto& clip : track->clips) {
        NC_CHECK(clip.effects.empty());
    }
}
