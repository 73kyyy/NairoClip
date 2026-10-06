#include "test_framework.h"
#include "nairoclip/core/transition.h"
using namespace nairoclip::core;
namespace {
Clip makeClip(std::uint64_t id, const Time& start, const Time& duration) {
    Clip c;
    c.id = ClipId{id};
    c.start = start;
    c.duration = duration;
    return c;
}
Track makeTrack() {
    Track t;
    t.id = TrackId{1};
    t.clips.push_back(makeClip(1, Rational::make(0), Rational::make(2)));
    t.clips.push_back(makeClip(2, Rational::make(3), Rational::make(2)));
    return t;
}
Transition makeTransition(std::uint64_t id, TransitionType type, const Time& duration) {
    Transition tr;
    tr.id = TransitionId{id};
    tr.type = type;
    tr.duration = duration;
    tr.fromClip = ClipId{1};
    tr.toClip = ClipId{2};
    return tr;
}
}  // namespace
NC_TEST(Transition, Names) {
    NC_CHECK(std::string(transitionName(TransitionType::CrossDissolve)) == "交叉溶解");
    NC_CHECK(std::string(transitionName(TransitionType::FlashWhite)) == "闪白");
    NC_CHECK(std::string(transitionName(TransitionType::Zoom)) == "缩放");
}
NC_TEST(Transition, ValidWithinGap) {
    const Track track = makeTrack();
    const Transition tr = makeTransition(1, TransitionType::CrossDissolve, Rational::make(1));
    NC_CHECK(validateTransition(track, tr).isOk());
}
NC_TEST(Transition, RejectsZeroDuration) {
    const Track track = makeTrack();
    Transition tr = makeTransition(1, TransitionType::CrossDissolve, Rational::make(1));
    tr.duration = Rational::make(0);
    const Result r = validateTransition(track, tr);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
}
NC_TEST(Transition, RejectsMissingClips) {
    const Track track = makeTrack();
    Transition tr = makeTransition(1, TransitionType::CrossDissolve, Rational::make(1));
    tr.toClip = ClipId{99};
    const Result r = validateTransition(track, tr);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::NotFound);
}
NC_TEST(Transition, RejectsTooLongForGap) {
    const Track track = makeTrack();
    const Transition tr = makeTransition(1, TransitionType::CrossDissolve, Rational::make(2));
    const Result r = validateTransition(track, tr);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
}
NC_TEST(Transition, RejectsReversedOrder) {
    const Track track = makeTrack();
    Transition tr = makeTransition(1, TransitionType::CrossDissolve, Rational::make(1));
    tr.fromClip = ClipId{2};
    tr.toClip = ClipId{1};
    const Result r = validateTransition(track, tr);
    NC_CHECK(!r.isOk());
}
NC_TEST(Transition, RejectsConflictWithExisting) {
    const Track track = makeTrack();
    const Transition a = makeTransition(1, TransitionType::CrossDissolve, Rational::make(1));
    Transition b = makeTransition(2, TransitionType::FlashWhite, Rational::make(1));
    const std::vector<Transition> existing{a};
    const Result r = validateTransition(track, b, existing);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
}
NC_TEST(Transition, AllowsSelfInExisting) {
    const Track track = makeTrack();
    const Transition a = makeTransition(1, TransitionType::CrossDissolve, Rational::make(1));
    const std::vector<Transition> existing{a};
    NC_CHECK(validateTransition(track, a, existing).isOk());
}
#include "nairoclip/core/editor.h"
NC_TEST(Transition, EditorAddAndUndo) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.type = MediaType::Video;
    a.duration = Rational::make(60);
    project.assets.push_back(a);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    ClipId c1;
    ClipId c2;
    Clip clip1;
    clip1.asset = a.id;
    clip1.start = Rational::make(0);
    clip1.duration = Rational::make(2);
    Clip clip2;
    clip2.asset = a.id;
    clip2.start = Rational::make(3);
    clip2.duration = Rational::make(2);
    NC_CHECK(editor.insertClip(seqId, trackId, clip1, &c1).isOk());
    NC_CHECK(editor.insertClip(seqId, trackId, clip2, &c2).isOk());
    Transition tr;
    tr.id = TransitionId{project.ids.next()};
    tr.type = TransitionType::CrossDissolve;
    tr.duration = Rational::make(1);
    tr.fromClip = c1;
    tr.toClip = c2;
    NC_CHECK(editor.addTransition(seqId, trackId, tr).isOk());
    const Track* track = findTrack(project, seqId, trackId);
    NC_CHECK_EQ(track->transitions.size(), std::size_t(1));
    // 撤销 → 移除
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(findTrack(project, seqId, trackId)->transitions.size(), std::size_t(0));
    // 重做 → 恢复
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(findTrack(project, seqId, trackId)->transitions.size(), std::size_t(1));
    // 删除转场
    NC_CHECK(editor.removeTransition(seqId, trackId, tr.id).isOk());
    NC_CHECK_EQ(findTrack(project, seqId, trackId)->transitions.size(), std::size_t(0));
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(findTrack(project, seqId, trackId)->transitions.size(), std::size_t(1));
}
NC_TEST(Transition, EditorRejectsInvalid) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    // 不存在的片段 → 拒绝
    Transition tr;
    tr.id = TransitionId{project.ids.next()};
    tr.fromClip = ClipId{99};
    tr.toClip = ClipId{100};
    const std::size_t undoBefore = editor.undoCount();
    const Result r = editor.addTransition(seqId, trackId, tr);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::NotFound);
    NC_CHECK_EQ(editor.undoCount(), undoBefore);  // 失败不入栈
}
