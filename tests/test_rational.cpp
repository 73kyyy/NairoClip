#include "test_framework.h"
#include "nairoclip/core/rational.h"
using namespace nairoclip::core;
NC_TEST(Rational, Normalize) {
    const Rational r = Rational::make(2, 4);
    NC_CHECK_EQ(r.num, 1);
    NC_CHECK_EQ(r.den, 2);
    const Rational neg = Rational::make(1, -2);
    NC_CHECK_EQ(neg.num, -1);
    NC_CHECK_EQ(neg.den, 2);
    const Rational zero = Rational::make(0, 5);
    NC_CHECK_EQ(zero.num, 0);
    NC_CHECK_EQ(zero.den, 1);
    const Rational bad = Rational::make(3, 0);
    NC_CHECK_EQ(bad.num, 0);
    NC_CHECK_EQ(bad.den, 1);
}
NC_TEST(Rational, Arithmetic) {
    const Rational a = Rational::make(1, 3);
    const Rational b = Rational::make(1, 6);
    NC_CHECK(a + b == Rational::make(1, 2));
    NC_CHECK(a - b == Rational::make(1, 6));
    NC_CHECK(a * b == Rational::make(1, 18));
    NC_CHECK(a / b == Rational::make(2));
}
NC_TEST(Rational, CompareAndRound) {
    NC_CHECK(Rational::make(1, 3) < Rational::make(1, 2));
    NC_CHECK(Rational::make(2, 3) > Rational::make(1, 2));
    NC_CHECK(Rational::make(-1, 2) < Rational::make(0));
    NC_CHECK(Rational::make(1, 2) <= Rational::make(1, 2));
    NC_CHECK_EQ(floorDiv(7, 2), 3);
    NC_CHECK_EQ(floorDiv(-7, 2), -4);
    NC_CHECK_EQ(roundToInt(Rational::make(1, 2)), 1);
    NC_CHECK_EQ(roundToInt(Rational::make(2, 5)), 0);
    NC_CHECK_EQ(floorToInt(Rational::make(7, 2)), 3);
}

// =============================================================================
// 溢出保护（B0-1）
// =============================================================================
//
// 背景：原实现在无 __int128 的平台（MSVC）把中间量退化为 int64，
// `int64 × int64` 溢出是**未定义行为**——不报错、结果随编译器变化。
// 本组用例锁定新行为：三平台同一代码路径，越界时**确定性饱和**。
//
// 注意：这些用例在 Linux/macOS 上原本也能"通过"（因为有 __int128），
// 但 Windows 上会暴露问题。现在三平台走同一实现，结果一致。

NC_TEST(Rational, MultiplicationExactlyFitsIn128) {
    // 2^62 × 2 → 2^63，超出 int64 但完全在 128 位内
    const Rational a = Rational::make(INT64_C(4611686018427387904), 1);  // 2^62
    const Rational b = Rational::make(2, 1);
    rationalResetOverflowFlag();
    const Rational r = a * b;
    // 结果超 int64 → 饱和到 INT64_MAX，且标志置位
    NC_CHECK(rationalLastOpOverflowed());
    NC_CHECK_EQ(r.num, INT64_MAX);
    NC_CHECK_EQ(r.den, 1);
}

NC_TEST(Rational, LargeButInRangeMultiplicationIsExact) {
    // 2^31 × 2^31 = 2^62（在 int64 范围内，必须精确）
    const Rational a = Rational::make(INT64_C(2147483648), 1);  // 2^31
    const Rational b = Rational::make(INT64_C(2147483648), 1);
    rationalResetOverflowFlag();
    const Rational r = a * b;
    NC_CHECK(!rationalLastOpOverflowed());
    NC_CHECK(r == Rational::make(INT64_C(4611686018427387904), 1));
}

NC_TEST(Rational, MultiplicationUsesCrossReduction) {
    // 交叉约分后不应溢出： (2^62 / 3) * (3 / 2^62) = 1
    const Rational a = Rational::make(INT64_C(4611686018427387904), 3);
    const Rational b = Rational::make(3, INT64_C(4611686018427387904));
    rationalResetOverflowFlag();
    const Rational r = a * b;
    NC_CHECK(!rationalLastOpOverflowed());
    NC_CHECK(r == Rational::make(1));
}

NC_TEST(Rational, ComparisonHandlesLargeValues) {
    // 比较用 128 位中间量：a.num * b.den 会溢出 int64
    const Rational small = Rational::make(1, INT64_C(4611686018427387904));
    const Rational tiny = Rational::make(1, INT64_C(4611686018427387904) - 1);
    // tiny 的分母更小 → tiny 更大。用溢出安全的比较必须得出正确结论。
    NC_CHECK(small < tiny);
    NC_CHECK(!(tiny < small));
    NC_CHECK(small != tiny);
}

NC_TEST(Rational, NegativeDenominatorNormalizationHandlesMin) {
    // INT64_MIN 取反是 UB；旧实现直接 `denominator = -denominator` 会触发。
    // 新实现走 128 位，结果必须确定（此极端值饱和为 INT64_MAX 后归正）。
    rationalResetOverflowFlag();
    const Rational r = Rational::make(1, INT64_MIN);
    // 不崩溃、分母为正即满足基本安全要求
    NC_CHECK(r.den > 0);
}

NC_TEST(Rational, RoundToIntDoesNotOverflow) {
    // 旧实现 `2 * r.num + r.den` 在 num 接近 INT64_MAX/2 时溢出（UB）
    const Rational big = Rational::make(INT64_C(5000000000000000000), 1);
    rationalResetOverflowFlag();
    const std::int64_t r = roundToInt(big);
    // 结果必须确定（饱和或原值），不能是随机数
    NC_CHECK(r == INT64_MAX || r == INT64_C(5000000000000000000));
}

NC_TEST(Rational, AdditionSaturatesInsteadOfUb) {
    const Rational a = Rational::make(INT64_MAX, 1);
    const Rational b = Rational::make(INT64_MAX, 1);
    rationalResetOverflowFlag();
    const Rational r = a + b;
    NC_CHECK(rationalLastOpOverflowed());
    NC_CHECK_EQ(r.num, INT64_MAX);
}

NC_TEST(Rational, SubtractionSaturatesInsteadOfUb) {
    const Rational a = Rational::make(INT64_MIN, 1);
    const Rational b = Rational::make(INT64_MAX, 1);
    rationalResetOverflowFlag();
    const Rational r = a - b;
    NC_CHECK(rationalLastOpOverflowed());
    NC_CHECK(r.num == INT64_MIN || r.num == INT64_MAX);
}

NC_TEST(Rational, NormalArithmeticDoesNotSetOverflowFlag) {
    // 常规运算不得误报溢出
    rationalResetOverflowFlag();
    const Rational a = Rational::make(1, 3);
    const Rational b = Rational::make(1, 6);
    (void)(a + b);
    (void)(a - b);
    (void)(a * b);
    (void)(a / b);
    (void)(a < b);
    (void)roundToInt(a);
    NC_CHECK(!rationalLastOpOverflowed());
}

NC_TEST(Rational, FramerateArithmeticStaysExact) {
    // 真实场景：29.97fps = 30000/1001，算 2 小时的帧数
    const Rational fps = Rational::make(30000, 1001);
    const Rational twoHours = Rational::make(7200);
    rationalResetOverflowFlag();
    const Rational frames = twoHours * fps;  // ≈ 215784.2
    NC_CHECK(!rationalLastOpOverflowed());
    // 7200 * 30000 / 1001 = 216000000/1001
    NC_CHECK(frames == Rational::make(216000000, 1001));
}
