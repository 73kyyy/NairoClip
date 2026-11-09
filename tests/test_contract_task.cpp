// 契约层 · 异步任务模型测试（03 §3 / §4.3）
#include "test_framework.h"

#include "nairoclip/core/contract/task.h"

#include <string>

using namespace nairoclip::core;
using namespace nairoclip::core::contract;

NC_TEST(ContractTask, CreateReturnsUniqueId) {
    TaskRegistry reg;
    const std::string a = reg.create("import", 100);
    const std::string b = reg.create("export", 200);
    NC_CHECK(!a.empty());
    NC_CHECK(!b.empty());
    NC_CHECK(a != b);
    NC_CHECK_EQ(reg.size(), std::size_t(2));
}

NC_TEST(ContractTask, InvalidTypeRejected) {
    TaskRegistry reg;
    NC_CHECK(reg.create("").empty());
    NC_CHECK(reg.create("Import").empty());   // 大写
    NC_CHECK(reg.create("im port").empty());  // 空格
    NC_CHECK(reg.create(std::string(40, 'a')).empty());  // 超长
}

NC_TEST(ContractTask, LifecycleQueuedRunningCompleted) {
    TaskRegistry reg;
    const std::string id = reg.create("proxy");
    const TaskInfo* info = reg.find(id);
    NC_CHECK(info != nullptr);
    NC_CHECK(info->state == TaskState::Queued);

    NC_CHECK(reg.start(id));
    NC_CHECK(reg.find(id)->state == TaskState::Running);
    NC_CHECK(reg.complete(id));
    NC_CHECK(reg.find(id)->state == TaskState::Completed);
    NC_CHECK_EQ(reg.find(id)->progress, 1.0);
}

NC_TEST(ContractTask, ProgressIsClamped) {
    TaskRegistry reg;
    const std::string id = reg.create("thumbnail");
    reg.start(id);
    reg.setProgress(id, -5.0);
    NC_CHECK_EQ(reg.find(id)->progress, 0.0);
    reg.setProgress(id, 42.0);
    NC_CHECK_EQ(reg.find(id)->progress, 1.0);
    reg.setProgress(id, 0.25);
    NC_CHECK_EQ(reg.find(id)->progress, 0.25);
}

NC_TEST(ContractTask, ProgressAutoStarts) {
    TaskRegistry reg;
    const std::string id = reg.create("ai");
    // 未显式 start 就上报进度：应自动进入 Running，避免 UI 看到矛盾状态。
    NC_CHECK(reg.setProgress(id, 0.5));
    NC_CHECK(reg.find(id)->state == TaskState::Running);
}

NC_TEST(ContractTask, TerminalStateCannotRegress) {
    TaskRegistry reg;
    const std::string id = reg.create("export");
    reg.start(id);
    reg.complete(id);
    NC_CHECK(!reg.start(id));
    NC_CHECK(!reg.setProgress(id, 0.1));
    NC_CHECK(!reg.fail(id, "boom"));
    NC_CHECK(!reg.cancel(id));
    NC_CHECK(reg.find(id)->state == TaskState::Completed);
}

NC_TEST(ContractTask, FailRecordsError) {
    TaskRegistry reg;
    const std::string id = reg.create("export");
    reg.start(id);
    NC_CHECK(reg.fail(id, "NC-EXPORT-NO_SPACE: 磁盘不足"));
    NC_CHECK(reg.find(id)->state == TaskState::Failed);
    NC_CHECK_EQ(reg.find(id)->error, std::string("NC-EXPORT-NO_SPACE: 磁盘不足"));
}

NC_TEST(ContractTask, CancelSemantics) {
    TaskRegistry reg;
    const std::string id = reg.create("export");
    reg.start(id);
    // T2.2 验收补充：取消请求 ≠ 已取消。
    NC_CHECK(reg.cancel(id));
    NC_CHECK(reg.find(id)->state == TaskState::CancelRequested);
    NC_CHECK(reg.isCancelRequested(id));
    NC_CHECK(reg.cancel(id));                    // 重复登记：幂等成功
    NC_CHECK(reg.acknowledgeCancel(id));         // 执行器确认后才算 Cancelled
    NC_CHECK(reg.find(id)->state == TaskState::Cancelled);
    NC_CHECK(!reg.acknowledgeCancel(id));        // 已终态不可再确认
    NC_CHECK(!reg.cancel(id));                   // 终态不可再取消
}

NC_TEST(ContractTask, MissingTaskReturnsFalseAndNull) {
    TaskRegistry reg;
    NC_CHECK(reg.find("task-999") == nullptr);
    NC_CHECK(!reg.start("task-999"));
    NC_CHECK(!reg.setProgress("task-999", 0.5));
    NC_CHECK(!reg.complete("task-999"));
    NC_CHECK(!reg.fail("task-999", "x"));
    NC_CHECK(!reg.cancel("task-999"));
    NC_CHECK(!reg.remove("task-999"));
    NC_CHECK(!reg.isCancelRequested("task-999"));
}

NC_TEST(ContractTask, CountInStateAndSnapshotOrder) {
    TaskRegistry reg;
    const std::string a = reg.create("import");
    const std::string b = reg.create("export");
    reg.create("ai");
    reg.start(a);
    reg.complete(a);
    reg.start(b);

    NC_CHECK_EQ(reg.countInState(TaskState::Completed), std::size_t(1));
    NC_CHECK_EQ(reg.countInState(TaskState::Running), std::size_t(1));
    NC_CHECK_EQ(reg.countInState(TaskState::Queued), std::size_t(1));

    const std::vector<TaskInfo> snap = reg.snapshot();
    NC_CHECK_EQ(snap.size(), std::size_t(3));
    // map 按 taskId 字典序：task-1 < task-2 < task-3
    NC_CHECK_EQ(snap[0].taskId, std::string("task-1"));
    NC_CHECK_EQ(snap[2].taskId, std::string("task-3"));
}

NC_TEST(ContractTask, RemoveFreesSlot) {
    TaskRegistry reg;
    const std::string id = reg.create("import");
    NC_CHECK(reg.remove(id));
    NC_CHECK_EQ(reg.size(), std::size_t(0));
    NC_CHECK(reg.find(id) == nullptr);
}

NC_TEST(ContractTask, StateNameRoundTrip) {
    for (int i = 0; i <= static_cast<int>(TaskState::Cancelled); ++i) {
        const TaskState st = static_cast<TaskState>(i);
        TaskState parsed = TaskState::Queued;
        NC_CHECK(taskStateFromName(taskStateName(st), &parsed));
        NC_CHECK(parsed == st);
    }
    TaskState parsed;
    NC_CHECK(!taskStateFromName("nope", &parsed));
    NC_CHECK(isTerminal(TaskState::Completed));
    NC_CHECK(isTerminal(TaskState::Failed));
    NC_CHECK(isTerminal(TaskState::Cancelled));
    NC_CHECK(!isTerminal(TaskState::Running));
}
