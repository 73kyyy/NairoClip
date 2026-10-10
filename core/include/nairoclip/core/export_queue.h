#pragma once
// =============================================================================
// 导出队列管理（Export Queue）—— 文档第 6 部分「二、导出队列管理」
// =============================================================================
// 产品对应（原文）：
//   「导出队列 (2个等待中)   ← 免费版上限1个，Pro版最多5个
//      1. 春游Vlog    ▓▓▓░░ 60%
//      2. 产品宣传片   等待中
//         [⏸暂停] [✕移除]
//      完成后: ✅系统通知」
//
// 文档第 4 部分「四、后台导出，可继续编辑」：
//   平衡模式（Balanced）——用户可切换到其他项目继续工作，导出完成后系统通知提醒。
//
// 本模块定位（纯状态机 + 调度决策，零线程/文件系统依赖）：
//   · 队列结构：任务列表（id/名称/路径/预设/性能模式/状态/进度）；
//   · 状态机：等待 → 运行 → 完成/失败/已暂停/已取消（合法迁移约束）；
//   · 并发策略：性能模式决定「允许的同时运行数」（Turbo=独占、Balanced=可并行、
//     Eco=单任务降速）；由平台层按 pollNext 调度真实执行；
//   · 权益限额：Free 1 / Pro 5（Entitlement::maxExportQueue，单一事实来源）；
//   · 暂停/继续/取消/移除：真实作用于任务状态。
//
// 职责边界（与全项目一致）：
//   · core 不做真实编码（由 MediaWriter 的职责，平台层注入）；
//   · 本模块给出「下一个该跑哪个任务」的调度决策与状态记录；
//   · 平台层在工作线程调用 runExport 真实执行，并把进度/结果回填队列。
//
// 暂停语义（诚实实现，不做假功能）：
//   · 等待中任务暂停 → 不会被 pollNext 选中启动；
//   · 运行中任务暂停 → 平台层据任务状态停止喂帧（runExport 取消路径），
//     当前输出文件由写入器 abort() 清理；「继续」将从头重跑并在 note 中注明
//     （H.264 流不支持断点续写，这是编码格式的客观限制）。
//
// 线程约束（与 nc_editor 单句柄约定一致）：
//   队列实例不保证线程安全；平台层必须串行化访问（UI 线程改状态、
//   工作线程上报进度时经同一条串行通道，如互斥锁或任务投递）。
//
// 持久化：队列为运行时状态（不写入 .nclip）；应用重启后清空。

#include "nairoclip/core/entitlement.h"
#include "nairoclip/core/export_preset.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 队列任务状态。
enum class ExportJobState {
    Queued = 0,     ///< 等待中（「等待中」）
    Running = 1,    ///< 导出中（「▓▓▓░░ 60%」）
    Paused = 2,     ///< 已暂停（[▶继续]）
    Completed = 3,  ///< 已完成（「✅」）
    Failed = 4,     ///< 失败（note 记录原因）
    Cancelled = 5,  ///< 已取消（用户取消运行中任务）
};

/// 状态名称（"queued"/"running"/"paused"/"completed"/"failed"/"cancelled"）。
const char* exportJobStateName(ExportJobState state);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool exportJobStateFromString(const char* text, ExportJobState& out);

/// 状态迁移是否合法（状态机不变量；非法迁移不得改变状态）。
/// - Queued → Running / Cancelled；Paused（暂停等待中的任务）；
/// - Running → Paused / Completed / Failed / Cancelled；
/// - Paused → Running / Cancelled；
/// - Completed / Failed / Cancelled 为终态（不可再迁移，仅可移除）。
bool exportJobTransitionAllowed(ExportJobState from, ExportJobState to);

/// 单个导出任务。
struct ExportJob {
    /// 任务 ID（队列内唯一；由队列分配，从 1 起）。
    std::uint64_t id = 0;
    /// 展示名（如「春游Vlog」；供队列 UI 列表）。
    std::string name;
    /// 输出路径（UTF-8）。
    std::string outputPath;
    /// 导出预设（决定分辨率/帧率/画质；Custom = 沿用序列设置）。
    ExportPresetId preset = ExportPresetId::Custom;
    /// 性能模式（决定调度与编码参数）。
    PerformanceMode mode = PerformanceMode::Balanced;
    /// 是否包含音频。
    bool includeAudio = true;
    /// 是否附加 AI 内容标识（合规开关；使用 AI 成片/TTS 时必须为 true）。
    bool attachAiLabel = false;
    /// AI 标识文案（attachAiLabel 时生效）。
    std::string aiLabelText = "AI 生成";
    /// 当前状态。
    ExportJobState state = ExportJobState::Queued;
    /// 进度（0..1；仅 Running/Completed 有意义）。
    double progress = 0.0;
    /// 队列序号（展示顺序；创建时分配，移除后不重排）。
    std::int64_t sequence = 0;
    /// 入队时间（Unix 秒）。
    std::int64_t enqueuedAtUnix = 0;
    /// 开始/结束时间（Unix 秒；0 = 未发生）。
    std::int64_t startedAtUnix = 0;
    std::int64_t finishedAtUnix = 0;
    /// 输出帧数（完成后回填；供「✅」详情）。
    std::int64_t framesWritten = 0;
    /// 备注（失败原因/门控降级说明/续跑说明等，可直接展示）。
    std::string note;

    /// 是否处于活动过程（等待/运行/暂停）。
    bool isActive() const {
        return state == ExportJobState::Queued || state == ExportJobState::Running ||
               state == ExportJobState::Paused;
    }
    /// 是否终态。
    bool isTerminal() const {
        return state == ExportJobState::Completed || state == ExportJobState::Failed ||
               state == ExportJobState::Cancelled;
    }
};

/// 队列调度策略（由性能模式派生；平台层按此决定并发）。
struct ExportQueuePolicy {
    /// 允许同时运行的任务数。
    int maxConcurrent = 1;
    /// 是否应暂停编辑交互（Turbo = true；文档「极速模式(暂停编辑，最快)」）。
    bool pauseEditing = false;
    /// 说明文案（供 UI 展示）。
    std::string description;
};

/// 由性能模式与设备并行度推导调度策略。
/// - Turbo：1 个（全速独占，暂停编辑交互）；
/// - Balanced：min(2, 设备并行度)（可继续编辑其他项目）；
/// - Eco：1 个（单线程降速静默，减少发热与电量消耗）。
ExportQueuePolicy exportQueuePolicy(PerformanceMode mode, int deviceConcurrency = 0);

/// 队列操作结果。
struct ExportQueueResult {
    Result result;
    /// 受影响的任务 ID（成功时有效）。
    std::uint64_t jobId = 0;
};

/// 导出队列（运行时状态机；不持久化）。
class ExportQueue {
public:
    /// 入队（分配 ID 与队列序号；state = Queued）。
    /// - 前置条件：名称非空、输出路径非空；
    /// - entitlement/nowUnix：限额校验（活动任务数达到 maxExportQueue 时拒绝，
    ///   返回 fail 与提示文案，供 UI 弹付费墙；文档 6.2 免费版上限 1 个）。
    ExportQueueResult enqueue(const std::string& name, const std::string& outputPath,
                              ExportPresetId preset, PerformanceMode mode,
                              const Entitlement& entitlement, std::int64_t nowUnix,
                              bool includeAudio = true, bool attachAiLabel = false,
                              const std::string& aiLabelText = "AI 生成");

    /// 查询任务（未找到返回 nullptr）。
    const ExportJob* find(std::uint64_t jobId) const;
    ExportJob* find(std::uint64_t jobId);

    /// 全部任务（按入队顺序）。
    const std::vector<ExportJob>& jobs() const { return jobs_; }
    /// 任务总数。
    std::size_t size() const { return jobs_.size(); }
    /// 等待中数量（「导出队列 (2个等待中)」）。
    std::size_t queuedCount() const;
    /// 运行中数量。
    std::size_t runningCount() const;
    /// 已暂停数量。
    std::size_t pausedCount() const;
    /// 活动任务数（等待 + 运行 + 暂停；限额校验依据）。
    std::size_t activeCount() const;

    /// 调度决策：返回下一个应启动执行的任务 ID（无则 0）。
    /// - 按入队顺序取首个 Queued；
    /// - 已有 maxConcurrent 个运行中的任务时返回 0（并发上限）；
    /// - policy.maxConcurrent <= 0 视为 1。
    std::uint64_t pollNext(const ExportQueuePolicy& policy) const;

    /// 标记任务开始运行（Queued → Running；记录开始时间）。
    /// - 同一任务重复调用返回失败（状态机约束）。
    ExportQueueResult markStarted(std::uint64_t jobId, std::int64_t nowUnix);
    /// 回填进度（仅 Running；钳制到 [0,1] 且单调不减）。
    ExportQueueResult updateProgress(std::uint64_t jobId, double progress);
    /// 标记完成（Running → Completed；记录帧数与备注）。
    ExportQueueResult markCompleted(std::uint64_t jobId, std::int64_t framesWritten,
                                    std::int64_t nowUnix, const std::string& note = "");
    /// 标记失败（Running/Paused → Failed；记录原因）。
    ExportQueueResult markFailed(std::uint64_t jobId, const std::string& reason,
                                 std::int64_t nowUnix);
    /// 暂停（Queued/Running → Paused；进度保留）。
    /// 运行中任务的暂停意味着平台层应停止喂帧（写入器 abort 清理半成品）。
    ExportQueueResult pause(std::uint64_t jobId, std::int64_t nowUnix);
    /// 继续（Paused → Running；记录续跑说明）。
    ExportQueueResult resume(std::uint64_t jobId, std::int64_t nowUnix);
    /// 移除任务（终态直接移除；活动态先转 Cancelled 再移除）。
    /// 返回结果中的 jobId 为被移除任务 ID；调用方需停止对应工作线程。
    ExportQueueResult remove(std::uint64_t jobId, std::int64_t nowUnix);

    /// 停止所有活动任务（应用退出时调用；转为 Cancelled，保留记录供展示）。
    void cancelAll(std::int64_t nowUnix);
    /// 清空全部任务（含终态记录）。
    void clear();

private:
    ExportJob* findMutable(std::uint64_t jobId);

    std::vector<ExportJob> jobs_;
    std::uint64_t nextId_ = 1;
    std::int64_t nextSequence_ = 1;
};

}  // namespace core
}  // namespace nairoclip
