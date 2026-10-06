#include "test_framework.h"

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/media_ffmpeg.h"
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
#else
NC_TEST(MediaFFmpeg, NotCompiled) {
    // FFmpeg 未编译进：占位测试（确保测试框架不空）
    NC_CHECK(true);
}
#endif
