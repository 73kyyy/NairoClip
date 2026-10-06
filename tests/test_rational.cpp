#include "test_framework.h"
#include "nairoclip/core/rational.h"
using namespace nairoclip::core;
NC_TEST(Rational, Normalize) {
    const Rational r = Rational::make(2, 4);
    NC_CHECK_EQ(r.num, 1);
    NC_CHECK_EQ(r.den, 2);
    const Rational neg = Rational::make(1, -2);
    NC_CHECK_EQ(neg.num, -1);
    NC_CHECK_EQ(neg.den, 2);
    const Rational zero = Rational::make(0, 5);
    NC_CHECK_EQ(zero.num, 0);
    NC_CHECK_EQ(zero.den, 1);
    const Rational bad = Rational::make(3, 0);
    NC_CHECK_EQ(bad.num, 0);
    NC_CHECK_EQ(bad.den, 1);
}
NC_TEST(Rational, Arithmetic) {
    const Rational a = Rational::make(1, 3);
    const Rational b = Rational::make(1, 6);
    NC_CHECK(a + b == Rational::make(1, 2));
    NC_CHECK(a - b == Rational::make(1, 6));
    NC_CHECK(a * b == Rational::make(1, 18));
    NC_CHECK(a / b == Rational::make(2));
}
NC_TEST(Rational, CompareAndRound) {
    NC_CHECK(Rational::make(1, 3) < Rational::make(1, 2));
    NC_CHECK(Rational::make(2, 3) > Rational::make(1, 2));
    NC_CHECK(Rational::make(-1, 2) < Rational::make(0));
    NC_CHECK(Rational::make(1, 2) <= Rational::make(1, 2));
    NC_CHECK_EQ(floorDiv(7, 2), 3);
    NC_CHECK_EQ(floorDiv(-7, 2), -4);
    NC_CHECK_EQ(roundToInt(Rational::make(1, 2)), 1);
    NC_CHECK_EQ(roundToInt(Rational::make(2, 5)), 0);
    NC_CHECK_EQ(floorToInt(Rational::make(7, 2)), 3);
}
