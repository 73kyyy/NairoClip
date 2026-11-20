#include "nairoclip/core/color_grade.h"

#include <algorithm>

namespace nairoclip {
namespace core {

namespace {

double evaluateValidCurve(const ColorCurve& curve, double input) {
    const double x = std::max(0.0, std::min(1.0, input));
    for (std::size_t i = 1; i < curve.points.size(); ++i) {
        const auto& right = curve.points[i];
        if (x <= right.input) {
            const auto& left = curve.points[i - 1];
            const double t = (x - left.input) / (right.input - left.input);
            return left.output + (right.output - left.output) * t;
        }
    }
    return curve.points.back().output;
}

void applyValidatedGrade(const ColorGrade& grade, double& r, double& g, double& b) {
    r = evaluateValidCurve(grade.master, r);
    g = evaluateValidCurve(grade.master, g);
    b = evaluateValidCurve(grade.master, b);
    r = evaluateValidCurve(grade.red, r);
    g = evaluateValidCurve(grade.green, g);
    b = evaluateValidCurve(grade.blue, b);
}

}  // namespace

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
    return evaluateValidCurve(*this, input);
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
    applyValidatedGrade(*this, r, g, b);
}

void applyColorGradeToRgba(std::uint8_t* pixels, std::size_t pixelCount,
                           const ColorGrade& grade) {
    if (pixels == nullptr || pixelCount == 0 || grade.isIdentity() || !grade.valid()) return;
    for (std::size_t i = 0; i < pixelCount; ++i) {
        double r = static_cast<double>(pixels[i * 4]) / 255.0;
        double g = static_cast<double>(pixels[i * 4 + 1]) / 255.0;
        double b = static_cast<double>(pixels[i * 4 + 2]) / 255.0;
        applyValidatedGrade(grade, r, g, b);
        pixels[i * 4] = static_cast<std::uint8_t>(std::lround(r * 255.0));
        pixels[i * 4 + 1] = static_cast<std::uint8_t>(std::lround(g * 255.0));
        pixels[i * 4 + 2] = static_cast<std::uint8_t>(std::lround(b * 255.0));
    }
}

}  // namespace core
}  // namespace nairoclip
