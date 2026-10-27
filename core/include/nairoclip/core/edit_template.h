#pragma once
// =============================================================================
// 剪辑模板数据结构（B2-1）
// =============================================================================
// 用途：把「卡点 / 汽车 / 动漫 / 旅拍」这类剪辑套路固化成可复用模板。
// 用户选模板 + 一批素材，由 B2-2 的智能填充算法把素材塞进槽位。
//
// ## 与文档草案的三处偏差（按实际代码库修正）
// 1. 文档写 `Time duration` 且暗示有独立的 Time 结构体——本库 `Time` 就是
//    `Rational`（见 time.h）；这里照用，序列化时转微秒整数避免浮点误差。
// 2. 文档写 `std::vector<EffectSpec> effects`——`EffectSpec` 是**静态规格**
//    （min/max/default/name，描述"某类特效的取值范围"），不是特效实例。
//    槽位里存 `TemplateEffect{type, value}`，才表达得了"这个槽位加 0.7 强度
//    的 ZoomPunch"。
// 3. 文档写 `double shakeAmplitude`——B1-5 已实现 `ShakeParams`，这里沿用
//    同一口径（振幅 = 画幅占比、频率 Hz），避免出现两套摇晃语义。
//
// ## 设计取舍
// - 槽位**不存素材 ID**：模板描述形状，素材由填充算法决定（B2-2）。
// - `totalDuration` 是冗余字段，但保留：UI 不必遍历槽位即可显示模板时长，
//   载入时还能校验「槽位总长是否自洽」（见 validateEditTemplate）。
// - 单位为「秒」的浮点在 JSON 里会有精度问题，因此序列化用**微秒整数**。

#include "nairoclip/core/effects.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/transition_types.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 槽位在画面中的层级角色。
///
/// 竖屏短片常见「主画面 + 画中画」结构；填充算法要按角色选不同尺寸/位置的
/// 素材（主轴要满屏，前景 2/3 常是小窗），因此角色必须显式建模。
enum class LayerRole {
    Main = 0,         ///< 主轴（满屏主画面）
    Foreground2 = 1,  ///< 前景 2（常为画中画）
    Foreground3 = 2,  ///< 前景 3
};

/// 角色展示名（中文）。未知值返回「未知」。
const char* layerRoleName(LayerRole role);
/// 角色序列化键（"main" / "fg2" / "fg3"）。
const char* layerRoleKey(LayerRole role);
/// 从序列化键解析；无法识别返回 false 且不改 out。
bool layerRoleFromKey(const std::string& key, LayerRole& out);

/// 槽位内的一个特效应用（类型 + 强度）。
struct TemplateEffect {
    EffectType type = EffectType::None;
    double value = 0.0;

    bool valid() const {
        const EffectSpec spec = effectSpec(type);
        return value >= spec.minValue && value <= spec.maxValue;
    }
};

/// 槽位：模板中的一个时间片段。
struct TemplateSlot {
    /// 槽位时长（必须 > 0）。
    Time duration;
    /// 层级角色。
    LayerRole role = LayerRole::Main;
    /// 入场转场（`TransitionType::Cut` = 硬切）。
    TransitionType inTransition = TransitionType::Cut;
    /// 转场时长；`Cut` 时忽略。
    Time transitionDuration;
    /// 叠加在本槽位上的特效（按数组顺序应用）。
    std::vector<TemplateEffect> effects;
    /// 摇晃强度（0 = 无摇晃）；口径同 B1-5：占画幅短边比例。
    double shakeAmplitude = 0.0;
    /// 摇晃频率（Hz）。
    double shakeFrequency = 0.0;

    bool valid() const;
    /// 本槽位是否启用摇晃。
    bool hasShake() const { return shakeAmplitude > 0.0; }
};

/// 剪辑模板。
struct EditTemplate {
    std::string id;        ///< 稳定标识（资源文件名，需唯一）
    std::string name;      ///< 展示名，如「关键帧摇晃特效剪辑」
    std::string category;  ///< 卡点 / 汽车 / 动漫 / 旅拍
    /// 模板总时长（应等于所有槽位时长之和；载入时校验）。
    Time totalDuration;
    std::vector<TemplateSlot> slots;
    std::string musicHint;  ///< 配乐建议（如「强节奏电子 120-140 BPM」）
    std::string coverPath;  ///< 封面图路径（可为空）

    bool valid() const;
    /// 槽位时长之和。
    Time slotsDuration() const;
    /// 槽位数量。
    std::size_t slotCount() const { return slots.size(); }
};

/// 校验并返回失败原因（成功时 code == Ok）。
///
/// 为什么用 Result 而不是 bool：模板来自**用户可编辑的 JSON 资源**，
/// 失败必须能说清「第 3 个槽位时长非法」，否则用户只能看到「模板无效」。
/// （B0-3 的同一原则。）
Result validateEditTemplate(const EditTemplate& tmpl);

/// 序列化为 JSON 文本。
std::string editTemplateToJson(const EditTemplate& tmpl, bool pretty = false);

/// 从 JSON 文本反序列化。
/// 失败返回带原因的 Result，且**不修改** out（调用方原值保留）。
Result editTemplateFromJson(const std::string& text, EditTemplate& out);

/// 内置模板（B2-1 交付要求：10 个）。
const std::vector<EditTemplate>& builtinTemplates();

/// 按 id 查找内置模板；找不到返回 nullptr。
const EditTemplate* findBuiltinTemplate(const std::string& id);

}  // namespace core
}  // namespace nairoclip
