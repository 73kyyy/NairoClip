#include "test_framework.h"
#include "nairoclip/core/project.h"
using namespace nairoclip::core;
namespace {
Clip makeClip(std::uint64_t id, const AssetId& asset, const Time& start, const Time& duration,
              const Rational& speed = Rational::make(1)) {
    Clip c;
    c.id = ClipId{id};
    c.asset = asset;
    c.start = start;
    c.duration = duration;
    c.speed = speed;
    return c;
}
}  // namespace
NC_TEST(Model, ClipDerivedValues) {
    const Clip c = makeClip(1, AssetId{7}, Rational::make(2), Rational::make(3), Rational::make(2));
    NC_CHECK(c.end() == Rational::make(5));
    NC_CHECK(c.sourceOut() == Rational::make(6));
    NC_CHECK(c.contains(Rational::make(2)));
    NC_CHECK(c.contains(Rational::make(4)));
    NC_CHECK(!c.contains(Rational::make(5)));
    NC_CHECK(c.valid());
}
NC_TEST(Model, TrackInvariantsAndQueries) {
    Track track;
    track.id = TrackId{1};
    track.clips.push_back(makeClip(1, AssetId{}, Rational::make(0), Rational::make(2)));
    track.clips.push_back(makeClip(2, AssetId{}, Rational::make(3), Rational::make(2)));
    NC_CHECK(track.invariantsHold());
    NC_CHECK(track.findClip(ClipId{2}) != nullptr);
    NC_CHECK(track.findClip(ClipId{99}) == nullptr);
    NC_CHECK(track.clipAt(Rational::make(1)) != nullptr);
    NC_CHECK(track.clipAt(Rational::make(2)) == nullptr);
    NC_CHECK(track.clipAt(Rational::make(3)) != nullptr);
    NC_CHECK_EQ(track.indexOf(ClipId{2}), 1);
    NC_CHECK(!track.hasOverlap(Rational::make(2), Rational::make(3)));
    NC_CHECK(track.hasOverlap(Rational::make(1), Rational::make(4)));
    NC_CHECK(!track.hasOverlap(Rational::make(0), Rational::make(2), ClipId{1}));
}
NC_TEST(Model, TrackDetectsInvalidState) {
    Track track;
    track.clips.push_back(makeClip(1, AssetId{}, Rational::make(0), Rational::make(4)));
    track.clips.push_back(makeClip(2, AssetId{}, Rational::make(3), Rational::make(4)));
    NC_CHECK(!track.invariantsHold());
}
NC_TEST(Model, SequenceDuration) {
    Project project;
    Sequence seq;
    seq.id = SequenceId{1};
    Track t1;
    t1.id = TrackId{1};
    t1.clips.push_back(makeClip(1, AssetId{}, Rational::make(0), Rational::make(2)));
    Track t2;
    t2.id = TrackId{2};
    t2.clips.push_back(makeClip(2, AssetId{}, Rational::make(5), Rational::make(3)));
    seq.tracks.push_back(t1);
    seq.tracks.push_back(t2);
    project.sequences.push_back(seq);
    const Sequence* main = project.mainSequence();
    NC_CHECK(main != nullptr);
    NC_CHECK(main->duration() == Rational::make(8));
    NC_CHECK(main->invariantsHold());
}
