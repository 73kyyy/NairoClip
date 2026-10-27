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

// -----------------------------------------------------------------------------
// 溢出检测（诊断用）
// -----------------------------------------------------------------------------
//
// 背景：中间运算使用自实现的 128 位整数（见 rational.cpp），
// 64×64 乘法在三平台均精确。仅当最终结果超出 int64 可表示范围时，
// 才发生**饱和**（返回 INT64_MAX / INT64_MIN）并置溢出标志。
//
// 为什么不直接返回错误：本类型被 40+ 模块用于时间计算，
// 改签名会波及全库。因此采用「饱和 + 可查询标志」的兼容方案：
// 精确范围内的结果保证正确；越界时行为确定（非 UB），且可被测试断言。
//
// 调用方（如导出校验）可据此拒绝执行，而不是产出错误成片。

/// 自上次 reset 以来是否发生过溢出（线程局部）。
bool rationalLastOpOverflowed();
/// 清除溢出标志。
void rationalResetOverflowFlag();

}  // namespace core
}  // namespace nairoclip
