#pragma once
// ============================================================================
// 契约层 · 异步任务模型
// ----------------------------------------------------------------------------
// 依据 03 §3 Task API 与 §4.3：耗时操作必须有
//   taskId / 类型 / 状态 / 进度 / 取消 / 错误 / 完成事件。
//
// 覆盖范围：媒体导入、缩略图、代理生成、AI 分析、导出。
// 与既有 ExportQueue 的关系：ExportQueue 是导出专用队列，本类是**通用**
// 任务登记表；后续把 ExportQueue 的状态回填到这里即可（P3 迁移）。
// ============================================================================

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {
namespace contract {

enum class TaskState {
    Queued = 0,
    Running,
    Completed,
    Failed,
    Cancelled,
};

const char* taskStateName(TaskState state);
bool taskStateFromName(const std::string& name, TaskState* out);
/// 终态（Completed/Failed/Cancelled）不可再迁移。
bool isTerminal(TaskState state);

/// 任务超时配置。
///
/// 心跳超时与总执行超时必须**分开管理**（T2.2 决策）：
///   · heartbeatTimeoutMs：多久没收到真实进度就判定卡死；
///   · totalTimeoutMs：任务总耗时上限（0 = 不限）。
/// 两者语义不同：长任务可以长时间运行，但**不允许**长时间无进度。
struct TaskConfig {
    std::int32_t heartbeatTimeoutMs = 60000;  ///< 默认 60s；快速任务可传 15000
    std::int64_t totalTimeoutMs = 0;          ///< 0 = 不限制
};

struct TaskInfo {
    std::string taskId;
    std::string type;              ///< 如 "import"、"thumbnail"、"proxy"、"ai"、"export"
    TaskState state = TaskState::Queued;
    double progress = 0.0;         ///< [0,1]
    std::string error;             ///< 失败原因（含稳定码时格式为 NC-... 前缀）
    std::int64_t createdAtMs = 0;
    bool cancelRequested = false;  ///< 取消请求已发出，等待任务自行收敛

    // ---- T2.2 扩展 ----
    std::string stageNote;         ///< 当前阶段说明（真实进度来源，可空）
    std::string resultType;        ///< 结果类型标签，如 "ai.matting.mask"
    std::string resultPayload;     ///< 结果载荷（≤ kMaxResultBytes）
    TaskConfig config;
    std::int64_t startedAtMs = 0;
    std::int64_t lastHeartbeatMs = 0;
    bool timedOut = false;         ///< 是否被判超时（区分「失败」与「卡死」）
    bool retryable = false;        ///< 失败是否可重试（供 UI 决定是否显示「重试」）
    /// 是否已获得过真实时钟。不能拿 startedAtMs==0 当"无时钟"标记——
    /// 0 是合法的毫秒时间戳（epoch），用它当哨兵会让超时永远不触发。
    bool hasClock = false;
};

/// 结果载荷上限 1MB。超过即判任务失败，避免无界内存占用。
inline constexpr std::size_t kMaxResultBytes = 1024 * 1024;

/// 任务登记表。非线程安全，由 Engine 线程独占。
class TaskRegistry {
public:
    /// 创建任务，返回 taskId。type 非法时返回空串。
    std::string create(const std::string& type, std::int64_t createdAtMs = 0);

    /// 创建任务（带超时配置）；nowMs 同时作为创建时间与首个心跳基准。
    std::string createEx(const std::string& type, const TaskConfig& config, std::int64_t nowMs);

    bool start(const std::string& taskId);
    /// 开始任务并记录 startedAtMs。nowMs = 0 表示调用方不提供时钟（禁用超时判定）。
    bool start(const std::string& taskId, std::int64_t nowMs);

    /// 真实进度上报（心跳）。progress 单调不减；stageNote 为空则保留原值。
    /// **禁止用定时器伪造心跳**：调用方必须来自真实进度或阶段切换。
    bool heartbeat(const std::string& taskId, double progress, const std::string& stageNote,
                   std::int64_t nowMs);

    /// 完成并写入结果。结果超过 kMaxResultBytes 时任务转为 Failed（NC-SERVICE-RESULT_TOO_LARGE）。
    bool completeWithResult(const std::string& taskId, std::string resultType,
                            std::string payload, std::int64_t nowMs = 0);

    /// 看门狗：推进时钟，把心跳超时/总超时的任务判为 Failed。
    /// 返回本次被判超时的任务数。不创建线程——完全由调用方驱动。
    std::size_t tick(std::int64_t nowMs);
    /// 设置进度；越界会被钳制到 [0,1]。
    /// 已进入终态时返回 false（不允许终态回退）。
    bool setProgress(const std::string& taskId, double progress);
    bool complete(const std::string& taskId);
    bool fail(const std::string& taskId, std::string error, bool retryable = true);
    /// 请求取消：把状态置为 Cancelled 并打取消标记。
    /// 已完成/已失败的任务返回 false。
    bool cancel(const std::string& taskId);

    bool isCancelRequested(const std::string& taskId) const;

    /// 查询。不存在返回 nullptr（返回指针在下次修改前有效）。
    const TaskInfo* find(const std::string& taskId) const;

    bool remove(const std::string& taskId);

    std::size_t size() const { return tasks_.size(); }
    std::size_t countInState(TaskState state) const;

    /// 快照，按 taskId 字典序（便于测试与日志稳定）。
    std::vector<TaskInfo> snapshot() const;

private:
    std::map<std::string, TaskInfo> tasks_;
    std::uint64_t nextSeq_ = 1;
};

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
