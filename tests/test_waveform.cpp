#include "test_framework.h"
#include "nairoclip/core/waveform.h"
#include <cmath>
using namespace nairoclip::core;
namespace {
AudioBlock makeBlock(double startSec, int frames, int channels, float value) {
    AudioBlock block;
    block.start = Rational::make(static_cast<std::int64_t>(startSec * 1000), 1000);
    block.sampleRate = 48000;
    block.channels = channels;
    block.samples.assign(static_cast<std::size_t>(frames) * channels, value);
    return block;
}
}  // namespace
NC_TEST(Waveform, EmptyInputReturnsEmpty) {
    const auto wf = buildWaveform({}, 100);
    NC_CHECK(wf.buckets.empty());
    NC_CHECK_EQ(wf.peak(), 0.0f);
}
NC_TEST(Waveform, ConstantSignal) {
    std::vector<AudioBlock> blocks = {makeBlock(0.0, 48000, 1, 0.5f)};
    const auto wf = buildWaveform(blocks, 10);
    NC_CHECK_EQ(wf.buckets.size(), std::size_t(10));
    NC_CHECK_EQ(wf.sampleRate, 48000);
    NC_CHECK(std::abs(wf.peak() - 0.5f) < 1e-6f);
    // 每个桶 min=max=0.5，rms=0.5
    for (const auto& b : wf.buckets) {
        NC_CHECK(std::abs(b.minValue - 0.5f) < 1e-6f);
        NC_CHECK(std::abs(b.maxValue - 0.5f) < 1e-6f);
        NC_CHECK(std::abs(b.rms - 0.5f) < 1e-6f);
    }
}
NC_TEST(Waveform, MixedSignalMinMax) {
    // 前半 -0.8，后半 +0.6
    AudioBlock first;
    first.start = Rational::make(0);
    first.sampleRate = 48000;
    first.channels = 1;
    first.samples.assign(24000, -0.8f);
    AudioBlock second;
    second.start = Rational::make(1, 2);
    second.sampleRate = 48000;
    second.channels = 1;
    second.samples.assign(24000, 0.6f);
    const auto wf = buildWaveform({first, second}, 2);
    NC_CHECK_EQ(wf.buckets.size(), std::size_t(2));
    // 第一桶 -0.8，第二桶 +0.6
    NC_CHECK(std::abs(wf.buckets[0].minValue - (-0.8f)) < 1e-6f);
    NC_CHECK(std::abs(wf.buckets[0].maxValue - (-0.8f)) < 1e-6f);
    NC_CHECK(std::abs(wf.buckets[1].minValue - 0.6f) < 1e-6f);
    NC_CHECK(std::abs(wf.buckets[1].maxValue - 0.6f) < 1e-6f);
    NC_CHECK(std::abs(wf.peak() - 0.8f) < 1e-6f);
}
NC_TEST(Waveform, StereoMonoMixdown) {
    // 立体声：L=1.0，R=-1.0 → 单声道平均 = 0
    AudioBlock block;
    block.start = Rational::make(0);
    block.sampleRate = 48000;
    block.channels = 2;
    for (int i = 0; i < 1000; ++i) {
        block.samples.push_back(1.0f);   // L
        block.samples.push_back(-1.0f);  // R
    }
    const auto wf = buildWaveform({block}, 1);
    NC_CHECK_EQ(wf.buckets.size(), std::size_t(1));
    // L+R 平均为 0
    NC_CHECK(std::abs(wf.buckets[0].minValue) < 1e-6f);
    NC_CHECK(std::abs(wf.buckets[0].maxValue) < 1e-6f);
}
NC_TEST(Waveform, SilenceDetection) {
    // 静音块 + 有声块 + 静音块
    std::vector<AudioBlock> blocks = {makeBlock(0.0, 4800, 1, 0.0f),
                                      makeBlock(1.0, 4800, 1, 0.5f),
                                      makeBlock(2.0, 4800, 1, 0.005f)};
    const auto silence = detectSilence(blocks, 0.01f);
    NC_CHECK_EQ(silence.size(), std::size_t(3));
    NC_CHECK(silence[0]);      // 0.0 < 0.01 → 静音
    NC_CHECK(!silence[1]);     // 0.5 → 有声
    NC_CHECK(silence[2]);      // 0.005 < 0.01 → 静音
}
NC_TEST(Waveform, DurationComputed) {
    // 2 块 x 4800 帧 @48000 = 0.2s
    std::vector<AudioBlock> blocks = {makeBlock(0.0, 4800, 1, 0.1f),
                                      makeBlock(0.1, 4800, 1, 0.1f)};
    const auto wf = buildWaveform(blocks, 20);
    NC_CHECK(std::abs(wf.duration.toDouble() - 0.2) < 1e-6);
}
