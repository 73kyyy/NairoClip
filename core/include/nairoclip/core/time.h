#pragma once

#include "nairoclip/core/rational.h"

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {

/// 时间：以「秒」为单位的精确有理数。
using Time = Rational;

/// 帧率，例如 30000/1001（≈29.97）、25/1、24/1、60/1。
struct FrameRate {
    Rational value = Rational::make(30);

    static FrameRate of(std::int64_t fps);
    static FrameRate of(std::int64_t numerator, std::int64_t denominator);

    double toDouble() const { return value.toDouble(); }
    bool isValid() const { return value.isPositive(); }
    bool operator==(const FrameRate& o) const { return value == o.value; }
    bool operator!=(const FrameRate& o) const { return !(*this == o); }
};

FrameRate fps24();
FrameRate fps25();
FrameRate fps30();
FrameRate fps2997();   // 30000/1001
FrameRate fps60();

/// 第 frame 帧的起始时刻 = frame / fps。
Time timeAtFrame(const FrameRate& fps, std::int64_t frame);

/// t 时刻所处帧号（向下取整） = floor(t * fps)。
std::int64_t frameAtTime(const FrameRate& fps, const Time& t);

Time fromSeconds(std::int64_t seconds);
Time fromMilliseconds(std::int64_t milliseconds);
std::int64_t toMicroseconds(const Time& t);
Time fromMicroseconds(std::int64_t micros);

/// 非丢帧时码 "HH:MM:SS:FF"。
std::string formatTimecode(const FrameRate& fps, const Time& t);

/// "MM:SS.mmm"（>= 1 小时时为 "HH:MM:SS.mmm"）。
std::string formatClock(const Time& t);

}  // namespace core
}  // namespace nairoclip
