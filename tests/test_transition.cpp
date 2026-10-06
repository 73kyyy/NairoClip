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
