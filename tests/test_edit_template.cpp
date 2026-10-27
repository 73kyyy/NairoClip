#include "test_framework.h"
#include "nairoclip/core/edit_template.h"

#include <cmath>
#include <set>
#include <string>

using namespace nairoclip::core;

namespace {

TemplateSlot mkSlot(double seconds) {
    TemplateSlot s;
    s.duration = fromMicroseconds(static_cast<std::int64_t>(seconds * 1000000.0 + 0.5));
    return s;
}

EditTemplate mkTemplate() {
    EditTemplate t;
    t.id = "t1";
    t.name = "测试模板";
    t.category = "卡点";
    t.slots.push_back(mkSlot(1.0));
    t.slots.push_back(mkSlot(2.0));
    t.totalDuration = t.slotsDuration();
    return t;
}

}  // namespace

// =============================================================================
// B2-1 模板数据结构
// =============================================================================

NC_TEST(EditTemplate, BuiltinTemplatesAreValid) {
    // 内置模板是手写数字，最容易出现「总时长与槽位之和不一致」这类错误。
    // 这条用例对 10 个模板逐个跑完整校验。
    const auto& all = builtinTemplates();
    NC_CHECK_EQ(all.size(), static_cast<std::size_t>(10));
    for (const auto& t : all) {
        const Result r = validateEditTemplate(t);
        if (!r.isOk()) {
            ::nairoclip::test::reportFailure(
                __FILE__, __LINE__,
                "内置模板「" + t.id + "」校验失败: " + r.message);
        }
    }
}

NC_TEST(EditTemplate, BuiltinIdsAreUnique) {
    std::set<std::string> ids;
    for (const auto& t : builtinTemplates()) {
        NC_CHECK(ids.insert(t.id).second);  // 插入失败 = 重复
        NC_CHECK(!t.name.empty());
        NC_CHECK(!t.category.empty());
    }
}

NC_TEST(EditTemplate, FindBuiltinByIdWorks) {
    const EditTemplate* t = findBuiltinTemplate("keyframe_shake");
    NC_CHECK(t != nullptr);
    if (t != nullptr) {
        NC_CHECK_EQ(t->name, std::string("关键帧摇晃特效剪辑"));
        NC_CHECK(!t->slots.empty());
    }
    NC_CHECK(findBuiltinTemplate("no_such_template") == nullptr);
}

NC_TEST(EditTemplate, TotalDurationEqualsSlotSum) {
    // 冗余字段的价值在于「能被校验」——这里验证它确实是槽位之和
    const EditTemplate t = mkTemplate();
    NC_CHECK_EQ(toMicroseconds(t.slotsDuration()), static_cast<std::int64_t>(3000000));
    NC_CHECK_EQ(toMicroseconds(t.totalDuration), static_cast<std::int64_t>(3000000));
}

NC_TEST(EditTemplate, RejectsInconsistentTotalDuration) {
    EditTemplate t = mkTemplate();
    t.totalDuration = fromMicroseconds(1000000);  // 故意写成 1 秒（应为 3 秒）
    const Result r = validateEditTemplate(t);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.message.find("总时长") != std::string::npos);
}

NC_TEST(EditTemplate, RejectsZeroDurationSlot) {
    EditTemplate t = mkTemplate();
    t.slots[1].duration = Time{};  // 0
    t.totalDuration = t.slotsDuration();
    const Result r = validateEditTemplate(t);
    NC_CHECK(!r.isOk());
    // 必须指出是第几个槽位
    NC_CHECK(r.message.find("第 2 个槽位") != std::string::npos);
}

NC_TEST(EditTemplate, RejectsTransitionLongerThanSlot) {
    EditTemplate t = mkTemplate();
    t.slots[0].inTransition = TransitionType::CrossDissolve;
    t.slots[0].transitionDuration = fromMicroseconds(2000000);  // 2s > 1s 槽位
    const Result r = validateEditTemplate(t);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.message.find("转场时长超过槽位时长") != std::string::npos);
}

NC_TEST(EditTemplate, RejectsCutWithNonZeroTransitionDuration) {
    // Cut 时转场时长应被忽略，而不是报错——但如果设了转场却时长为 0 才是错
    EditTemplate t = mkTemplate();
    t.slots[0].inTransition = TransitionType::CrossDissolve;
    t.slots[0].transitionDuration = Time{};  // 0
    NC_CHECK(!validateEditTemplate(t).isOk());
}

NC_TEST(EditTemplate, RejectsOutOfRangeShake) {
    EditTemplate t = mkTemplate();
    t.slots[0].shakeAmplitude = 0.5;  // 上限 0.2
    NC_CHECK(!validateEditTemplate(t).isOk());

    EditTemplate t2 = mkTemplate();
    t2.slots[0].shakeFrequency = 50.0;  // 上限 20
    NC_CHECK(!validateEditTemplate(t2).isOk());
}

NC_TEST(EditTemplate, RejectsNegativeShake) {
    EditTemplate t = mkTemplate();
    t.slots[0].shakeAmplitude = -0.01;
    NC_CHECK(!validateEditTemplate(t).isOk());
}

NC_TEST(EditTemplate, RejectsInvalidEffectValue) {
    EditTemplate t = mkTemplate();
    TemplateEffect e;
    e.type = EffectType::Brightness;
    e.value = 999.0;  // 远超合法范围
    t.slots[0].effects.push_back(e);
    NC_CHECK(!validateEditTemplate(t).isOk());
}

NC_TEST(EditTemplate, RejectsEmptyTemplate) {
    EditTemplate t;
    t.id = "x";
    t.name = "x";
    NC_CHECK(!validateEditTemplate(t).isOk());  // 无槽位

    EditTemplate t2 = mkTemplate();
    t2.id.clear();
    NC_CHECK(!validateEditTemplate(t2).isOk());  // id 为空
}

NC_TEST(EditTemplate, SerializationRoundtrip) {
    EditTemplate t = mkTemplate();
    t.musicHint = "120 BPM";
    t.coverPath = "cover.png";
    t.slots[0].role = LayerRole::Foreground2;
    t.slots[0].inTransition = TransitionType::CrossDissolve;
    t.slots[0].transitionDuration = fromMicroseconds(300000);
    t.slots[1].shakeAmplitude = 0.03;
    t.slots[1].shakeFrequency = 9.0;
    t.slots[1].effects.push_back(TemplateEffect{EffectType::ZoomPunch, 0.5});
    t.totalDuration = t.slotsDuration();

    const std::string json = editTemplateToJson(t, true);
    NC_CHECK(!json.empty());
    NC_CHECK(json.find("\"keyframe") == std::string::npos);  // 不是内置模板

    EditTemplate back;
    const Result r = editTemplateFromJson(json, back);
    NC_CHECK(r.isOk());
    if (!r.isOk()) return;

    NC_CHECK_EQ(back.id, t.id);
    NC_CHECK_EQ(back.name, t.name);
    NC_CHECK_EQ(back.category, t.category);
    NC_CHECK_EQ(back.musicHint, t.musicHint);
    NC_CHECK_EQ(back.coverPath, t.coverPath);
    NC_CHECK_EQ(toMicroseconds(back.totalDuration), toMicroseconds(t.totalDuration));
    NC_CHECK_EQ(back.slots.size(), t.slots.size());
    NC_CHECK(back.slots[0].role == LayerRole::Foreground2);
    NC_CHECK(back.slots[0].inTransition == TransitionType::CrossDissolve);
    NC_CHECK_EQ(toMicroseconds(back.slots[0].transitionDuration),
                toMicroseconds(t.slots[0].transitionDuration));
    NC_CHECK(std::fabs(back.slots[1].shakeAmplitude - 0.03) < 1e-9);
    NC_CHECK(std::fabs(back.slots[1].shakeFrequency - 9.0) < 1e-9);
    NC_CHECK_EQ(back.slots[1].effects.size(), static_cast<std::size_t>(1));
    NC_CHECK(back.slots[1].effects[0].type == EffectType::ZoomPunch);
    NC_CHECK(std::fabs(back.slots[1].effects[0].value - 0.5) < 1e-9);
}

NC_TEST(EditTemplate, BuiltinTemplatesRoundtrip) {
    // 所有内置模板都必须能序列化 → 反序列化 → 仍然合法
    for (const auto& t : builtinTemplates()) {
        const std::string json = editTemplateToJson(t);
        EditTemplate back;
        const Result r = editTemplateFromJson(json, back);
        if (!r.isOk()) {
            ::nairoclip::test::reportFailure(
                __FILE__, __LINE__, "模板「" + t.id + "」往返失败: " + r.message);
            continue;
        }
        if (toMicroseconds(back.totalDuration) != toMicroseconds(t.totalDuration)) {
            ::nairoclip::test::reportFailure(
                __FILE__, __LINE__, "模板「" + t.id + "」总时长往返不一致");
        }
        if (back.slots.size() != t.slots.size()) {
            ::nairoclip::test::reportFailure(
                __FILE__, __LINE__, "模板「" + t.id + "」槽位数量往返不一致");
        }
    }
}

NC_TEST(EditTemplate, FailedParseDoesNotModifyOutput) {
    // B0-3 原则：失败不能污染调用方的数据
    EditTemplate existing = mkTemplate();
    existing.id = "keep_me";

    EditTemplate target = existing;
    NC_CHECK(!editTemplateFromJson("{not json}", target).isOk());
    NC_CHECK_EQ(target.id, std::string("keep_me"));

    NC_CHECK(!editTemplateFromJson("{\"id\":\"x\"}", target).isOk());  // 缺 slots
    NC_CHECK_EQ(target.id, std::string("keep_me"));
}

NC_TEST(EditTemplate, ParseErrorMentionsProblem) {
    EditTemplate t;
    const Result r1 = editTemplateFromJson("{bad", t);
    NC_CHECK(!r1.isOk());
    NC_CHECK(r1.message.find("解析失败") != std::string::npos);

    const Result r2 = editTemplateFromJson("{\"id\":\"a\",\"name\":\"b\",\"slots\":[]}", t);
    NC_CHECK(!r2.isOk());
    NC_CHECK(r2.message.find("至少需要一个槽位") != std::string::npos);
}

NC_TEST(EditTemplate, LayerRoleKeysRoundtrip) {
    for (LayerRole role : {LayerRole::Main, LayerRole::Foreground2, LayerRole::Foreground3}) {
        LayerRole back;
        NC_CHECK(layerRoleFromKey(layerRoleKey(role), back));
        NC_CHECK(back == role);
        NC_CHECK(layerRoleName(role) != std::string());
    }
    LayerRole dummy;
    NC_CHECK(!layerRoleFromKey("bogus", dummy));
}

NC_TEST(EditTemplate, RejectsInvalidRoleKey) {
    EditTemplate t;
    const std::string json =
        "{\"id\":\"a\",\"name\":\"b\",\"totalDurationUs\":1000000,"
        "\"slots\":[{\"durationUs\":1000000,\"role\":\"nope\"}]}";
    const Result r = editTemplateFromJson(json, t);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.message.find("role") != std::string::npos);
}

NC_TEST(EditTemplate, OmitsDefaultFieldsInJson) {
    // 默认值不应写进 JSON（保持 diff 稳定，与 B2-10 同策略）
    EditTemplate t = mkTemplate();
    t.slots[0].shakeAmplitude = 0.0;  // 默认
    const std::string json = editTemplateToJson(t);
    NC_CHECK(json.find("shakeAmplitude") == std::string::npos);
    NC_CHECK(json.find("musicHint") == std::string::npos);
    NC_CHECK(json.find("coverPath") == std::string::npos);
}
