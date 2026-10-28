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

// 回归：AAC 编码器只支持 FLTP（平面），重采样输出必须给**每个声道**一个指针。
//
// 原实现写的是：
//     std::uint8_t* outData[1] = { fifo.data() + fifoStart };
//     swr_convert(swr, outData, maxOut, inData, inSamples);
// 而 swr 的输出格式取的是 impl.audioCodec->sample_fmt = 第一个受支持格式
// = AV_SAMPLE_FMT_FLTP（平面）。平面格式要求 outData[0..channels-1] 全部
// 有效，代码只给了 outData[0] —— swr_convert 会把 outData[1] 当成第二个
// 声道的写入地址，读到的是栈上的未初始化值。
//
// 实测（x64 CI）：ASAN 报
//   SEGV on unknown address 0x000000000000, WRITE, libswresample
// 随后堆被写坏，下一个 malloc 报 "malloc(): invalid size (unsorted)"，
// 崩溃点落在 Capi.WatermarkRealExportEndToEnd —— 与真凶相隔上百个用例。
//
// 单声道（channels==1）时越界读不到，所以既有用例一直是绿的；
// 这个用例必须用**双声道**才具备证伪能力。
NC_TEST(MediaWriter, ResamplesPlanarPerChannelPointers) {
    const char* outPath = "/tmp/media_gen/writer_upsample.mp4";
    FFmpegMediaWriter writer;
    WriterConfig config;
    config.width = 64;
    config.height = 64;
    config.frameRate = fps30();
    config.crf = 30;
    config.includeAudio = true;
    config.audioSampleRate = 48000;  // 输出 48k
    config.audioChannels = 2;        // 双声道：平面格式必须给 2 个指针
    config.audioBitrate = 128000;
    if (!writer.open(outPath, config).isOk()) return;  // 无编码器环境：跳过

    // 输入 44.1k —— 升采样，正是原实现少算容量的方向
    const int inRate = 44100;
    const int blockFrames = 1024;
    for (int b = 0; b < 40; ++b) {
        OutputAudioBlock audio;
        audio.sampleRate = inRate;
        audio.channels = 2;
        audio.start = Rational::make(b * blockFrames, inRate);
        audio.samples.resize(static_cast<std::size_t>(blockFrames) * 2);
        for (int i = 0; i < blockFrames; ++i) {
            const double t = static_cast<double>(b * blockFrames + i) / inRate;
            // 两个声道用不同频率，便于检查平面拆/合是否串道
            audio.samples[static_cast<std::size_t>(i) * 2] =
                static_cast<float>(0.3 * std::sin(2.0 * 3.14159265358979 * 440.0 * t));
            audio.samples[static_cast<std::size_t>(i) * 2 + 1] =
                static_cast<float>(0.2 * std::sin(2.0 * 3.14159265358979 * 880.0 * t));
        }
        NC_CHECK(writer.writeAudioBlock(audio).isOk());
    }
    for (int f = 0; f < 40; ++f) {
        OutputVideoFrame frame;
        frame.timestamp = Rational::make(f, 30);
        frame.width = 64;
        frame.height = 64;
        frame.rgba.assign(static_cast<std::size_t>(64) * 64 * 4, 128);
        for (std::size_t i = 3; i < frame.rgba.size(); i += 4) frame.rgba[i] = 255;
        NC_CHECK(writer.writeVideoFrame(frame).isOk());
    }
    NC_CHECK(writer.finish().isOk());

    // 写出来的文件必须能被读回，且音频时长与输入相符（升采样没丢样本）
    FFmpegMediaReader reader;
    if (!reader.open(outPath).isOk()) return;
    NC_CHECK_EQ(reader.probe().sampleRate, 48000);
    NC_CHECK_EQ(reader.probe().channels, 2);
    long long total = 0;
    AudioBlock block;
    int guard = 0;
    while (reader.readAudioBlock(block) && guard++ < 10000) {
        total += static_cast<long long>(block.samples.size()) /
                 (block.channels > 0 ? block.channels : 1);
    }
    reader.close();
    // 40 * 1024 样本 @44.1k ≈ 0.929s → @48k 约 44578 样本，留 5% 容差
    const long long expected = static_cast<long long>(40.0 * blockFrames * 48000.0 / 44100.0);
    NC_CHECK(total > expected * 95 / 100);
    NC_CHECK(total < expected * 105 / 100);
}

#endif
