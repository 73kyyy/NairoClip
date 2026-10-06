#include "test_framework.h"
#include "nairoclip/core/media_io.h"
using namespace nairoclip::core;
namespace {
MockMediaReader::Config makeConfig() {
    MockMediaReader::Config cfg;
    cfg.probe.type = MediaType::Video;
    cfg.probe.duration = Rational::make(10);
    cfg.probe.frameRate = fps30();
    cfg.probe.width = 320;
    cfg.probe.height = 240;
    cfg.probe.sampleRate = 48000;
    cfg.probe.channels = 2;
    cfg.probe.codec = "mock";
    cfg.videoFrameCount = 5;
    cfg.audioBlockCount = 3;
    cfg.audioBlockFrames = 256;
    return cfg;
}
}  // namespace
NC_TEST(MediaIo, OpenCloseLifecycle) {
    MockMediaReader reader(makeConfig());
    NC_CHECK(!reader.isOpen());
    NC_CHECK(reader.open("/fake/video.mp4").isOk());
    NC_CHECK(reader.isOpen());
    NC_CHECK_EQ(reader.openCount(), 1);
    NC_CHECK(reader.lastOpenedPath() == std::string("/fake/video.mp4"));
    reader.close();
    NC_CHECK(!reader.isOpen());
}
NC_TEST(MediaIo, OpenFailure) {
    MockMediaReader::Config cfg = makeConfig();
    cfg.failOpen = true;
    MockMediaReader reader(cfg);
    const Result r = reader.open("/fake/video.mp4");
    NC_CHECK(!r.isOk());
    NC_CHECK(!reader.isOpen());
    // 空路径
    MockMediaReader reader2(makeConfig());
    NC_CHECK(!reader2.open("").isOk());
}
NC_TEST(MediaIo, ReadVideoFrames) {
    MockMediaReader reader(makeConfig());
    NC_CHECK(reader.open("/fake.mp4").isOk());
    int count = 0;
    VideoFrame frame;
    while (reader.readVideoFrame(frame)) {
        ++count;
        NC_CHECK_EQ(frame.width, 320);
        NC_CHECK_EQ(frame.height, 240);
        NC_CHECK_EQ(frame.stride, 320 * 4);
        NC_CHECK_EQ(frame.pixels.size(), std::size_t(320 * 240 * 4));
        // 时间戳 = frameIndex / fps
        NC_CHECK(frame.timestamp == timeAtFrame(fps30(), count - 1));
    }
    NC_CHECK_EQ(count, 5);
    // 再读返回 false
    NC_CHECK(!reader.readVideoFrame(frame));
}
NC_TEST(MediaIo, ReadAudioBlocks) {
    MockMediaReader reader(makeConfig());
    NC_CHECK(reader.open("/fake.mp4").isOk());
    int count = 0;
    AudioBlock block;
    while (reader.readAudioBlock(block)) {
        ++count;
        NC_CHECK_EQ(block.sampleRate, 48000);
        NC_CHECK_EQ(block.channels, 2);
        NC_CHECK_EQ(block.frameCount(), std::size_t(256));
        NC_CHECK_EQ(block.samples.size(), std::size_t(512));
        // 相位跟随绝对时间：首块首样点接近 0（sin(0)）
        if (count == 1) {
            NC_CHECK(block.samples[0] > -0.01f && block.samples[0] < 0.01f);
        }
        // 所有样本幅度不超过 0.25（正弦幅度）
        for (const float s : block.samples) {
            NC_CHECK(s >= -0.2501f && s <= 0.2501f);
        }
    }
    NC_CHECK_EQ(count, 3);
}
NC_TEST(MediaIo, SeekResetsStreams) {
    MockMediaReader reader(makeConfig());
    NC_CHECK(reader.open("/fake.mp4").isOk());
    VideoFrame f;
    NC_CHECK(reader.readVideoFrame(f));
    NC_CHECK(f.timestamp == Rational::make(0));
    NC_CHECK(reader.seek(fromMilliseconds(500)).isOk());
    NC_CHECK(reader.readVideoFrame(f));
    // seek 后第一帧时间戳 = seek 基准
    NC_CHECK(f.timestamp == fromMilliseconds(500));
    // 继续读，第二帧 = 基准 + 1/30
    NC_CHECK(reader.readVideoFrame(f));
    NC_CHECK(f.timestamp == fromMilliseconds(500) + timeAtFrame(fps30(), 1));
}
NC_TEST(MediaIo, SeekBeforeOpenFails) {
    MockMediaReader reader(makeConfig());
    NC_CHECK(!reader.seek(Rational::make(1)).isOk());
}
