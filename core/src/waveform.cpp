#include "nairoclip/core/waveform.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

float Waveform::peak() const {
    float p = 0.0f;
    for (const auto& b : buckets) {
        p = std::max(p, std::max(std::abs(b.minValue), std::abs(b.maxValue)));
    }
    return p;
}

Waveform buildWaveform(const std::vector<AudioBlock>& blocks, int bucketCount) {
    Waveform result;
    if (blocks.empty() || bucketCount <= 0) {
        return result;
    }
    // 计算总采样帧数与时序
    std::int64_t totalFrames = 0;
    const int channels = blocks.front().channels > 0 ? blocks.front().channels : 1;
    const int sampleRate = blocks.front().sampleRate > 0 ? blocks.front().sampleRate : 48000;
    result.sampleRate = sampleRate;
    result.channels = channels;
    result.start = blocks.front().start;

    // 以 48000 采样率的等价位计算总帧数（跨块统一）
    for (const auto& block : blocks) {
        totalFrames += static_cast<std::int64_t>(block.frameCount());
    }
    if (totalFrames <= 0) {
        return result;
    }
    result.duration = Rational::make(totalFrames, sampleRate);

    result.buckets.resize(static_cast<std::size_t>(bucketCount));
    const std::int64_t framesPerBucket =
        std::max<std::int64_t>(1, totalFrames / bucketCount);

    std::int64_t globalFrame = 0;
    // 标记各桶是否已有样本（正信号时 min 初始化 0 会错误）
    std::vector<bool> bucketTouched(static_cast<std::size_t>(bucketCount), false);
    for (const auto& block : blocks) {
        const std::int64_t frames = static_cast<std::int64_t>(block.frameCount());
        const std::int64_t ch = std::max(1, block.channels);
        for (std::int64_t f = 0; f < frames; ++f, ++globalFrame) {
            const std::size_t bucketIndex = static_cast<std::size_t>(
                std::min<std::int64_t>(globalFrame / framesPerBucket, bucketCount - 1));
            WaveformBucket& bucket = result.buckets[bucketIndex];
            // 混音到单声道（取各声道平均）
            double sum = 0.0;
            for (int c = 0; c < ch; ++c) {
                const std::size_t idx =
                    static_cast<std::size_t>(f) * static_cast<std::size_t>(ch) +
                    static_cast<std::size_t>(c);
                if (idx < block.samples.size()) {
                    sum += block.samples[idx];
                }
            }
            const float mono = static_cast<float>(sum / static_cast<double>(ch));
            if (!bucketTouched[bucketIndex]) {
                bucket.minValue = mono;
                bucket.maxValue = mono;
                bucketTouched[bucketIndex] = true;
            } else {
                bucket.minValue = std::min(bucket.minValue, mono);
                bucket.maxValue = std::max(bucket.maxValue, mono);
            }
            bucket.rms += mono * mono;
        }
    }
    // 完成 RMS 计算
    for (auto& bucket : result.buckets) {
        bucket.rms = std::sqrt(bucket.rms / static_cast<float>(framesPerBucket));
    }
    return result;
}

std::vector<bool> detectSilence(const std::vector<AudioBlock>& blocks, float threshold) {
    std::vector<bool> silence;
    silence.reserve(blocks.size());
    for (const auto& block : blocks) {
        float peak = 0.0f;
        for (const float s : block.samples) {
            peak = std::max(peak, std::abs(s));
        }
        silence.push_back(peak < threshold);
    }
    return silence;
}

}  // namespace core
}  // namespace nairoclip
