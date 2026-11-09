#include "nairoclip/core/contract/task.h"

#include <algorithm>

namespace nairoclip {
namespace core {
namespace contract {
namespace {

struct StateInfo {
    TaskState state;
    const char* name;
};

const StateInfo kStates[] = {
    {TaskState::Queued, "queued"},       {TaskState::Running, "running"},
    {TaskState::Completed, "completed"}, {TaskState::Failed, "failed"},
    {TaskState::Cancelled, "cancelled"},
};

/// taskType 允许小写字母/数字/下划线/点，1..32。
bool isValidTaskType(const std::string& type) {
    if (type.empty() || type.size() > 32) return false;
    for (char c : type) {
        const bool lower = c >= 'a' && c <= 'z';
        const bool digit = c >= '0' && c <= '9';
        if (!lower && !digit && c != '_' && c != '.') return false;
    }
    return true;
}

}  // namespace

const char* taskStateName(TaskState state) {
    for (const StateInfo& s : kStates) {
        if (s.state == state) return s.name;
    }
    return kStates[0].name;
}

bool taskStateFromName(const std::string& name, TaskState* out) {
    for (const StateInfo& s : kStates) {
        if (name == s.name) {
            if (out != nullptr) *out = s.state;
            return true;
        }
    }
    return false;
}

bool isTerminal(TaskState state) {
    return state == TaskState::Completed || state == TaskState::Failed ||
           state == TaskState::Cancelled;
}

std::string TaskRegistry::create(const std::string& type, std::int64_t createdAtMs) {
    TaskConfig config;
    return createEx(type, config, createdAtMs);
}

std::string TaskRegistry::createEx(const std::string& type, const TaskConfig& config,
                                   std::int64_t nowMs) {
    if (!isValidTaskType(type)) return std::string();
    TaskInfo info;
    info.taskId = "task-" + std::to_string(nextSeq_++);
    info.type = type;
    info.state = TaskState::Queued;
    info.progress = 0.0;
    info.createdAtMs = nowMs;
    info.config = config;
    // 心跳阈值必须为正；非法值回落到默认 60s，避免「永不超时」被误用。
    if (info.config.heartbeatTimeoutMs <= 0) info.config.heartbeatTimeoutMs = 60000;
    const std::string id = info.taskId;
    tasks_[id] = std::move(info);
    return id;
}

bool TaskRegistry::start(const std::string& taskId) { return start(taskId, 0); }

bool TaskRegistry::start(const std::string& taskId, std::int64_t nowMs) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    it->second.state = TaskState::Running;
    if (nowMs > 0) {
        it->second.startedAtMs = nowMs;
        // 首个心跳基准就是启动时刻：否则「启动后再无进度」永远触发不了超时。
        it->second.lastHeartbeatMs = nowMs;
        it->second.hasClock = true;
    }
    return true;
}

bool TaskRegistry::setProgress(const std::string& taskId, double progress) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;
    it->second.progress = progress;
    // 首次上报进度自动置为 Running：调用方常常忘记显式 start，
    // 若这里不兜底，UI 会看到「进度 80% 但状态仍是 queued」的矛盾。
    if (it->second.state == TaskState::Queued) it->second.state = TaskState::Running;
    return true;
}

bool TaskRegistry::heartbeat(const std::string& taskId, double progress,
                             const std::string& stageNote, std::int64_t nowMs) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;
    // 单调不减：进度回跳会让进度条倒退，视为无效上报但不报错。
    if (progress > it->second.progress) it->second.progress = progress;
    if (!stageNote.empty()) it->second.stageNote = stageNote;
    if (it->second.state == TaskState::Queued) {
        it->second.state = TaskState::Running;
        if (!it->second.hasClock) it->second.startedAtMs = nowMs;
    }
    if (nowMs > 0) {
        it->second.lastHeartbeatMs = nowMs;
        it->second.hasClock = true;
    }
    return true;
}

bool TaskRegistry::completeWithResult(const std::string& taskId, std::string resultType,
                                      std::string payload, std::int64_t nowMs) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    if (payload.size() > kMaxResultBytes) {
        it->second.state = TaskState::Failed;
        it->second.error = "NC-SERVICE-RESULT_TOO_LARGE: 结果超过 1MB 上限（" +
                           std::to_string(payload.size()) + " 字节）";
        return false;
    }
    it->second.state = TaskState::Completed;
    it->second.progress = 1.0;
    it->second.resultType = std::move(resultType);
    it->second.resultPayload = std::move(payload);
    it->second.error.clear();
    if (nowMs > 0) it->second.lastHeartbeatMs = nowMs;
    return true;
}

std::size_t TaskRegistry::tick(std::int64_t nowMs) {
    if (nowMs <= 0) return 0;  // 调用方未提供时钟 → 不做超时判定
    std::size_t timedOut = 0;
    for (auto& kv : tasks_) {
        TaskInfo& t = kv.second;
        if (t.state != TaskState::Running) continue;
        // 首次 tick 时为「无时钟启动」的任务补一个基准：
        // 旧接口 nc_ai_task_begin 拿不到 now_ms，若永远不补基准，
        // 它的超时就永远不触发（RT-1 卡死依旧）。从第一次 tick 开始计时。
        if (!t.hasClock) {
            t.hasClock = true;
            t.startedAtMs = nowMs;
            t.lastHeartbeatMs = nowMs;
            continue;
        }
        bool hit = false;
        // 总超时优先：它是任务的硬上限。
        if (t.config.totalTimeoutMs > 0 && nowMs - t.startedAtMs > t.config.totalTimeoutMs) {
            t.error = "NC-SERVICE-TASK_TIMEOUT: 任务总耗时超过上限";
            hit = true;
        } else if (t.config.heartbeatTimeoutMs > 0 &&
                   nowMs - t.lastHeartbeatMs > t.config.heartbeatTimeoutMs) {
            t.error = "NC-SERVICE-TASK_HEARTBEAT_TIMEOUT: 超过 " +
                      std::to_string(t.config.heartbeatTimeoutMs) + "ms 未收到真实进度";
            hit = true;
        }
        if (hit) {
            t.state = TaskState::Failed;
            t.timedOut = true;
            ++timedOut;
        }
    }
    return timedOut;
}

bool TaskRegistry::complete(const std::string& taskId) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    it->second.state = TaskState::Completed;
    it->second.progress = 1.0;
    return true;
}

bool TaskRegistry::fail(const std::string& taskId, std::string error, bool retryable) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    it->second.state = TaskState::Failed;
    it->second.error = std::move(error);
    it->second.retryable = retryable;
    return true;
}

bool TaskRegistry::cancel(const std::string& taskId) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    it->second.state = TaskState::Cancelled;
    it->second.cancelRequested = true;
    return true;
}

bool TaskRegistry::isCancelRequested(const std::string& taskId) const {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    return it->second.cancelRequested;
}

const TaskInfo* TaskRegistry::find(const std::string& taskId) const {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return nullptr;
    return &it->second;
}

bool TaskRegistry::remove(const std::string& taskId) {
    return tasks_.erase(taskId) > 0;
}

std::size_t TaskRegistry::countInState(TaskState state) const {
    std::size_t n = 0;
    for (const auto& kv : tasks_) {
        if (kv.second.state == state) ++n;
    }
    return n;
}

std::vector<TaskInfo> TaskRegistry::snapshot() const {
    std::vector<TaskInfo> out;
    out.reserve(tasks_.size());
    for (const auto& kv : tasks_) out.push_back(kv.second);
    return out;
}

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
