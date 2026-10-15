#include "nairoclip/core/speed_curve.h"

#include <algorithm>
#include <cmath>
#include <string>

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

// =============================================================================
// 速度曲线预设（文档 Part 4-A 1.1「曲线预设」）
// =============================================================================

const char* speedCurvePresetName(SpeedCurvePreset preset) {
    switch (preset) {
        case SpeedCurvePreset::Custom: return "custom";
        case SpeedCurvePreset::Montage: return "montage";
        case SpeedCurvePreset::HeroMoment: return "hero_moment";
        case SpeedCurvePreset::BulletTime: return "bullet_time";
        case SpeedCurvePreset::Jump: return "jump";
        case SpeedCurvePreset::FlashIn: return "flash_in";
        case SpeedCurvePreset::FlashOut: return "flash_out";
    }
    return "custom";
}

const char* speedCurvePresetDisplayName(SpeedCurvePreset preset) {
    switch (preset) {
        case SpeedCurvePreset::Custom: return "自定义";
        case SpeedCurvePreset::Montage: return "蒙太奇";
        case SpeedCurvePreset::HeroMoment: return "英雄时刻";
        case SpeedCurvePreset::BulletTime: return "子弹时间";
        case SpeedCurvePreset::Jump: return "跳跃";
        case SpeedCurvePreset::FlashIn: return "闪进";
        case SpeedCurvePreset::FlashOut: return "闪出";
    }
    return "自定义";
}

bool speedCurvePresetFromString(const char* text, SpeedCurvePreset& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    for (int i = 0; i <= 6; ++i) {
        const auto preset = static_cast<SpeedCurvePreset>(i);
        if (s == speedCurvePresetName(preset)) {
            out = preset;
            return true;
        }
    }
    if (s == "heromoment" || s == "hero") { out = SpeedCurvePreset::HeroMoment; return true; }
    if (s == "bullettime" || s == "bullet") { out = SpeedCurvePreset::BulletTime; return true; }
    if (s == "flashin" || s == "in") { out = SpeedCurvePreset::FlashIn; return true; }
    if (s == "flashout" || s == "out") { out = SpeedCurvePreset::FlashOut; return true; }
    for (int i = 0; i <= 6; ++i) {
        const auto preset = static_cast<SpeedCurvePreset>(i);
        if (s == speedCurvePresetDisplayName(preset)) {
            out = preset;
            return true;
        }
    }
    return false;
}

bool speedCurvePresetFromInt(int value, SpeedCurvePreset& out) {
    if (value < 0 || value > 6) {
        return false;
    }
    out = static_cast<SpeedCurvePreset>(value);
    return true;
}

SpeedCurve buildSpeedCurvePreset(SpeedCurvePreset preset, const Time& sourceDuration) {
    SpeedCurve curve;
    if (!sourceDuration.isPositive() || preset == SpeedCurvePreset::Custom) {
        return curve;  // 空曲线 = 恒速（自定义由用户添加控制点）
    }
    const auto at = [&sourceDuration](double fraction) {
        return fromMicroseconds(static_cast<std::int64_t>(
            std::llround(sourceDuration.toDouble() * fraction * 1000000.0)));
    };
    const auto add = [&curve](const Time& time, double speed) {
        SpeedPoint point;
        point.sourceTime = time;
        point.speed = Rational::make(static_cast<std::int64_t>(std::llround(speed * 1000.0)),
                                     1000);
        point.easing = Easing::easeInOut();
        (void)curve.addPoint(point);
    };
    switch (preset) {
        case SpeedCurvePreset::Montage:
            // 蒙太奇：多次快慢交替（0.5x 慢 → 3x 快 → 0.5x → 3x → 1x）
            add(at(0.0), 1.0);
            add(at(0.15), 3.0);
            add(at(0.30), 0.5);
            add(at(0.50), 3.0);
            add(at(0.65), 0.5);
            add(at(0.85), 3.0);
            add(at(1.0), 1.0);
            break;
        case SpeedCurvePreset::HeroMoment:
            // 英雄时刻：前后正常 + 中段极慢（突出高光）
            add(at(0.0), 1.0);
            add(at(0.35), 1.0);
            add(at(0.50), 0.25);
            add(at(0.65), 1.0);
            add(at(1.0), 1.0);
            break;
        case SpeedCurvePreset::BulletTime:
            // 子弹时间：中段极致慢速（接近定格）
            add(at(0.0), 1.0);
            add(at(0.30), 1.0);
            add(at(0.45), 0.1);
            add(at(0.60), 1.0);
            add(at(1.0), 1.0);
            break;
        case SpeedCurvePreset::Jump:
            // 跳跃：周期性快跳（快-慢-快）
            add(at(0.0), 0.5);
            add(at(0.20), 4.0);
            add(at(0.40), 0.5);
            add(at(0.60), 4.0);
            add(at(0.80), 0.5);
            add(at(1.0), 1.0);
            break;
        case SpeedCurvePreset::FlashIn:
            // 闪进：片头极快，迅速进入正片
            add(at(0.0), 6.0);
            add(at(0.15), 1.0);
            add(at(1.0), 1.0);
            break;
        case SpeedCurvePreset::FlashOut:
            // 闪出：片尾极快收束
            add(at(0.0), 1.0);
            add(at(0.85), 1.0);
            add(at(1.0), 6.0);
            break;
        case SpeedCurvePreset::Custom:
            break;
    }
    return curve;
}

}  // namespace core
}  // namespace nairoclip
