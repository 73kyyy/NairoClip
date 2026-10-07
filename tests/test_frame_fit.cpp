#include "test_framework.h"
#include "nairoclip/core/frame_fit.h"

#include <cstring>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 构造纯色 RGBA 图像（alpha 不透明）。
std::vector<std::uint8_t> solidImage(int w, int h, std::uint8_t r, std::uint8_t g,
                                     std::uint8_t b) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = 255;
    }
    return pixels;
}

/// 读取像素 RGB。
void pixelAt(const std::vector<std::uint8_t>& image, int width, int x, int y, int rgb[3]) {
    const std::size_t idx = (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) * 4;
    rgb[0] = image[idx];
    rgb[1] = image[idx + 1];
    rgb[2] = image[idx + 2];
}

/// 构造左右两色图像（左红右蓝），用于验证居中与方向。
std::vector<std::uint8_t> leftRightImage(int w, int h) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                 static_cast<std::size_t>(x)) * 4;
            if (x < w / 2) {
                pixels[idx] = 255;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 0;
            } else {
                pixels[idx] = 0;
                pixels[idx + 1] = 0;
                pixels[idx + 2] = 255;
            }
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

}  // namespace

// =============================================================================
// 几何计算
// =============================================================================

NC_TEST(FrameFit, FitLetterboxesWideSource) {
    // 16:9 源 → 9:16 画布：等比内缩，宽度占满，上下出现黑边（letterbox）。
    // 1080 * (1080/1920) = 607.5 → 四舍五入 608。
    FrameFitRect rect;
    NC_CHECK(computeFrameFitRect(FrameFit::Fit, 1920, 1080, 1080, 1920, rect));
    NC_CHECK_EQ(rect.dstW, 1080);
    NC_CHECK_EQ(rect.dstH, 608);
    NC_CHECK_EQ(rect.dstX, 0);
    NC_CHECK_EQ(rect.dstY, (1920 - 608) / 2);
    NC_CHECK_EQ(rect.srcW, 1920);
    NC_CHECK_EQ(rect.srcH, 1080);
}

NC_TEST(FrameFit, FitPillarboxesTallSource) {
    // 9:16 源 → 16:9 画布：左右黑边（pillarbox）。1920 * (1080/1920) = 607.5 → 608。
    FrameFitRect rect;
    NC_CHECK(computeFrameFitRect(FrameFit::Fit, 1080, 1920, 1920, 1080, rect));
    NC_CHECK_EQ(rect.dstH, 1080);
    NC_CHECK_EQ(rect.dstW, 608);
    NC_CHECK_EQ(rect.dstY, 0);
    NC_CHECK_EQ(rect.dstX, (1920 - 608) / 2);
}

NC_TEST(FrameFit, FitSameAspectFillsCanvas) {
    FrameFitRect rect;
    NC_CHECK(computeFrameFitRect(FrameFit::Fit, 1280, 720, 1920, 1080, rect));
    NC_CHECK_EQ(rect.dstW, 1920);
    NC_CHECK_EQ(rect.dstH, 1080);
    NC_CHECK_EQ(rect.dstX, 0);
    NC_CHECK_EQ(rect.dstY, 0);
}

NC_TEST(FrameFit, FillCoversCanvasAndCrops) {
    // 16:9 源 → 9:16 画布：等比放大铺满，水平溢出居中裁切。
    FrameFitRect rect;
    NC_CHECK(computeFrameFitRect(FrameFit::Fill, 1920, 1080, 1080, 1920, rect));
    NC_CHECK_EQ(rect.dstH, 1920);
    NC_CHECK_EQ(rect.dstW, 3413);
    NC_CHECK(rect.dstX < 0);
    NC_CHECK_EQ(rect.dstY, 0);
    NC_CHECK(rect.dstX <= 0);
    NC_CHECK(rect.dstX + rect.dstW >= 1080);
}

NC_TEST(FrameFit, CropTakesCenterOneToOne) {
    FrameFitRect rect;
    NC_CHECK(computeFrameFitRect(FrameFit::Crop, 1920, 1080, 1080, 1920, rect));
    NC_CHECK_EQ(rect.scale, 1.0);
    NC_CHECK_EQ(rect.srcW, 1080);
    NC_CHECK_EQ(rect.srcH, 1080);
    NC_CHECK_EQ(rect.srcX, 420);
    NC_CHECK_EQ(rect.srcY, 0);
    NC_CHECK_EQ(rect.dstW, 1080);
    NC_CHECK_EQ(rect.dstH, 1080);
    NC_CHECK_EQ(rect.dstX, 0);
    NC_CHECK_EQ(rect.dstY, (1920 - 1080) / 2);
}

NC_TEST(FrameFit, RectRejectsInvalidSizes) {
    FrameFitRect rect;
    NC_CHECK(!computeFrameFitRect(FrameFit::Fit, 0, 1080, 1080, 1920, rect));
    NC_CHECK(!computeFrameFitRect(FrameFit::Fit, 1920, 0, 1080, 1920, rect));
    NC_CHECK(!computeFrameFitRect(FrameFit::Fit, 1920, 1080, 0, 1920, rect));
    NC_CHECK(!computeFrameFitRect(FrameFit::Fit, 1920, 1080, 1080, -5, rect));
}

// =============================================================================
// 像素渲染
// =============================================================================

NC_TEST(FrameFit, RenderFitAddsBlackBars) {
    // 160x120（4:3）源 → 160x160（1:1）画布：Fit 上下各约 20px 黑边。
    const auto src = solidImage(160, 120, 200, 100, 50);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(160) * 160 * 4, 0);
    NC_CHECK(renderFrameFit(src.data(), 160, 120, 160 * 4, dst.data(), 160, 160,
                            FrameFit::Fit));
    int rgb[3];
    pixelAt(dst, 160, 80, 3, rgb);
    NC_CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);  // 顶部黑边
    pixelAt(dst, 160, 80, 80, rgb);
    NC_CHECK_EQ(rgb[0], 200);  // 中部源色
    NC_CHECK_EQ(rgb[1], 100);
    NC_CHECK_EQ(rgb[2], 50);
    pixelAt(dst, 160, 80, 156, rgb);
    NC_CHECK(rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0);  // 底部黑边
    bool alphaOk = true;
    for (std::size_t i = 3; i < dst.size(); i += 4) {
        if (dst[i] != 255) { alphaOk = false; break; }
    }
    NC_CHECK(alphaOk);
}

NC_TEST(FrameFit, RenderFillCropsNoBars) {
    // Fill：铺满无黑边，四角均为源色。
    const auto src = solidImage(160, 120, 30, 180, 90);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(160) * 160 * 4, 0);
    NC_CHECK(renderFrameFit(src.data(), 160, 120, 160 * 4, dst.data(), 160, 160,
                            FrameFit::Fill));
    int rgb[3];
    pixelAt(dst, 160, 0, 0, rgb);
    NC_CHECK(rgb[0] == 30 && rgb[1] == 180 && rgb[2] == 90);
    pixelAt(dst, 160, 159, 0, rgb);
    NC_CHECK(rgb[0] == 30 && rgb[1] == 180 && rgb[2] == 90);
    pixelAt(dst, 160, 0, 159, rgb);
    NC_CHECK(rgb[0] == 30 && rgb[1] == 180 && rgb[2] == 90);
    pixelAt(dst, 160, 159, 159, rgb);
    NC_CHECK(rgb[0] == 30 && rgb[1] == 180 && rgb[2] == 90);
}

NC_TEST(FrameFit, RenderFillPreservesLeftRightOrientation) {
    // 左右两色源 → Fill：左侧偏红、右侧偏蓝（方向不颠倒）。
    const auto src = leftRightImage(160, 120);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(160) * 160 * 4, 0);
    NC_CHECK(renderFrameFit(src.data(), 160, 120, 160 * 4, dst.data(), 160, 160,
                            FrameFit::Fill));
    int left[3];
    int right[3];
    pixelAt(dst, 160, 10, 80, left);
    pixelAt(dst, 160, 150, 80, right);
    NC_CHECK(left[0] > left[2]);
    NC_CHECK(right[2] > right[0]);
}

NC_TEST(FrameFit, RenderCropIsCenterExact) {
    // 192x108 → 96x96 Crop：取源中心 96x96（srcX=48），无插值。
    const auto src = leftRightImage(192, 108);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(96) * 96 * 4, 0);
    NC_CHECK(renderFrameFit(src.data(), 192, 108, 192 * 4, dst.data(), 96, 96,
                            FrameFit::Crop));
    int rgb[3];
    pixelAt(dst, 96, 5, 40, rgb);   // srcX=48+5=53 → 红
    NC_CHECK_EQ(rgb[0], 255);
    NC_CHECK_EQ(rgb[2], 0);
    pixelAt(dst, 96, 90, 40, rgb);  // srcX=48+90=138 → 蓝
    NC_CHECK_EQ(rgb[0], 0);
    NC_CHECK_EQ(rgb[2], 255);
}

NC_TEST(FrameFit, RenderFitBackgroundColorConfigurable) {
    const auto src = solidImage(160, 120, 10, 10, 10);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(160) * 160 * 4, 0);
    NC_CHECK(renderFrameFit(src.data(), 160, 120, 160 * 4, dst.data(), 160, 160,
                            FrameFit::Fit, 0x1E1E1EFF));
    int rgb[3];
    pixelAt(dst, 160, 80, 3, rgb);
    NC_CHECK_EQ(rgb[0], 0x1E);
    NC_CHECK_EQ(rgb[1], 0x1E);
    NC_CHECK_EQ(rgb[2], 0x1E);
    pixelAt(dst, 160, 80, 80, rgb);
    NC_CHECK_EQ(rgb[0], 10);
}

NC_TEST(FrameFit, RenderHandlesStride) {
    // stride 大于 width*4（对齐填充）时逐行读取正确。
    const int srcW = 4;
    const int srcH = 2;
    const int stride = srcW * 4 + 8;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(stride) * srcH, 0xEE);
    for (int y = 0; y < srcH; ++y) {
        for (int x = 0; x < srcW; ++x) {
            std::uint8_t* px = src.data() + static_cast<std::size_t>(y) * stride +
                               static_cast<std::size_t>(x) * 4;
            px[0] = 100;
            px[1] = 150;
            px[2] = 200;
            px[3] = 255;
        }
    }
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(srcW) * srcH * 4, 0);
    NC_CHECK(renderFrameFit(src.data(), srcW, srcH, stride, dst.data(), srcW, srcH,
                            FrameFit::Fit));
    int rgb[3];
    pixelAt(dst, srcW, 2, 1, rgb);
    NC_CHECK(rgb[0] == 100 && rgb[1] == 150 && rgb[2] == 200);
}

NC_TEST(FrameFit, RenderRejectsBadArgs) {
    const auto src = solidImage(16, 16, 1, 2, 3);
    std::vector<std::uint8_t> dst(16 * 16 * 4, 0);
    NC_CHECK(!renderFrameFit(nullptr, 16, 16, 64, dst.data(), 16, 16, FrameFit::Fit));
    NC_CHECK(!renderFrameFit(src.data(), 16, 16, 64, nullptr, 16, 16, FrameFit::Fit));
    NC_CHECK(!renderFrameFit(src.data(), 0, 16, 64, dst.data(), 16, 16, FrameFit::Fit));
    NC_CHECK(!renderFrameFit(src.data(), 16, 16, 64, dst.data(), 0, 16, FrameFit::Fit));
    NC_CHECK(!renderFrameFit(src.data(), 16, 16, 16, dst.data(), 16, 16, FrameFit::Fit));
}

// =============================================================================
// 名称与解析
// =============================================================================

NC_TEST(FrameFit, NamesAndParsing) {
    NC_CHECK_EQ(std::string(frameFitName(FrameFit::Fit)), std::string("fit"));
    NC_CHECK_EQ(std::string(frameFitName(FrameFit::Fill)), std::string("fill"));
    NC_CHECK_EQ(std::string(frameFitName(FrameFit::Crop)), std::string("crop"));

    FrameFit mode;
    NC_CHECK(frameFitFromInt(0, mode));
    NC_CHECK(mode == FrameFit::Fit);
    NC_CHECK(frameFitFromInt(2, mode));
    NC_CHECK(mode == FrameFit::Crop);
    NC_CHECK(!frameFitFromInt(7, mode));

    NC_CHECK(frameFitFromString("fill", mode));
    NC_CHECK(mode == FrameFit::Fill);
    NC_CHECK(frameFitFromString("CROP", mode));
    NC_CHECK(mode == FrameFit::Crop);
    NC_CHECK(!frameFitFromString("stretch", mode));
    NC_CHECK(!frameFitFromString(nullptr, mode));
}

// =============================================================================
// 集成：序列化持久化 + 导出计划透传 + 执行器真实渲染
// =============================================================================

#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/serialization.h"

namespace {
/// Mock 读取器：固定 4:3 源（160x120），用于验证混比例适配。
class WideSourceFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        MockMediaReader::Config cfg;
        cfg.probe.type = MediaType::Video;
        cfg.probe.frameRate = fps30();
        cfg.probe.width = 160;
        cfg.probe.height = 120;
        cfg.probe.duration = Rational::make(60);
        cfg.videoFrameCount = 100000;
        cfg.grayValue = 200;
        return std::make_unique<MockMediaReader>(cfg);
    }
};
}  // namespace

NC_TEST(FrameFit, SerializationRoundtripsFrameFit) {
    // 画幅设置必须持久化：导出预设是工程的一部分，重开项目应保持一致。
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 1080;
    seq.height = 1920;
    seq.frameFit = FrameFit::Fill;
    seq.backgroundRgba = 0x1E1E1EFF;
    project.sequences.push_back(seq);

    const std::string text = saveProjectToNclip(project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    NC_CHECK(!loaded.sequences.empty());
    NC_CHECK(loaded.sequences[0].frameFit == FrameFit::Fill);
    NC_CHECK_EQ(loaded.sequences[0].backgroundRgba, std::uint32_t(0x1E1E1EFF));
}

NC_TEST(FrameFit, DefaultIsFitAndOmittedFromFile) {
    // 默认 Fit 不应写入 JSON（保持旧工程 diff 稳定与向后兼容）。
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const std::string text = saveProjectToNclip(project);
    NC_CHECK(text.find("frameFit") == std::string::npos);
    NC_CHECK(text.find("backgroundRgba") == std::string::npos);
    // 旧工程（无该字段）载入后应为默认 Fit
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK(loaded.sequences[0].frameFit == FrameFit::Fit);
    NC_CHECK_EQ(loaded.sequences[0].backgroundRgba, std::uint32_t(0x000000FF));
}

NC_TEST(FrameFit, ExportPlanCarriesSequenceSettings) {
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 1080;
    seq.height = 1920;
    seq.frameFit = FrameFit::Crop;
    seq.backgroundRgba = 0xFF00FFFF;
    project.sequences.push_back(seq);
    const ExportPlan plan = compileExportPlan(project, project.sequences[0]);
    NC_CHECK(plan.frameFit == FrameFit::Crop);
    NC_CHECK_EQ(plan.backgroundRgba, std::uint32_t(0xFF00FFFF));
}

NC_TEST(FrameFit, ExportExecutorAppliesFillToMismatchedSource) {
    // 4:3 源(160x120) → 1:1 画布(160x160)：Fill 应铺满无黑边。
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    seq.width = 160;
    seq.height = 160;
    seq.frameFit = FrameFit::Fill;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;

    MediaAsset asset;
    asset.id = AssetId{project.ids.next()};
    asset.path = "/video/wide.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(60);
    project.assets.push_back(asset);
    const AssetId assetId = project.assets.front().id;

    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip clip;
    clip.asset = assetId;
    clip.start = Rational::make(0);
    clip.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(seqId, trackId, clip).isOk());

    const ExportPlan plan = compileExportPlan(project, project.sequences.front());
    WideSourceFactory factory;
    int checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                NC_CHECK_EQ(frame.width, 160);
                NC_CHECK_EQ(frame.height, 160);
                // Fill：四角均为源灰值 200（无黑边）
                const int corners[4][2] = {{0, 0}, {159, 0}, {0, 159}, {159, 159}};
                for (const auto& c : corners) {
                    const std::size_t idx = (static_cast<std::size_t>(c[1]) * 160 +
                                             static_cast<std::size_t>(c[0])) * 4;
                    NC_CHECK_EQ(frame.rgba[idx], std::uint8_t(200));
                }
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &project);
    NC_CHECK(checked > 0);
}

NC_TEST(FrameFit, ExportExecutorAppliesFitLetterboxToMismatchedSource) {
    // 4:3 源(160x120) → 1:1 画布(160x160)：Fit 应上下留黑边、中部为源。
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    seq.width = 160;
    seq.height = 160;
    seq.frameFit = FrameFit::Fit;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;

    MediaAsset asset;
    asset.id = AssetId{project.ids.next()};
    asset.path = "/video/wide.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(60);
    project.assets.push_back(asset);
    const AssetId assetId = project.assets.front().id;

    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip clip;
    clip.asset = assetId;
    clip.start = Rational::make(0);
    clip.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(seqId, trackId, clip).isOk());

    const ExportPlan plan = compileExportPlan(project, project.sequences.front());
    WideSourceFactory factory;
    int checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                const std::size_t topIdx = (static_cast<std::size_t>(2) * 160 + 80) * 4;
                NC_CHECK_EQ(frame.rgba[topIdx], std::uint8_t(0));  // 顶部黑边
                const std::size_t midIdx = (static_cast<std::size_t>(80) * 160 + 80) * 4;
                NC_CHECK_EQ(frame.rgba[midIdx], std::uint8_t(200));  // 中部源色
                const std::size_t botIdx = (static_cast<std::size_t>(157) * 160 + 80) * 4;
                NC_CHECK_EQ(frame.rgba[botIdx], std::uint8_t(0));  // 底部黑边
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &project);
    NC_CHECK(checked > 0);
}
