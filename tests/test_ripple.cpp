#include "test_framework.h"
#include "nairoclip/core/editor.h"
using namespace nairoclip::core;
namespace {
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
        a.type = MediaType::Video;
        a.duration = Rational::make(1000);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
    }
    ClipId addClip(const Time& start, const Time& duration) {
        Clip c;
        c.asset = assetId;
        c.start = start;
        c.duration = duration;
        ClipId id;
        editor.insertClip(seqId, trackId, c, &id);
        return id;
    }
    Track* track() { return findTrack(project, seqId, trackId); }
};
}  // namespace
NC_TEST(Ripple, DeleteShiftsFollowingClips) {
    Fixture f;
    const ClipId a = f.addClip(Rational::make(0), Rational::make(2));    // [0,2)
    const ClipId b = f.addClip(Rational::make(5), Rational::make(3));    // [5,8)
    // 删除 a → b 前移 2s → [3,6)
    NC_CHECK(f.editor.rippleDelete(f.seqId, f.trackId, {a}).isOk());
    const Track* t = f.track();
    NC_CHECK_EQ(t->clips.size(), std::size_t(1));
    NC_CHECK(t->clips[0].id == b);
    NC_CHECK(t->clips[0].start == Rational::make(3));
    NC_CHECK(t->invariantsHold());
}
NC_TEST(Ripple, DeleteMiddleClip) {
    Fixture f;
    const ClipId a = f.addClip(Rational::make(0), Rational::make(2));    // [0,2)
    const ClipId b = f.addClip(Rational::make(2), Rational::make(1));    // [2,3)
    const ClipId c = f.addClip(Rational::make(3), Rational::make(2));    // [3,5)
    NC_CHECK(f.editor.rippleDelete(f.seqId, f.trackId, {b}).isOk());
    const Track* t = f.track();
    NC_CHECK_EQ(t->clips.size(), std::size_t(2));
    NC_CHECK(t->clips[0].id == a);
    NC_CHECK(t->clips[1].id == c);
    NC_CHECK(t->clips[1].start == Rational::make(2));  // c 前移 1s
}
NC_TEST(Ripple, UndoRestoresTimeline) {
    Fixture f;
    const ClipId a = f.addClip(Rational::make(0), Rational::make(2));
    const ClipId b = f.addClip(Rational::make(5), Rational::make(3));
    NC_CHECK(f.editor.rippleDelete(f.seqId, f.trackId, {a}).isOk());
    NC_CHECK(f.track()->clips[0].start == Rational::make(3));
    // 撤销
    NC_CHECK(f.editor.undo().isOk());
    const Track* t = f.track();
    NC_CHECK_EQ(t->clips.size(), std::size_t(2));
    NC_CHECK(t->clips[0].id == a);
    NC_CHECK(t->clips[0].start == Rational::make(0));
    NC_CHECK(t->clips[1].id == b);
    NC_CHECK(t->clips[1].start == Rational::make(5));
    NC_CHECK(t->invariantsHold());
    // 重做
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
    NC_CHECK(f.track()->clips[0].start == Rational::make(3));
}
NC_TEST(Ripple, BatchDelete) {
    Fixture f;
    const ClipId a = f.addClip(Rational::make(0), Rational::make(1));
    const ClipId b = f.addClip(Rational::make(1), Rational::make(2));
    const ClipId c = f.addClip(Rational::make(3), Rational::make(1));
    // 批量删除 a、c → b 前移
    NC_CHECK(f.editor.rippleDelete(f.seqId, f.trackId, {a, c}).isOk());
    const Track* t = f.track();
    NC_CHECK_EQ(t->clips.size(), std::size_t(1));
    NC_CHECK(t->clips[0].id == b);
    // a 与 c 共删除 2s：a 删除后 b[1,3)→[0,2)；c[3,4)→[2,3) 删除后再前移 1s → [1,2)
    NC_CHECK(t->invariantsHold());
}
NC_TEST(Ripple, DeleteMissingClipRejected) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(1));
    const Result r = f.editor.rippleDelete(f.seqId, f.trackId, {ClipId{999}});
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::NotFound);
}
NC_TEST(Ripple, InsertEditShiftsAndSplits) {
    Fixture f;
    const ClipId a = f.addClip(Rational::make(0), Rational::make(4));  // [0,4)
    // 在 t=2 插入 1s 新片段：a 分割为 [0,2)+[3,5)
    Clip newClip;
    newClip.asset = f.assetId;
    newClip.duration = Rational::make(1);
    newClip.sourceIn = Rational::make(10);
    NC_CHECK(f.editor.insertEdit(f.seqId, f.trackId, newClip, Rational::make(2)).isOk());
    const Track* t = f.track();
    NC_CHECK_EQ(t->clips.size(), std::size_t(3));
    NC_CHECK(t->invariantsHold());
    // [0,2) 左半
    NC_CHECK(t->clips[0].id == a);
    NC_CHECK(t->clips[0].start == Rational::make(0));
    NC_CHECK(t->clips[0].duration == Rational::make(2));
    // [2,3) 新插入
    NC_CHECK(t->clips[1].start == Rational::make(2));
    NC_CHECK(t->clips[1].duration == Rational::make(1));
    NC_CHECK(t->clips[1].sourceIn == Rational::make(10));
    // [3,5) 右半（原 [2,4) 后移 1s）
    NC_CHECK(t->clips[2].start == Rational::make(3));
    NC_CHECK(t->clips[2].duration == Rational::make(2));
    NC_CHECK(t->clips[2].sourceIn == Rational::make(2));
}
NC_TEST(Ripple, InsertEditUndo) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(4));
    Clip newClip;
    newClip.asset = f.assetId;
    newClip.duration = Rational::make(1);
    NC_CHECK(f.editor.insertEdit(f.seqId, f.trackId, newClip, Rational::make(2)).isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(3));
    NC_CHECK(f.editor.undo().isOk());
    const Track* t = f.track();
    NC_CHECK_EQ(t->clips.size(), std::size_t(1));
    NC_CHECK(t->clips[0].start == Rational::make(0));
    NC_CHECK(t->clips[0].duration == Rational::make(4));
    NC_CHECK(t->invariantsHold());
}
