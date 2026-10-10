// =============================================================================
// 导出队列测试（M2.24，文档第 6 部分「二、导出队列管理」+ 第 4 部分「后台导出」）
// =============================================================================
// 覆盖：
//   · 状态机：合法/非法迁移、终态约束；
//   · 调度：并发上限（Turbo/Balanced/Eco）、入队顺序、暂停不参与调度；
//   · 权益：Free 1 / Pro 5 限额（含活动任务计数语义）；
//   · 操作：暂停/继续/取消/移除、进度单调钳制、完成/失败回填；
//   · 边界：不存在任务、重复开始、乱序进度。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/export_queue.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

Entitlement freeTier() { return Entitlement{}; }

Entitlement proTier() {
    Entitlement e;
    e.tier = MembershipTier::Pro;
    return e;
}

}  // namespace

// ---------------------------------------------------------------------------
// 状态名称与迁移
// ---------------------------------------------------------------------------

NC_TEST(ExportQueue, StateNameRoundTrip) {
    NC_CHECK_EQ(std::string(exportJobStateName(ExportJobState::Queued)), std::string("queued"));
    NC_CHECK_EQ(std::string(exportJobStateName(ExportJobState::Running)), std::string("running"));
    NC_CHECK_EQ(std::string(exportJobStateName(ExportJobState::Paused)), std::string("paused"));
    NC_CHECK_EQ(std::string(exportJobStateName(ExportJobState::Completed)),
                std::string("completed"));
    NC_CHECK_EQ(std::string(exportJobStateName(ExportJobState::Failed)), std::string("failed"));
    NC_CHECK_EQ(std::string(exportJobStateName(ExportJobState::Cancelled)),
                std::string("cancelled"));
    for (const auto* name : {"queued", "running", "paused", "completed", "failed", "cancelled"}) {
        ExportJobState state;
        NC_CHECK(exportJobStateFromString(name, state));
    }
    ExportJobState state;
    NC_CHECK(!exportJobStateFromString("bogus", state));
    NC_CHECK(!exportJobStateFromString(nullptr, state));
}

NC_TEST(ExportQueue, TransitionRules) {
    // 合法迁移
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Queued, ExportJobState::Running));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Queued, ExportJobState::Cancelled));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Queued, ExportJobState::Paused));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Running, ExportJobState::Paused));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Running, ExportJobState::Completed));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Running, ExportJobState::Failed));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Running, ExportJobState::Cancelled));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Paused, ExportJobState::Running));
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Paused, ExportJobState::Cancelled));
    // 幂等（同状态不算冲突）
    NC_CHECK(exportJobTransitionAllowed(ExportJobState::Running, ExportJobState::Running));
    // 终态不可迁移
    NC_CHECK(!exportJobTransitionAllowed(ExportJobState::Completed, ExportJobState::Running));
    NC_CHECK(!exportJobTransitionAllowed(ExportJobState::Failed, ExportJobState::Running));
    NC_CHECK(!exportJobTransitionAllowed(ExportJobState::Cancelled, ExportJobState::Queued));
    // 非法
    NC_CHECK(!exportJobTransitionAllowed(ExportJobState::Queued, ExportJobState::Completed));
    NC_CHECK(!exportJobTransitionAllowed(ExportJobState::Paused, ExportJobState::Completed));
}

NC_TEST(ExportQueue, JobStateHelpers) {
    ExportJob job;
    job.state = ExportJobState::Queued;
    NC_CHECK(job.isActive());
    NC_CHECK(!job.isTerminal());
    job.state = ExportJobState::Running;
    NC_CHECK(job.isActive());
    job.state = ExportJobState::Paused;
    NC_CHECK(job.isActive());
    job.state = ExportJobState::Completed;
    NC_CHECK(!job.isActive());
    NC_CHECK(job.isTerminal());
    job.state = ExportJobState::Failed;
    NC_CHECK(job.isTerminal());
    job.state = ExportJobState::Cancelled;
    NC_CHECK(job.isTerminal());
}

// ---------------------------------------------------------------------------
// 调度策略
// ---------------------------------------------------------------------------

NC_TEST(ExportQueue, PolicyTurboExclusive) {
    const ExportQueuePolicy policy = exportQueuePolicy(PerformanceMode::Turbo, 8);
    NC_CHECK_EQ(policy.maxConcurrent, 1);
    NC_CHECK(policy.pauseEditing);
    NC_CHECK(!policy.description.empty());
}

NC_TEST(ExportQueue, PolicyBalancedParallel) {
    // 并发 = min(2, 设备并行度)
    NC_CHECK_EQ(exportQueuePolicy(PerformanceMode::Balanced, 8).maxConcurrent, 2);
    NC_CHECK_EQ(exportQueuePolicy(PerformanceMode::Balanced, 1).maxConcurrent, 1);
    NC_CHECK_EQ(exportQueuePolicy(PerformanceMode::Balanced, 0).maxConcurrent, 2);  // 未知按 2
    NC_CHECK(!exportQueuePolicy(PerformanceMode::Balanced, 4).pauseEditing);
}

NC_TEST(ExportQueue, PolicyEcoSingle) {
    const ExportQueuePolicy policy = exportQueuePolicy(PerformanceMode::Eco, 8);
    NC_CHECK_EQ(policy.maxConcurrent, 1);
    NC_CHECK(!policy.pauseEditing);
}

// ---------------------------------------------------------------------------
// 入队与限额
// ---------------------------------------------------------------------------

NC_TEST(ExportQueue, EnqueueAssignsIdsAndSequence) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Douyin,
                                 PerformanceMode::Balanced, freeTier(), 1000);
    NC_CHECK(a.result.isOk());
    NC_CHECK(a.jobId != 0);
    const ExportJob* job = queue.find(a.jobId);
    NC_CHECK(job != nullptr);
    NC_CHECK_EQ(job->name, std::string("A"));
    NC_CHECK_EQ(job->outputPath, std::string("/out/a.mp4"));
    NC_CHECK_EQ(job->sequence, std::int64_t(1));
    NC_CHECK_EQ(job->enqueuedAtUnix, std::int64_t(1000));
    NC_CHECK_EQ(job->state, ExportJobState::Queued);
}

NC_TEST(ExportQueue, EnqueueRejectsInvalidArguments) {
    ExportQueue queue;
    NC_CHECK(!queue.enqueue("", "/out/a.mp4", ExportPresetId::Custom, PerformanceMode::Balanced,
                            freeTier(), 1000)
                  .result.isOk());
    NC_CHECK(!queue.enqueue("A", "", ExportPresetId::Custom, PerformanceMode::Balanced, freeTier(),
                            1000)
                  .result.isOk());
    NC_CHECK_EQ(queue.size(), std::size_t(0));
}

NC_TEST(ExportQueue, FreeTierLimitOne) {
    ExportQueue queue;
    NC_CHECK(queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom, PerformanceMode::Balanced,
                           freeTier(), 1000)
                  .result.isOk());
    // 第二个被拒（免费版上限 1）
    const auto second = queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom,
                                      PerformanceMode::Balanced, freeTier(), 1001);
    NC_CHECK(!second.result.isOk());
    NC_CHECK(second.result.message.find("上限") != std::string::npos);
    NC_CHECK_EQ(queue.size(), std::size_t(1));
}

NC_TEST(ExportQueue, ProTierLimitFive) {
    ExportQueue queue;
    for (int i = 0; i < 5; ++i) {
        const auto r = queue.enqueue("J" + std::to_string(i), "/out/j.mp4",
                                     ExportPresetId::Custom, PerformanceMode::Balanced,
                                     proTier(), 1000);
        NC_CHECK(r.result.isOk());
    }
    // 第 6 个被拒
    NC_CHECK(!queue.enqueue("J6", "/out/j6.mp4", ExportPresetId::Custom,
                            PerformanceMode::Balanced, proTier(), 1000)
                  .result.isOk());
    NC_CHECK_EQ(queue.activeCount(), std::size_t(5));
}

NC_TEST(ExportQueue, TerminalJobsFreeQuota) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, freeTier(), 1000);
    NC_CHECK(queue.markStarted(a.jobId, 1001).result.isOk());
    NC_CHECK(queue.markCompleted(a.jobId, 100, 1002).result.isOk());
    // 终态不占额度 → 可入队新任务
    NC_CHECK(queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom, PerformanceMode::Balanced,
                           freeTier(), 1003)
                  .result.isOk());
    NC_CHECK_EQ(queue.activeCount(), std::size_t(1));
    NC_CHECK_EQ(queue.size(), std::size_t(2));
}

NC_TEST(ExportQueue, PausedJobStillOccupiesQuota) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, freeTier(), 1000);
    NC_CHECK(queue.pause(a.jobId, 1001).result.isOk());
    NC_CHECK_EQ(queue.pausedCount(), std::size_t(1));
    // 暂停中仍占额度
    NC_CHECK(!queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom,
                            PerformanceMode::Balanced, freeTier(), 1002)
                  .result.isOk());
}

// ---------------------------------------------------------------------------
// 调度
// ---------------------------------------------------------------------------

NC_TEST(ExportQueue, PollNextChoosesOldestQueued) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    const auto b = queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1001);
    // Turbo 策略（并发上限 1）：先取最早入队的 A
    const ExportQueuePolicy turbo = exportQueuePolicy(PerformanceMode::Turbo, 4);
    NC_CHECK_EQ(queue.pollNext(turbo), a.jobId);
    NC_CHECK(queue.markStarted(a.jobId, 1002).result.isOk());
    // A 运行中占用唯一并发位 → 不再调度
    NC_CHECK_EQ(queue.pollNext(turbo), std::uint64_t(0));
    // A 完成后轮到 B
    queue.markCompleted(a.jobId, 1, 1003);
    NC_CHECK_EQ(queue.pollNext(turbo), b.jobId);
}

NC_TEST(ExportQueue, PollNextRespectsConcurrency) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom, PerformanceMode::Balanced,
                  proTier(), 1001);
    const ExportQueuePolicy turbo = exportQueuePolicy(PerformanceMode::Turbo, 4);
    NC_CHECK(queue.markStarted(a.jobId, 1002).result.isOk());
    // Turbo：1 个运行中 → 不再调度
    NC_CHECK_EQ(queue.pollNext(turbo), std::uint64_t(0));
    // Balanced（并行 2）→ 可调度第二个
    const ExportQueuePolicy balanced = exportQueuePolicy(PerformanceMode::Balanced, 8);
    NC_CHECK(queue.pollNext(balanced) != 0);
}

NC_TEST(ExportQueue, PollNextSkipsPaused) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    const auto b = queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1001);
    // 暂停 A → 调度 B
    NC_CHECK(queue.pause(a.jobId, 1002).result.isOk());
    const ExportQueuePolicy policy = exportQueuePolicy(PerformanceMode::Turbo, 4);
    NC_CHECK_EQ(queue.pollNext(policy), b.jobId);
}

NC_TEST(ExportQueue, PollNextEmptyWhenAllTerminal) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    queue.markStarted(a.jobId, 1001);
    queue.markCompleted(a.jobId, 1, 1002);
    NC_CHECK_EQ(queue.pollNext(exportQueuePolicy(PerformanceMode::Turbo, 4)), std::uint64_t(0));
}

// ---------------------------------------------------------------------------
// 操作：进度/完成/失败/暂停/继续/移除
// ---------------------------------------------------------------------------

NC_TEST(ExportQueue, ProgressMonotonicAndClamped) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, freeTier(), 1000);
    // 未开始时不可更新
    NC_CHECK(!queue.updateProgress(a.jobId, 0.5).result.isOk());
    queue.markStarted(a.jobId, 1001);
    NC_CHECK(queue.updateProgress(a.jobId, 0.5).result.isOk());
    NC_CHECK(queue.updateProgress(a.jobId, 0.3).result.isOk());  // 回退被忽略
    NC_CHECK(queue.find(a.jobId)->progress > 0.49 && queue.find(a.jobId)->progress < 0.51);
    NC_CHECK(queue.updateProgress(a.jobId, 1.5).result.isOk());  // 钳制到 1
    NC_CHECK(queue.find(a.jobId)->progress <= 1.0);
}

NC_TEST(ExportQueue, CompleteRequiresRunning) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, freeTier(), 1000);
    // 等待中不可完成
    NC_CHECK(!queue.markCompleted(a.jobId, 10, 1001).result.isOk());
    queue.markStarted(a.jobId, 1002);
    NC_CHECK(queue.markCompleted(a.jobId, 300, 1003).result.isOk());
    const ExportJob* job = queue.find(a.jobId);
    NC_CHECK_EQ(job->state, ExportJobState::Completed);
    NC_CHECK_EQ(job->framesWritten, std::int64_t(300));
    NC_CHECK_EQ(job->finishedAtUnix, std::int64_t(1003));
    NC_CHECK_EQ(job->progress, 1.0);
}

NC_TEST(ExportQueue, FailFromRunningAndPaused) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    const auto b = queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1001);
    queue.markStarted(a.jobId, 1002);
    NC_CHECK(queue.markFailed(a.jobId, "编码器错误", 1003).result.isOk());
    NC_CHECK_EQ(queue.find(a.jobId)->note, std::string("编码器错误"));
    // Paused → Failed
    queue.markStarted(b.jobId, 1004);
    queue.pause(b.jobId, 1005);
    NC_CHECK(queue.markFailed(b.jobId, "磁盘已满", 1006).result.isOk());
    NC_CHECK_EQ(queue.find(b.jobId)->state, ExportJobState::Failed);
    // 等待中不可标记失败
    const auto c = queue.enqueue("C", "/out/c.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1007);
    NC_CHECK(!queue.markFailed(c.jobId, "x", 1008).result.isOk());
}

NC_TEST(ExportQueue, PauseResumeKeepsProgress) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, freeTier(), 1000);
    queue.markStarted(a.jobId, 1001);
    queue.updateProgress(a.jobId, 0.42);
    NC_CHECK(queue.pause(a.jobId, 1002).result.isOk());
    NC_CHECK_EQ(queue.find(a.jobId)->state, ExportJobState::Paused);
    NC_CHECK(queue.find(a.jobId)->progress > 0.41 && queue.find(a.jobId)->progress < 0.43);
    // 暂停说明（诚实实现：继续将重跑）
    NC_CHECK(!queue.find(a.jobId)->note.empty());
    NC_CHECK(queue.resume(a.jobId, 1003).result.isOk());
    NC_CHECK_EQ(queue.find(a.jobId)->state, ExportJobState::Running);
    NC_CHECK_EQ(queue.find(a.jobId)->startedAtUnix, std::int64_t(1003));
}

NC_TEST(ExportQueue, PauseQueuedAllowedButResumeOnlyFromPaused) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    // 等待中可直接暂停
    NC_CHECK(queue.pause(a.jobId, 1001).result.isOk());
    NC_CHECK(queue.resume(a.jobId, 1002).result.isOk());
    NC_CHECK_EQ(queue.find(a.jobId)->state, ExportJobState::Running);
    // 运行中不可再次 resume
    NC_CHECK(!queue.resume(a.jobId, 1003).result.isOk());
}

NC_TEST(ExportQueue, RemoveTerminalAndActive) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    const auto b = queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1001);
    // 终态移除
    queue.markStarted(a.jobId, 1002);
    queue.markCompleted(a.jobId, 1, 1003);
    NC_CHECK(queue.remove(a.jobId, 1004).result.isOk());
    NC_CHECK_EQ(queue.size(), std::size_t(1));
    NC_CHECK(queue.find(a.jobId) == nullptr);
    // 活动态移除（内部先转 Cancelled 再删）
    NC_CHECK(queue.remove(b.jobId, 1005).result.isOk());
    NC_CHECK_EQ(queue.size(), std::size_t(0));
    // 不存在
    NC_CHECK(!queue.remove(9999, 1006).result.isOk());
}

NC_TEST(ExportQueue, MissingJobOperationsFail) {
    ExportQueue queue;
    NC_CHECK_EQ(queue.pollNext(exportQueuePolicy(PerformanceMode::Turbo, 4)), std::uint64_t(0));
    NC_CHECK(!queue.markStarted(42, 1000).result.isOk());
    NC_CHECK(!queue.updateProgress(42, 0.5).result.isOk());
    NC_CHECK(!queue.markCompleted(42, 1, 1000).result.isOk());
    NC_CHECK(!queue.markFailed(42, "x", 1000).result.isOk());
    NC_CHECK(!queue.pause(42, 1000).result.isOk());
    NC_CHECK(!queue.resume(42, 1000).result.isOk());
    NC_CHECK(!queue.remove(42, 1000).result.isOk());
    NC_CHECK(queue.find(42) == nullptr);
}

NC_TEST(ExportQueue, DuplicateStartRejected) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, freeTier(), 1000);
    NC_CHECK(queue.markStarted(a.jobId, 1001).result.isOk());
    NC_CHECK(!queue.markStarted(a.jobId, 1002).result.isOk());
}

// ---------------------------------------------------------------------------
// 批量操作
// ---------------------------------------------------------------------------

NC_TEST(ExportQueue, CancelAllMarksCancelled) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom, PerformanceMode::Balanced,
                  proTier(), 1001);
    queue.markStarted(a.jobId, 1002);
    queue.cancelAll(1003);
    NC_CHECK_EQ(queue.activeCount(), std::size_t(0));
    NC_CHECK_EQ(queue.find(a.jobId)->state, ExportJobState::Cancelled);
    // 记录保留（供展示）
    NC_CHECK_EQ(queue.size(), std::size_t(2));
}

NC_TEST(ExportQueue, ClearResetsEverything) {
    ExportQueue queue;
    queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom, PerformanceMode::Balanced,
                  proTier(), 1000);
    queue.clear();
    NC_CHECK_EQ(queue.size(), std::size_t(0));
    // 清空后新任务从 1 起（ID/序号重置）
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1001);
    NC_CHECK_EQ(a.jobId, std::uint64_t(1));
}

NC_TEST(ExportQueue, CountsReflectStates) {
    ExportQueue queue;
    const auto a = queue.enqueue("A", "/out/a.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1000);
    const auto b = queue.enqueue("B", "/out/b.mp4", ExportPresetId::Custom,
                                 PerformanceMode::Balanced, proTier(), 1001);
    queue.enqueue("C", "/out/c.mp4", ExportPresetId::Custom, PerformanceMode::Balanced,
                  proTier(), 1002);
    NC_CHECK_EQ(queue.queuedCount(), std::size_t(3));
    queue.markStarted(a.jobId, 1003);
    queue.markStarted(b.jobId, 1004);
    queue.pause(b.jobId, 1005);
    NC_CHECK_EQ(queue.queuedCount(), std::size_t(1));
    NC_CHECK_EQ(queue.runningCount(), std::size_t(1));
    NC_CHECK_EQ(queue.pausedCount(), std::size_t(1));
    NC_CHECK_EQ(queue.activeCount(), std::size_t(3));
}
