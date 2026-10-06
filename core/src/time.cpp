#include "nairoclip/core/time.h"

#include <cstdio>

namespace nairoclip {
namespace core {

FrameRate FrameRate::of(std::int64_t fps) {
    FrameRate fr;
    fr.value = Rational::make(fps);
    return fr;
}

FrameRate FrameRate::of(std::int64_t numerator, std::int64_t denominator) {
    FrameRate fr;
    fr.value = Rational::make(numerator, denominator);
    return fr;
}

FrameRate fps24() { return FrameRate::of(24); }
FrameRate fps25() { return FrameRate::of(25); }
FrameRate fps30() { return FrameRate::of(30); }
FrameRate fps2997() { return FrameRate::of(30000, 1001); }
FrameRate fps60() { return FrameRate::of(60); }

Time timeAtFrame(const FrameRate& fps, std::int64_t frame) {
    if (!fps.isValid()) {
        return Time{};
    }
    return Rational::make(frame) / fps.value;
}

std::int64_t frameAtTime(const FrameRate& fps, const Time& t) {
    if (!fps.isValid()) {
        return 0;
    }
    return floorToInt(t * fps.value);
}

Time fromSeconds(std::int64_t seconds) {
    return Rational::make(seconds);
}

Time fromMilliseconds(std::int64_t milliseconds) {
    return Rational::make(milliseconds, 1000);
}

std::int64_t toMicroseconds(const Time& t) {
    return roundToInt(t * Rational::make(1000000));
}

Time fromMicroseconds(std::int64_t micros) {
    return Rational::make(micros, 1000000);
}

std::string formatTimecode(const FrameRate& fps, const Time& t) {
    if (!fps.isValid()) {
        return "--:--:--:--";
    }
    const std::int64_t total = frameAtTime(fps, t);
    std::int64_t remaining = total < 0 ? 0 : total;
    const std::int64_t nominal = static_cast<std::int64_t>(fps.toDouble() + 0.5);
    const std::int64_t safeNominal = nominal > 0 ? nominal : 1;
    const std::int64_t ff = remaining % safeNominal;
    remaining /= safeNominal;
    const std::int64_t ss = remaining % 60;
    remaining /= 60;
    const std::int64_t mm = remaining % 60;
    remaining /= 60;
    const std::int64_t hh = remaining;

    char buf[128];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld:%02lld",
                  static_cast<long long>(hh), static_cast<long long>(mm),
                  static_cast<long long>(ss), static_cast<long long>(ff));
    return std::string(buf);
}

std::string formatClock(const Time& t) {
    std::int64_t totalMs = roundToInt(t * Rational::make(1000));
    if (totalMs < 0) {
        totalMs = 0;
    }
    const std::int64_t ms = totalMs % 1000;
    std::int64_t remaining = totalMs / 1000;
    const std::int64_t ss = remaining % 60;
    remaining /= 60;
    const std::int64_t mm = remaining % 60;
    remaining /= 60;
    const std::int64_t hh = remaining;

    char buf[128];
    if (hh > 0) {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld.%03lld",
                      static_cast<long long>(hh), static_cast<long long>(mm),
                      static_cast<long long>(ss), static_cast<long long>(ms));
    } else {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld.%03lld",
                      static_cast<long long>(mm), static_cast<long long>(ss),
                      static_cast<long long>(ms));
    }
    return std::string(buf);
}

}  // namespace core
}  // namespace nairoclip