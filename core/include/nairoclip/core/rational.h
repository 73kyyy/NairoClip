#pragma once

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {

/// 精确有理数（分子/分母，单位通常为「秒」或「倍率」）。
/// 不变量：den > 0；始终约分到最简；0 表示为 0/1。
/// 用途：所有时间量与帧率均以有理数精确表示，杜绝浮点漂移。
struct Rational {
    std::int64_t num = 0;
    std::int64_t den = 1;

    Rational() = default;

    /// 构造并规范化（约分、分母归正）。den 为 0 时返回 0/1。
    static Rational make(std::int64_t numerator, std::int64_t denominator = 1);

    double toDouble() const;
    bool isZero() const { return num == 0; }
    bool isNegative() const { return num < 0; }
    bool isPositive() const { return num > 0; }

    std::string toString() const;
};

Rational operator+(const Rational& a, const Rational& b);
Rational operator-(const Rational& a, const Rational& b);
Rational operator*(const Rational& a, const Rational& b);
Rational operator/(const Rational& a, const Rational& b);

bool operator==(const Rational& a, const Rational& b);
bool operator!=(const Rational& a, const Rational& b);
bool operator<(const Rational& a, const Rational& b);
bool operator<=(const Rational& a, const Rational& b);
bool operator>(const Rational& a, const Rational& b);
bool operator>=(const Rational& a, const Rational& b);

/// 向负无穷取整的整数除法（b > 0；b <= 0 时返回 0）。
std::int64_t floorDiv(std::int64_t a, std::int64_t b);

/// 向下取整 / 四舍五入（0.5 向上）为 int64。
std::int64_t floorToInt(const Rational& r);
std::int64_t roundToInt(const Rational& r);

}  // namespace core
}  // namespace nairoclip
