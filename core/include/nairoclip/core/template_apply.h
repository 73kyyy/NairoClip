#pragma once
// =============================================================================
// 模板应用管线（B2-3）
// =============================================================================
// 把「模板 + 填充结果」变成时间线上真实的片段/转场/特效，并且**整体可撤销**。
//
// ## 关键约束：必须是一条撤销记录
// 用户点一次「套用模板」，撤销一次就应该完全回到原状。因此整个应用过程
// 包在一个 `beginTransaction`/`endTransaction` 里。
//
// ## 关于「撤销前后 Project 一致」怎么验证
// 只断言「片段数回到原值」是不够的 —— 特效没删干净、轨道多了一条、ID
// 分配器前进过，都算不一致。测试用 **序列化文本全等** 作为判据
// （`saveProjectToNclip` 前后逐字符比较），这能抓住所有残留。
//
// ## 与 B2-2 的分工
// B2-2（`fillTemplateSlots`）只决定「哪个素材进哪个槽位」，不碰编辑器；
// 本模块负责落盘。分开的好处：填充算法可以脱离编辑器单测，
// 应用管线也可以利用外部指定的映射（用户手动拖素材）。
//
// ## 槽位 → 时间线的排布规则
// - 所有槽位**首尾相接**排在同一条新轨道上（模板描述的是线性结构）；
// - 前景角色（Foreground2/3）放到**独立的前景轨道**，时间区间与对应槽位对齐，
//   这样才能形成画中画；
// - 素材入点默认从 0 开始（`sourceIn = 0`）；
// - 片段时长 = 槽位时长，与素材自身的时长无关（素材更长则只取前半段）。

#include "nairoclip/core/edit_template.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/template_fill.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 一个槽位对应的素材指定（调用方手工给出，或从 B2-2 的 FillResult 转换）。
struct MaterialAssignment {
    /// 槽位下标。
    std::size_t slotIndex = 0;
    /// 素材 ID（必须是工程里已存在的素材）。
    AssetId asset;
};

/// 应用结果摘要。
struct TemplateApplyResult {
    /// 插入的片段数。
    int clipsInserted = 0;
    /// 添加的转场数。
    int transitionsAdded = 0;
    /// 添加的特效数。
    int effectsAdded = 0;
    /// 应用了摇晃的槽位数。
    int shakesApplied = 0;
    /// 使用的轨道数（含前景轨道）。
    int tracksUsed = 0;
    /// 未应用的槽位（及其原因），便于 UI 显示「3 个槽位素材不足」。
    std::vector<std::string> warnings;
};

/// 把 B2-2 的填充结果转成本模块需要的映射（跳过未填充的槽位）。
std::vector<MaterialAssignment> assignmentsFromFill(
    const FillResult& fill, const std::vector<FillCandidate>& candidates);

/// 应用模板。
///
/// 前置条件：
/// - `assignments` 里引用的素材必须存在于 `editor.project()` 的素材表中；
/// - 每个槽位最多一个 assignment。
///
/// 行为：
/// - 所有改动包在一条事务里；**任何一步失败都会回滚整个事务**并返回错误，
///   不会留下「一半应用」的状态；
/// - 未提供素材的槽位会被跳过（记入 `warnings`），不阻断其余槽位。
Result applyTemplate(TimelineEditor& editor, SequenceId sequenceId, const EditTemplate& tmpl,
                     const std::vector<MaterialAssignment>& assignments,
                     TemplateApplyResult& out);

/// 说明文案（供 UI 显示「插入 5 段，加了 3 个特效」）。
std::string describeApplyResult(const TemplateApplyResult& result);

}  // namespace core
}  // namespace nairoclip
