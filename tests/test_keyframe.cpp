#include "test_framework.h"
#include "nairoclip/core/keyframe.h"
using namespace nairoclip::core;
namespace {
Keyframe makeKey(std::int64_t timeNum, std::int64_t timeDen, double value) {
    Keyframe k;
    k.time = Rational::make(timeNum, timeDen);
    k.value = value;
    return k;
}
bool nearly(double a, double b, double eps = 1e-6) {
    const double d = a - b;
    return (d < 0 ? -d : d) < eps;
}
}  // namespace
NC_TEST(Keyframe, EasingPresets) {
    NC_CHECK(nearly(Easing::linear().map(0.5), 0.5));
    NC_CHECK(nearly(Easing::easeIn().map(0.0), 0.0));
    NC_CHECK(nearly(Easing::easeIn().map(1.0), 1.0));
    NC_CHECK(nearly(Easing::easeOut().map(0.0), 0.0));
    NC_CHECK(nearly(Easing::easeOut().map(1.0), 1.0));
    NC_CHECK(nearly(Easing::easeInOut().map(0.5), 0.5, 1e-3));
    NC_CHECK(Easing::easeIn().map(0.5) < 0.5);
    NC_CHECK(Easing::easeOut().map(0.5) > 0.5);
    NC_CHECK(nearly(Easing::stepped().map(0.99), 0.0));
    NC_CHECK(nearly(Easing::stepped().map(1.0), 1.0));
}
NC_TEST(Keyframe, BezierClampsControlX) {
    const Easing e = Easing::bezier(-1.0, 0.3, 2.0, 0.7);
    NC_CHECK(nearly(e.c1x, 0.0));
    NC_CHECK(nearly(e.c2x, 1.0));
    NC_CHECK(nearly(e.c1y, 0.3));
    NC_CHECK(nearly(e.c2y, 0.7));
    NC_CHECK(nearly(e.map(0.0), 0.0));
    NC_CHECK(nearly(e.map(1.0), 1.0));
    NC_CHECK(e.map(0.5) > 0.0 && e.map(0.5) < 1.0);
}
NC_TEST(Keyframe, InsertSortedAndReplace) {
    KeyframeTrack track;
    NC_CHECK(track.insert(makeKey(1, 1, 10.0)).isOk());
    NC_CHECK(track.insert(makeKey(0, 1, 0.0)).isOk());
    NC_CHECK(track.insert(makeKey(2, 1, 20.0)).isOk());
    NC_CHECK_EQ(track.size(), std::size_t(3));
    NC_CHECK(track.invariantsHold());
    NC_CHECK(track.keyframes()[0].time == Rational::make(0));
    NC_CHECK(track.keyframes()[1].time == Rational::make(1));
    NC_CHECK(track.keyframes()[2].time == Rational::make(2));
    NC_CHECK(track.insert(makeKey(1, 1, 99.0)).isOk());
    NC_CHECK_EQ(track.size(), std::size_t(3));
    NC_CHECK(nearly(track.keyframes()[1].value, 99.0));
}
NC_TEST(Keyframe, InsertRejectsNegativeTime) {
    KeyframeTrack track;
    const Result r = track.insert(makeKey(-1, 1, 5.0));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
    NC_CHECK(track.empty());
}
NC_TEST(Keyframe, SampleInterpolates) {
    KeyframeTrack track;
    NC_CHECK(track.insert(makeKey(0, 1, 0.0)).isOk());
    NC_CHECK(track.insert(makeKey(2, 1, 100.0)).isOk());
    NC_CHECK(nearly(track.sample(Rational::make(1)), 50.0));
    NC_CHECK(nearly(track.sample(Rational::make(0)), 0.0));
    NC_CHECK(nearly(track.sample(Rational::make(2)), 100.0));
}
NC_TEST(Keyframe, SampleClampsOutside) {
    KeyframeTrack track;
    NC_CHECK(track.insert(makeKey(1, 1, 10.0)).isOk());
    NC_CHECK(track.insert(makeKey(3, 1, 30.0)).isOk());
    NC_CHECK(nearly(track.sample(Rational::make(0)), 10.0));
    NC_CHECK(nearly(track.sample(Rational::make(5)), 30.0));
}
NC_TEST(Keyframe, SampleWithEasing) {
    KeyframeTrack track;
    Keyframe a = makeKey(0, 1, 0.0);
    a.easing = Easing::easeIn();
    NC_CHECK(track.insert(a).isOk());
    NC_CHECK(track.insert(makeKey(1, 1, 100.0)).isOk());
    const double mid = track.sample(Rational::make(1, 2));
    NC_CHECK(mid < 50.0);
    NC_CHECK(mid > 0.0);
}
NC_TEST(Keyframe, SteppedHoldsValue) {
    KeyframeTrack track;
    Keyframe a = makeKey(0, 1, 5.0);
    a.easing = Easing::stepped();
    NC_CHECK(track.insert(a).isOk());
    NC_CHECK(track.insert(makeKey(1, 1, 50.0)).isOk());
    NC_CHECK(nearly(track.sample(Rational::make(1, 4)), 5.0));
    NC_CHECK(nearly(track.sample(Rational::make(3, 4)), 5.0));
    NC_CHECK(nearly(track.sample(Rational::make(1)), 50.0));
}
NC_TEST(Keyframe, RemoveAndClear) {
    KeyframeTrack track;
    NC_CHECK(track.insert(makeKey(0, 1, 1.0)).isOk());
    NC_CHECK(track.insert(makeKey(1, 1, 2.0)).isOk());
    NC_CHECK(track.remove(Rational::make(0)));
    NC_CHECK(!track.remove(Rational::make(99)));
    NC_CHECK_EQ(track.size(), std::size_t(1));
    NC_CHECK(track.invariantsHold());
    track.clear();
    NC_CHECK(track.empty());
}
NC_TEST(Keyframe, SampleEmptyReturnsFallback) {
    KeyframeTrack track;
    NC_CHECK(nearly(track.sample(Rational::make(5), -1.0), -1.0));
}
NC_TEST(Keyframe, FractionalTimesNoDrift) {
    KeyframeTrack track;
    NC_CHECK(track.insert(makeKey(0, 1, 0.0)).isOk());
    NC_CHECK(track.insert(makeKey(1001, 30000, 1.0)).isOk());
    NC_CHECK(track.invariantsHold());
    const double quarter = track.sample(Rational::make(1001, 120000));
    NC_CHECK(nearly(quarter, 0.25, 1e-4));
}
