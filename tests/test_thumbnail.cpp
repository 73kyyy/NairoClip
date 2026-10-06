#include "test_framework.h"
#include "nairoclip/core/thumbnail.h"
#include "nairoclip/core/media_io.h"
using namespace nairoclip::core;
namespace {
MockMediaReader::Config thumbConfig() {
    MockMediaReader::Config cfg;
    cfg.probe.type = MediaType::Video;
    cfg.probe.frameRate = fps30();
    cfg.probe.width = 320;
    cfg.probe.height = 240;
    cfg.probe.duration = Rational::make(60);
    cfg.videoFrameCount = 100000;
    return cfg;
}
}  // namespace
NC_TEST(Thumbnail, DownscaleKeepsAspect) {
    // 320x240 → 160x120
    std::vector<std::uint8_t> src(static_cast<std::size_t>(320) * 240 * 4, 200);
    std::vector<std::uint8_t> dst;
    int w = 160, h = 120;
    NC_CHECK(downscaleRgba(src.data(), 320, 240, &w, &h, dst));
    NC_CHECK_EQ(dst.size(), std::size_t(160 * 120 * 4));
    for (const auto px : dst) {
        NC_CHECK_EQ(px, std::uint8_t(200));
    }
}
NC_TEST(Thumbnail, DownscaleRejectsNoop) {
    std::vector<std::uint8_t> src(16);
    std::vector<std::uint8_t> dst;
    int w = 800, h = 600;  // 比源大：拒绝
    NC_CHECK(!downscaleRgba(src.data(), 4, 4, &w, &h, dst));
}
NC_TEST(Thumbnail, GeneratesRequestedCount) {
    MockMediaReader reader(thumbConfig());
    ThumbnailConfig cfg;
    cfg.count = 8;
    cfg.maxWidth = 160;
    cfg.maxHeight = 90;
    const auto thumbs = generateThumbnails(reader, "/video.mp4", Rational::make(30), cfg);
    NC_CHECK_EQ(thumbs.size(), std::size_t(8));
    for (const auto& t : thumbs) {
        // 输出缩放至 <=160x90（320x240 → 等比 120x90）
        NC_CHECK(t.width <= 160);
        NC_CHECK(t.height <= 90);
        NC_CHECK_EQ(t.rgba.size(),
                    static_cast<std::size_t>(t.width) * static_cast<std::size_t>(t.height) * 4);
    }
    // 采样时间应居中分布（centered=true → 30s/8 = 每个区间中点，第一张 ≈ 1.875s）
    NC_CHECK(thumbs[0].sourceTime.toDouble() >= 1.5);
    NC_CHECK(thumbs[0].sourceTime.toDouble() <= 2.5);
    NC_CHECK(thumbs[7].sourceTime < Rational::make(30));
}
NC_TEST(Thumbnail, EmptyDurationReturnsEmpty) {
    MockMediaReader reader(thumbConfig());
    const auto thumbs = generateThumbnails(reader, "/video.mp4", Rational::make(0), {});
    NC_CHECK(thumbs.empty());
}

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include <fstream>
NC_TEST(Thumbnail, FFmpegRealVideo) {
    std::ifstream f("/tmp/media_gen/test_av.mp4");
    if (!f.good()) return;
    FFmpegMediaReader reader;
    ThumbnailConfig cfg;
    cfg.count = 6;
    cfg.maxWidth = 80;
    cfg.maxHeight = 60;
    const auto thumbs = generateThumbnails(reader, "/tmp/media_gen/test_av.mp4",
                                           Rational::make(3), cfg);
    NC_CHECK_EQ(thumbs.size(), std::size_t(6));
    for (const auto& t : thumbs) {
        NC_CHECK(t.width <= 80);
        NC_CHECK(t.height <= 60);
        NC_CHECK(!t.rgba.empty());
        // 必须有真实内容（testsrc 彩条）
        bool sawColor = false;
        for (std::size_t i = 0; i + 3 < t.rgba.size(); i += 4) {
            if (t.rgba[i] > 60 || t.rgba[i + 1] > 60 || t.rgba[i + 2] > 60) {
                sawColor = true;
                break;
            }
        }
        NC_CHECK(sawColor);
    }
}
#endif
