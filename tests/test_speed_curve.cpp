#include "test_framework.h"
#include "nairoclip/core/speed_curve.h"
using namespace nairoclip::core;
namespace {
SpeedPoint makePoint(std::int64_t tNum, std::int64_t tDen, const Rational& speed) {
    SpeedPoint p;
    p.sourceTime = Rational::make(tNum, tDen);
    p.speed = speed;
    return p;
}
bool nearly(double a, double b, double eps = 1e-6) {
    const double d = a - b;
    return (d < 0 ? -d : d) < eps;
}
}  // namespace
NC_TEST(SpeedCurve, AddAndReplacePoint) {
    SpeedCurve curve;
    NC_CHECK(curve.addPoint(makePoint(0, 1, Rational::make(2))).isOk());
    NC_CHECK(curve.addPoint(makePoint(5, 1, Rational::make(1, 2))).isOk());
    NC_CHECK_EQ(curve.size(), std::size_t(2));
    NC_CHECK(curve.invariantsHold());
    NC_CHECK(curve.addPoint(makePoint(0, 1, Rational::make(4))).isOk());
    NC_CHECK_EQ(curve.size(), std::size_t(2));
    NC_CHECK(nearly(curve.speedAt(Rational::make(0)), 4.0));
}
NC_TEST(SpeedCurve, RejectsOutOfRange) {
    SpeedCurve curve;
    const Result tooFast = curve.addPoint(makePoint(0, 1, Rational::make(20)));
    NC_CHECK(!tooFast.isOk());
    const Result tooSlow = curve.addPoint(makePoint(0, 1, Rational::make(1, 100)));
    NC_CHECK(!tooSlow.isOk());
    NC_CHECK(curve.empty());
}
NC_TEST(SpeedCurve, ConstantSpeed) {
    const SpeedCurve curve = SpeedCurve::constant(Rational::make(2));
    NC_CHECK(nearly(curve.speedAt(Rational::make(0)), 2.0));
    NC_CHECK(nearly(curve.speedAt(Rational::make(100)), 2.0));
    const Time d = curve.timelineDuration(Rational::make(0), Rational::make(10));
    NC_CHECK(nearly(d.toDouble(), 5.0, 1e-3));
}
NC_TEST(SpeedCurve, SpeedAtInterpolates) {
    SpeedCurve curve;
    NC_CHECK(curve.addPoint(makePoint(0, 1, Rational::make(1))).isOk());
    NC_CHECK(curve.addPoint(makePoint(10, 1, Rational::make(3))).isOk());
    NC_CHECK(nearly(curve.speedAt(Rational::make(5)), 2.0, 1e-4));
    NC_CHECK(nearly(curve.speedAt(Rational::make(0)), 1.0));
    NC_CHECK(nearly(curve.speedAt(Rational::make(10)), 3.0));
    NC_CHECK(nearly(curve.speedAt(Rational::make(-5)), 1.0));
    NC_CHECK(nearly(curve.speedAt(Rational::make(50)), 3.0));
}
NC_TEST(SpeedCurve, RampIntegration) {
    SpeedCurve curve;
    NC_CHECK(curve.addPoint(makePoint(0, 1, Rational::make(1))).isOk());
    NC_CHECK(curve.addPoint(makePoint(2, 1, Rational::make(2))).isOk());
    const Time d = curve.timelineDuration(Rational::make(0), Rational::make(2));
    // ∫0^2 1/(1+s/2) ds = 2*ln(2) ≈ 1.3863
    NC_CHECK(nearly(d.toDouble(), 1.3862943, 1e-3));
}
NC_TEST(SpeedCurve, RemoveAndClear) {
    SpeedCurve curve;
    NC_CHECK(curve.addPoint(makePoint(0, 1, Rational::make(1))).isOk());
    NC_CHECK(curve.addPoint(makePoint(3, 1, Rational::make(2))).isOk());
    NC_CHECK(curve.removePoint(Rational::make(0)));
    NC_CHECK(!curve.removePoint(Rational::make(99)));
    NC_CHECK_EQ(curve.size(), std::size_t(1));
    curve.clear();
    NC_CHECK(curve.empty());
}
NC_TEST(SpeedCurve, EmptyCurveUsesFallback) {
    const SpeedCurve curve;
    NC_CHECK(nearly(curve.speedAt(Rational::make(5), Rational::make(3)), 3.0));
    const Time d = curve.timelineDuration(Rational::make(0), Rational::make(9),
                                          Rational::make(3));
    NC_CHECK(nearly(d.toDouble(), 3.0, 1e-6));
}
NC_TEST(SpeedCurve, ZeroRangeReturnsZero) {
    const SpeedCurve curve = SpeedCurve::constant(Rational::make(2));
    const Time d = curve.timelineDuration(Rational::make(5), Rational::make(5));
    NC_CHECK(nearly(d.toDouble(), 0.0));
    const Time neg = curve.timelineDuration(Rational::make(5), Rational::make(3));
    NC_CHECK(nearly(neg.toDouble(), 0.0));
}
