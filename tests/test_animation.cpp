#include "test_framework.h"
#include "nairoclip/core/animation.h"
using namespace nairoclip::core;
namespace {
bool nearly(double a, double b, double eps = 1e-6) {
    const double d = a - b;
    return (d < 0 ? -d : d) < eps;
}
Keyframe key(double t, double value) {
    Keyframe k;
    k.time = Rational::make(static_cast<std::int64_t>(t * 1000), 1000);
    k.value = value;
    return k;
}
}  // namespace
NC_TEST(Animation, DefaultValuesWhenNoCurve) {
    const ClipAnimation anim;
    NC_CHECK(nearly(anim.sample(ClipProperty::Opacity, Rational::make(1)), 1.0));
    NC_CHECK(nearly(anim.sample(ClipProperty::Scale, Rational::make(1)), 1.0));
    NC_CHECK(nearly(anim.sample(ClipProperty::PositionX, Rational::make(1)), 0.0));
    NC_CHECK(nearly(anim.sample(ClipProperty::Rotation, Rational::make(1)), 0.0));
    NC_CHECK(nearly(anim.sample(ClipProperty::Volume, Rational::make(1)), 1.0));
}
NC_TEST(Animation, PropertySpecAndNames) {
    const PropertySpec opacity = propertySpec(ClipProperty::Opacity);
    NC_CHECK(nearly(opacity.minValue, 0.0));
    NC_CHECK(nearly(opacity.maxValue, 1.0));
    const PropertySpec scale = propertySpec(ClipProperty::Scale);
    NC_CHECK(nearly(scale.minValue, 0.01));
    NC_CHECK(nearly(scale.maxValue, 100.0));
    NC_CHECK_EQ(std::string(propertyName(ClipProperty::Opacity)), std::string("透明度"));
    NC_CHECK_EQ(std::string(propertyName(ClipProperty::Volume)), std::string("音量"));
}
NC_TEST(Animation, CurveInsertAndSample) {
    ClipAnimation anim;
    NC_CHECK(anim.empty());
    NC_CHECK(anim.curve(ClipProperty::Opacity).insert(key(0, 0.0)).isOk());
    NC_CHECK(anim.curve(ClipProperty::Opacity).insert(key(2, 1.0)).isOk());
    NC_CHECK(anim.hasCurve(ClipProperty::Opacity));
    NC_CHECK_EQ(anim.curveCount(), std::size_t(1));
    NC_CHECK(nearly(anim.sample(ClipProperty::Opacity, Rational::make(1)), 0.5));
}
NC_TEST(Animation, RemovesAndClearsCurves) {
    ClipAnimation anim;
    NC_CHECK(anim.curve(ClipProperty::Scale).insert(key(0, 2.0)).isOk());
    NC_CHECK(anim.curve(ClipProperty::Volume).insert(key(0, 0.5)).isOk());
    NC_CHECK_EQ(anim.curveCount(), std::size_t(2));
    NC_CHECK(anim.removeCurve(ClipProperty::Scale));
    NC_CHECK(!anim.removeCurve(ClipProperty::Scale));
    NC_CHECK_EQ(anim.curveCount(), std::size_t(1));
    anim.clear();
    NC_CHECK(anim.empty());
}
NC_TEST(Animation, SampleClampsToSpec) {
    ClipAnimation anim;
    // 透明度关键帧值超出 [0,1]（直接写入底层轨道后采样应被钳制）
    NC_CHECK(anim.curve(ClipProperty::Opacity).insert(key(0, -0.5)).isOk());
    NC_CHECK(anim.curve(ClipProperty::Opacity).insert(key(2, 1.5)).isOk());
    NC_CHECK(nearly(anim.sample(ClipProperty::Opacity, Rational::make(0)), 0.0));
    NC_CHECK(nearly(anim.sample(ClipProperty::Opacity, Rational::make(2)), 1.0));
}
NC_TEST(Animation, InvariantsDetectOutOfRange) {
    ClipAnimation anim;
    NC_CHECK(anim.curve(ClipProperty::Volume).insert(key(0, 1.0)).isOk());
    NC_CHECK(anim.invariantsHold());
    // 直接插入越界值（>4）后应不满足不变量
    NC_CHECK(anim.curve(ClipProperty::Volume).insert(key(1, 9.0)).isOk());
    NC_CHECK(!anim.invariantsHold());
}
NC_TEST(Animation, LocalTimeSampling) {
    ClipAnimation anim;
    // PositionX 合法范围 [-10,10]：用 0→10 验证插值与环境钳制
    NC_CHECK(anim.curve(ClipProperty::PositionX).insert(key(1, 0.0)).isOk());
    NC_CHECK(anim.curve(ClipProperty::PositionX).insert(key(3, 10.0)).isOk());
    // 本地时间 2s → 中间值 5.0
    NC_CHECK(nearly(anim.sample(ClipProperty::PositionX, Rational::make(2)), 5.0));
    // 本地时间 0.5s（在首帧之前）→ 钳制到首值
    NC_CHECK(nearly(anim.sample(ClipProperty::PositionX, Rational::make(1, 2)), 0.0));
}
NC_TEST(Animation, EasedCurveSample) {
    ClipAnimation anim;
    Keyframe a = key(0, 0.0);
    a.easing = Easing::easeIn();
    NC_CHECK(anim.curve(ClipProperty::Opacity).insert(a).isOk());
    NC_CHECK(anim.curve(ClipProperty::Opacity).insert(key(1, 1.0)).isOk());
    const double mid = anim.sample(ClipProperty::Opacity, Rational::make(1, 2));
    NC_CHECK(mid > 0.0 && mid < 0.5);
}
