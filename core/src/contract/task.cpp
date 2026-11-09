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
    if (!isValidTaskType(type)) return std::string();
    TaskInfo info;
    info.taskId = "task-" + std::to_string(nextSeq_++);
    info.type = type;
    info.state = TaskState::Queued;
    info.progress = 0.0;
    info.createdAtMs = createdAtMs;
    const std::string id = info.taskId;
    tasks_[id] = std::move(info);
    return id;
}

bool TaskRegistry::start(const std::string& taskId) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    it->second.state = TaskState::Running;
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

bool TaskRegistry::complete(const std::string& taskId) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    it->second.state = TaskState::Completed;
    it->second.progress = 1.0;
    return true;
}

bool TaskRegistry::fail(const std::string& taskId, std::string error) {
    auto it = tasks_.find(taskId);
    if (it == tasks_.end()) return false;
    if (isTerminal(it->second.state)) return false;
    it->second.state = TaskState::Failed;
    it->second.error = std::move(error);
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
