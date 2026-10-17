#pragma once
// =============================================================================
// 人声分离 AI（Stem Separation）—— 文档 Part 3-A 音频增补「人声分离」真实推理
// =============================================================================
// 产品对应（原文音效增补）：
//   「分离模式 [Radio]：仅提取人声 / 仅提取伴奏 / 人声+伴奏（生成两条独立音轨）
//     分离强度 [滑块] 0~100  默认 80
//     处理进度 [进度条] 0~100%
//     ✓ 分离完成后自动生成"人声轨"和"伴奏轨"，可分别独立调整音量/静音/删除」
//
// 本模块实现「真实推理链路」（UVR-MDX-NET ONNX）：
//   · 前处理：PCM → STFT（n_fft=6144, hop=1024, dim_f=3072, dim_t=8）→ 频谱张量
//     [1, 4, 3072, 256]（4 通道 = 立体声实部/虚部交错）；
//   · 推理：UVR-MDX → 人声频谱（与输入同形）；
//   · 后处理：istft → 人声波形；伴奏 = 原混音 - 人声；
//   · 输出：两条独立音轨（人声 / 伴奏）供平台层落盘为独立音频轨
//     （文档：「自动生成"人声轨"和"伴奏轨"」）。
//
// 可用性：需 ONNX Runtime + UVR-MDX 模型；不可用时明确失败（不伪造分离结果）。
// 性能：CPU 处理 1 秒音频约 1-3 秒（取决于片段长度）；平台层应经
//       AI 任务状态机驱动进度与取消。

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nairoclip/core/ai_engine.h"

namespace nairoclip {
namespace core {

/// 分离模式（文档「分离模式」）。
enum class StemMode {
    VocalsOnly = 0,     ///< 仅提取人声
    InstrumentalOnly = 1, ///< 仅提取伴奏
    Both = 2,           ///< 人声 + 伴奏（两条独立音轨）
};

/// 模式名称（"vocals"/"instrumental"/"both"）。
const char* stemModeName(StemMode mode);
/// 从整数解析（0..2）。
bool stemModeFromInt(int value, StemMode& out);

/// 分离结果（单声道或双声道交错 PCM）。
struct StemResult {
    /// 人声轨（交错；channels 声道；sampleRate 采样率）。模式为 InstrumentalOnly 时为空。
    std::vector<float> vocals;
    /// 伴奏轨（交错）。模式为 VocalsOnly 时为空。
    std::vector<float> instrumental;
    int sampleRate = 0;
    int channels = 0;
    /// 是否有效（至少一条轨非空且参数合法）。
    bool valid() const {
        return sampleRate > 0 && channels > 0 &&
               (!vocals.empty() || !instrumental.empty());
    }
};

/// 人声分离推理器（UVR-MDX 会话复用；单实例单线程）。
class StemSeparator {
public:
    StemSeparator();
    ~StemSeparator();
    StemSeparator(const StemSeparator&) = delete;
    StemSeparator& operator=(const StemSeparator&) = delete;

    /// 加载模型（UVR-MDX .onnx 路径）。
    /// 校验：输入形状须为 [N,4,3072,256]（UVR-MDX 标准）。
    bool load(const std::string& modelPath, std::string* outputFailure = nullptr);
    bool loaded() const { return loaded_; }
    void unload();

    /// 分离音频。
    /// - samples：交错 PCM（[-1,1]）；frameCount：每声道帧数；channels：声道数
    ///   （1 或 2；>2 时按前两声道处理）；
    /// - sampleRate：采样率（内部重采样到 44100）；
    /// - mode：分离模式；
    /// - intensity：分离强度 0..100（控制掩码软硬：100 = 硬掩码；低值更平滑）；
    /// - out：出参分离结果；
    /// - progress：进度回调（0..1；返回 false 取消；可空）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：未加载 / 音频为空 / 推理失败 / 用户取消。
    bool separate(const float* samples, std::size_t frameCount, int channels, int sampleRate,
                  StemMode mode, double intensity, StemResult& out,
                  const std::function<bool(double)>& progress = nullptr,
                  std::string* outputFailure = nullptr);

    /// 模型频谱参数（UVR-MDX 标准；供测试与平台层对齐）。
    static constexpr int fftSize() { return 6144; }
    static constexpr int hopSize() { return 1024; }
    static constexpr int dimF() { return 3072; }
    static constexpr int dimT() { return 256; }
    static constexpr int modelSampleRate() { return 44100; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

/// 模块说明（供 UI/日志）。
const char* stemAiPurpose();

}  // namespace core
}  // namespace nairoclip
