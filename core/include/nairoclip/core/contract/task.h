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

struct TaskInfo {
    std::string taskId;
    std::string type;              ///< 如 "import"、"thumbnail"、"proxy"、"ai"、"export"
    TaskState state = TaskState::Queued;
    double progress = 0.0;         ///< [0,1]
    std::string error;             ///< 失败原因（含稳定码时格式为 NC-... 前缀）
    std::int64_t createdAtMs = 0;
    bool cancelRequested = false;  ///< 取消请求已发出，等待任务自行收敛
};

/// 任务登记表。非线程安全，由 Engine 线程独占。
class TaskRegistry {
public:
    /// 创建任务，返回 taskId。type 非法时返回空串。
    std::string create(const std::string& type, std::int64_t createdAtMs = 0);

    bool start(const std::string& taskId);
    /// 设置进度；越界会被钳制到 [0,1]。
    /// 已进入终态时返回 false（不允许终态回退）。
    bool setProgress(const std::string& taskId, double progress);
    bool complete(const std::string& taskId);
    bool fail(const std::string& taskId, std::string error);
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
