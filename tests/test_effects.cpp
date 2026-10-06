#include "test_framework.h"
#include "nairoclip/core/effects.h"
using namespace nairoclip::core;
namespace {
Effect makeEffect(std::uint64_t id, EffectType type, double value) {
    Effect e;
    e.id = EffectId{id};
    e.type = type;
    e.value = value;
    return e;
}
bool nearly(double a, double b, double eps = 1e-6) {
    const double d = a - b;
    return (d < 0 ? -d : d) < eps;
}
}  // namespace
NC_TEST(Effects, SpecAndNames) {
    const EffectSpec brightness = effectSpec(EffectType::Brightness);
    NC_CHECK(nearly(brightness.minValue, -1.0));
    NC_CHECK(nearly(brightness.maxValue, 1.0));
    NC_CHECK_EQ(std::string(effectName(EffectType::Brightness)), std::string("亮度"));
    NC_CHECK_EQ(std::string(effectName(EffectType::GaussianBlur)), std::string("高斯模糊"));
    const EffectSpec contrast = effectSpec(EffectType::Contrast);
    NC_CHECK(nearly(contrast.defaultValue, 1.0));
}
NC_TEST(Effects, StackAddReplaceRemove) {
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::Brightness, 0.2)).isOk());
    NC_CHECK(stack.add(makeEffect(2, EffectType::Contrast, 1.5)).isOk());
    NC_CHECK_EQ(stack.size(), std::size_t(2));
    NC_CHECK(stack.invariantsHold());
    // 替换
    NC_CHECK(stack.add(makeEffect(1, EffectType::Brightness, 0.5)).isOk());
    NC_CHECK_EQ(stack.size(), std::size_t(2));
    NC_CHECK(nearly(stack.find(EffectId{1})->value, 0.5));
    // 删除
    NC_CHECK(stack.remove(EffectId{1}));
    NC_CHECK(!stack.remove(EffectId{99}));
    NC_CHECK_EQ(stack.size(), std::size_t(1));
}
NC_TEST(Effects, RejectsInvalidParams) {
    EffectStack stack;
    // 亮度超范围
    NC_CHECK(!stack.add(makeEffect(1, EffectType::Brightness, 5.0)).isOk());
    // None 类型拒绝
    NC_CHECK(!stack.add(makeEffect(2, EffectType::None, 0.0)).isOk());
    NC_CHECK(stack.empty());
}
NC_TEST(Effects, StackOrderMove) {
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::Brightness, 0.1)).isOk());
    NC_CHECK(stack.add(makeEffect(2, EffectType::Saturation, 0.5)).isOk());
    NC_CHECK(stack.add(makeEffect(3, EffectType::Fade, 0.3)).isOk());
    NC_CHECK(stack.effects()[0].id == EffectId{1});
    NC_CHECK(stack.moveUp(EffectId{3}));
    NC_CHECK(stack.effects()[1].id == EffectId{3});
    NC_CHECK(stack.moveDown(EffectId{3}));
    NC_CHECK(stack.effects()[2].id == EffectId{3});
    NC_CHECK(!stack.moveDown(EffectId{3}));   // 已在底
    NC_CHECK(!stack.moveUp(EffectId{1}));     // 已在顶
}
NC_TEST(Effects, ColorTransformBrightness) {
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::Brightness, 0.1)).isOk());
    const ColorTransform t = evaluateColorTransform(stack);
    double r = 0.5, g = 0.5, b = 0.5;
    t.apply(r, g, b);
    NC_CHECK(nearly(r, 0.6, 1e-9));
    NC_CHECK(nearly(g, 0.6, 1e-9));
    NC_CHECK(nearly(b, 0.6, 1e-9));
}
NC_TEST(Effects, ColorTransformClampsTo01) {
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::Brightness, 1.0)).isOk());
    const ColorTransform t = evaluateColorTransform(stack);
    double r = 0.5, g = 0.8, b = 0.2;
    t.apply(r, g, b);
    NC_CHECK(r <= 1.0 && r >= 0.0);
    NC_CHECK(nearly(g, 1.0));
    // b = 0.2 + 1.0 = 1.2 → 钳制到 1.0
    NC_CHECK(nearly(b, 1.0));
}
NC_TEST(Effects, ColorTransformGrayscale) {
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::Grayscale, 1.0)).isOk());
    const ColorTransform t = evaluateColorTransform(stack);
    double r = 1.0, g = 0.0, b = 0.0;
    t.apply(r, g, b);
    NC_CHECK(nearly(r, g, 1e-9));
    NC_CHECK(nearly(g, b, 1e-9));
    NC_CHECK(nearly(r, 0.299, 1e-9));
}
NC_TEST(Effects, DisabledEffectIgnored) {
    EffectStack stack;
    Effect e = makeEffect(1, EffectType::Brightness, 0.5);
    e.enabled = false;
    NC_CHECK(stack.add(e).isOk());
    const ColorTransform t = evaluateColorTransform(stack);
    NC_CHECK(nearly(t.brightness, 0.0));
}
NC_TEST(Effects, SpatialEffectsSkippedInColorTransform) {
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::GaussianBlur, 20.0)).isOk());
    NC_CHECK(stack.add(makeEffect(2, EffectType::Sharpen, 2.0)).isOk());
    const ColorTransform t = evaluateColorTransform(stack);
    // 空间效果不影响颜色变换
    NC_CHECK(nearly(t.brightness, 0.0));
    NC_CHECK(nearly(t.contrast, 1.0));
}
NC_TEST(Effects, MultipliedEffectsCombine) {
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::Contrast, 2.0)).isOk());
    NC_CHECK(stack.add(makeEffect(2, EffectType::Contrast, 1.5)).isOk());
    const ColorTransform t = evaluateColorTransform(stack);
    NC_CHECK(nearly(t.contrast, 3.0, 1e-9));
}
#include "nairoclip/core/editor.h"
NC_TEST(Effects, EditorEffectCommandsWithUndo) {
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
    Clip c;
    c.asset = a.id;
    c.start = Rational::make(0);
    c.duration = Rational::make(5);
    ClipId clipId;
    NC_CHECK(editor.insertClip(seqId, trackId, c, &clipId).isOk());
    // 添加特效
    Effect e;
    e.id = EffectId{project.ids.next()};
    e.type = EffectType::Brightness;
    e.value = 0.15;
    NC_CHECK(editor.addEffect(seqId, trackId, clipId, e).isOk());
    const Clip* clip = findClip(project, seqId, trackId, clipId);
    NC_CHECK_EQ(clip->effects.size(), std::size_t(1));
    // 替换
    e.value = 0.3;
    NC_CHECK(editor.addEffect(seqId, trackId, clipId, e).isOk());
    NC_CHECK(clip->effects.size() == std::size_t(1));
    NC_CHECK(std::abs(findClip(project, seqId, trackId, clipId)->effects.find(e.id)->value - 0.3) < 1e-9);
    // 撤销替换 → 回到 0.15
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(std::abs(findClip(project, seqId, trackId, clipId)->effects.find(e.id)->value - 0.15) < 1e-9);
    // 撤销添加 → 无特效
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(findClip(project, seqId, trackId, clipId)->effects.empty());
    // 重做两次 → 0.3
    NC_CHECK(editor.redo().isOk());
    NC_CHECK(editor.redo().isOk());
    // 删除特效
    NC_CHECK(editor.removeEffect(seqId, trackId, clipId, e.id).isOk());
    NC_CHECK(findClip(project, seqId, trackId, clipId)->effects.empty());
    // 撤销删除 → 恢复
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(findClip(project, seqId, trackId, clipId)->effects.size(), std::size_t(1));
}
