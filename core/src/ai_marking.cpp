#include "nairoclip/core/ai_marking.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.141592653589793;

/// 生成一段带淡入淡出的正弦提示音（交错 float）。
std::vector<float> makeTone(double frequency, double durationSeconds, double amplitude,
                            int sampleRate, int channels) {
    const int frames = static_cast<int>(durationSeconds * sampleRate + 0.5);
    if (frames <= 0 || channels <= 0) {
        return {};
    }
    std::vector<float> samples(static_cast<std::size_t>(frames) * channels, 0.0f);
    // 淡入淡出时长 = 最短 10ms 或总时长 1/8（防爆音）
    const double fadeSeconds = std::min(0.010, durationSeconds / 8.0);
    const int fadeFrames = std::max(1, static_cast<int>(fadeSeconds * sampleRate + 0.5));
    for (int i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        double envelope = 1.0;
        if (i < fadeFrames) {
            envelope = static_cast<double>(i) / fadeFrames;
        } else if (i >= frames - fadeFrames) {
            envelope = static_cast<double>(frames - 1 - i) / fadeFrames;
        }
        envelope = std::max(0.0, std::min(1.0, envelope));
        const double value = amplitude * envelope * std::sin(2.0 * kPi * frequency * t);
        for (int ch = 0; ch < channels; ++ch) {
            samples[static_cast<std::size_t>(i) * channels + ch] =
                static_cast<float>(value);
        }
    }
    return samples;
}

}  // namespace

// =============================================================================
// 隐式标识（元数据）
// =============================================================================

std::vector<AiMetadataEntry> buildAiMetadata(const std::string& generatorName,
                                             const std::string& producedBy) {
    const std::string generator = generatorName.empty() ? "NairoClip" : generatorName;
    std::vector<AiMetadataEntry> entries;
    // comment：通用可读说明（分发平台/工具链普遍展示该字段）。
    // 说明：MP4/MOV 容器的标准 tag 集合有限（muxer 会过滤未登记的自定义键），
    // 因此把「机器可读」的关键信息一并压缩进 comment（行业通行做法），
    // 同时保留下面两条自定义键——在支持的容器（MKV/WebM）中会作为独立字段
    // 落盘，在不支持的容器中自动被忽略（不报错，不损坏文件）。
    std::string comment = "本内容由人工智能生成合成（" + generator + "）；ai_generated=true";
    if (!producedBy.empty()) {
        comment += "；ai_generated_feature=" + producedBy;
    }
    entries.push_back(AiMetadataEntry{"comment", comment});
    // ai_generated：机器可读布尔（行业通行写法之一；MKV/WebM 等容器保留）
    entries.push_back(AiMetadataEntry{"ai_generated", "true"});
    // ai_generated_by：生成工具
    entries.push_back(AiMetadataEntry{"ai_generated_by", generator});
    // ai_generated_feature：触发标识的具体功能（便于审核定位）
    if (!producedBy.empty()) {
        entries.push_back(AiMetadataEntry{"ai_generated_feature", producedBy});
    }
    return entries;
}

bool parseAiMetadata(const std::vector<AiMetadataEntry>& entries, std::string* outFeature) {
    bool detected = false;
    for (const auto& entry : entries) {
        if (entry.key == "ai_generated") {
            // 值容差：true/1/yes（大小写不敏感）
            std::string value;
            for (char c : entry.value) {
                value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            if (value == "true" || value == "1" || value == "yes") {
                detected = true;
            }
        } else if (entry.key == "ai_generated_feature" && outFeature != nullptr) {
            *outFeature = entry.value;
        } else if (entry.key == "comment") {
            // 兼容 MP4 等容器：comment 内压缩了机器可读信息
            // （"…；ai_generated=true；ai_generated_feature=…"）
            if (entry.value.find("ai_generated=true") != std::string::npos) {
                detected = true;
            }
            const std::string marker = "ai_generated_feature=";
            const std::size_t pos = entry.value.find(marker);
            if (pos != std::string::npos && outFeature != nullptr && outFeature->empty()) {
                *outFeature = entry.value.substr(pos + marker.size());
            }
        }
    }
    return detected;
}

// =============================================================================
// 显式标识（音频提示音）
// =============================================================================

std::vector<float> generateAiToneMarks(const AiAudioMarkConfig& config) {
    if (!config.valid()) {
        return {};
    }
    const std::vector<float> tone =
        makeTone(config.toneFrequency, config.toneDurationSeconds, config.toneAmplitude,
                 config.sampleRate, config.channels);
    if (tone.empty()) {
        return {};
    }
    // 起始 + 末尾提示音（各一段；法规要求"起始/末尾/中间适当位置"）
    std::vector<float> result;
    if (config.atStart) {
        result.insert(result.end(), tone.begin(), tone.end());
    }
    if (config.atEnd) {
        result.insert(result.end(), tone.begin(), tone.end());
    }
    return result;
}

bool applyAiAudioMarks(std::vector<float>& samples, int channels, int sampleRate,
                       const AiAudioMarkConfig& config) {
    if (channels <= 0 || sampleRate <= 0) {
        return false;
    }
    if (samples.size() % static_cast<std::size_t>(channels) != 0) {
        return false;  // 交错缓冲不完整
    }
    if (!config.valid() || config.sampleRate != sampleRate) {
        return false;
    }
    const std::vector<float> startTone =
        config.atStart ? makeTone(config.toneFrequency, config.toneDurationSeconds,
                                  config.toneAmplitude, sampleRate, channels)
                       : std::vector<float>{};
    const std::vector<float> endTone =
        config.atEnd ? makeTone(config.toneFrequency, config.toneDurationSeconds,
                                config.toneAmplitude, sampleRate, channels)
                     : std::vector<float>{};
    if (startTone.empty() && endTone.empty()) {
        return false;
    }
    std::vector<float> result;
    result.reserve(samples.size() + startTone.size() + endTone.size());
    result.insert(result.end(), startTone.begin(), startTone.end());
    result.insert(result.end(), samples.begin(), samples.end());
    result.insert(result.end(), endTone.begin(), endTone.end());
    samples = std::move(result);
    return true;
}

const char* aiMarkingComplianceStatement() {
    return "本应用包含 AI 生成合成功能（智能配音/人声分离/字幕翻译/AI 视觉增强等）。"
           "依据《人工智能生成合成内容标识办法》，导出内容将添加显式标识"
           "（可见水印/音频提示音）与隐式标识（文件元数据），标识不可关闭；"
           "如需申请不含显式标识的版本，请通过反馈渠道提交申请（依法留存日志）";
}

}  // namespace core
}  // namespace nairoclip
