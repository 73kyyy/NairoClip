#include "test_framework.h"
#include "nairoclip/core/watermark.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 构造纯色 RGBA 画面（alpha 不透明）。
std::vector<std::uint8_t> solidFrame(int w, int h, std::uint8_t value) {
    std::vector<std::uint8_t> pixels(
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, value);
    for (std::size_t i = 3; i < pixels.size(); i += 4) {
        pixels[i] = 255;
    }
    return pixels;
}

/// 统计矩形区域内满足条件的像素数。
int countPixels(const std::vector<std::uint8_t>& frame, int frameW, int x0, int y0, int x1, int y1,
                bool (*pred)(const std::uint8_t*)) {
    int count = 0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t idx =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(frameW) +
                 static_cast<std::size_t>(x)) * 4;
            if (pred(frame.data() + idx)) {
                ++count;
            }
        }
    }
    return count;
}

bool isBright(const std::uint8_t* px) { return px[0] > 150 && px[1] > 150 && px[2] > 150; }
bool isNotPure(const std::uint8_t* px) { return px[0] != 255 || px[1] != 255 || px[2] != 255; }

}  // namespace

// =============================================================================
// 几何（文档 6.3.9：右下角 / 16px@1080p / 宽度 8%）
// =============================================================================

NC_TEST(Watermark, GeometryAt1080p) {
    const WatermarkSpec spec;  // 默认：8% 宽、16px 边距 @1080
    WatermarkGeometry g;
    NC_CHECK(computeWatermarkGeometry(spec, 1920, 1080, g));
    NC_CHECK_EQ(g.marginPx, 16);                       // 16px @ 1080p 基准
    NC_CHECK_EQ(g.blockW, 154);                        // round(1920 × 0.08) = 153.6 → 154
    NC_CHECK_EQ(g.blockH, 46);                         // round(154 × 0.30) = 46
    NC_CHECK_EQ(g.blockX, 1920 - 16 - 154);            // 右对齐
    NC_CHECK_EQ(g.blockY, 1080 - 16 - 46);             // 下对齐
    NC_CHECK(g.blockX + g.blockW + g.marginPx == 1920);
    NC_CHECK(g.blockY + g.blockH + g.marginPx == 1080);
}

NC_TEST(Watermark, GeometryVerticalFrame) {
    // 竖屏 9:16：短边 1080 → 边距仍 16；宽度 = 画面宽度 × 8%
    const WatermarkSpec spec;
    WatermarkGeometry g;
    NC_CHECK(computeWatermarkGeometry(spec, 1080, 1920, g));
    NC_CHECK_EQ(g.marginPx, 16);
    NC_CHECK_EQ(g.blockW, 86);  // round(1080 × 0.08) = 86.4 → 86
    NC_CHECK_EQ(g.blockX, 1080 - 16 - 86);
}

NC_TEST(Watermark, GeometryScalesMarginWithShortSide) {
    // 16px @ 1080p 基准 → 4K（短边 2160）应为 32px；720p（短边 720）应为 round(10.67)=11。
    const WatermarkSpec spec;
    WatermarkGeometry g4k;
    NC_CHECK(computeWatermarkGeometry(spec, 3840, 2160, g4k));
    NC_CHECK_EQ(g4k.marginPx, 32);
    WatermarkGeometry g720;
    NC_CHECK(computeWatermarkGeometry(spec, 1280, 720, g720));
    NC_CHECK_EQ(g720.marginPx, 11);
}

NC_TEST(Watermark, GeometryRejectsInvalidSizes) {
    const WatermarkSpec spec;
    WatermarkGeometry g;
    NC_CHECK(!computeWatermarkGeometry(spec, 0, 1080, g));
    NC_CHECK(!computeWatermarkGeometry(spec, 1920, 0, g));
    NC_CHECK(!computeWatermarkGeometry(spec, -5, 1080, g));
}

NC_TEST(Watermark, GeometryClampsToFrameForTinyFrames) {
    // 极小画面：几何必须仍落在画面内（不越界、不崩溃）
    const WatermarkSpec spec;
    WatermarkGeometry g;
    NC_CHECK(computeWatermarkGeometry(spec, 8, 8, g));
    NC_CHECK(g.blockW >= 1);
    NC_CHECK(g.blockX >= 0);
    NC_CHECK(g.blockX + g.blockW <= 8);
    NC_CHECK(g.blockY + g.blockH <= 8);
}

// =============================================================================
// 像素渲染
// =============================================================================

NC_TEST(Watermark, DrawsMarkAndKeepsOutsideUntouched) {
    const int w = 640;
    const int h = 360;
    auto frame = solidFrame(w, h, 0);  // 黑底
    const auto original = frame;
    WatermarkSpec spec;
    spec.enabled = true;
    std::vector<SubtitleRenderItem> items;
    NC_CHECK(applyWatermark(frame.data(), w, h, spec, &items));

    WatermarkGeometry g;
    NC_CHECK(computeWatermarkGeometry(spec, w, h, g));
    // 标记区域内应有亮像素（白色 70% 绘制）
    const int bright = countPixels(frame, w, g.blockX, g.blockY, g.blockX + g.blockW,
                                   g.blockY + g.blockH, isBright);
    NC_CHECK(bright > 10);
    // 黑底上白 70% 混合 = 178~179，不应达到纯白 255（证明 70% 不透明度）
    int maxValue = 0;
    for (int y = g.blockY; y < g.blockY + g.blockH; ++y) {
        for (int x = g.blockX; x < g.blockX + g.blockW; ++x) {
            const std::size_t idx =
                (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
            maxValue = std::max(maxValue, static_cast<int>(frame[idx]));
        }
    }
    NC_CHECK(maxValue >= 170);
    NC_CHECK(maxValue <= 190);
    // 标记区域外必须逐字节不变
    bool outsideUntouched = true;
    for (int y = 0; y < h && outsideUntouched; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool inBlock = x >= g.blockX && x < g.blockX + g.blockW && y >= g.blockY &&
                                 y < g.blockY + g.blockH;
            if (inBlock) {
                continue;
            }
            const std::size_t idx =
                (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
            if (frame[idx] != original[idx] || frame[idx + 1] != original[idx + 1] ||
                frame[idx + 2] != original[idx + 2] || frame[idx + 3] != original[idx + 3]) {
                outsideUntouched = false;
                break;
            }
        }
    }
    NC_CHECK(outsideUntouched);
    // alpha 通道保持不透明
    bool alphaOk = true;
    for (std::size_t i = 3; i < frame.size(); i += 4) {
        if (frame[i] != 255) {
            alphaOk = false;
            break;
        }
    }
    NC_CHECK(alphaOk);
}

NC_TEST(Watermark, OutlineVisibleOnWhiteBackground) {
    // 白底：白色主体不可见，但 30% 黑色描边应使边缘像素变暗（对比保障）
    const int w = 640;
    const int h = 360;
    auto frame = solidFrame(w, h, 255);
    WatermarkSpec spec;
    spec.enabled = true;
    NC_CHECK(applyWatermark(frame.data(), w, h, spec, nullptr));
    WatermarkGeometry g;
    NC_CHECK(computeWatermarkGeometry(spec, w, h, g));
    const int darkened =
        countPixels(frame, w, g.blockX, g.blockY, g.blockX + g.blockW, g.blockY + g.blockH,
                    isNotPure);
    NC_CHECK(darkened > 5);  // 存在被描边压暗的像素
}

NC_TEST(Watermark, EmitsCaptionDescriptor) {
    const int w = 1920;
    const int h = 1080;
    auto frame = solidFrame(w, h, 0);
    WatermarkSpec spec;
    spec.enabled = true;
    std::vector<SubtitleRenderItem> items;
    NC_CHECK(applyWatermark(frame.data(), w, h, spec, &items));
    NC_CHECK_EQ(items.size(), std::size_t(1));
    NC_CHECK_EQ(items[0].text, std::string("用剪辑软件制作"));
    // 锚点在右下象限（归一化）
    NC_CHECK(items[0].anchorX > 0.5);
    NC_CHECK(items[0].anchorY > 0.5);
    // 白色文字 + 30% 黑色描边
    NC_CHECK_EQ(items[0].fillRgba, std::uint32_t(0xFFFFFFFF));
    NC_CHECK_EQ(items[0].outlineRgba, std::uint32_t(0x0000004D));
}

NC_TEST(Watermark, NullItemsAllowed) {
    const int w = 320;
    const int h = 240;
    auto frame = solidFrame(w, h, 0);
    WatermarkSpec spec;
    spec.enabled = true;
    NC_CHECK(applyWatermark(frame.data(), w, h, spec, nullptr));  // 不崩溃
}

NC_TEST(Watermark, RejectsInvalidArgs) {
    std::vector<std::uint8_t> frame(16 * 16 * 4, 0);
    const WatermarkSpec spec;
    std::vector<SubtitleRenderItem> items;
    NC_CHECK(!applyWatermark(nullptr, 16, 16, spec, &items));
    NC_CHECK(!applyWatermark(frame.data(), 0, 16, spec, &items));
    NC_CHECK(!applyWatermark(frame.data(), 16, -1, spec, &items));
    // 参数非法时不产生描述符
    NC_CHECK(items.empty());
}

NC_TEST(Watermark, DisabledSpecStillRendersWhenCalled) {
    // applyWatermark 是纯渲染函数（enabled 由调用方/执行器判断）；
    // 这里确认默认规格渲染行为稳定（供执行器与测试共用）。
    const int w = 320;
    const int h = 240;
    auto frame = solidFrame(w, h, 0);
    std::vector<SubtitleRenderItem> items;

    NC_CHECK(applyWatermark(frame.data(), w, h, WatermarkSpec{}, &items));
    NC_CHECK(!items.empty());
}
