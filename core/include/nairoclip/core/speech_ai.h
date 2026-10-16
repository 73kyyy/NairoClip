#pragma once
// =============================================================================
// 语音转字幕 AI（Speech AI）—— 文档 Part 4-A 3.2「识别字幕 / 识别歌词」真实推理
// =============================================================================
// 产品对应（原文 3.2）：
//   「语言选择 [下拉]：中文/英语/日语/韩语等
//     识别进度 [进度条] 0~100%
//     识别结果 [文本列表]」
//
// 本模块实现「真实推理链路」（Whisper-tiny ONNX 编码器 + 解码器）：
//   · 前处理：PCM float 采样 → 16kHz 重采样 → Mel 频谱（80 通道 × 3000 帧）；
//   · 编码：encoder.onnx（input_features → last_hidden_state [1,1500,384]）；
//   · 解码：decoder.onnx 贪心循环（起始 token → 逐 token 生成 → 遇到 EOT）；
//   · 输出：识别文本 + 时间戳分段（按 Whisper 的段落切分策略简化）。
//
// 可用性：需 ONNX Runtime + Whisper ONNX 模型（encoder + decoder）；
//         不可用时明确失败（不伪造识别结果）。
//
// 说明：真实模型推理耗时较长（CPU 上单段 30s 音频约数秒~数十秒），
//       平台层应经 AI 任务状态机（nc_ai_task_*）驱动进度与取消。

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nairoclip/core/ai_engine.h"

namespace nairoclip {
namespace core {

/// 识别语言（文档 3.2「语言选择」）。
enum class SpeechLanguage {
    Auto = 0,     ///< 自动检测
    Chinese = 1,  ///< 中文
    English = 2,  ///< 英语
    Japanese = 3, ///< 日语
    Korean = 4,   ///< 韩语
};

/// 语言名称（"auto"/"zh"/"en"/"ja"/"ko"；供 UI 与解码提示）。
const char* speechLanguageName(SpeechLanguage language);
/// 从整数解析（0..4）。
bool speechLanguageFromInt(int value, SpeechLanguage& out);
/// Whisper 起始 token 序列中的语言 token（简化：tiny 模型的多语言 token 表）。
int speechLanguageToken(SpeechLanguage language);

/// 识别分段（时间 + 文本）。
struct SpeechSegment {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    std::string text;
};

/// 语音识别器（Whisper encoder + decoder 会话复用；单实例单线程）。
class SpeechRecognizer {
public:
    SpeechRecognizer();
    ~SpeechRecognizer();
    SpeechRecognizer(const SpeechRecognizer&) = delete;
    SpeechRecognizer& operator=(const SpeechRecognizer&) = delete;

    /// 加载模型（encoder + decoder 路径）。
    bool load(const std::string& encoderPath, const std::string& decoderPath,
              std::string* outputFailure = nullptr);
    bool loaded() const { return loaded_; }
    void unload();

    /// 识别单声道 PCM（float，[-1,1]）。
    /// - samples：单声道 PCM；sampleRate 源采样率（内部重采样到 16kHz）；
    /// - language：语言（Auto 需模型自动检测；当前实现按中文/英语提示）；
    /// - outSegments：出参识别分段（文本 + 时间）；
    /// - progress：进度回调（0..1；返回 false 取消；可空）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：未加载 / 音频为空 / 推理失败 / 用户取消。
    bool transcribe(const float* samples, std::size_t frameCount, int sampleRate,
                    SpeechLanguage language, std::vector<SpeechSegment>& outSegments,
                    const std::function<bool(double)>& progress = nullptr,
                    std::string* outputFailure = nullptr);

    /// Mel 频谱参数（Whisper 标准；供测试与平台层对齐）。
    static constexpr int melBins() { return 80; }
    static constexpr int melFrames() { return 3000; }  ///< 30 秒窗口
    static constexpr int sampleRate16k() { return 16000; }

    /// 计算 Whisper Mel 频谱（80 × frames；供测试与诊断）。
    /// - samples 单声道 float；sampleRate 源采样率；frames 目标帧数。
    static bool computeMelSpectrogram(const float* samples, std::size_t frameCount,
                                      int sampleRate, int frames,
                                      std::vector<float>& outMel);

    /// 加载 BPE 词表（tokenizer.json；识别文本解码必需）。
    /// 说明：Whisper 的 50258 词表约 2.4MB，由平台层随模型分发；
    ///       未加载时 decodeTokens 返回空串（不伪造文本）。
    bool loadTokenizer(const std::string& tokenizerJsonPath,
                       std::string* outputFailure = nullptr);
    /// 是否已加载词表。
    bool tokenizerLoaded() const;
    /// token 序列 → 文本（BPE 字节级解码；未加载词表返回空串）。
    std::string decodeTokens(const std::vector<int>& tokens) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

/// 模块说明（供 UI/日志）。
const char* speechAiPurpose();

}  // namespace core
}  // namespace nairoclip
