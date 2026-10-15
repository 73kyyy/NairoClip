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
    if (feature == AiFeatureType::None) {
        if (outputFailure != nullptr) *outputFailure = "未指定 AI 功能类型";
        return false;
    }
    if (task_.state == AiTaskState::Running) {
        if (outputFailure != nullptr) {
            *outputFailure = std::string(aiFeatureDisplayName(task_.feature)) + "处理中，请稍候";
        }
        return false;
    }
    task_ = AiTask{};
    task_.feature = feature;
    task_.state = AiTaskState::Running;
    task_.progress = 0.0;
    task_.targetId = targetId;
    task_.stageNote = "准备中";
    return true;
}

bool AiTaskRunner::setProgress(double progress, const std::string& stageNote) {
    if (task_.state != AiTaskState::Running) {
        return false;
    }
    progress = std::clamp(progress, 0.0, 1.0);
    // 单调不减（防止调用方乱序回调导致进度条回跳）
    if (progress > task_.progress) {
        task_.progress = progress;
    }
    if (!stageNote.empty()) {
        task_.stageNote = stageNote;
    }
    return true;
}

bool AiTaskRunner::succeed(const std::string& resultPayload) {
    if (task_.state != AiTaskState::Running) {
        return false;
    }
    task_.state = AiTaskState::Succeeded;
    task_.progress = 1.0;
    task_.resultPayload = resultPayload;
    task_.failureReason.clear();
    return true;
}

bool AiTaskRunner::fail(const std::string& reason, bool retryable) {
    if (task_.state != AiTaskState::Running) {
        return false;
    }
    task_.state = AiTaskState::Failed;
    // 文档第 30.3 节：禁止无提示卡死——原因必须非空
    task_.failureReason = reason.empty()
                              ? std::string("处理过程中出现未知错误（").append(
                                    aiFeatureDisplayName(task_.feature)).append("）")
                              : reason;
    task_.retryable = retryable;
    return true;
}

bool AiTaskRunner::cancel() {
    if (task_.state != AiTaskState::Running) {
        return false;
    }
    task_.state = AiTaskState::Cancelled;
    return true;
}

void AiTaskRunner::reset() {
    task_ = AiTask{};
}

std::string AiTaskRunner::failureText() const {
    const std::string name = aiFeatureDisplayName(task_.feature);
    if (task_.state == AiTaskState::Failed) {
        if (!task_.failureReason.empty()) {
            return name + "处理失败：" + task_.failureReason;
        }
        return name + "处理未能完成，请重试";
    }
    return std::string();
}

}  // namespace core
}  // namespace nairoclip
