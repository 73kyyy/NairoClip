#include "nairoclip/core/rational.h"

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

#if defined(__SIZEOF_INT128__)
using Wide = __int128;
#else
using Wide = std::int64_t;
#endif

std::int64_t narrow(Wide v) {
    return static_cast<std::int64_t>(v);
}

}  // namespace

Rational Rational::make(std::int64_t numerator, std::int64_t denominator) {
    Rational r;
    if (denominator == 0) {
        return r;  // 0/1
    }
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
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

Rational operator+(const Rational& a, const Rational& b) {
    const std::int64_t g = gcd64(a.den, b.den);
    const std::int64_t lcm = a.den / g * b.den;
    const Wide an = static_cast<Wide>(a.num) * (lcm / a.den);
    const Wide bn = static_cast<Wide>(b.num) * (lcm / b.den);
    return Rational::make(narrow(an + bn), lcm);
}

Rational operator-(const Rational& a, const Rational& b) {
    const std::int64_t g = gcd64(a.den, b.den);
    const std::int64_t lcm = a.den / g * b.den;
    const Wide an = static_cast<Wide>(a.num) * (lcm / a.den);
    const Wide bn = static_cast<Wide>(b.num) * (lcm / b.den);
    return Rational::make(narrow(an - bn), lcm);
}

Rational operator*(const Rational& a, const Rational& b) {
    // 交叉约分，缩小中间值
    const std::int64_t g1 = gcd64(a.num, b.den);
    const std::int64_t g2 = gcd64(b.num, a.den);
    const Wide n = static_cast<Wide>(a.num / g1) * (b.num / g2);
    const Wide d = static_cast<Wide>(a.den / g2) * (b.den / g1);
    return Rational::make(narrow(n), narrow(d));
}

Rational operator/(const Rational& a, const Rational& b) {
    if (b.num == 0) {
        return Rational{};  // 除零保护：返回 0
    }
    return a * Rational::make(b.den, b.num);
}

bool operator==(const Rational& a, const Rational& b) {
    return static_cast<Wide>(a.num) * b.den == static_cast<Wide>(b.num) * a.den;
}

bool operator!=(const Rational& a, const Rational& b) {
    return !(a == b);
}

bool operator<(const Rational& a, const Rational& b) {
    return static_cast<Wide>(a.num) * b.den < static_cast<Wide>(b.num) * a.den;
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
    // 四舍五入（0.5 向上）：floor(x + 1/2)
    return floorDiv(2 * r.num + r.den, 2 * r.den);
}

}  // namespace core
}  // namespace nairoclip