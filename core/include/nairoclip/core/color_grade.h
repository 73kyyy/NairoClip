#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

/// One normalized input/output control point for a 1D grading curve.
struct ColorCurvePoint {
    double input = 0.0;
    double output = 0.0;
};

/// Monotonic-input, normalized RGB grading curve. Identity is the default.
struct ColorCurve {
    std::vector<ColorCurvePoint> points{{0.0, 0.0}, {1.0, 1.0}};

    static constexpr std::size_t kMinPoints = 2;
    static constexpr std::size_t kMaxPoints = 32;

    bool valid() const;
    bool isIdentity() const;
    double evaluate(double input) const;
};

/// Clip-level primary/master and per-channel curves. All values are normalized
/// to [0, 1]; master is evaluated before the individual RGB channel curves.
struct ColorGrade {
    ColorCurve master;
    ColorCurve red;
    ColorCurve green;
    ColorCurve blue;

    bool valid() const;
    bool isIdentity() const;
    void apply(double& r, double& g, double& b) const;
};

}  // namespace core
}  // namespace nairoclip
