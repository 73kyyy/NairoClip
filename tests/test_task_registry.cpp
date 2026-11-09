// T2.2：通用任务契约（TaskRegistry / nc_task_*）测试
// 覆盖：生命周期、真实进度心跳、心跳超时、总超时、取消语义、结果上限、跨层 JSON
#include "test_framework.h"

#include "nairoclip/core/capi.h"
#include "nairoclip/core/contract/task.h"
#include "nairoclip/core/json.h"

#include <string>

using namespace nairoclip::core;
using namespace nairoclip::core::contract;

namespace {

TaskConfig fastConfig() {
    TaskConfig c;
    c.heartbeatTimeoutMs = 1000;
    c.totalTimeoutMs = 5000;
    return c;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 注册表基础
// ---------------------------------------------------------------------------

NC_TEST(TaskRegistry2, CreateExAssignsIdAndConfig) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.matting", fastConfig(), 1000);
    NC_CHECK(!id.empty());
    const TaskInfo* t = reg.find(id);
    NC_CHECK(t != nullptr);
    NC_CHECK_EQ(t->type, std::string("ai.matting"));
    NC_CHECK(t->state == TaskState::Queued);
    NC_CHECK_EQ(t->config.heartbeatTimeoutMs, 1000);
    NC_CHECK_EQ(t->config.totalTimeoutMs, std::int64_t(5000));
}

NC_TEST(TaskRegistry2, InvalidHeartbeatFallsBackToDefault) {
    TaskRegistry reg;
    TaskConfig c;
    c.heartbeatTimeoutMs = 0;
    const std::string id = reg.createEx("import", c, 0);
    NC_CHECK_EQ(reg.find(id)->config.heartbeatTimeoutMs, 60000);
}

NC_TEST(TaskRegistry2, HeartbeatIsMonotonicAndUpdatesStage) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.tts", fastConfig(), 100);
    reg.start(id, 100);
    NC_CHECK(reg.heartbeat(id, 0.4, "推理中", 200));
    NC_CHECK(reg.heartbeat(id, 0.2, "", 300));  // 回跳被忽略
    NC_CHECK(reg.heartbeat(id, 0.6, "收尾", 400));
    const TaskInfo* t = reg.find(id);
    NC_CHECK_EQ(t->progress, 0.6);
    NC_CHECK_EQ(t->stageNote, std::string("收尾"));
    NC_CHECK_EQ(t->lastHeartbeatMs, std::int64_t(400));
}

NC_TEST(TaskRegistry2, HeartbeatAutoStartsQueuedTask) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.matting", fastConfig(), 50);
    NC_CHECK(reg.heartbeat(id, 0.1, "", 60));
    NC_CHECK(reg.find(id)->state == TaskState::Running);
}

NC_TEST(TaskRegistry2, HeartbeatRejectedAfterTerminal) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.tts", fastConfig(), 0);
    reg.start(id, 0);
    reg.completeWithResult(id, "ai.result", "{}", 0);
    NC_CHECK(!reg.heartbeat(id, 0.5, "", 10));
    NC_CHECK(!reg.completeWithResult(id, "x", "{}", 0));
    NC_CHECK(!reg.cancel(id));
}

// ---------------------------------------------------------------------------
// 2. 超时看门狗（心跳与总超时独立）
// ---------------------------------------------------------------------------

NC_TEST(TaskRegistry2, HeartbeatTimeoutFailsTask) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.superres", fastConfig(), 1000);
    reg.start(id, 1000);
    NC_CHECK_EQ(reg.tick(1500), std::size_t(0));   // 500ms < 1000ms
    NC_CHECK_EQ(reg.tick(2100), std::size_t(1));   // 1100ms > 1000ms
    const TaskInfo* t = reg.find(id);
    NC_CHECK(t->state == TaskState::Failed);
    NC_CHECK(t->timedOut);
    NC_CHECK(t->error.find("TASK_HEARTBEAT_TIMEOUT") != std::string::npos);
    NC_CHECK(t->retryable == false);  // tick 判失败默认不可重试，由调用方决定重试策略
}

NC_TEST(TaskRegistry2, TotalTimeoutIndependentOfHeartbeat) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.stem", fastConfig(), 0);
    reg.start(id, 0);
    // 心跳很勤（不触发心跳超时），但总耗时越界
    reg.heartbeat(id, 0.2, "", 4500);
    NC_CHECK_EQ(reg.tick(5100), std::size_t(1));
    const TaskInfo* t = reg.find(id);
    NC_CHECK(t->state == TaskState::Failed);
    NC_CHECK(t->error.find("TASK_TIMEOUT") != std::string::npos);
    NC_CHECK(t->error.find("TASK_HEARTBEAT_TIMEOUT") == std::string::npos);
}

NC_TEST(TaskRegistry2, NoClockMeansNoTimeout) {
    TaskRegistry reg;
    TaskConfig c;
    c.heartbeatTimeoutMs = 10;
    const std::string id = reg.createEx("ai.matting", c, 0);
    reg.start(id, 0);              // 未提供时钟
    NC_CHECK_EQ(reg.tick(100000), std::size_t(0));
    NC_CHECK(reg.find(id)->state == TaskState::Running);
}

NC_TEST(TaskRegistry2, TickIgnoresTerminalTasks) {
    TaskRegistry reg;
    const std::string done = reg.createEx("ai.tts", fastConfig(), 0);
    reg.start(done, 0);
    reg.completeWithResult(done, "r", "{}", 0);
    NC_CHECK_EQ(reg.tick(999999), std::size_t(0));
}

// ---------------------------------------------------------------------------
// 3. 结果与取消
// ---------------------------------------------------------------------------

NC_TEST(TaskRegistry2, ResultStoredWithType) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.matting", fastConfig(), 0);
    reg.start(id, 0);
    NC_CHECK(reg.completeWithResult(id, "ai.matting.mask", "{\"mask\":\"a.png\"}", 0));
    const TaskInfo* t = reg.find(id);
    NC_CHECK(t->state == TaskState::Completed);
    NC_CHECK_EQ(t->resultType, std::string("ai.matting.mask"));
    NC_CHECK_EQ(t->resultPayload, std::string("{\"mask\":\"a.png\"}"));
    NC_CHECK_EQ(t->progress, 1.0);
}

NC_TEST(TaskRegistry2, OversizedResultFailsTask) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.tts", fastConfig(), 0);
    reg.start(id, 0);
    const std::string big(kMaxResultBytes + 1, 'x');
    NC_CHECK(!reg.completeWithResult(id, "r", big, 0));
    const TaskInfo* t = reg.find(id);
    NC_CHECK(t->state == TaskState::Failed);
    NC_CHECK(t->error.find("RESULT_TOO_LARGE") != std::string::npos);
    NC_CHECK(t->resultPayload.empty());
}

NC_TEST(TaskRegistry2, CancelSetsFlagAndState) {
    TaskRegistry reg;
    const std::string id = reg.createEx("ai.depth", fastConfig(), 0);
    reg.start(id, 0);
    NC_CHECK(reg.cancel(id));
    const TaskInfo* t = reg.find(id);
    NC_CHECK(t->state == TaskState::Cancelled);
    NC_CHECK(t->cancelRequested);  // 执行器必须轮询此标志
}

// ---------------------------------------------------------------------------
// 4. C ABI 跨层
// ---------------------------------------------------------------------------

namespace {

std::string takeString(char* s) {
    if (s == nullptr) return std::string();
    const std::string out(s);
    nc_free_string(s);
    return out;
}

}  // namespace

NC_TEST(TaskCapi, BeginQueryHeartbeatComplete) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    char* outId = nullptr;
    char* outErr = nullptr;
    const int32_t rc = nc_task_begin(ed, "ai.matting", 7, 60000, 0, 1000, &outId, &outErr);
    NC_CHECK_EQ(rc, NC_OK);
    const std::string id = takeString(outId);
    NC_CHECK(!id.empty());
    if (outErr) nc_free_string(outErr);

    char* q = nc_task_query(ed, id.c_str());
    NC_CHECK(q != nullptr);
    const std::string qs = takeString(q);
    JsonValue j;
    JsonParseError pe;
    NC_CHECK(parseJson(qs, j, pe));
    NC_CHECK_EQ(j.get("type").asString(), std::string("ai.matting"));
    NC_CHECK_EQ(j.get("state").asString(), std::string("running"));

    NC_CHECK_EQ(nc_task_heartbeat(ed, id.c_str(), 0.5, "推理", 1200), NC_OK);
    NC_CHECK_EQ(nc_task_complete(ed, id.c_str(), "ai.matting.mask", "{\"m\":1}", 1300), NC_OK);

    q = nc_task_query(ed, id.c_str());
    NC_CHECK(parseJson(takeString(q), j, pe));
    NC_CHECK_EQ(j.get("state").asString(), std::string("completed"));
    NC_CHECK_EQ(j.get("resultType").asString(), std::string("ai.matting.mask"));
    NC_CHECK_EQ(j.get("progress").asNumber(0.0), 1.0);
    nc_editor_destroy(ed);
}

NC_TEST(TaskCapi, TickFailsStalledTask) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    char* outId = nullptr;
    // now_ms = 0：模拟旧接口（拿不到时钟）。第一次 tick 只建立基准，
    // 从这一刻开始计时，之后的 tick 才可能判超时。
    nc_task_begin(ed, "export", 0, 1000, 0, 0, &outId, nullptr);
    const std::string id = takeString(outId);
    NC_CHECK_EQ(nc_task_tick(ed, 500), 0);    // 建立基准，不判超时
    NC_CHECK_EQ(nc_task_tick(ed, 1400), 0);   // 900ms < 1000ms
    NC_CHECK_EQ(nc_task_tick(ed, 1600), 1);   // 1100ms > 1000ms
    char* q = nc_task_query(ed, id.c_str());
    JsonValue j;
    JsonParseError pe;
    NC_CHECK(parseJson(takeString(q), j, pe));
    NC_CHECK_EQ(j.get("state").asString(), std::string("failed"));
    NC_CHECK(j.get("timedOut").asBool(false));
    nc_editor_destroy(ed);
}

NC_TEST(TaskCapi, CancelAndUnknownId) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    char* outId = nullptr;
    nc_task_begin(ed, "proxy", 0, 0, 0, 0, &outId, nullptr);
    const std::string id = takeString(outId);
    NC_CHECK_EQ(nc_task_cancel(ed, id.c_str()), NC_OK);
    NC_CHECK(nc_task_query(ed, "task-999") == nullptr);
    NC_CHECK(nc_task_cancel(ed, "task-999") != NC_OK);
    nc_editor_destroy(ed);
}

NC_TEST(TaskCapi, AiBeginExReturnsTaskId) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    char* outId = nullptr;
    char* outErr = nullptr;
    // 快速任务：15s 心跳超时
    const int32_t rc = nc_ai_task_begin_ex(ed, 6 /* Tts */, 0, 15000, 0, 1000, &outId, &outErr);
    NC_CHECK_EQ(rc, NC_OK);
    const std::string id = takeString(outId);
    NC_CHECK(!id.empty());
    if (outErr) nc_free_string(outErr);
    // 同一个 TaskRegistry：通用查询能看到它
    char* q = nc_task_query(ed, id.c_str());
    NC_CHECK(q != nullptr);
    JsonValue j;
    JsonParseError pe;
    NC_CHECK(parseJson(takeString(q), j, pe));
    NC_CHECK_EQ(j.get("type").asString(), std::string("ai.tts"));
    NC_CHECK_EQ(j.get("heartbeatTimeoutMs").asInteger(0), std::int64_t(15000));
    nc_editor_destroy(ed);
}

NC_TEST(TaskCapi, FailureTextDelivered) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    NC_CHECK_EQ(nc_ai_task_begin(ed, 3 /* SuperResolution */, 0, nullptr), NC_OK);
    NC_CHECK_EQ(nc_ai_task_fail(ed, "模型未下载", 1), NC_OK);
    const std::string text = takeString(nc_ai_task_failure_text(ed));
    NC_CHECK(text.find("超分辨率") != std::string::npos);
    NC_CHECK(text.find("模型未下载") != std::string::npos);
    nc_editor_destroy(ed);
}
