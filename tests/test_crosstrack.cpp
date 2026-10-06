#include "test_framework.h"
#include "nairoclip/core/editor.h"
using namespace nairoclip::core;
namespace {
struct Fixture {
    Project project;
    SequenceId seqId;
    TrackId videoTrack1;
    TrackId videoTrack2;
    TrackId audioTrack;
    AssetId assetId;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "clip.mp4";
        asset.type = MediaType::Video;
        asset.duration = Rational::make(60);
        project.assets.push_back(asset);
        assetId = project.assets.front().id;
        TimelineEditor editor(project);
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &videoTrack1).isOk());
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V2", &videoTrack2).isOk());
        NC_CHECK(editor.addTrack(seqId, TrackType::Audio, "A1", &audioTrack).isOk());
    }
    Track* track(TrackId id) { return findTrack(project, seqId, id); }
};
Clip makeClip(const AssetId& asset, const Time& start, const Time& duration) {
    Clip c;
    c.asset = asset;
    c.start = start;
    c.duration = duration;
    return c;
}
}  // namespace
NC_TEST(CrossTrack, MoveToAnotherTrack) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId id;
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack1,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &id)
                 .isOk());
    NC_CHECK(editor.moveClipToTrack(f.seqId, f.videoTrack1, f.videoTrack2, id,
                                    Rational::make(2))
                 .isOk());
    NC_CHECK_EQ(f.track(f.videoTrack1)->clips.size(), std::size_t(0));
    NC_CHECK_EQ(f.track(f.videoTrack2)->clips.size(), std::size_t(1));
    NC_CHECK(f.track(f.videoTrack2)->clips[0].start == Rational::make(2));
    NC_CHECK(f.track(f.videoTrack2)->invariantsHold());
}
NC_TEST(CrossTrack, UndoRestoresBothTracks) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack1,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack2,
                               makeClip(f.assetId, Rational::make(7), Rational::make(5)), &b)
                 .isOk());
    // b 占用 [7,12)，移动到 [12,17) 避免重叠
    NC_CHECK(editor.moveClipToTrack(f.seqId, f.videoTrack1, f.videoTrack2, a,
                                    Rational::make(12))
                 .isOk());
    NC_CHECK_EQ(f.track(f.videoTrack2)->clips.size(), std::size_t(2));
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.track(f.videoTrack1)->clips.size(), std::size_t(1));
    NC_CHECK_EQ(f.track(f.videoTrack2)->clips.size(), std::size_t(1));
    NC_CHECK(f.track(f.videoTrack1)->clips[0].id == a);
    NC_CHECK(f.track(f.videoTrack1)->clips[0].start == Rational::make(0));
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.track(f.videoTrack1)->clips.size(), std::size_t(0));
    NC_CHECK_EQ(f.track(f.videoTrack2)->clips.size(), std::size_t(2));
}
NC_TEST(CrossTrack, RejectsSameTrack) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId id;
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack1,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &id)
                 .isOk());
    const Result r =
        editor.moveClipToTrack(f.seqId, f.videoTrack1, f.videoTrack1, id, Rational::make(1));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
}
NC_TEST(CrossTrack, RejectsOverlapOnTarget) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack1,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack2,
                               makeClip(f.assetId, Rational::make(3), Rational::make(5)), &b)
                 .isOk());
    const Result r =
        editor.moveClipToTrack(f.seqId, f.videoTrack1, f.videoTrack2, a, Rational::make(4));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
    NC_CHECK_EQ(f.track(f.videoTrack1)->clips.size(), std::size_t(1));
    NC_CHECK_EQ(f.track(f.videoTrack2)->clips.size(), std::size_t(1));
}
NC_TEST(CrossTrack, RejectsLockedTarget) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId id;
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack1,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &id)
                 .isOk());
    f.track(f.videoTrack2)->locked = true;
    const Result r =
        editor.moveClipToTrack(f.seqId, f.videoTrack1, f.videoTrack2, id, Rational::make(1));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Locked);
}
NC_TEST(CrossTrack, HistoryName) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId id;
    NC_CHECK(editor.insertClip(f.seqId, f.videoTrack1,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &id)
                 .isOk());
    NC_CHECK(editor.moveClipToTrack(f.seqId, f.videoTrack1, f.videoTrack2, id,
                                    Rational::make(0))
                 .isOk());
    const auto names = editor.undoHistoryNames();
    NC_CHECK_EQ(names.size(), std::size_t(2));
    NC_CHECK_EQ(names[1], std::string("跨轨移动"));
}
