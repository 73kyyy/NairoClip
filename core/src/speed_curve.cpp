#include "nairoclip/core/speed_curve.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

/// 数值积分步数（固定步长，保证跨平台确定性）。
constexpr int kIntegrationSteps = 64;

}  // namespace

Rational SpeedCurve::minSpeed() { return Rational::make(1, 10); }
Rational SpeedCurve::maxSpeed() { return Rational::make(10); }

Result SpeedCurve::addPoint(SpeedPoint point) {
    if (point.sourceTime.isNegative()) {
        return Result::fail(ErrorCode::InvalidArgument, "控制点时间不能为负");
    }
    if (!(point.speed >= minSpeed()) || !(point.speed <= maxSpeed())) {
        return Result::fail(ErrorCode::InvalidArgument, "速度需在 0.1x 到 10x 之间");
    }
    for (auto& p : points_) {
        if (p.sourceTime == point.sourceTime) {
            p = std::move(point);
            return Result::ok();
        }
    }
    const auto it = std::lower_bound(
        points_.begin(), points_.end(), point.sourceTime,
        [](const SpeedPoint& p, const Time& t) { return p.sourceTime < t; });
    points_.insert(it, std::move(point));
    return Result::ok();
}

bool SpeedCurve::removePoint(const Time& sourceTime) {
    const auto it = std::find_if(
        points_.begin(), points_.end(),
        [&sourceTime](const SpeedPoint& p) { return p.sourceTime == sourceTime; });
    if (it == points_.end()) {
        return false;
    }
    points_.erase(it);
    return true;
}

void SpeedCurve::clear() { points_.clear(); }

SpeedCurve SpeedCurve::constant(const Rational& speed) {
    SpeedCurve curve;
    SpeedPoint point;
    point.sourceTime = Rational::make(0);
    point.speed = speed;
    curve.points_.push_back(point);
    return curve;
}

double SpeedCurve::speedAt(const Time& sourceTime, const Rational& fallback) const {
    if (points_.empty()) {
        return fallback.toDouble();
    }
    if (sourceTime <= points_.front().sourceTime) {
        return points_.front().speed.toDouble();
    }
    if (sourceTime >= points_.back().sourceTime) {
        return points_.back().speed.toDouble();
    }
    std::size_t i = 0;
    for (; i + 1 < points_.size(); ++i) {
        if (points_[i + 1].sourceTime > sourceTime) {
            break;
        }
    }
    const SpeedPoint& a = points_[i];
    const SpeedPoint& b = points_[i + 1];
    const double span = (b.sourceTime - a.sourceTime).toDouble();
    if (span <= 0.0) {
        return b.speed.toDouble();
    }
    const double u = (sourceTime - a.sourceTime).toDouble() / span;
    const double eased = a.easing.map(u);
    const double sa = a.speed.toDouble();
    const double sb = b.speed.toDouble();
    return sa + (sb - sa) * eased;
}

Time SpeedCurve::timelineDuration(const Time& sourceIn, const Time& sourceOut,
                                  const Rational& fallback) const {
    const Time range = sourceOut - sourceIn;
    if (!range.isPositive()) {
        return Time{};
    }
    if (points_.empty()) {
        return range / fallback;
    }
    // 数值积分：dt_timeline = ds / speed(s)，Simpson 复合公式。
    const double lo = sourceIn.toDouble();
    const double hi = sourceOut.toDouble();
    const int steps = kIntegrationSteps % 2 == 0 ? kIntegrationSteps : kIntegrationSteps + 1;
    const double h = (hi - lo) / steps;
    double total = 0.0;
    for (int i = 0; i <= steps; ++i) {
        const double s = lo + h * i;
        const Time sampleTime = fromMicroseconds(static_cast<std::int64_t>(std::llround(s * 1000000.0)));
        const double speed = speedAt(sampleTime, fallback);
        const double invV = speed > 1e-9 ? 1.0 / speed : 1e9;
        const double weight = (i == 0 || i == steps) ? 1.0 : (i % 2 == 0 ? 2.0 : 4.0);
        total += weight * invV;
    }
    total *= h / 3.0;
    // 量化到微秒（保证确定性）
    const std::int64_t micros = static_cast<std::int64_t>(std::llround(total * 1000000.0));
    return Rational::make(micros, 1000000);
}

bool SpeedCurve::invariantsHold() const {
    for (std::size_t i = 0; i < points_.size(); ++i) {
        if (points_[i].sourceTime.isNegative()) {
            return false;
        }
        if (!(points_[i].speed >= minSpeed()) || !(points_[i].speed <= maxSpeed())) {
            return false;
        }
        if (i > 0 && !(points_[i - 1].sourceTime < points_[i].sourceTime)) {
            return false;
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
