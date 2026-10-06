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
#include "nairoclip/core/editor.h"
NC_TEST(Animation, EditorKeyframeWithUndo) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.type = MediaType::Video;
    a.duration = Rational::make(60);
    project.assets.push_back(a);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip clip;
    clip.asset = a.id;
    clip.start = Rational::make(0);
    clip.duration = Rational::make(5);
    ClipId clipId;
    NC_CHECK(editor.insertClip(seqId, trackId, clip, &clipId).isOk());
    Keyframe k;
    k.time = Rational::make(0);
    k.value = 0.0;
    NC_CHECK(editor.insertKeyframe(seqId, trackId, clipId, ClipProperty::Opacity, k).isOk());
    Keyframe k2;
    k2.time = Rational::make(2);
    k2.value = 1.0;
    NC_CHECK(editor.insertKeyframe(seqId, trackId, clipId, ClipProperty::Opacity, k2).isOk());
    // 验证采样
    const Clip* c = findClip(project, seqId, trackId, clipId);
    NC_CHECK(c != nullptr);
    NC_CHECK(c->animation.hasCurve(ClipProperty::Opacity));
    NC_CHECK(nearly(c->animation.sample(ClipProperty::Opacity, Rational::make(1)), 0.5));
    // 撤销
    NC_CHECK(editor.undo().isOk());
    c = findClip(project, seqId, trackId, clipId);
    const KeyframeTrack* curve = c->animation.findCurve(ClipProperty::Opacity);
    NC_CHECK(curve != nullptr);
    NC_CHECK_EQ(curve->size(), std::size_t(1));
    // 重做
    NC_CHECK(editor.redo().isOk());
    c = findClip(project, seqId, trackId, clipId);
    NC_CHECK_EQ(c->animation.findCurve(ClipProperty::Opacity)->size(), std::size_t(2));
    // 删除关键帧
    NC_CHECK(editor.removeKeyframe(seqId, trackId, clipId, ClipProperty::Opacity,
                                   Rational::make(0))
                 .isOk());
    c = findClip(project, seqId, trackId, clipId);
    NC_CHECK_EQ(c->animation.findCurve(ClipProperty::Opacity)->size(), std::size_t(1));
    // 撤销删除 → 恢复
    NC_CHECK(editor.undo().isOk());
    c = findClip(project, seqId, trackId, clipId);
    NC_CHECK_EQ(c->animation.findCurve(ClipProperty::Opacity)->size(), std::size_t(2));
}
