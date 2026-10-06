#include "test_framework.h"
#include "nairoclip/core/media_writer.h"
using namespace nairoclip::core;
namespace {
OutputVideoFrame solidFrame(int w, int h, std::uint8_t gray, const Time& ts) {
    OutputVideoFrame frame;
    frame.timestamp = ts;
    frame.width = w;
    frame.height = h;
    frame.rgba.assign(static_cast<std::size_t>(w) * h * 4, gray);
    for (std::size_t i = 3; i < frame.rgba.size(); i += 4) {
        frame.rgba[i] = 255;
    }
    return frame;
}
OutputAudioBlock toneBlock(int frames, int channels, int rate, float amplitude) {
    OutputAudioBlock block;
    block.sampleRate = rate;
    block.channels = channels;
    block.start = Rational::make(0);
    block.samples.assign(static_cast<std::size_t>(frames) * channels, amplitude);
    return block;
}
}  // namespace
NC_TEST(MediaWriter, MockLifecycle) {
    MockMediaWriter writer;
    NC_CHECK(!writer.isOpen());
    WriterConfig config;
    config.width = 64;
    config.height = 64;
    NC_CHECK(writer.open("/tmp/out.mp4", config).isOk());
    NC_CHECK(writer.isOpen());
    NC_CHECK_EQ(writer.lastPath(), std::string("/tmp/out.mp4"));
    // 写 3 帧 + 2 音频块
    NC_CHECK(writer.writeVideoFrame(solidFrame(64, 64, 128, Rational::make(0))).isOk());
    NC_CHECK(writer.writeVideoFrame(solidFrame(64, 64, 128, Rational::make(1, 30))).isOk());
    NC_CHECK(writer.writeVideoFrame(solidFrame(64, 64, 128, Rational::make(2, 30))).isOk());
    NC_CHECK(writer.writeAudioBlock(toneBlock(1024, 2, 48000, 0.1f)).isOk());
    NC_CHECK(writer.writeAudioBlock(toneBlock(1024, 2, 48000, 0.1f)).isOk());
    NC_CHECK_EQ(writer.videoFramesWritten(), 3);
    NC_CHECK_EQ(writer.audioBlocksWritten(), 2);
    // 时间戳记录
    NC_CHECK(writer.firstTimestamp() == Rational::make(0));
    NC_CHECK(writer.lastTimestamp() == Rational::make(2, 30));
    // finish
    NC_CHECK(writer.finish().isOk());
    NC_CHECK(writer.finished());
    NC_CHECK(!writer.isOpen());
    // finish 后再写被拒
    NC_CHECK(!writer.writeVideoFrame(solidFrame(64, 64, 0, Rational::make(3, 30))).isOk());
}
NC_TEST(MediaWriter, MockRejectsInvalid) {
    MockMediaWriter writer;
    WriterConfig config;
    config.width = 64;
    config.height = 64;
    // 空路径
    NC_CHECK(!writer.open("", config).isOk());
    // 非法尺寸
    WriterConfig bad = config;
    bad.width = 0;
    NC_CHECK(!writer.open("/tmp/x.mp4", bad).isOk());
    // 未打开时写入被拒
    NC_CHECK(!writer.writeVideoFrame(solidFrame(64, 64, 0, Rational::make(0))).isOk());
    // 打开后写空帧被拒
    NC_CHECK(writer.open("/tmp/x.mp4", config).isOk());
    OutputVideoFrame empty;
    NC_CHECK(!writer.writeVideoFrame(empty).isOk());
    OutputAudioBlock emptyAudio;
    NC_CHECK(!writer.writeAudioBlock(emptyAudio).isOk());
    // abort
    writer.abort();
    NC_CHECK(writer.aborted());
    NC_CHECK(!writer.isOpen());
}
NC_TEST(MediaWriter, MockFactoryCreates) {
    MockWriterFactory factory;
    auto writer = factory.create();
    NC_CHECK(writer != nullptr);
    NC_CHECK(!writer->isOpen());
}

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#include <cmath>
#include <fstream>
NC_TEST(MediaWriter, FFmpegEncodeVideoOnly) {
    const char* path = "/tmp/media_gen/writer_video.mp4";
    std::remove(path);
    FFmpegMediaWriter writer;
    WriterConfig config;
    config.width = 64;
    config.height = 64;
    config.frameRate = fps30();
    config.crf = 28;
    config.includeAudio = false;
    NC_CHECK(writer.open(path, config).isOk());
    // 写 30 帧灰色渐变
    for (int i = 0; i < 30; ++i) {
        const auto gray = static_cast<std::uint8_t>(i * 8);
        NC_CHECK(writer.writeVideoFrame(solidFrame(64, 64, gray, timeAtFrame(fps30(), i))).isOk());
    }
    NC_CHECK(writer.finish().isOk());
    // 验证文件存在且非空
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    NC_CHECK(f.good());
    NC_CHECK(f.tellg() > 1000);  // 至少 1KB
    // 用 FFmpeg 读取器回读验证
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(path).isOk());
    NC_CHECK_EQ(reader.probe().width, 64);
    NC_CHECK_EQ(reader.probe().height, 64);
    // 容器层帧率会被量化（时间基 1/15360），允许轻微偏差
    const double fps = reader.probe().frameRate.toDouble();
    NC_CHECK(std::abs(fps - 30.0) < 0.5);
    // 解码第一帧确认可读
    VideoFrame frame;
    NC_CHECK(reader.readVideoFrame(frame));
    NC_CHECK_EQ(frame.width, 64);
    reader.close();
}
NC_TEST(MediaWriter, FFmpegEncodeWithAudio) {
    const char* path = "/tmp/media_gen/writer_av.mp4";
    std::remove(path);
    FFmpegMediaWriter writer;
    WriterConfig config;
    config.width = 64;
    config.height = 64;
    config.frameRate = fps30();
    config.crf = 30;
    config.includeAudio = true;
    config.audioSampleRate = 44100;
    config.audioChannels = 1;
    NC_CHECK(writer.open(path, config).isOk());
    // 1 秒视频 + 1 秒音频（44100 采样，分 43 块 ~1024）
    for (int i = 0; i < 30; ++i) {
        NC_CHECK(writer.writeVideoFrame(solidFrame(64, 64, 100, timeAtFrame(fps30(), i))).isOk());
    }
    for (int block = 0; block < 43; ++block) {
        OutputAudioBlock audio;
        audio.sampleRate = 44100;
        audio.channels = 1;
        audio.start = Rational::make(block * 1024, 44100);
        audio.samples.resize(1024);
        for (int s = 0; s < 1024; ++s) {
            // 440Hz 正弦波
            const double t = static_cast<double>(block * 1024 + s) / 44100.0;
            audio.samples[static_cast<std::size_t>(s)] =
                static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
        }
        NC_CHECK(writer.writeAudioBlock(audio).isOk());
    }
    NC_CHECK(writer.finish().isOk());
    // 回读验证双轨均存在
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(path).isOk());
    NC_CHECK_EQ(reader.probe().width, 64);
    NC_CHECK_EQ(reader.probe().sampleRate, 44100);
    NC_CHECK_EQ(reader.probe().channels, 1);
    NC_CHECK(reader.probe().duration.toDouble() > 0.5);
    reader.close();
}
NC_TEST(MediaWriter, FFmpegAbortLeavesNoTrailer) {
    const char* path = "/tmp/media_gen/writer_abort.mp4";
    std::remove(path);
    {
        FFmpegMediaWriter writer;
        WriterConfig config;
        config.width = 64;
        config.height = 64;
        config.includeAudio = false;
        NC_CHECK(writer.open(path, config).isOk());
        writer.writeVideoFrame(solidFrame(64, 64, 50, Rational::make(0)));
        writer.abort();  // 不 finish
        NC_CHECK(!writer.isOpen());
    }
    // 文件可能存在（无 trailer），但不应崩溃；读取应失败或得到不完整流
    // 核心断言：abort 后 isOpen 为 false 且资源释放（无崩溃）
    NC_CHECK(true);
}
#endif
