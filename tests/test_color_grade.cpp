#include "test_framework.h"
#include "nairoclip/core/color_grade.h"

#include <cmath>

using namespace nairoclip::core;

NC_TEST(ColorGrade, IdentityAndInterpolation) {
    ColorCurve curve;
    NC_CHECK(curve.valid());
    NC_CHECK(curve.isIdentity());
    NC_CHECK(std::abs(curve.evaluate(0.25) - 0.25) < 1e-9);
    curve.points = {{0.0, 0.0}, {0.5, 0.8}, {1.0, 1.0}};
    NC_CHECK(curve.valid());
    NC_CHECK(std::abs(curve.evaluate(0.25) - 0.4) < 1e-9);
}

NC_TEST(ColorGrade, RejectsInvalidControlPoints) {
    ColorCurve curve;
    curve.points = {{0.0, 0.0}, {0.7, 0.6}, {0.6, 0.8}, {1.0, 1.0}};
    NC_CHECK(!curve.valid());
    curve.points = {{0.0, 0.0}, {1.0, 1.2}};
    NC_CHECK(!curve.valid());
    curve.points = {{0.0, 0.0}};
    NC_CHECK(!curve.valid());
}

NC_TEST(ColorGrade, MasterThenChannelOrder) {
    ColorGrade grade;
    grade.master.points = {{0.0, 0.0}, {1.0, 0.5}};
    grade.red.points = {{0.0, 0.0}, {1.0, 1.0}};
    double r = 0.8, g = 0.8, b = 0.8;
    grade.apply(r, g, b);
    NC_CHECK(std::abs(r - 0.4) < 1e-9);
    NC_CHECK(std::abs(g - 0.4) < 1e-9);
    NC_CHECK(std::abs(b - 0.4) < 1e-9);
}
