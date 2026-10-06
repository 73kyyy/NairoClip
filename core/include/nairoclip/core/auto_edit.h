#pragma once

#include "nairoclip/core/editor.h"
#include "nairoclip/core/reference_style.h"

#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 自动剪辑执行器（Auto Edit）
// =============================================================================
// 闭环：StyleProfile（分析）→ CutSuggestion（建议）→ 时间轴序列（落盘）。
//
// 合规注意：自动剪辑只使用「用户自己的素材」，参考素材仅贡献
// 统计特征（节奏/色调），不参与成片内容。

/// 自动剪辑配置。
struct AutoEditOptions {
    /// 是否应用参考色调（映射为调色特效）。
    bool applyColor = true;
    /// 是否应用参考节奏的转场偏好（在相邻片段间加转场）。
    bool applyTransitions = false;
    /// 应用转场时的最大转场数量（防止过度添加）。
    std::size_t maxTransitions = 10;
    /// 事务名称（撤销历史显示）。
    std::string transactionName = "参考风格生成";
};

/// 自动剪辑结果统计。
struct AutoEditResult {
    std::size_t clipsInserted = 0;
    std::size_t effectsAdded = 0;
    std::size_t transitionsAdded = 0;
    std::vector<std::string> warnings;
    Result result;
};

/// 在空轨道上执行自动剪辑：
///  - 按 suggestCuts 的建议切割用户素材并插入时间轴；
///  - applyColor 时给每个片段加参考色调特效；
///  - applyTransitions 时在相邻片段间添加转场（需间隙支持）；
///  - 全程包在一个撤销事务里（一次撤销可整体回退）。
///
/// 前置条件：目标轨道已存在且为空（非空则拒绝，避免破坏用户已有剪辑）。
AutoEditResult applyAutoEdit(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                             TrackId trackId, const StyleProfile& profile,
                             const std::vector<MaterialSegment>& material,
                             const AutoEditOptions& options = {});

}  // namespace core
}  // namespace nairoclip
