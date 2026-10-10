#include "nairoclip/core/export_queue.h"

#include <algorithm>
#include <cctype>

namespace nairoclip {
namespace core {

// =============================================================================
// 状态与名称
// =============================================================================

const char* exportJobStateName(ExportJobState state) {
    switch (state) {
        case ExportJobState::Queued: return "queued";
        case ExportJobState::Running: return "running";
        case ExportJobState::Paused: return "paused";
        case ExportJobState::Completed: return "completed";
        case ExportJobState::Failed: return "failed";
        case ExportJobState::Cancelled: return "cancelled";
    }
    return "queued";
}

bool exportJobStateFromString(const char* text, ExportJobState& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s == "queued" || s == "waiting") { out = ExportJobState::Queued; return true; }
    if (s == "running" || s == "exporting") { out = ExportJobState::Running; return true; }
    if (s == "paused") { out = ExportJobState::Paused; return true; }
    if (s == "completed" || s == "done") { out = ExportJobState::Completed; return true; }
    if (s == "failed") { out = ExportJobState::Failed; return true; }
    if (s == "cancelled" || s == "canceled") { out = ExportJobState::Cancelled; return true; }
    return false;
}

bool exportJobTransitionAllowed(ExportJobState from, ExportJobState to) {
    if (from == to) {
        return true;  // 幂等（重复通知同一状态不算冲突）
    }
    switch (from) {
        case ExportJobState::Queued:
            return to == ExportJobState::Running || to == ExportJobState::Paused ||
                   to == ExportJobState::Cancelled;
        case ExportJobState::Running:
            return to == ExportJobState::Paused || to == ExportJobState::Completed ||
                   to == ExportJobState::Failed || to == ExportJobState::Cancelled;
        case ExportJobState::Paused:
            return to == ExportJobState::Running || to == ExportJobState::Cancelled;
        case ExportJobState::Completed:
        case ExportJobState::Failed:
        case ExportJobState::Cancelled:
            return false;  // 终态
    }
    return false;
}

// =============================================================================
// 调度策略
// =============================================================================

ExportQueuePolicy exportQueuePolicy(PerformanceMode mode, int deviceConcurrency) {
    ExportQueuePolicy policy;
    const int concurrency = deviceConcurrency > 0 ? deviceConcurrency : 2;
    switch (mode) {
        case PerformanceMode::Turbo:
            policy.maxConcurrent = 1;
            policy.pauseEditing = true;
            policy.description = "极速模式：全速导出（编辑暂停），单任务独占";
            break;
        case PerformanceMode::Balanced:
            policy.maxConcurrent = std::max(1, std::min(2, concurrency));
            policy.pauseEditing = false;
            policy.description = "平衡模式：可继续编辑其他项目，最多并行 " +
                                 std::to_string(policy.maxConcurrent) + " 个导出";
            break;
        case PerformanceMode::Eco:
            policy.maxConcurrent = 1;
            policy.pauseEditing = false;
            policy.description = "省电模式：降速静默导出（单任务，减少发热与耗电）";
            break;
    }
    return policy;
}

// =============================================================================
// 队列
// =============================================================================

ExportQueueResult ExportQueue::enqueue(const std::string& name, const std::string& outputPath,
                                       ExportPresetId preset, PerformanceMode mode,
                                       const Entitlement& entitlement, std::int64_t nowUnix,
                                       bool includeAudio, bool attachAiLabel,
                                       const std::string& aiLabelText) {
    ExportQueueResult out;
    if (name.empty() || outputPath.empty()) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "任务名称与输出路径不能为空");
        return out;
    }
    // 权益限额（Free 1 / Pro 5；活动任务计入，终态不占用额度）
    const int limit = entitlement.maxExportQueue();
    if (limit > 0 && static_cast<int>(activeCount()) >= limit) {
        out.result = Result::fail(
            ErrorCode::Unsupported,
            "导出队列已满（当前权益上限 " + std::to_string(limit) +
                " 个任务）；Pro 会员最多可同时排队 5 个任务");
        return out;
    }
    ExportJob job;
    job.id = nextId_++;
    job.name = name;
    job.outputPath = outputPath;
    job.preset = preset;
    job.mode = mode;
    job.includeAudio = includeAudio;
    job.attachAiLabel = attachAiLabel;
    if (!aiLabelText.empty()) {
        job.aiLabelText = aiLabelText;
    }
    job.state = ExportJobState::Queued;
    job.sequence = nextSequence_++;
    job.enqueuedAtUnix = nowUnix;
    out.jobId = job.id;
    out.result = Result::ok();
    jobs_.push_back(std::move(job));
    return out;
}

const ExportJob* ExportQueue::find(std::uint64_t jobId) const {
    for (const auto& job : jobs_) {
        if (job.id == jobId) {
            return &job;
        }
    }
    return nullptr;
}

ExportJob* ExportQueue::find(std::uint64_t jobId) { return findMutable(jobId); }

ExportJob* ExportQueue::findMutable(std::uint64_t jobId) {
    for (auto& job : jobs_) {
        if (job.id == jobId) {
            return &job;
        }
    }
    return nullptr;
}

std::size_t ExportQueue::queuedCount() const {
    std::size_t count = 0;
    for (const auto& job : jobs_) {
        if (job.state == ExportJobState::Queued) {
            ++count;
        }
    }
    return count;
}

std::size_t ExportQueue::runningCount() const {
    std::size_t count = 0;
    for (const auto& job : jobs_) {
        if (job.state == ExportJobState::Running) {
            ++count;
        }
    }
    return count;
}

std::size_t ExportQueue::pausedCount() const {
    std::size_t count = 0;
    for (const auto& job : jobs_) {
        if (job.state == ExportJobState::Paused) {
            ++count;
        }
    }
    return count;
}

std::size_t ExportQueue::activeCount() const {
    std::size_t count = 0;
    for (const auto& job : jobs_) {
        if (job.isActive()) {
            ++count;
        }
    }
    return count;
}

std::uint64_t ExportQueue::pollNext(const ExportQueuePolicy& policy) const {
    const int maxConcurrent = policy.maxConcurrent > 0 ? policy.maxConcurrent : 1;
    if (static_cast<int>(runningCount()) >= maxConcurrent) {
        return 0;  // 并发上限
    }
    // 按入队顺序（sequence 已按入队分配且单调）取首个等待中任务
    const ExportJob* candidate = nullptr;
    for (const auto& job : jobs_) {
        if (job.state != ExportJobState::Queued) {
            continue;
        }
        if (candidate == nullptr || job.sequence < candidate->sequence) {
            candidate = &job;
        }
    }
    return candidate != nullptr ? candidate->id : 0;
}

ExportQueueResult ExportQueue::markStarted(std::uint64_t jobId, std::int64_t nowUnix) {
    ExportQueueResult out;
    out.jobId = jobId;
    ExportJob* job = findMutable(jobId);
    if (job == nullptr) {
        out.result = Result::fail(ErrorCode::NotFound, "任务不存在");
        return out;
    }
    if (job->state != ExportJobState::Queued) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "仅等待中的任务可以开始");
        return out;
    }
    if (!exportJobTransitionAllowed(job->state, ExportJobState::Running)) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "状态迁移非法");
        return out;
    }
    job->state = ExportJobState::Running;
    job->startedAtUnix = nowUnix;
    if (job->progress < 0.0) {
        job->progress = 0.0;
    }
    out.result = Result::ok();
    return out;
}

ExportQueueResult ExportQueue::updateProgress(std::uint64_t jobId, double progress) {
    ExportQueueResult out;
    out.jobId = jobId;
    ExportJob* job = findMutable(jobId);
    if (job == nullptr) {
        out.result = Result::fail(ErrorCode::NotFound, "任务不存在");
        return out;
    }
    if (job->state != ExportJobState::Running) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "仅导出中的任务可更新进度");
        return out;
    }
    double clamped = progress;
    if (clamped < 0.0) clamped = 0.0;
    if (clamped > 1.0) clamped = 1.0;
    // 单调不减（防止乱序上报导致进度回退）
    if (clamped > job->progress) {
        job->progress = clamped;
    }
    out.result = Result::ok();
    return out;
}

ExportQueueResult ExportQueue::markCompleted(std::uint64_t jobId, std::int64_t framesWritten,
                                             std::int64_t nowUnix, const std::string& note) {
    ExportQueueResult out;
    out.jobId = jobId;
    ExportJob* job = findMutable(jobId);
    if (job == nullptr) {
        out.result = Result::fail(ErrorCode::NotFound, "任务不存在");
        return out;
    }
    if (job->state != ExportJobState::Running) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "仅导出中的任务可标记完成");
        return out;
    }
    job->state = ExportJobState::Completed;
    job->progress = 1.0;
    job->framesWritten = framesWritten;
    job->finishedAtUnix = nowUnix;
    if (!note.empty()) {
        job->note = note;
    }
    out.result = Result::ok();
    return out;
}

ExportQueueResult ExportQueue::markFailed(std::uint64_t jobId, const std::string& reason,
                                          std::int64_t nowUnix) {
    ExportQueueResult out;
    out.jobId = jobId;
    ExportJob* job = findMutable(jobId);
    if (job == nullptr) {
        out.result = Result::fail(ErrorCode::NotFound, "任务不存在");
        return out;
    }
    if (job->state != ExportJobState::Running && job->state != ExportJobState::Paused) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "仅导出中/已暂停的任务可标记失败");
        return out;
    }
    job->state = ExportJobState::Failed;
    job->finishedAtUnix = nowUnix;
    job->note = reason.empty() ? "导出失败" : reason;
    out.result = Result::ok();
    return out;
}

ExportQueueResult ExportQueue::pause(std::uint64_t jobId, std::int64_t nowUnix) {
    (void)nowUnix;
    ExportQueueResult out;
    out.jobId = jobId;
    ExportJob* job = findMutable(jobId);
    if (job == nullptr) {
        out.result = Result::fail(ErrorCode::NotFound, "任务不存在");
        return out;
    }
    if (job->state != ExportJobState::Queued && job->state != ExportJobState::Running) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "仅等待中/导出中的任务可暂停");
        return out;
    }
    const bool wasRunning = job->state == ExportJobState::Running;
    job->state = ExportJobState::Paused;
    if (wasRunning) {
        // 运行中的暂停说明（平台层停止喂帧、写入器 abort 清理半成品）
        job->note = "已暂停；继续将从头重新导出（视频流不支持断点续写）";
    }
    out.result = Result::ok();
    return out;
}

ExportQueueResult ExportQueue::resume(std::uint64_t jobId, std::int64_t nowUnix) {
    ExportQueueResult out;
    out.jobId = jobId;
    ExportJob* job = findMutable(jobId);
    if (job == nullptr) {
        out.result = Result::fail(ErrorCode::NotFound, "任务不存在");
        return out;
    }
    if (job->state != ExportJobState::Paused) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "仅已暂停的任务可继续");
        return out;
    }
    job->state = ExportJobState::Running;
    job->startedAtUnix = nowUnix;
    out.result = Result::ok();
    return out;
}

ExportQueueResult ExportQueue::remove(std::uint64_t jobId, std::int64_t nowUnix) {
    ExportQueueResult out;
    out.jobId = jobId;
    const auto it = std::find_if(jobs_.begin(), jobs_.end(),
                                 [jobId](const ExportJob& j) { return j.id == jobId; });
    if (it == jobs_.end()) {
        out.result = Result::fail(ErrorCode::NotFound, "任务不存在");
        return out;
    }
    // 活动态：先转 Cancelled（供平台层识别需要停止工作线程），再移除
    if (it->isActive()) {
        it->state = ExportJobState::Cancelled;
        it->finishedAtUnix = nowUnix;
        it->note = "已取消";
    }
    jobs_.erase(it);
    out.result = Result::ok();
    return out;
}

void ExportQueue::cancelAll(std::int64_t nowUnix) {
    for (auto& job : jobs_) {
        if (job.isActive()) {
            job.state = ExportJobState::Cancelled;
            job.finishedAtUnix = nowUnix;
            job.note = "已取消（应用退出）";
        }
    }
}

void ExportQueue::clear() {
    jobs_.clear();
    nextId_ = 1;
    nextSequence_ = 1;
}

}  // namespace core
}  // namespace nairoclip
