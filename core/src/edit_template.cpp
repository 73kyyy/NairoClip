#include "nairoclip/core/edit_template.h"

#include "nairoclip/core/json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nairoclip {
namespace core {

namespace {

/// 微秒整数 → Time（序列化用，避免浮点漂移）。
Time timeFromUs(std::int64_t us) { return fromMicroseconds(us); }

std::string fail(const std::string& msg) { return msg; }

/// 从 JSON 对象读微秒并转 Time；缺失或非整数视为 0。
Time readTimeUs(const JsonValue& obj, const char* key) {
    if (!obj.has(key)) return Time{};
    return timeFromUs(obj.get(key).asInteger(0));
}

void writeTimeUs(JsonValue& obj, const char* key, const Time& t) {
    obj.set(key, JsonValue::integer(toMicroseconds(t)));
}

/// 转场类型名（复用既有枚举；越界时返回 Cut）。
TransitionType transitionFromInt(std::int64_t v) {
    const int maxV = static_cast<int>(TransitionType::Cut);
    (void)maxV;
    if (v < 0 || v > 31) return TransitionType::Cut;
    return static_cast<TransitionType>(v);
}

/// 简易槽位构造器（只在 builtinTemplates 里用）。
TemplateSlot slot(double seconds, LayerRole role = LayerRole::Main,
                  TransitionType in = TransitionType::Cut, double transSeconds = 0.0) {
    TemplateSlot s;
    // 用 1/1000 秒精度：0.5s → 500000us，避免 Rational::make(0.5) 截断成 0。
    s.duration = fromMicroseconds(static_cast<std::int64_t>(seconds * 1000000.0 + 0.5));
    s.role = role;
    s.inTransition = in;
    s.transitionDuration =
        fromMicroseconds(static_cast<std::int64_t>(transSeconds * 1000000.0 + 0.5));
    return s;
}

TemplateSlot withShake(TemplateSlot s, double amplitude, double frequency) {
    s.shakeAmplitude = amplitude;
    s.shakeFrequency = frequency;
    return s;
}

TemplateSlot withEffect(TemplateSlot s, EffectType type, double value) {
    s.effects.push_back(TemplateEffect{type, value});
    return s;
}

}  // namespace

const char* layerRoleName(LayerRole role) {
    switch (role) {
        case LayerRole::Main: return "主轴";
        case LayerRole::Foreground2: return "前景2";
        case LayerRole::Foreground3: return "前景3";
    }
    return "未知";
}

const char* layerRoleKey(LayerRole role) {
    switch (role) {
        case LayerRole::Main: return "main";
        case LayerRole::Foreground2: return "fg2";
        case LayerRole::Foreground3: return "fg3";
    }
    return "main";
}

bool layerRoleFromKey(const std::string& key, LayerRole& out) {
    if (key == "main") {
        out = LayerRole::Main;
        return true;
    }
    if (key == "fg2") {
        out = LayerRole::Foreground2;
        return true;
    }
    if (key == "fg3") {
        out = LayerRole::Foreground3;
        return true;
    }
    return false;
}

bool TemplateSlot::valid() const {
    if (!duration.isPositive()) return false;
    if (!(shakeAmplitude >= 0.0 && shakeAmplitude <= 0.2)) return false;
    if (!(shakeFrequency >= 0.0 && shakeFrequency <= 20.0)) return false;
    // 转场时长：Cut 时可为 0；非 Cut 时必须 > 0 且不超过槽位时长
    if (inTransition != TransitionType::Cut) {
        if (!transitionDuration.isPositive()) return false;
        if (transitionDuration > duration) return false;
    }
    for (const auto& fx : effects) {
        if (!fx.valid()) return false;
    }
    return true;
}

Time EditTemplate::slotsDuration() const {
    Time total = Rational::make(0);
    for (const auto& s : slots) {
        total = total + s.duration;
    }
    return total;
}

bool EditTemplate::valid() const {
    return validateEditTemplate(*this).isOk();
}

Result validateEditTemplate(const EditTemplate& tmpl) {
    if (tmpl.id.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "模板 id 不能为空");
    }
    if (tmpl.id.size() > 64) {
        return Result::fail(ErrorCode::InvalidArgument, "模板 id 过长（上限 64 字符）");
    }
    if (tmpl.name.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "模板名称不能为空");
    }
    if (tmpl.slots.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "模板至少需要一个槽位");
    }
    for (std::size_t i = 0; i < tmpl.slots.size(); ++i) {
        const TemplateSlot& s = tmpl.slots[i];
        const std::string where = "第 " + std::to_string(i + 1) + " 个槽位";
        if (!s.duration.isPositive()) {
            return Result::fail(ErrorCode::InvalidArgument, where + "时长必须大于 0");
        }
        if (!(s.shakeAmplitude >= 0.0 && s.shakeAmplitude <= 0.2)) {
            return Result::fail(ErrorCode::OutOfRange,
                                where + "摇晃强度超出范围（0~0.2）: " +
                                    std::to_string(s.shakeAmplitude));
        }
        if (!(s.shakeFrequency >= 0.0 && s.shakeFrequency <= 20.0)) {
            return Result::fail(ErrorCode::OutOfRange,
                                where + "摇晃频率超出范围（0~20Hz）: " +
                                    std::to_string(s.shakeFrequency));
        }
        if (s.inTransition != TransitionType::Cut) {
            if (!s.transitionDuration.isPositive()) {
                return Result::fail(ErrorCode::InvalidArgument,
                                    where + "设了转场但转场时长为 0");
            }
            if (s.transitionDuration > s.duration) {
                return Result::fail(ErrorCode::OutOfRange,
                                    where + "转场时长超过槽位时长");
            }
        }
        for (std::size_t j = 0; j < s.effects.size(); ++j) {
            if (!s.effects[j].valid()) {
                return Result::fail(ErrorCode::OutOfRange,
                                    where + "第 " + std::to_string(j + 1) +
                                        " 个特效强度超出该特效的合法范围");
            }
        }
    }
    // 总时长自洽性：允许 1 微秒误差（浮点/十进制换算的常见残差）
    const Time sum = tmpl.slotsDuration();
    const std::int64_t diffUs = std::llabs(toMicroseconds(sum) - toMicroseconds(tmpl.totalDuration));
    if (diffUs > 1) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "模板总时长与槽位之和相差 " + std::to_string(diffUs) +
                                " 微秒（应在 1 微秒以内）");
    }
    return Result::ok();
}

std::string editTemplateToJson(const EditTemplate& tmpl, bool pretty) {
    JsonValue root = JsonValue::object();
    root.set("id", JsonValue::string(tmpl.id));
    root.set("name", JsonValue::string(tmpl.name));
    root.set("category", JsonValue::string(tmpl.category));
    writeTimeUs(root, "totalDurationUs", tmpl.totalDuration);
    if (!tmpl.musicHint.empty()) {
        root.set("musicHint", JsonValue::string(tmpl.musicHint));
    }
    if (!tmpl.coverPath.empty()) {
        root.set("coverPath", JsonValue::string(tmpl.coverPath));
    }
    JsonValue slots = JsonValue::array();
    for (const auto& s : tmpl.slots) {
        JsonValue item = JsonValue::object();
        writeTimeUs(item, "durationUs", s.duration);
        item.set("role", JsonValue::string(layerRoleKey(s.role)));
        item.set("inTransition", JsonValue::integer(static_cast<std::int64_t>(s.inTransition)));
        writeTimeUs(item, "transitionDurationUs", s.transitionDuration);
        if (s.shakeAmplitude > 0.0) {
            item.set("shakeAmplitude", JsonValue::number(s.shakeAmplitude));
            item.set("shakeFrequency", JsonValue::number(s.shakeFrequency));
        }
        if (!s.effects.empty()) {
            JsonValue fx = JsonValue::array();
            for (const auto& e : s.effects) {
                JsonValue one = JsonValue::object();
                one.set("type", JsonValue::integer(static_cast<std::int64_t>(e.type)));
                one.set("value", JsonValue::number(e.value));
                fx.push(std::move(one));
            }
            item.set("effects", std::move(fx));
        }
        slots.push(std::move(item));
    }
    root.set("slots", std::move(slots));
    return toJsonString(root, pretty);
}

Result editTemplateFromJson(const std::string& text, EditTemplate& out) {
    JsonValue root;
    JsonParseError err;
    if (!parseJson(text, root, err)) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "模板 JSON 解析失败（偏移 " + std::to_string(err.offset) + "）：" +
                                err.message);
    }
    if (!root.isObject()) {
        return Result::fail(ErrorCode::InvalidArgument, "模板 JSON 根节点必须是对象");
    }

    EditTemplate tmpl;
    tmpl.id = root.get("id").asString();
    tmpl.name = root.get("name").asString();
    tmpl.category = root.get("category").asString();
    tmpl.totalDuration = readTimeUs(root, "totalDurationUs");
    if (root.has("musicHint")) tmpl.musicHint = root.get("musicHint").asString();
    if (root.has("coverPath")) tmpl.coverPath = root.get("coverPath").asString();

    if (!root.has("slots") || !root.get("slots").isArray()) {
        return Result::fail(ErrorCode::InvalidArgument, "模板缺少 slots 数组");
    }
    const JsonValue& slots = root.get("slots");
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const JsonValue& item = slots.at(i);
        if (!item.isObject()) {
            return Result::fail(ErrorCode::InvalidArgument,
                                "第 " + std::to_string(i + 1) + " 个槽位不是对象");
        }
        TemplateSlot s;
        s.duration = readTimeUs(item, "durationUs");
        LayerRole role = LayerRole::Main;
        if (item.has("role")) {
            if (!layerRoleFromKey(item.get("role").asString(), role)) {
                return Result::fail(ErrorCode::InvalidArgument,
                                    "第 " + std::to_string(i + 1) + " 个槽位 role 非法: " +
                                        item.get("role").asString());
            }
        }
        s.role = role;
        s.inTransition = transitionFromInt(item.get("inTransition").asInteger(8));
        s.transitionDuration = readTimeUs(item, "transitionDurationUs");
        s.shakeAmplitude = item.get("shakeAmplitude").asNumber(0.0);
        s.shakeFrequency = item.get("shakeFrequency").asNumber(0.0);
        if (item.has("effects") && item.get("effects").isArray()) {
            const JsonValue& fx = item.get("effects");
            for (std::size_t j = 0; j < fx.size(); ++j) {
                const JsonValue& one = fx.at(j);
                TemplateEffect e;
                e.type = static_cast<EffectType>(one.get("type").asInteger(0));
                e.value = one.get("value").asNumber(0.0);
                s.effects.push_back(e);
            }
        }
        tmpl.slots.push_back(std::move(s));
    }

    const Result r = validateEditTemplate(tmpl);
    if (!r.isOk()) {
        return r;  // 不修改 out
    }
    out = std::move(tmpl);
    return Result::ok();
}

// -----------------------------------------------------------------------------
// 内置模板（10 个）
// -----------------------------------------------------------------------------
//
// 说明：这些模板的**值**是产品决策（多长、什么转场、摇晃多强），
// 但结构必须能通过 validateEditTemplate。测试会逐个校验，防止手写数字
// 出现「总时长与槽位之和不一致」这类低级错误。

const std::vector<EditTemplate>& builtinTemplates() {
    static const std::vector<EditTemplate> kTemplates = [] {
        std::vector<EditTemplate> list;

        // 1. 关键帧摇晃特效剪辑（文档点名的示例）
        {
            EditTemplate t;
            t.id = "keyframe_shake";
            t.name = "关键帧摇晃特效剪辑";
            t.category = "卡点";
            t.musicHint = "强节奏电子 120-140 BPM";
            t.slots = {
                withShake(withEffect(slot(1.2, LayerRole::Main, TransitionType::FlashWhite, 0.12),
                                     EffectType::ZoomPunch, 0.6),
                          0.02, 8.0),
                withShake(withEffect(slot(0.8, LayerRole::Main, TransitionType::Cut),
                                     EffectType::RgbSplit, 0.4),
                          0.03, 10.0),
                withShake(withEffect(slot(1.6, LayerRole::Main, TransitionType::WhipPan, 0.2),
                                     EffectType::ZoomPunch, 0.3),
                          0.01, 6.0),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 2. 三连击卡点
        {
            EditTemplate t;
            t.id = "triple_hit_beat";
            t.name = "三连击卡点";
            t.category = "卡点";
            t.musicHint = "鼓点密集 150+ BPM";
            t.slots = {
                slot(0.5, LayerRole::Main, TransitionType::FlashWhite, 0.08),
                slot(0.5, LayerRole::Main, TransitionType::Cut),
                slot(0.5, LayerRole::Main, TransitionType::Cut),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 3. 汽车展示（慢推 + 细节切换）
        {
            EditTemplate t;
            t.id = "car_showcase";
            t.name = "汽车展示";
            t.category = "汽车";
            t.musicHint = "低音氛围 90-110 BPM";
            t.slots = {
                slot(2.0, LayerRole::Main, TransitionType::FadeFromBlack, 0.5),
                slot(1.5, LayerRole::Main, TransitionType::CrossDissolve, 0.3),
                withEffect(slot(1.0, LayerRole::Foreground2, TransitionType::Cut),
                           EffectType::Brightness, 0.1),
                slot(2.5, LayerRole::Main, TransitionType::Zoom, 0.4),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 4. 汽车引擎特写（快速硬切）
        {
            EditTemplate t;
            t.id = "car_engine_cut";
            t.name = "引擎特写快切";
            t.category = "汽车";
            t.musicHint = "工业感打击 130 BPM";
            t.slots = {
                slot(0.6, LayerRole::Main, TransitionType::Cut),
                slot(0.4, LayerRole::Main, TransitionType::Cut),
                slot(0.4, LayerRole::Main, TransitionType::FlashWhite, 0.06),
                slot(0.6, LayerRole::Main, TransitionType::Cut),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 5. 动漫闪现（高频快切 + 摇晃）
        {
            EditTemplate t;
            t.id = "anime_flash";
            t.name = "动漫闪现";
            t.category = "动漫";
            t.musicHint = "高速电子 160 BPM";
            t.slots = {
                withShake(withEffect(slot(0.3, LayerRole::Main, TransitionType::FlashWhite, 0.05),
                                     EffectType::ZoomPunch, 0.8),
                          0.04, 14.0),
                withShake(withEffect(slot(0.3, LayerRole::Main, TransitionType::Cut),
                                     EffectType::RgbSplit, 0.6),
                          0.04, 12.0),
                withShake(withEffect(slot(0.4, LayerRole::Main, TransitionType::Cut),
                                     EffectType::Glitch, 0.5),
                          0.03, 10.0),
                slot(0.8, LayerRole::Main, TransitionType::CrossDissolve, 0.2),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 6. 动漫冲击帧（单帧爆发）
        {
            EditTemplate t;
            t.id = "anime_impact";
            t.name = "冲击帧";
            t.category = "动漫";
            t.musicHint = "重音落点";
            t.slots = {
                slot(0.2, LayerRole::Main, TransitionType::Cut),
                withEffect(withEffect(slot(0.15, LayerRole::Main, TransitionType::Cut),
                                      EffectType::RgbSplit, 0.9),
                           EffectType::ZoomPunch, 1.0),
                slot(1.2, LayerRole::Main, TransitionType::FadeToBlack, 0.3),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 7. 旅拍开场（宽幅留白）
        {
            EditTemplate t;
            t.id = "travel_open";
            t.name = "旅拍开场";
            t.category = "旅拍";
            t.musicHint = "轻快民谣 100 BPM";
            t.slots = {
                slot(2.5, LayerRole::Main, TransitionType::FadeFromBlack, 0.8),
                slot(1.8, LayerRole::Main, TransitionType::CrossDissolve, 0.6),
                slot(1.8, LayerRole::Main, TransitionType::CrossDissolve, 0.6),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 8. 旅拍转场串（叠化为主）
        {
            EditTemplate t;
            t.id = "travel_dissolve_chain";
            t.name = "旅拍叠化串联";
            t.category = "旅拍";
            t.musicHint = "舒缓 80-95 BPM";
            t.slots = {
                slot(1.5, LayerRole::Main, TransitionType::CrossDissolve, 0.5),
                slot(1.5, LayerRole::Main, TransitionType::CrossDissolve, 0.5),
                slot(1.5, LayerRole::Main, TransitionType::CrossDissolve, 0.5),
                slot(2.0, LayerRole::Main, TransitionType::FadeToBlack, 0.6),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 9. 画中画解说（主轴 + 前景2）
        {
            EditTemplate t;
            t.id = "pip_commentary";
            t.name = "画中画解说";
            t.category = "旅拍";
            t.musicHint = "轻背景 90 BPM";
            t.slots = {
                slot(3.0, LayerRole::Main, TransitionType::FadeFromBlack, 0.4),
                slot(3.0, LayerRole::Foreground2, TransitionType::Cut),
                slot(2.5, LayerRole::Main, TransitionType::CrossDissolve, 0.4),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        // 10. 收尾定格
        {
            EditTemplate t;
            t.id = "ending_hold";
            t.name = "收尾定格";
            t.category = "卡点";
            t.musicHint = "收束尾音";
            t.slots = {
                slot(0.8, LayerRole::Main, TransitionType::Cut),
                slot(1.8, LayerRole::Main, TransitionType::FadeToBlack, 0.5),
            };
            t.totalDuration = t.slotsDuration();
            list.push_back(std::move(t));
        }

        return list;
    }();
    return kTemplates;
}

const EditTemplate* findBuiltinTemplate(const std::string& id) {
    const auto& all = builtinTemplates();
    for (const auto& t : all) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

}  // namespace core
}  // namespace nairoclip
