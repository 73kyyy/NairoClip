#include "nairoclip/core/color_grade.h"

#include <algorithm>

namespace nairoclip {
namespace core {

bool ColorCurve::valid() const {
    if (points.size() < kMinPoints || points.size() > kMaxPoints) return false;
    if (points.front().input != 0.0 || points.back().input != 1.0) return false;
    double previousInput = -1.0;
    for (const auto& point : points) {
        if (!std::isfinite(point.input) || !std::isfinite(point.output) ||
            point.input < 0.0 || point.input > 1.0 ||
            point.output < 0.0 || point.output > 1.0 ||
            point.input <= previousInput) {
            return false;
        }
        previousInput = point.input;
    }
    return true;
}

bool ColorCurve::isIdentity() const {
    if (points.size() != 2) return false;
    return points[0].input == 0.0 && points[0].output == 0.0 &&
           points[1].input == 1.0 && points[1].output == 1.0;
}

double ColorCurve::evaluate(double input) const {
    if (!valid() || !std::isfinite(input)) return input;
    const double x = std::max(0.0, std::min(1.0, input));
    for (std::size_t i = 1; i < points.size(); ++i) {
        const auto& right = points[i];
        if (x <= right.input) {
            const auto& left = points[i - 1];
            const double t = (x - left.input) / (right.input - left.input);
            return left.output + (right.output - left.output) * t;
        }
    }
    return points.back().output;
}

bool ColorGrade::valid() const {
    return master.valid() && red.valid() && green.valid() && blue.valid();
}

bool ColorGrade::isIdentity() const {
    return master.isIdentity() && red.isIdentity() &&
           green.isIdentity() && blue.isIdentity();
}

void ColorGrade::apply(double& r, double& g, double& b) const {
    if (!valid()) return;
    r = master.evaluate(r);
    g = master.evaluate(g);
    b = master.evaluate(b);
    r = red.evaluate(r);
    g = green.evaluate(g);
    b = blue.evaluate(b);
}

}  // namespace core
}  // namespace nairoclip
