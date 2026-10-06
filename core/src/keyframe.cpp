#include "nairoclip/core/keyframe.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

constexpr int kSolveIterations = 8;
constexpr int kBisectIterations = 32;
constexpr double kEpsilon = 1e-7;

/// 三次贝塞尔（P0=0, P3=1）单分量：3p1·t(1-t)^2 + 3p2·t^2(1-t) + t^3。
double bezierComponent(double p1, double p2, double t) {
    const double oneMinusT = 1.0 - t;
    return 3.0 * p1 * t * oneMinusT * oneMinusT + 3.0 * p2 * t * t * oneMinusT +
           t * t * t;
}

double bezierSlope(double p1, double p2, double t) {
    const double oneMinusT = 1.0 - t;
    return 3.0 * (p1 * (oneMinusT * oneMinusT - 2.0 * t * oneMinusT) +
                  p2 * (2.0 * t * oneMinusT - t * t) + t * t);
}

/// 求解 bezierX(t) = x 的 t（先牛顿迭代，失败退化为二分）。x 已在 [0,1]。
double solveForT(double c1x, double c2x, double x) {
    double t = x;
    for (int i = 0; i < kSolveIterations; ++i) {
        const double error = bezierComponent(c1x, c2x, t) - x;
        if (std::fabs(error) < kEpsilon) {
            return t;
        }
        const double slope = bezierSlope(c1x, c2x, t);
        if (std::fabs(slope) < kEpsilon) {
            break;
        }
        t -= error / slope;
    }
    double lo = 0.0;
    double hi = 1.0;
    t = x;
    for (int i = 0; i < kBisectIterations; ++i) {
        const double value = bezierComponent(c1x, c2x, t);
        if (std::fabs(value - x) < kEpsilon) {
            break;
        }
        if (value < x) {
            lo = t;
        } else {
            hi = t;
        }
        t = (lo + hi) * 0.5;
    }
    return t;
}

double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

}  // namespace

// ---------------- Easing ----------------

Easing Easing::linear() { return Easing{}; }

Easing Easing::easeIn() {
    Easing e;
    e.type = EasingType::EaseIn;
    e.c1x = 0.42;
    e.c1y = 0.0;
    e.c2x = 1.0;
    e.c2y = 1.0;
    return e;
}

Easing Easing::easeOut() {
    Easing e;
    e.type = EasingType::EaseOut;
    e.c1x = 0.0;
    e.c1y = 0.0;
    e.c2x = 0.58;
    e.c2y = 1.0;
    return e;
}

Easing Easing::easeInOut() {
    Easing e;
    e.type = EasingType::EaseInOut;
    e.c1x = 0.42;
    e.c1y = 0.0;
    e.c2x = 0.58;
    e.c2y = 1.0;
    return e;
}

Easing Easing::stepped() {
    Easing e;
    e.type = EasingType::Stepped;
    return e;
}

Easing Easing::bezier(double c1x, double c1y, double c2x, double c2y) {
    Easing e;
    e.type = EasingType::Bezier;
    e.c1x = clamp01(c1x);
    e.c2x = clamp01(c2x);
    e.c1y = c1y;
    e.c2y = c2y;
    return e;
}

double Easing::map(double u) const {
    if (u <= 0.0) return 0.0;
    if (u >= 1.0) return 1.0;
    switch (type) {
        case EasingType::Linear:
            return u;
        case EasingType::Stepped:
            return 0.0;
        case EasingType::EaseIn:
        case EasingType::EaseOut:
        case EasingType::EaseInOut:
        case EasingType::Bezier: {
            const double t = solveForT(c1x, c2x, clamp01(u));
            return bezierComponent(c1y, c2y, t);
        }
    }
    return u;
}

// ---------------- KeyframeTrack ----------------

Result KeyframeTrack::insert(Keyframe keyframe) {
    if (keyframe.time.isNegative()) {
        return Result::fail(ErrorCode::InvalidArgument, "关键帧时刻不能为负");
    }
    for (auto& k : keyframes_) {
        if (k.time == keyframe.time) {
            k = std::move(keyframe);  // 同时刻替换
            return Result::ok();
        }
    }
    const auto it = std::lower_bound(
        keyframes_.begin(), keyframes_.end(), keyframe.time,
        [](const Keyframe& k, const Time& t) { return k.time < t; });
    keyframes_.insert(it, std::move(keyframe));
    return Result::ok();
}

bool KeyframeTrack::remove(const Time& at) {
    const auto it = std::find_if(
        keyframes_.begin(), keyframes_.end(),
        [&at](const Keyframe& k) { return k.time == at; });
    if (it == keyframes_.end()) {
        return false;
    }
    keyframes_.erase(it);
    return true;
}

void KeyframeTrack::clear() { keyframes_.clear(); }

double KeyframeTrack::sample(const Time& t, double fallback) const {
    if (keyframes_.empty()) {
        return fallback;
    }
    if (t <= keyframes_.front().time) {
        return keyframes_.front().value;
    }
    if (t >= keyframes_.back().time) {
        return keyframes_.back().value;
    }
    // 找到包含 t 的区间 [a, b)
    std::size_t i = 0;
    for (; i + 1 < keyframes_.size(); ++i) {
        if (keyframes_[i + 1].time > t) {
            break;
        }
    }
    const Keyframe& a = keyframes_[i];
    const Keyframe& b = keyframes_[i + 1];
    const double span = (b.time - a.time).toDouble();
    if (span <= 0.0) {
        return b.value;
    }
    const double u = (t - a.time).toDouble() / span;
    const double eased = a.easing.map(u);
    return a.value + (b.value - a.value) * eased;
}

bool KeyframeTrack::invariantsHold() const {
    for (std::size_t i = 0; i < keyframes_.size(); ++i) {
        if (keyframes_[i].time.isNegative()) {
            return false;
        }
        if (i > 0 && !(keyframes_[i - 1].time < keyframes_[i].time)) {
            return false;
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
