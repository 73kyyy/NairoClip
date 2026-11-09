#include "nairoclip/core/ai_task.h"

#include <algorithm>
#include <string>

namespace nairoclip {
namespace core {

const char* aiFeatureName(AiFeatureType feature) {
    switch (feature) {
        case AiFeatureType::None: return "none";
        case AiFeatureType::Matting: return "matting";
        case AiFeatureType::FrameInterpolation: return "frame_interpolation";
        case AiFeatureType::SuperResolution: return "super_resolution";
        case AiFeatureType::StemSeparation: return "stem_separation";
        case AiFeatureType::SubtitleTranslate: return "subtitle_translate";
        case AiFeatureType::Tts: return "tts";
        case AiFeatureType::CameraTracking: return "camera_tracking";
        case AiFeatureType::DepthMap: return "depth_map";
        case AiFeatureType::ReferenceEdit: return "reference_edit";
        case AiFeatureType::SpeechToSubtitle: return "speech_to_subtitle";
    }
    return "none";
}

const char* aiFeatureDisplayName(AiFeatureType feature) {
    switch (feature) {
        case AiFeatureType::None: return "无";
        case AiFeatureType::Matting: return "智能抠像";
        case AiFeatureType::FrameInterpolation: return "光流补帧";
        case AiFeatureType::SuperResolution: return "超分辨率";
        case AiFeatureType::StemSeparation: return "人声分离";
        case AiFeatureType::SubtitleTranslate: return "字幕翻译";
        case AiFeatureType::Tts: return "文本转语音";
        case AiFeatureType::CameraTracking: return "镜头追踪";
        case AiFeatureType::DepthMap: return "深度图";
        case AiFeatureType::ReferenceEdit: return "参考仿剪";
        case AiFeatureType::SpeechToSubtitle: return "识别字幕";
    }
    return "无";
}

bool aiFeatureFromString(const char* text, AiFeatureType& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    for (int i = 0; i <= 10; ++i) {
        const auto feature = static_cast<AiFeatureType>(i);
        if (s == aiFeatureName(feature)) {
            out = feature;
            return true;
        }
    }
    // 常见别名
    if (s == "interpolation") { out = AiFeatureType::FrameInterpolation; return true; }
    if (s == "superres" || s == "upscale") { out = AiFeatureType::SuperResolution; return true; }
    if (s == "vocal_separation" || s == "stem_sep") { out = AiFeatureType::StemSeparation; return true; }
    if (s == "translate") { out = AiFeatureType::SubtitleTranslate; return true; }
    if (s == "tracking") { out = AiFeatureType::CameraTracking; return true; }
    if (s == "depth") { out = AiFeatureType::DepthMap; return true; }
    if (s == "reference" || s == "smart_edit") { out = AiFeatureType::ReferenceEdit; return true; }
    if (s == "whisper" || s == "asr" || s == "subtitle") { out = AiFeatureType::SpeechToSubtitle; return true; }
    return false;
}

bool aiFeatureFromInt(int value, AiFeatureType& out) {
    if (value < 0 || value > 10) {
        return false;
    }
    out = static_cast<AiFeatureType>(value);
    return true;
}

bool aiFeatureRequiresNetwork(AiFeatureType feature) {
    // 文档 Part 6 第 41.3 节：智能抠像/光流补帧/超分辨率/人声分离/镜头追踪/
    // 字幕翻译/TTS/参考仿剪/深度图 共 9 项需登录（联网算力）。
    // 识别字幕/歌词（whisper.cpp）为**本地模型**，不需联网（文档 Part 4-A 3.2
    // 明确「本期使用模拟占位文本」——但架构上 whisper 为本地推理）。
    switch (feature) {
        case AiFeatureType::SpeechToSubtitle:
        case AiFeatureType::None:
            return false;
        default:
            return true;
    }
}

bool aiFeatureCountsTowardUndo(AiFeatureType feature) {
    // 文档 Part 6 第 43 节：AI 处理计入撤销栈；**参考仿剪除外**（备份草稿）。
    return feature != AiFeatureType::ReferenceEdit;
}

const char* aiTaskStateName(AiTaskState state) {
    switch (state) {
        case AiTaskState::Idle: return "idle";
        case AiTaskState::Running: return "running";
        case AiTaskState::Succeeded: return "succeeded";
        case AiTaskState::Failed: return "failed";
        case AiTaskState::Cancelled: return "cancelled";
    }
    return "idle";
}

bool AiTaskRunner::begin(AiFeatureType feature, std::uint64_t targetId,
                         std::string* outputFailure) {
    contract::TaskConfig config;  // 默认心跳 60s、总超时不限
    return beginEx(feature, targetId, config, 0, outputFailure);
}

bool AiTaskRunner::beginEx(AiFeatureType feature, std::uint64_t targetId,
                           const contract::TaskConfig& config, std::int64_t nowMs,
                           std::string* outputFailure) {
    if (feature == AiFeatureType::None) {
        if (outputFailure != nullptr) *outputFailure = "未指定 AI 功能类型";
        return false;
    }
    // 单活动任务约束（文档 Part 4 自查规则第 4 项）：
    // 以 Registry 中的运行态为准，而不是本类的私有字段。
    if (!taskId_.empty()) {
        const contract::TaskInfo* cur = registry_.find(taskId_);
        if (cur != nullptr && (cur->state == contract::TaskState::Running ||
                               cur->state == contract::TaskState::CancelRequested)) {
            if (outputFailure != nullptr) {
                *outputFailure = std::string(aiFeatureDisplayName(feature_)) + "处理中，请稍候";
            }
            return false;
        }
    }
    const std::string kind = std::string("ai.") + aiFeatureName(feature);
    const std::string id = registry_.createEx(kind, config, nowMs);
    if (id.empty()) {
        if (outputFailure != nullptr) *outputFailure = "任务类型非法";
        return false;
    }
    registry_.start(id, nowMs);
    taskId_ = id;
    feature_ = feature;
    targetId_ = targetId;
    return true;
}

bool AiTaskRunner::setProgress(double progress, const std::string& stageNote,
                               std::int64_t nowMs) {
    if (taskId_.empty()) return false;
    return registry_.heartbeat(taskId_, progress, stageNote, nowMs);
}

bool AiTaskRunner::succeed(const std::string& resultPayload) {
    if (taskId_.empty()) return false;
    const std::string kind = std::string("ai.") + aiFeatureName(feature_) + ".result";
    return registry_.completeWithResult(taskId_, kind, resultPayload, 0);
}

bool AiTaskRunner::fail(const std::string& reason, bool retryable) {
    if (taskId_.empty()) return false;
    // 文档第 30.3 节：禁止无提示卡死——原因必须非空
    const std::string text = reason.empty()
                                 ? std::string("处理过程中出现未知错误（")
                                       .append(aiFeatureDisplayName(feature_))
                                       .append("）")
                                 : reason;
    return registry_.fail(taskId_, text, retryable);
}

bool AiTaskRunner::cancel() {
    if (taskId_.empty()) return false;
    return registry_.cancel(taskId_);
}

bool AiTaskRunner::confirmCancel() {
    if (taskId_.empty()) return false;
    return registry_.acknowledgeCancel(taskId_);
}

void AiTaskRunner::reset() {
    if (!taskId_.empty()) registry_.remove(taskId_);
    taskId_.clear();
    feature_ = AiFeatureType::None;
    targetId_ = 0;
}

AiTask AiTaskRunner::task() const {
    AiTask view;
    if (taskId_.empty()) return view;
    const contract::TaskInfo* info = registry_.find(taskId_);
    if (info == nullptr) return view;
    view.feature = feature_;
    view.targetId = targetId_;
    view.progress = info->progress;
    view.stageNote = info->stageNote;
    view.resultPayload = info->resultPayload;
    view.retryable = info->retryable;
    view.failureReason = info->error;
    view.cancelRequested = info->cancelRequested;
    switch (info->state) {
        case contract::TaskState::Queued:
            view.state = AiTaskState::Idle;
            break;
        case contract::TaskState::Running:
            view.state = AiTaskState::Running;
            break;
        case contract::TaskState::Completed:
            view.state = AiTaskState::Succeeded;
            break;
        case contract::TaskState::Failed:
            view.state = AiTaskState::Failed;
            break;
        case contract::TaskState::Cancelled:
            view.state = AiTaskState::Cancelled;
            break;
        case contract::TaskState::CancelRequested:
            // 关键：取消请求 ≠ 已取消。底层可能仍在跑，对外仍是 Running。
            view.state = AiTaskState::Running;
            break;
    }
    return view;
}

bool AiTaskRunner::active() const {
    if (taskId_.empty()) return false;
    const contract::TaskInfo* info = registry_.find(taskId_);
    return info != nullptr && info->state == contract::TaskState::Running;
}

std::string AiTaskRunner::failureText() const {
    const AiTask t = task();
    const std::string name = aiFeatureDisplayName(t.feature);
    if (t.state == AiTaskState::Failed) {
        if (!t.failureReason.empty()) {
            return name + "处理失败：" + t.failureReason;
        }
        return name + "处理未能完成，请重试";
    }
    return std::string();
}

}  // namespace core
}  // namespace nairoclip
