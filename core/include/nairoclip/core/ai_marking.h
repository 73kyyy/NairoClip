#pragma once
// =============================================================================
// AI 生成合成内容标识（显式 + 隐式）—— 《人工智能生成合成内容标识办法》
//   （2025-09-01 施行）+ 文档「一、AI生成内容标识办法合规」最高优先级缺口
// =============================================================================
// 法规要求（原文摘要）：
//   · 显式标识（用户可感知）：
//       音频 → 在起始/末尾/中间适当位置添加语音提示或音频节奏提示；
//       图片/视频 → 在适当位置添加显著提示标识；
//   · 隐式标识（元数据层面，机器可读）：在文件元数据中嵌入「内容由 AI 生成/
//     合成」的标识信息；
//   · 日志留存：提供「不含显式标识版本」须依法留存不少于 6 个月日志（平台层
//     职责，见 docs/COMPLIANCE.md）；
//   · 标识义务不可被普通用户关闭（与 ai_label 的既有原则一致）。
//
// 与「隐形水印」的关系（职责分离，不混用）：
//   · 隐形水印（invisible_watermark）：内嵌账号哈希 → 用途 = 泄露溯源；
//   · 本模块隐式标识：内嵌「AI 生成」内容属性（明文元数据键值）→ 用途 =
//     合规告知（分发平台/监管可读）。
//   两者共存、互不替代（文档明确要求"不能混用同一套机制敷衍了事"）。
//
// 显式标识的触发范围（文档 1.1 列举）：
//   · TTS 文本转语音（合成音频）→ 音频显式标识（提示音）；
//   · 人声分离 / 字幕翻译 / AI 视觉增强（超分、抠像、景深等）→ 画面或音频
//     显式标识（本模块的 video 标识沿用 ai_label 的可见渲染项）；
//   · 自动剪辑（参考风格生成）等既有触发条件保持（ai_label）。
//
// 实现说明（零第三方依赖、可测）：
//   · 音频提示音：程序生成正弦提示音（默认 880Hz 0.3s 双声道；起始与末尾各一）
//     —— 不依赖素材文件，确定性可测；音量与音色可配置；
//   · 元数据标识：产出标准键值对清单（导出器写入容器元数据；FFmpeg 用
//     av_dict_set 写入 format/stream metadata），键名采用行业通行写法
//     （comment / ai_generated / ai_generated_by 等），值为固定文案。

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 隐式标识（元数据）键值对。
struct AiMetadataEntry {
    std::string key;
    std::string value;
};

/// 构建 AI 隐式标识元数据（供导出器写入文件容器元数据）。
/// - generatorName：生成工具名（默认 "NairoClip"；平台层可注入版本号）；
/// - producedBy：触发标识的功能描述（如 "TTS 智能配音" / "AI 超分辨率"）；
/// - 返回固定顺序的键值对（同一输入 → 同一输出，可测）：
///   comment / ai_generated / ai_generated_by / ai_generated_feature。
std::vector<AiMetadataEntry> buildAiMetadata(const std::string& generatorName,
                                             const std::string& producedBy);

/// 从元数据键值对中识别隐式标识（供导入时检测"该素材为 AI 生成"）。
/// 返回 true = 检出（outFeature 填入触发功能，可空）。
bool parseAiMetadata(const std::vector<AiMetadataEntry>& entries, std::string* outFeature);

/// 音频显式标识配置（法规：起始/末尾/中间适当位置的语音提示或音频节奏提示）。
struct AiAudioMarkConfig {
    /// 提示音频率（Hz；默认 880 = A5，清晰可辨且不刺耳）。
    double toneFrequency = 880.0;
    /// 提示音时长（秒；默认 0.3）。
    double toneDurationSeconds = 0.3;
    /// 提示音幅值（0..1；默认 0.25，低于典型对白电平但可感知）。
    double toneAmplitude = 0.25;
    /// 起始提示音是否启用（法规要求"起始/末尾/中间"至少其一；默认首尾都加）。
    bool atStart = true;
    /// 末尾提示音是否启用。
    bool atEnd = true;
    /// 采样率（Hz；默认 48000）。
    int sampleRate = 48000;
    /// 声道数（默认 2）。
    int channels = 2;

    bool valid() const {
        return toneFrequency > 0.0 && toneDurationSeconds > 0.0 && toneAmplitude > 0.0 &&
               toneAmplitude <= 1.0 && sampleRate > 0 && channels > 0 &&
               (atStart || atEnd);
    }
};

/// 生成提示音采样（交错 float；含淡入淡出包络避免爆音）。
/// 返回样本数（= 帧数 × 声道）；参数非法返回空。
std::vector<float> generateAiToneMarks(const AiAudioMarkConfig& config);

/// 在音频块前/后追加显式标识提示音（导出管线用）。
/// - samples：交错 float 缓冲（原地修改：前置起始音、后置末尾音）；
/// - 说明：导出收尾时调用（对完整输出追加）；块式流式导出由平台层在首块/末块
///   调用 appendAtStart / appendAtEnd 变体。
/// 参数非法返回 false 且不改动。
bool applyAiAudioMarks(std::vector<float>& samples, int channels, int sampleRate,
                       const AiAudioMarkConfig& config);

/// 法规合规文案（供「关于-开源软件许可」/隐私政策引用）。
const char* aiMarkingComplianceStatement();

}  // namespace core
}  // namespace nairoclip
