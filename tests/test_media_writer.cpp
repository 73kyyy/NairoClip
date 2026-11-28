#include "test_framework.h"
#include "nairoclip/core/media_writer.h"
#include "nairoclip/core/codec_policy.h"
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
/* 测试编码格式偏好为 VP9，但产品策略只允许平台系统媒体编码器。
   CI/沙箱没有可用硬件设备时，端到端用例必须跳过；不能使用 libvpx
   等应用自带软件编码器伪造系统编码能力。 */
inline std::string testVideoCodec() { return "vp9"; }

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
    config.videoCodec = testVideoCodec();  // 本机无硬件 H.264 编码器
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
    config.videoCodec = testVideoCodec();  // 本机无硬件 H.264 编码器
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
#include "nairoclip/core/media_ffmpeg_writer.h"
NC_TEST(MediaWriter, FFmpegWriterRejectsAudioUntilSystemMuxerIsConnected) {
    FFmpegMediaWriter writer;
    WriterConfig config;
    config.videoCodec = "vp9";
    config.width = 64;
    config.height = 64;
    config.includeAudio = true;
    const Result result = writer.open("/tmp/media_gen/system_aac_required.mp4", config);
    NC_CHECK(!result.isOk());
    NC_CHECK(result.code == ErrorCode::Unsupported);
    NC_CHECK(result.message.find("系统 AAC") != std::string::npos);
    NC_CHECK(!writer.isOpen());
    writer.abort();
    NC_CHECK(!writer.isOpen());
}

NC_TEST(MediaWriter, PolicyRejectsBundledSoftwareVp9Encoder) {
    EncoderCandidate software;
    software.name = "libvpx-vp9";
    software.codec = "vp9";
    software.hardware = false;
    const auto ranked =
        rankVideoEncoderCandidates("vp9", {software}, EncoderPolicy{});
    NC_CHECK(ranked.empty());
    NC_CHECK(!encoderAllowed("libvpx-vp9", EncoderPolicy{}));
}
#endif
