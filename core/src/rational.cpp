#include "nairoclip/core/rational.h"

#include <cstdint>
#include <limits>

namespace nairoclip {
namespace core {

namespace {

std::int64_t gcd64(std::int64_t a, std::int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) {
        const std::int64_t t = a % b;
        a = b;
        b = t;
    }
    return a == 0 ? 1 : a;
}

// =============================================================================
// 128 位中间运算（自实现，不依赖 __int128）
// =============================================================================
//
// 为什么不用 `#if defined(__SIZEOF_INT128__)`：
//   MSVC 没有 __int128。原实现回退到 `using Wide = std::int64_t`，于是
//   `int64 × int64` 溢出成为**未定义行为（UB）**——不报错、不崩溃，
//   结果随编译器和优化级别变化。本项目的 CI 覆盖 Windows，
//   这意味着 Windows 上的长时间线（帧号×时间基）会静默算错。
//
// 方案：始终使用自实现的 128 位二补码整数。
//   · 三平台同一套代码路径 → 同一结果（可测试、可复现）；
//   · 64×64 乘法在 128 位内**精确**，无信息损失；
//   · 仅最终 narrow 回 int64 时可能溢出 → 饱和处理并置标志，
//     绝不产生 UB，也绝不静默给垃圾值。

/// 128 位有符号整数（二补码：hi 为高 64 位，lo 为低 64 位）。
struct Wide {
    std::int64_t hi = 0;
    std::uint64_t lo = 0;

    bool isNegative() const { return hi < 0; }
};

Wide wideFrom(std::int64_t v) {
    Wide w;
    w.hi = (v < 0) ? -1 : 0;
    w.lo = static_cast<std::uint64_t>(v);
    return w;
}

/// 二补码取反（~a + 1），对 INT64_MIN 亦正确。
Wide wideNegate(const Wide& a) {
    Wide r;
    r.lo = ~a.lo + 1u;
    r.hi = static_cast<std::int64_t>(~static_cast<std::uint64_t>(a.hi) +
                                     ((r.lo == 0u) ? 1u : 0u));
    return r;
}

/// 有符号比较。
bool wideLess(const Wide& a, const Wide& b) {
    if (a.hi != b.hi) {
        return a.hi < b.hi;
    }
    return a.lo < b.lo;
}

/// 加法；`overflow` 报告是否超出 128 位可表示范围。
Wide wideAdd(const Wide& a, const Wide& b, bool& overflow) {
    Wide r;
    r.lo = a.lo + b.lo;
    const std::uint64_t carry = (r.lo < a.lo) ? 1u : 0u;
    // 高位用无符号相加再转回有符号，避免有符号溢出（UB）。
    const std::uint64_t us = static_cast<std::uint64_t>(a.hi) +
                             static_cast<std::uint64_t>(b.hi) + carry;
    r.hi = static_cast<std::int64_t>(us);
    // 同号相加得到异号 → 128 位溢出。
    overflow = (a.hi >= 0 && b.hi >= 0 && r.hi < 0) ||
               (a.hi < 0 && b.hi < 0 && r.hi >= 0);
    return r;
}

Wide wideSub(const Wide& a, const Wide& b, bool& overflow) {
    return wideAdd(a, wideNegate(b), overflow);
}

/// int64 × int64 → 128 位（精确，无溢出）。
Wide wideMul(std::int64_t a, std::int64_t b) {
    const bool negative = (a < 0) != (b < 0);
    // 取绝对值（用无符号减法，INT64_MIN 亦正确得到 2^63）。
    const std::uint64_t ua =
        (a < 0) ? (0u - static_cast<std::uint64_t>(a)) : static_cast<std::uint64_t>(a);
    const std::uint64_t ub =
        (b < 0) ? (0u - static_cast<std::uint64_t>(b)) : static_cast<std::uint64_t>(b);

    // 32 位分解做 64×64 → 128 无符号乘法。
    const std::uint64_t a0 = ua & 0xFFFFFFFFu;
    const std::uint64_t a1 = ua >> 32;
    const std::uint64_t b0 = ub & 0xFFFFFFFFu;
    const std::uint64_t b1 = ub >> 32;

    const std::uint64_t p00 = a0 * b0;
    const std::uint64_t p01 = a0 * b1;
    const std::uint64_t p10 = a1 * b0;
    const std::uint64_t p11 = a1 * b1;

    const std::uint64_t mid =
        (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);

    Wide r;
    r.lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    r.hi = static_cast<std::int64_t>(p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32));
    return negative ? wideNegate(r) : r;
}

/// narrow 回 int64；超出范围时饱和并置 `overflow`。
std::int64_t wideToInt64(const Wide& v, bool& overflow) {
    constexpr std::uint64_t kMaxI64 = static_cast<std::uint64_t>(INT64_MAX);
    constexpr std::uint64_t kMinI64Magnitude = static_cast<std::uint64_t>(INT64_MAX) + 1u;

    if (v.hi == 0 && v.lo <= kMaxI64) {
        overflow = false;
        return static_cast<std::int64_t>(v.lo);
    }
    if (v.hi == -1 && v.lo >= kMinI64Magnitude) {
        overflow = false;
        return static_cast<std::int64_t>(v.lo);
    }
    // 饱和（确定性行为，非 UB）
    overflow = true;
    return v.isNegative() ? INT64_MIN : INT64_MAX;
}

/// 记录最近一次是否发生溢出（供测试断言；不影响计算结果）。
thread_local bool g_lastOverflow = false;

/// 由 Wide 分子/分母构造（约分 + 分母归正 + 溢出饱和）。
Rational rationalFromWide(const Wide& numW, const Wide& denW) {
    // 注意：wideToInt64 会**写** overflow 标志，两次调用必须用独立变量，
    // 否则后一次会把前一次的溢出覆盖掉（此前实现即此缺陷）。
    bool ofNum = false;
    bool ofDen = false;
    const std::int64_t num = wideToInt64(numW, ofNum);
    const std::int64_t den = wideToInt64(denW, ofDen);
    if (ofNum || ofDen) {
        g_lastOverflow = true;
    }
    return Rational::make(num, den);
}

}  // namespace

// =============================================================================
// 构造与查询
// =============================================================================

Rational Rational::make(std::int64_t numerator, std::int64_t denominator) {
    Rational r;
    if (denominator == 0) {
        return r;  // 0/1
    }
    if (denominator < 0) {
        // 注意：INT64_MIN 取反是 UB，先转 Wide 再取反。
        bool of = false;
        const Wide negNum = wideNegate(wideFrom(numerator));
        const Wide negDen = wideNegate(wideFrom(denominator));
        numerator = wideToInt64(negNum, of);
        denominator = wideToInt64(negDen, of);
        if (of) {
            g_lastOverflow = true;
        }
        if (denominator <= 0) {  // 取反后仍不合法（极端溢出）→ 退化为 0
            return r;
        }
    }
    const std::int64_t g = gcd64(numerator, denominator);
    r.num = numerator / g;
    r.den = denominator / g;
    return r;
}

double Rational::toDouble() const {
    return static_cast<double>(num) / static_cast<double>(den);
}

std::string Rational::toString() const {
    if (den == 1) {
        return std::to_string(num);
    }
    return std::to_string(num) + "/" + std::to_string(den);
}

// =============================================================================
// 算术
// =============================================================================

Rational operator+(const Rational& a, const Rational& b) {
    const std::int64_t g = gcd64(a.den, b.den);
    // lcm 可能溢出 int64，用 Wide 计算后再判定。
    bool of = false;
    const Wide lcmW = wideMul(a.den / g, b.den);
    const std::int64_t lcm = wideToInt64(lcmW, of);
    if (of || lcm <= 0) {
        g_lastOverflow = true;
        return Rational::make(INT64_MAX, 1);  // 饱和
    }
    const Wide an = wideMul(a.num, lcm / a.den);
    const Wide bn = wideMul(b.num, lcm / b.den);
    bool ofSum = false;
    const Wide sum = wideAdd(an, bn, ofSum);
    if (ofSum) {
        g_lastOverflow = true;
        return Rational::make(INT64_MAX, 1);
    }
    return rationalFromWide(sum, wideFrom(lcm));
}

Rational operator-(const Rational& a, const Rational& b) {
    const std::int64_t g = gcd64(a.den, b.den);
    bool of = false;
    const Wide lcmW = wideMul(a.den / g, b.den);
    const std::int64_t lcm = wideToInt64(lcmW, of);
    if (of || lcm <= 0) {
        g_lastOverflow = true;
        return Rational::make(INT64_MAX, 1);
    }
    const Wide an = wideMul(a.num, lcm / a.den);
    const Wide bn = wideMul(b.num, lcm / b.den);
    bool ofDiff = false;
    const Wide diff = wideSub(an, bn, ofDiff);
    if (ofDiff) {
        g_lastOverflow = true;
        return Rational::make(INT64_MAX, 1);
    }
    return rationalFromWide(diff, wideFrom(lcm));
}

Rational operator*(const Rational& a, const Rational& b) {
    // 交叉约分，缩小中间值。
    const std::int64_t g1 = gcd64(a.num, b.den);
    const std::int64_t g2 = gcd64(b.num, a.den);
    const Wide n = wideMul(a.num / g1, b.num / g2);
    const Wide d = wideMul(a.den / g2, b.den / g1);
    if (d.lo == 0u && d.hi == 0) {
        return Rational{};  // 分母为 0（不应发生，防御）
    }
    return rationalFromWide(n, d);
}

Rational operator/(const Rational& a, const Rational& b) {
    if (b.num == 0) {
        return Rational{};  // 除零保护：返回 0
    }
    return a * Rational::make(b.den, b.num);
}

// =============================================================================
// 比较
// =============================================================================

bool operator==(const Rational& a, const Rational& b) {
    const Wide l = wideMul(a.num, b.den);
    const Wide r = wideMul(b.num, a.den);
    return l.hi == r.hi && l.lo == r.lo;
}

bool operator!=(const Rational& a, const Rational& b) {
    return !(a == b);
}

bool operator<(const Rational& a, const Rational& b) {
    const Wide l = wideMul(a.num, b.den);
    const Wide r = wideMul(b.num, a.den);
    return wideLess(l, r);
}

bool operator<=(const Rational& a, const Rational& b) {
    return !(b < a);
}

bool operator>(const Rational& a, const Rational& b) {
    return b < a;
}

bool operator>=(const Rational& a, const Rational& b) {
    return !(a < b);
}

// =============================================================================
// 取整
// =============================================================================

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    if (b <= 0) {
        return 0;
    }
    const std::int64_t q = a / b;
    const std::int64_t r = a % b;
    if (r != 0 && a < 0) {
        return q - 1;
    }
    return q;
}

std::int64_t floorToInt(const Rational& r) {
    return floorDiv(r.num, r.den);
}

std::int64_t roundToInt(const Rational& r) {
    // 四舍五入（0.5 向上）：floor(x + 1/2) = floor((2·num + den) / (2·den))
    // 原实现直接写 `2 * r.num + r.den`，num 接近 INT64_MAX/2 时溢出（UB）。
    // 改用 Wide 计算。
    bool of = false;
    const Wide num2 = wideMul(r.num, 2);
    bool ofSum = false;
    const Wide numPlus = wideAdd(num2, wideFrom(r.den), ofSum);
    const Wide den2 = wideMul(r.den, 2);
    if (of || ofSum) {
        g_lastOverflow = true;
        return r.isNegative() ? INT64_MIN : INT64_MAX;
    }
    bool ofN = false, ofD = false;
    const std::int64_t n = wideToInt64(numPlus, ofN);
    const std::int64_t d = wideToInt64(den2, ofD);
    if (ofN || ofD) {
        g_lastOverflow = true;
        return r.isNegative() ? INT64_MIN : INT64_MAX;
    }
    return floorDiv(n, d);
}

bool rationalLastOpOverflowed() {
    return g_lastOverflow;
}

void rationalResetOverflowFlag() {
    g_lastOverflow = false;
}

}  // namespace core
}  // namespace nairoclip
