#pragma once

#include "nairoclip/core/media_io.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 音频波形生成器（Audio Waveform）
// =============================================================================
// 用途：时间轴音频轨波形显示 / 音频素材预览 / 音量检测。
// 输出：固定桶数的峰值（min/max 或 RMS）序列，供 UI 绘制。

/// 单桶波形数据。
struct WaveformBucket {
    float minValue = 0.0f;  // 桶内最小值 [-1,1]
    float maxValue = 0.0f;  // 桶内最大值 [-1,1]
    float rms = 0.0f;       // 桶内 RMS [0,1]
};

/// 波形结果。
struct Waveform {
    /// 桶序列（按时间升序）。
    std::vector<WaveformBucket> buckets;
    /// 覆盖的时间范围。
    Time start;
    Time duration;
    /// 采样来源格式。
    int sampleRate = 0;
    int channels = 0;

    /// 峰值绝对值（全波形最大）。
    float peak() const;
};

/// 从音频块序列生成波形。
/// - blocks：按时间升序的音频块（交错 float；采样率/声道需一致，首个为准）；
/// - bucketCount：目标桶数（波形分辨率）；
/// - 返回：波形（起始 = 首块 start；时长 = 末块结尾 - 首块 start）。
Waveform buildWaveform(const std::vector<AudioBlock>& blocks, int bucketCount);

/// 简单静音检测：给定阈值，返回各块是否静音（|峰值| < threshold）。
/// 用于自动剪辑中的静音段跳过（后续功能）与导入素材检测。
std::vector<bool> detectSilence(const std::vector<AudioBlock>& blocks, float threshold = 0.01f);

}  // namespace core
}  // namespace nairoclip
