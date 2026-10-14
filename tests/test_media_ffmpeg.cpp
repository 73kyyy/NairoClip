#include "test_framework.h"

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/media_ffmpeg.h"
#include <cmath>
#include <cstdio>
#include <fstream>
using namespace nairoclip::core;
namespace {
/// 检测测试素材是否存在（生成于 /tmp/media_gen/test_av.mp4）。
const char* kTestMedia = "/tmp/media_gen/test_av.mp4";
bool testMediaExists() {
    std::ifstream f(kTestMedia);
    return f.good();
}
}  // namespace
NC_TEST(MediaFFmpeg, ProbeRealFile) {
    if (!testMediaExists()) {
        return;  // 素材未生成：跳过（CI 无 ffmpeg 时）
    }
    FFmpegMediaReader reader;
    NC_CHECK(!reader.isOpen());
    const Result r = reader.open(kTestMedia);
    NC_CHECK(r.isOk());
    NC_CHECK(reader.isOpen());
    const MediaProbeResult& probe = reader.probe();
    NC_CHECK_EQ(probe.width, 160);
    NC_CHECK_EQ(probe.height, 120);
    NC_CHECK(probe.frameRate.value == Rational::make(30));
    NC_CHECK(std::abs(probe.duration.toDouble() - 3.0) < 0.1);
    NC_CHECK_EQ(probe.sampleRate, 44100);
    NC_CHECK_EQ(probe.channels, 1);
    NC_CHECK(probe.codec == std::string("h264"));
    reader.close();
    NC_CHECK(!reader.isOpen());
}
NC_TEST(MediaFFmpeg, OpenNonexistentFails) {
    FFmpegMediaReader reader;
    const Result r = reader.open("/tmp/does_not_exist_xyz.mp4");
    NC_CHECK(!r.isOk());
    NC_CHECK(!reader.isOpen());
}
NC_TEST(MediaFFmpeg, DecodeVideoFrames) {
    if (!testMediaExists()) return;
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(kTestMedia).isOk());
    // 顺序解码 5 帧
    int decoded = 0;
    VideoFrame frame;
    Time lastTs;
    while (reader.readVideoFrame(frame) && decoded < 5) {
        ++decoded;
        NC_CHECK_EQ(frame.width, 160);
        NC_CHECK_EQ(frame.height, 120);
        NC_CHECK_EQ(frame.stride, 160 * 4);
        NC_CHECK_EQ(frame.pixels.size(), std::size_t(160 * 120 * 4));
        // 时间戳递增
        NC_CHECK(frame.timestamp >= lastTs);
        lastTs = frame.timestamp;
    }
    NC_CHECK_EQ(decoded, 5);
}
NC_TEST(MediaFFmpeg, SeekAndDecode) {
    if (!testMediaExists()) return;
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(kTestMedia).isOk());
    NC_CHECK(reader.seek(fromMilliseconds(1500)).isOk());
    VideoFrame frame;
    NC_CHECK(reader.readVideoFrame(frame));
    // seek 到 1.5s：第一帧时间戳应 >= 1.5s 附近（关键帧对齐容差 0.5s）
    NC_CHECK(frame.timestamp.toDouble() >= 1.4);
    NC_CHECK(frame.timestamp.toDouble() <= 2.0);
}
NC_TEST(MediaFFmpeg, DecodeAudioBlocks) {
    if (!testMediaExists()) return;
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(kTestMedia).isOk());
    int blocks = 0;
    AudioBlock block;
    long totalFrames = 0;
    while (reader.readAudioBlock(block) && blocks < 10) {
        ++blocks;
        NC_CHECK_EQ(block.sampleRate, 44100);
        NC_CHECK_EQ(block.channels, 1);
        NC_CHECK(!block.samples.empty());
        totalFrames += static_cast<long>(block.frameCount());
        // 幅值在 [-1,1]
        for (const float s : block.samples) {
            NC_CHECK(s >= -1.0f && s <= 1.0f);
        }
    }
    NC_CHECK(blocks > 0);
    NC_CHECK(totalFrames > 0);
}
NC_TEST(MediaFFmpeg, AvailableFlag) {
    NC_CHECK(ffmpegAvailable());
}

NC_TEST(MediaFFmpeg, AudioSeekLandingAccuracy) {
    // 回归：含视频 MP4 的音频 seek 必须采样精确落在目标时间
    // （历史缺陷：按 stream_index=-1 定位只移动到视频关键帧，音频内容错位；
    //  实测 seek(3s) 读到 0s 处内容。修复：按音频流索引定位 + 目标前样本裁剪）
    if (!testMediaExists()) return;
    std::ifstream probe("/tmp/media_gen/tone_shift.mp4");
    if (!probe.good()) return;  // 素材未生成：跳过
    FFmpegMediaReader reader;
    NC_CHECK(reader.open("/tmp/media_gen/tone_shift.mp4").isOk());
    // 素材：0-2s 440Hz / 2-4s 3520Hz（视频轨关键帧不与之对齐）
    struct Case {
        double target;
        double expectLow;   // 期望主频区间下限
        double expectHigh;  // 上限
    };
    const Case cases[] = {{0.5, 400.0, 480.0}, {3.0, 3400.0, 3640.0}, {3.5, 3400.0, 3640.0}};
    for (const auto& c : cases) {
        NC_CHECK(reader.seek(Rational::make(static_cast<long long>(c.target * 1000000),
                                            1000000))
                     .isOk());
        AudioBlock block;
        NC_CHECK(reader.readAudioBlock(block));
        // 块起点应采样精确（容差 1ms；有理数微秒级定位）
        NC_CHECK(std::abs(block.start.toDouble() - c.target) < 0.001);
        // 主频判定（Goertzel 比较 440 vs 3520）
        auto energyAt = [&block](double f) {
            const double w = 2.0 * 3.141592653589793 * f / block.sampleRate;
            double re = 0.0;
            double im = 0.0;
            for (std::size_t i = 0; i < block.samples.size(); ++i) {
                re += block.samples[i] * std::cos(w * static_cast<double>(i));
                im += block.samples[i] * std::sin(w * static_cast<double>(i));
            }
            return re * re + im * im;
        };
        const double domFreq = energyAt(440.0) > energyAt(3520.0) ? 440.0 : 3520.0;
        NC_CHECK(domFreq >= c.expectLow && domFreq <= c.expectHigh);
    }
}
#else
NC_TEST(MediaFFmpeg, NotCompiled) {
    // FFmpeg 未编译进：占位测试（确保测试框架不空）
    NC_CHECK(true);
}
#endif
