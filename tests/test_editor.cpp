#include "test_framework.h"
#include "nairoclip/core/editor.h"
using namespace nairoclip::core;
namespace {
struct Fixture {
    Project project;
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    Fixture() {
        project.name = "测试工程";
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "测试视频.mp4";
        asset.type = MediaType::Video;
        asset.duration = Rational::make(60);
        asset.width = 1920;
        asset.height = 1080;
        project.assets.push_back(asset);
        assetId = project.assets.front().id;
        TimelineEditor editor(project);
        TrackId id;
        const Result r = editor.addTrack(seqId, TrackType::Video, "视频轨 1", &id);
        NC_CHECK(r.isOk());
        trackId = id;
    }
    Track* track() { return findTrack(project, seqId, trackId); }
};
Clip makeClip(const AssetId& asset, const Time& start, const Time& duration,
              const Time& sourceIn = Rational::make(0),
              const Rational& speed = Rational::make(1)) {
    Clip c;
    c.asset = asset;
    c.start = start;
    c.duration = duration;
    c.sourceIn = sourceIn;
    c.speed = speed;
    return c;
}
}  // namespace
NC_TEST(Editor, InsertAndQuery) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    const Result r = editor.insertClip(
        f.seqId, f.trackId, makeClip(f.assetId, Rational::make(0), Rational::make(5)), &clipId);
    NC_CHECK(r.isOk());
    NC_CHECK(clipId.isValid());
    Track* track = f.track();
    NC_CHECK(track != nullptr);
    NC_CHECK_EQ(track->clips.size(), std::size_t(1));
    NC_CHECK(track->clips[0].end() == Rational::make(5));
    NC_CHECK(track->invariantsHold());
}
NC_TEST(Editor, InsertOverlapRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)))
                 .isOk());
    const Result r = editor.insertClip(
        f.seqId, f.trackId, makeClip(f.assetId, Rational::make(3), Rational::make(5)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
    NC_CHECK_EQ(editor.undoCount(), std::size_t(1));
}
NC_TEST(Editor, SourceRangeRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    const Result r = editor.insertClip(
        f.seqId, f.trackId,
        makeClip(f.assetId, Rational::make(0), Rational::make(10), Rational::make(55)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
}
NC_TEST(Editor, UndoRedoInsert) {
    Fixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)))
                 .isOk());
    NC_CHECK_EQ(editor.undoCount(), std::size_t(1));
    NC_CHECK(editor.canUndo());
    NC_CHECK(!editor.canRedo());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(0));
    NC_CHECK(editor.canRedo());
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
    NC_CHECK(f.track()->clips[0].start == Rational::make(0));
}
NC_TEST(Editor, SplitClip) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(4)).isOk());
    Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(2));
    NC_CHECK(track->invariantsHold());
    NC_CHECK(track->clips[0].end() == Rational::make(4));
    NC_CHECK(track->clips[1].start == Rational::make(4));
    NC_CHECK(track->clips[1].sourceIn == Rational::make(4));
    NC_CHECK(editor.undo().isOk());
    track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(1));
    NC_CHECK(track->clips[0].end() == Rational::make(10));
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(2));
}
NC_TEST(Editor, SplitRejectsOutside) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(!editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(0)).isOk());
    NC_CHECK(!editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(10)).isOk());
    NC_CHECK(!editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(12)).isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
}
NC_TEST(Editor, MoveClip) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(2)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(5), Rational::make(2)), &b)
                 .isOk());
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, a, Rational::make(3)).isOk());
    NC_CHECK(f.track()->clips[0].start == Rational::make(3));
    const Result r = editor.moveClip(f.seqId, f.trackId, a, Rational::make(4));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
    NC_CHECK(f.track()->clips[0].start == Rational::make(3));
    NC_CHECK(!editor.moveClip(f.seqId, f.trackId, a, Rational::make(-1)).isOk());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.track()->clips[0].start == Rational::make(0));
}
NC_TEST(Editor, TrimStartAdjustsSourceIn) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(2), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.trimClipStart(f.seqId, f.trackId, clipId, Rational::make(5)).isOk());
    Track* track = f.track();
    NC_CHECK(track->clips[0].start == Rational::make(5));
    NC_CHECK(track->clips[0].duration == Rational::make(7));
    NC_CHECK(track->clips[0].sourceIn == Rational::make(3));
    NC_CHECK(editor.undo().isOk());
    track = f.track();
    NC_CHECK(track->clips[0].start == Rational::make(2));
    NC_CHECK(track->clips[0].sourceIn == Rational::make(0));
    NC_CHECK(track->clips[0].duration == Rational::make(10));
}
NC_TEST(Editor, TrimStartBeyondSourceRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(10), Rational::make(5)),
                               &clipId)
                 .isOk());
    const Result r = editor.trimClipStart(f.seqId, f.trackId, clipId, Rational::make(8));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
}
NC_TEST(Editor, TrimEnd) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.trimClipEnd(f.seqId, f.trackId, clipId, Rational::make(6)).isOk());
    NC_CHECK(f.track()->clips[0].duration == Rational::make(6));
    const Result r = editor.trimClipEnd(f.seqId, f.trackId, clipId, Rational::make(61));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
    NC_CHECK(f.track()->clips[0].duration == Rational::make(6));
}
NC_TEST(Editor, SetSpeedKeepsSourceRange) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(2)).isOk());
    Track* track = f.track();
    NC_CHECK(track->clips[0].duration == Rational::make(5));
    NC_CHECK(track->clips[0].sourceOut() == Rational::make(10));
    NC_CHECK(track->clips[0].speed == Rational::make(2));
    NC_CHECK(editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(1, 2)).isOk());
    track = f.track();
    NC_CHECK(track->clips[0].duration == Rational::make(20));
    NC_CHECK(track->clips[0].sourceOut() == Rational::make(10));
    NC_CHECK(!editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(20)).isOk());
    NC_CHECK(!editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(0)).isOk());
}
NC_TEST(Editor, SpeedOverlapRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(5), Rational::make(5)), &b)
                 .isOk());
    const Result r = editor.setClipSpeed(f.seqId, f.trackId, a, Rational::make(1, 2));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
    NC_CHECK(f.track()->clips[0].duration == Rational::make(5));
}
NC_TEST(Editor, RemoveClipAndUndoOrder) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    ClipId c;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(2)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(4), Rational::make(2)), &b)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(8), Rational::make(2)), &c)
                 .isOk());
    NC_CHECK(editor.removeClip(f.seqId, f.trackId, b).isOk());
    Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(2));
    NC_CHECK(track->findClip(b) == nullptr);
    NC_CHECK(editor.undo().isOk());
    track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(3));
    NC_CHECK(track->clips[1].id == b);
    NC_CHECK(track->invariantsHold());
}
NC_TEST(Editor, RedoTruncatedByNewCommand) {
    Fixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(2)))
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(4), Rational::make(2)))
                 .isOk());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(editor.canRedo());
    NC_CHECK_EQ(editor.redoCount(), std::size_t(1));
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(8), Rational::make(2)))
                 .isOk());
    NC_CHECK(!editor.canRedo());
    NC_CHECK_EQ(editor.redoCount(), std::size_t(0));
    NC_CHECK_EQ(editor.undoCount(), std::size_t(2));
}
NC_TEST(Editor, HistoryNames) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(4)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(2)).isOk());
    const auto names = editor.undoHistoryNames();
    NC_CHECK_EQ(names.size(), std::size_t(2));
    NC_CHECK_EQ(names[0], std::string("添加片段"));
    NC_CHECK_EQ(names[1], std::string("分割片段"));
}
NC_TEST(Editor, LockedTrackRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    f.track()->locked = true;
    const Result r = editor.insertClip(
        f.seqId, f.trackId, makeClip(f.assetId, Rational::make(0), Rational::make(2)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Locked);
}
NC_TEST(Editor, ImportAsset) {
    Fixture f;
    TimelineEditor editor(f.project);
    MediaAsset asset;
    asset.name = "音乐.mp3";
    asset.path = "/tmp/music.mp3";
    asset.type = MediaType::Audio;
    asset.duration = Rational::make(120);
    const AssetId id = editor.importAsset(asset);
    NC_CHECK(id.isValid());
    NC_CHECK(f.project.findAsset(id) != nullptr);
    NC_CHECK(f.project.findAsset(id)->name == std::string("音乐.mp3"));
}
