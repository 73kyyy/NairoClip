#pragma once

#include "nairoclip/core/editor.h"
#include "nairoclip/core/transition_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 批量多选操作（Batch Operations）
// 文档第 3 部分「八、框选/多选批量操作」+ 第 2 部分「多选模式（长按触发）」
// =============================================================================
// 桌面端右键菜单：批量删除 / 批量应用转场 / 批量调整音量 / 对齐到同一起点 /
//                创建复合片段（已有 createCompoundClip）
// 移动端多选模式：批量删除 / 批量变速 / 打组（已有 createCompoundClip）
//
// 统一语义（全项目一致）：
//   · 一条撤销事务包裹全部修改（一次撤销整体回退）；
//   · 逐项独立校验：单项失败不阻断其余项，失败原因逐条回报；
//   · 事务外先做参数级校验（整体非法时不产生任何副作用）。

/// 批量操作目标项（可跨轨道）。
struct BatchItem {
    TrackId track;
    ClipId clip;
};

/// 批量操作结果。
struct BatchResult {
    std::size_t succeeded = 0;   ///< 成功项数
    std::size_t failed = 0;      ///< 失败/跳过的项数
    /// 逐项说明（失败原因 / 跳过原因 / 参数钳制说明；可直接展示）。
    std::vector<std::string> messages;
    /// 整体结果（参数级校验失败时非 Ok，此时零副作用）。
    Result result;

    bool valid() const { return result.isOk(); }
    /// 是否全部成功。
    bool allSucceeded() const { return result.isOk() && failed == 0; }
};

/// ---------------------------------------------------------------------------
/// 批量删除
/// ---------------------------------------------------------------------------
/// - ripple=false：仅删除选中片段（保留空隙，其余片段不动）；
/// - ripple=true：按轨道分组执行波纹删除（同轨道内后续片段整体前移）。
/// 不存在的片段计入 failed 并说明（不阻断其余项）。
BatchResult batchDelete(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                        const std::vector<BatchItem>& items, bool ripple = false,
                        const std::string& transactionName = "批量删除");

/// ---------------------------------------------------------------------------
/// 批量应用转场（同一轨道上的相邻选中片段之间）
/// ---------------------------------------------------------------------------
struct BatchTransitionOptions {
    TransitionType type = TransitionType::CrossDissolve;
    /// 目标转场时长（相邻两种情形会调整，见下）。
    Time duration = Rational::make(3, 10);  // 0.3s
    /// 间隙不足时：true = 缩短转场以适配可用间隙；false = 跳过该对（记入 failed）。
    bool allowShrink = true;
    /// 无间隙（相邻片段首尾相接）时：true = 后移后续片段以创造间隙（改变时间轴总时长）；
    /// false = 跳过并说明（默认，避免用户意外改变时长）。
    bool shiftToMakeRoom = false;
    /// 数量上限（0 = 不限制；与 maxTransitions 语义一致，防止一次加太多）。
    std::size_t maxCount = 0;
};

/// 仅在「选中且在该轨道上相邻」的片段对之间应用转场：
/// - 已存在转场的片段对跳过；引用缺失跳过；参数非法整体拒绝。
/// 成功数写入 succeeded，跳过原因逐条写入 messages。
BatchResult batchApplyTransitions(TimelineEditor& editor, Project& project,
                                 SequenceId sequenceId, TrackId trackId,
                                 const std::vector<ClipId>& clipIds,
                                 const BatchTransitionOptions& options = {},
                                 const std::string& transactionName = "批量应用转场");

/// ---------------------------------------------------------------------------
/// 批量调整音量（含淡入淡出）
/// ---------------------------------------------------------------------------
struct BatchAudioOptions {
    /// 是否修改增益。
    bool setGain = false;
    /// 目标增益（dB；合法范围 [-60, +12]）。
    double gainDb = 0.0;
    /// 是否修改淡入淡出。
    bool setFades = false;
    Time fadeIn;
    Time fadeOut;
    /// 越界处理：true = 钳制到合法范围并在 messages 说明；false = 该项失败。
    bool clampToRange = true;
};

/// 批量设置片段音频属性（淡入淡出超出片段时长时按比例缩放并说明）。
BatchResult batchSetAudio(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                          const std::vector<BatchItem>& items,
                          const BatchAudioOptions& options = {},
                          const std::string& transactionName = "批量调整音量");

/// ---------------------------------------------------------------------------
/// 批量变速
/// ---------------------------------------------------------------------------
/// 语义与单片段变速一致：保持源区间不变，时间轴时长 = 源时长 / 新速度。
/// 变速后与相邻片段重叠的项失败（逐项回报，不阻断其余项）。
BatchResult batchSetSpeed(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                          const std::vector<BatchItem>& items, const Rational& speed,
                          const std::string& transactionName = "批量变速");

/// ---------------------------------------------------------------------------
/// 对齐到同一起点（跨轨道）
/// ---------------------------------------------------------------------------
struct BatchAlignOptions {
    /// true = 使用显式目标起点；false = 自动取选中项中最早的起点。
    bool useExplicitTarget = false;
    Time targetStart;
};

/// 把选中片段全部移动到统一起点（跨轨道；同轨道内重叠会逐项失败并说明）。
BatchResult batchAlignStart(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                            const std::vector<BatchItem>& items,
                            const BatchAlignOptions& options = {},
                            const std::string& transactionName = "对齐到同一起点");

}  // namespace core
}  // namespace nairoclip
