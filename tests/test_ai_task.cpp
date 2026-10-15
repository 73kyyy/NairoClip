// =============================================================================
// AI 任务状态机测试（文档 Part 4 统一任务契约 + Part 6 第 41/43 节）
// =============================================================================
// 覆盖：
//   1. 功能清单（名称/中文名/解析/网络与撤销语义——文档逐项核对）；
//   2. 状态机（开始/进度单调/成功/失败/取消/复位/单任务约束）；
//   3. 失败文案（统一结构；原因非空——文档禁止无提示卡死）；
//   4. C ABI（登录门控/状态查询/进度回填/载荷交接/引擎信息）。
#include "test_framework.h"
#include "nairoclip/core/ai_task.h"
#include "nairoclip/core/capi.h"

#include <string>

using namespace nairoclip::core;

namespace {

std::string takeString(char* text) {
    if (text == nullptr) {
        return std::string();
    }
    const std::string result(text);
    nc_free_string(text);
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 功能清单（文档 Part 4 逐项）
// ---------------------------------------------------------------------------

NC_TEST(AiTask, FeatureNamesMatchDocument) {
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::Matting)),
                std::string("智能抠像"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::FrameInterpolation)),
                std::string("光流补帧"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::SuperResolution)),
                std::string("超分辨率"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::StemSeparation)),
                std::string("人声分离"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::SubtitleTranslate)),
                std::string("字幕翻译"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::Tts)),
                std::string("文本转语音"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::CameraTracking)),
                std::string("镜头追踪"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::DepthMap)),
                std::string("深度图"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::ReferenceEdit)),
                std::string("参考仿剪"));
    NC_CHECK_EQ(std::string(aiFeatureDisplayName(AiFeatureType::SpeechToSubtitle)),
                std::string("识别字幕"));
}

NC_TEST(AiTask, FeatureParse) {
    AiFeatureType feature;
    NC_CHECK(aiFeatureFromString("matting", feature) && feature == AiFeatureType::Matting);
    NC_CHECK(aiFeatureFromString("TTS", feature) && feature == AiFeatureType::Tts);
    NC_CHECK(aiFeatureFromString("whisper", feature) &&
             feature == AiFeatureType::SpeechToSubtitle);
    NC_CHECK(!aiFeatureFromString("unknown_xyz", feature));
    NC_CHECK(aiFeatureFromInt(1, feature) && feature == AiFeatureType::Matting);
    NC_CHECK(aiFeatureFromInt(10, feature) && feature == AiFeatureType::SpeechToSubtitle);
    NC_CHECK(!aiFeatureFromInt(11, feature));
    NC_CHECK(!aiFeatureFromInt(-1, feature));
}

NC_TEST(AiTask, NetworkRequirementMatchesDocument) {
    // 文档 Part 6 第 41.3 节：9 项联网 AI 需要登录
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::Matting));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::FrameInterpolation));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::SuperResolution));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::StemSeparation));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::CameraTracking));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::SubtitleTranslate));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::Tts));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::ReferenceEdit));
    NC_CHECK(aiFeatureRequiresNetwork(AiFeatureType::DepthMap));
    // 识别字幕/歌词为本地模型（whisper.cpp）：不需联网
    NC_CHECK(!aiFeatureRequiresNetwork(AiFeatureType::SpeechToSubtitle));
}

NC_TEST(AiTask, UndoScopeMatchesDocument) {
    // 文档 Part 6 第 43 节：AI 处理计入撤销栈；参考仿剪除外（备份草稿）
    NC_CHECK(aiFeatureCountsTowardUndo(AiFeatureType::Matting));
    NC_CHECK(aiFeatureCountsTowardUndo(AiFeatureType::FrameInterpolation));
    NC_CHECK(aiFeatureCountsTowardUndo(AiFeatureType::Tts));
    NC_CHECK(aiFeatureCountsTowardUndo(AiFeatureType::DepthMap));
    NC_CHECK(!aiFeatureCountsTowardUndo(AiFeatureType::ReferenceEdit));
}

// ---------------------------------------------------------------------------
// 2. 状态机
// ---------------------------------------------------------------------------

NC_TEST(AiTask, Lifecycle) {
    AiTaskRunner runner;
    NC_CHECK_EQ(static_cast<int>(runner.task().state), 0);  // Idle
    NC_CHECK(!runner.active());
    // 开始
    NC_CHECK(runner.begin(AiFeatureType::Matting, 42));
    NC_CHECK(runner.active());
    NC_CHECK_EQ(static_cast<int>(runner.task().state), 1);  // Running
    NC_CHECK_EQ(runner.task().targetId, std::uint64_t(42));
    // 进度（单调不减）
    NC_CHECK(runner.setProgress(0.3, "分析中"));
    NC_CHECK(runner.setProgress(0.5, ""));
    NC_CHECK(runner.setProgress(0.2, ""));  // 回跳被忽略
    NC_CHECK(std::abs(runner.task().progress - 0.5) < 1e-9);
    NC_CHECK_EQ(runner.task().stageNote, std::string("分析中"));
    // 成功
    NC_CHECK(runner.succeed("{\"frames\":3}"));
    NC_CHECK(!runner.active());
    NC_CHECK_EQ(static_cast<int>(runner.task().state), 2);  // Succeeded
    NC_CHECK(std::abs(runner.task().progress - 1.0) < 1e-9);
    NC_CHECK_EQ(runner.task().resultPayload, std::string("{\"frames\":3}"));
    // 复位
    runner.reset();
    NC_CHECK_EQ(static_cast<int>(runner.task().state), 0);
}

NC_TEST(AiTask, SingleActiveTaskConstraint) {
    // 文档 Part 4 自查规则第 4 项：处理期间其他参数置灰禁用
    AiTaskRunner runner;
    NC_CHECK(runner.begin(AiFeatureType::Tts, 0));
    // 第二个任务被拒绝
    std::string failure;
    NC_CHECK(!runner.begin(AiFeatureType::DepthMap, 0, &failure));
    NC_CHECK(!failure.empty());
    // 结束后可开始新任务
    NC_CHECK(runner.cancel());
    NC_CHECK(runner.begin(AiFeatureType::DepthMap, 0));
    NC_CHECK_EQ(static_cast<int>(runner.task().feature), 8);
}

NC_TEST(AiTask, FailRequiresReason) {
    AiTaskRunner runner;
    NC_CHECK(runner.begin(AiFeatureType::SuperResolution, 7));
    // 空原因 → 回退为通用文案（文档禁止无提示卡死）
    NC_CHECK(runner.fail("", true));
    NC_CHECK_EQ(static_cast<int>(runner.task().state), 3);  // Failed
    NC_CHECK(!runner.task().failureReason.empty());
    NC_CHECK(runner.task().retryable);
    // 失败文案（统一结构）
    const std::string text = runner.failureText();
    NC_CHECK(text.find("超分辨率") != std::string::npos);
    NC_CHECK(text.find("失败") != std::string::npos);
}

NC_TEST(AiTask, ExplicitFailureReason) {
    AiTaskRunner runner;
    NC_CHECK(runner.begin(AiFeatureType::Matting, 1));
    NC_CHECK(runner.fail("人像未检测到", false));
    NC_CHECK_EQ(runner.task().failureReason, std::string("人像未检测到"));
    NC_CHECK(!runner.task().retryable);
    const std::string text = runner.failureText();
    NC_CHECK(text.find("人像未检测到") != std::string::npos);
}

NC_TEST(AiTask, InvalidOperationsRejected) {
    AiTaskRunner runner;
    // 未开始：进度/成功/失败/取消均失败
    NC_CHECK(!runner.setProgress(0.5));
    NC_CHECK(!runner.succeed("{}"));
    NC_CHECK(!runner.fail("x", true));
    NC_CHECK(!runner.cancel());
    // feature = None 拒绝开始
    NC_CHECK(!runner.begin(AiFeatureType::None, 0));
    // 成功后：不可再更新进度
    NC_CHECK(runner.begin(AiFeatureType::Tts, 0));
    NC_CHECK(runner.succeed("{}"));
    NC_CHECK(!runner.setProgress(0.9));
    NC_CHECK(!runner.fail("x", true));
}

NC_TEST(AiTask, StateNames) {
    NC_CHECK_EQ(std::string(aiTaskStateName(AiTaskState::Idle)), std::string("idle"));
    NC_CHECK_EQ(std::string(aiTaskStateName(AiTaskState::Running)), std::string("running"));
    NC_CHECK_EQ(std::string(aiTaskStateName(AiTaskState::Succeeded)), std::string("succeeded"));
    NC_CHECK_EQ(std::string(aiTaskStateName(AiTaskState::Failed)), std::string("failed"));
    NC_CHECK_EQ(std::string(aiTaskStateName(AiTaskState::Cancelled)), std::string("cancelled"));
}

// ---------------------------------------------------------------------------
// 3. C ABI
// ---------------------------------------------------------------------------

NC_TEST(AiTask, CapiLoginGate) {
    NcEditor* editor = nc_editor_create();
    // 联网功能未登录 → 不可用（弹登录拦截）
    std::int32_t requiresNetwork = -1;
    std::int32_t requiresPro = -1;
    char* reason = nullptr;
    NC_CHECK_EQ(nc_ai_feature_access(editor, 1, 0, &requiresNetwork, &requiresPro, &reason), 0);
    NC_CHECK_EQ(requiresNetwork, 1);
    NC_CHECK_EQ(requiresPro, 1);  // 抠像为 Pro
    NC_CHECK(!takeString(reason).empty());
    // 已登录 → 可用
    NC_CHECK_EQ(nc_ai_feature_access(editor, 1, 1, nullptr, nullptr, &reason), 1);
    (void)takeString(reason);
    // 本地功能（识别字幕）未登录也可用
    NC_CHECK_EQ(nc_ai_feature_access(editor, 10, 0, &requiresNetwork, nullptr, nullptr), 1);
    NC_CHECK_EQ(requiresNetwork, 0);
    // 非法功能
    NC_CHECK_EQ(nc_ai_feature_access(editor, 99, 1, nullptr, nullptr, nullptr), -1);
    nc_editor_destroy(editor);
}

NC_TEST(AiTask, CapiTaskLifecycle) {
    NcEditor* editor = nc_editor_create();
    // 初始空闲
    std::int32_t feature = -1;
    std::int32_t state = -1;
    double progress = -1.0;
    NC_CHECK_EQ(nc_ai_task_status(editor, &feature, &state, &progress, nullptr, nullptr, nullptr,
                                  nullptr),
                NC_OK);
    NC_CHECK_EQ(state, 0);  // Idle
    // 开始任务
    NC_CHECK_EQ(nc_ai_task_begin(editor, 3, 77, nullptr), NC_OK);  // 超分辨率
    NC_CHECK_EQ(nc_ai_task_status(editor, &feature, &state, &progress, nullptr, nullptr, nullptr,
                                  nullptr),
                NC_OK);
    NC_CHECK_EQ(feature, 3);
    NC_CHECK_EQ(state, 1);
    // 重复开始被拒绝
    char* error = nullptr;
    NC_CHECK_EQ(nc_ai_task_begin(editor, 6, 0, &error), NC_ERROR_UNSUPPORTED);
    NC_CHECK(!takeString(error).empty());
    // 回填进度（含阶段文案）
    NC_CHECK_EQ(nc_ai_task_progress(editor, 0.45, "处理中 45%"), NC_OK);
    char* stageNote = nullptr;
    NC_CHECK_EQ(nc_ai_task_status(editor, nullptr, nullptr, &progress, &stageNote, nullptr,
                                  nullptr, nullptr),
                NC_OK);
    NC_CHECK(std::abs(progress - 0.45) < 1e-9);
    NC_CHECK_EQ(takeString(stageNote), std::string("处理中 45%"));
    // 成功（载荷交接）
    NC_CHECK_EQ(nc_ai_task_succeed(editor, "{\"outputPath\":\"/tmp/x.png\"}"), NC_OK);
    char* payload = nullptr;
    NC_CHECK_EQ(nc_ai_task_status(editor, nullptr, &state, nullptr, nullptr, nullptr, nullptr,
                                  &payload),
                NC_OK);
    NC_CHECK_EQ(state, 2);
    NC_CHECK_EQ(takeString(payload), std::string("{\"outputPath\":\"/tmp/x.png\"}"));
    // 复位
    NC_CHECK_EQ(nc_ai_task_reset(editor), NC_OK);
    NC_CHECK_EQ(nc_ai_task_status(editor, nullptr, &state, nullptr, nullptr, nullptr, nullptr,
                                  nullptr),
                NC_OK);
    NC_CHECK_EQ(state, 0);
    nc_editor_destroy(editor);
}

NC_TEST(AiTask, CapiFailureFlow) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK_EQ(nc_ai_task_begin(editor, 4, 0, nullptr), NC_OK);  // 人声分离
    // 失败（原因 + 可重试）
    NC_CHECK_EQ(nc_ai_task_fail(editor, "未检测到人声", 1), NC_OK);
    std::int32_t state = -1;
    std::int32_t retryable = -1;
    char* failureReason = nullptr;
    NC_CHECK_EQ(nc_ai_task_status(editor, nullptr, &state, nullptr, nullptr, &failureReason,
                                  &retryable, nullptr),
                NC_OK);
    NC_CHECK_EQ(state, 3);  // Failed
    NC_CHECK_EQ(retryable, 1);
    NC_CHECK_EQ(takeString(failureReason), std::string("未检测到人声"));
    // 统一失败文案
    const std::string text = takeString(nc_ai_task_failure_text(editor));
    NC_CHECK(text.find("人声分离") != std::string::npos);
    NC_CHECK(text.find("未检测到人声") != std::string::npos);
    // 无运行中任务时操作被拒
    NC_CHECK_EQ(nc_ai_task_progress(editor, 0.5, nullptr), NC_ERROR_UNSUPPORTED);
    NC_CHECK_EQ(nc_ai_task_cancel(editor), NC_ERROR_UNSUPPORTED);
    // NULL 安全
    NC_CHECK_EQ(nc_ai_task_status(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(nc_ai_task_failure_text(nullptr) == nullptr);
    nc_editor_destroy(editor);
}

NC_TEST(AiTask, CapiCancelAndEngineInfo) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK_EQ(nc_ai_task_begin(editor, 7, 0, nullptr), NC_OK);  // 镜头追踪
    NC_CHECK_EQ(nc_ai_task_cancel(editor), NC_OK);
    std::int32_t state = -1;
    NC_CHECK_EQ(nc_ai_task_status(editor, nullptr, &state, nullptr, nullptr, nullptr, nullptr,
                                  nullptr),
                NC_OK);
    NC_CHECK_EQ(state, 4);  // Cancelled
    // 引擎信息（本构建含 ORT → 可用；不含则不可用——两种均为真实状态）
    std::int32_t available = -1;
    char* version = nullptr;
    char* note = nullptr;
    NC_CHECK_EQ(nc_ai_engine_info(editor, &available, &version, &note), NC_OK);
    NC_CHECK(available == 0 || available == 1);
    if (available == 1) {
        NC_CHECK(!takeString(version).empty());
    }
    (void)takeString(note);
    nc_editor_destroy(editor);
}
