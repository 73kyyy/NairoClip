#include "nairoclip/core/watermark.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

/// 水印块纵横比（宽:高）：容纳 Logo 方形 + 一行说明文字。
constexpr double kBlockAspect = 0.30;
/// 说明文字区域相对块高的字号比例。
constexpr double kCaptionFontRatio = 0.58;
/// Logo 描边宽度比例（相对 icon 边长）。
constexpr double kIconBorderRatio = 0.11;
/// Logo 圆角比例。
constexpr double kIconRadiusRatio = 0.22;
/// Logo 黑色描边外扩比例。
constexpr double kIconOutlineRatio = 0.06;

/// 钳制到 [0,1]。
double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

/// 圆角矩形内部判定（SDF：< 0 表示内部）。
/// - px/py：像素中心坐标；cx/cy：矩形中心；hw/hh：半宽/半高；r：圆角半径。
bool roundedRectInside(double px, double py, double cx, double cy, double hw, double hh,
                       double r) {
    if (hw <= 0.0 || hh <= 0.0) {
        return false;
    }
    const double rr = std::min(r, std::min(hw, hh));
    const double qx = std::fabs(px - cx) - (hw - rr);
    const double qy = std::fabs(py - cy) - (hh - rr);
    const double ax = std::max(qx, 0.0);
    const double ay = std::max(qy, 0.0);
    const double d = std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0) - rr;
    return d < 0.0;
}

/// 三角形有向距离（< 0 内部；> 0 外部，值为到最近边的归一化距离）。
/// 顶点须按「内部为负」的顺序给出（本文件内固定用法）。
double triangleSignedDistance(double ax, double ay, double bx, double by, double cx, double cy,
                              double px, double py) {
    const struct {
        double x0, y0, x1, y1;
    } edges[3] = {{ax, ay, bx, by}, {bx, by, cx, cy}, {cx, cy, ax, ay}};
    double maxDistance = -1e30;
    for (const auto& e : edges) {
        const double ex = e.x1 - e.x0;
        const double ey = e.y1 - e.y0;
        const double length = std::sqrt(ex * ex + ey * ey);
        if (length <= 0.0) {
            continue;
        }
        // 有向面积 / 边长 = 到该边直线的带符号距离
        const double signedDistance =
            (ex * (py - e.y0) - ey * (px - e.x0)) / length;
        maxDistance = std::max(maxDistance, signedDistance);
    }
    return maxDistance;
}

/// 像素混合（RGBA8，保留 alpha）。
void blendPixel(std::uint8_t* px, double r, double g, double b, double a) {
    if (a <= 0.0) {
        return;
    }
    const double inv = 1.0 - a;
    px[0] = static_cast<std::uint8_t>(std::lround(px[0] * inv + r * a));
    px[1] = static_cast<std::uint8_t>(std::lround(px[1] * inv + g * a));
    px[2] = static_cast<std::uint8_t>(std::lround(px[2] * inv + b * a));
}

}  // namespace

bool computeWatermarkGeometry(const WatermarkSpec& spec, int frameW, int frameH,
                              WatermarkGeometry& out) {
    if (frameW <= 0 || frameH <= 0) {
        return false;
    }
    WatermarkGeometry g;
    // 边距：16px @ 1080p 基准，按画布短边等比缩放（竖屏/横屏一致）。
    const int shortSide = std::min(frameW, frameH);
    const int refSide = std::max(1, spec.referenceShortSide);
    const int refMargin = std::max(0, spec.referenceMarginPx);
    g.marginPx = std::max(1, static_cast<int>(
                                std::lround(static_cast<double>(refMargin) * shortSide / refSide)));
    // 尺寸：宽度 = 画面宽度 × 8%（文档规格）。
    const double ratio = std::max(0.005, std::min(0.5, spec.widthRatio));
    g.blockW = std::max(1, static_cast<int>(std::lround(frameW * ratio)));
    g.blockH = std::max(1, static_cast<int>(std::lround(g.blockW * kBlockAspect)));
    // 定位：右下角。
    g.blockX = std::max(0, frameW - g.marginPx - g.blockW);
    g.blockY = std::max(0, frameH - g.marginPx - g.blockH);
    // Logo 方形区域（块左侧）。
    g.iconSize = std::max(1, g.blockH);
    g.iconX = g.blockX;
    g.iconY = g.blockY;
    // 说明文字区域（块右侧）。
    const int gap = std::max(1, static_cast<int>(std::lround(g.blockH * 0.15)));
    g.textX = g.iconX + g.iconSize + gap;
    g.textY = g.blockY;
    g.textW = std::max(0, g.blockX + g.blockW - g.textX);
    g.textH = g.blockH;
    out = g;
    return true;
}

bool applyWatermark(std::uint8_t* frame, int frameW, int frameH, const WatermarkSpec& spec,
                    std::vector<SubtitleRenderItem>* items) {
    if (frame == nullptr || frameW <= 0 || frameH <= 0) {
        return false;
    }
    WatermarkGeometry g;
    if (!computeWatermarkGeometry(spec, frameW, frameH, g)) {
        return false;
    }

    // ---- Logo 标记光栅化（圆角方框描边 + 播放三角）----
    const double cx = g.iconX + g.iconSize / 2.0;
    const double cy = g.iconY + g.iconSize / 2.0;
    // 半宽/半高留 0.5px 边距，避免贴边裁剪。
    const double hw = std::max(0.5, g.iconSize / 2.0 - 0.5);
    const double hh = std::max(0.5, g.iconSize / 2.0 - 0.5);
    const double border = std::max(1.0, g.iconSize * kIconBorderRatio);
    const double radius = std::max(1.0, g.iconSize * kIconRadiusRatio);
    const double expand = std::max(1.0, g.iconSize * kIconOutlineRatio);
    // 播放三角（指向右侧；顶点顺序保证内部为负距离）。
    const double triAx = cx - hw * 0.36;
    const double triAy = cy - hh * 0.46;
    const double triBx = cx - hw * 0.36;
    const double triBy = cy + hh * 0.46;
    const double triCx = cx + hw * 0.42;
    const double triCy = cy;

    const double whiteAlpha = clamp01(spec.opacity);
    const double outlineAlpha = whiteAlpha * clamp01(spec.outlineOpacity);

    for (int y = g.iconY; y < g.iconY + g.iconSize; ++y) {
        if (y < 0 || y >= frameH) {
            continue;
        }
        for (int x = g.iconX; x < g.iconX + g.iconSize; ++x) {
            if (x < 0 || x >= frameW) {
                continue;
            }
            const double px = x + 0.5;
            const double py = y + 0.5;
            // 描边轮廓（外扩的圆角方框 ∪ 外扩的三角）
            const bool inSilhouette =
                roundedRectInside(px, py, cx, cy, hw + expand, hh + expand, radius + expand) ||
                triangleSignedDistance(triAx, triAy, triBx, triBy, triCx, triCy, px, py) < expand;
            // 主体（圆角方框描边带 ∪ 三角内部）
            const bool inFrameRing =
                roundedRectInside(px, py, cx, cy, hw, hh, radius) &&
                !roundedRectInside(px, py, cx, cy, hw - border, hh - border,
                                   std::max(0.5, radius - border));
            const bool inTriangle =
                triangleSignedDistance(triAx, triAy, triBx, triBy, triCx, triCy, px, py) < 0.0;
            const bool inShape = inFrameRing || inTriangle;

            std::uint8_t* pixel = frame + (static_cast<std::size_t>(y) *
                                               static_cast<std::size_t>(frameW) +
                                           static_cast<std::size_t>(x)) *
                                              4;
            if (inSilhouette) {
                blendPixel(pixel, 0.0, 0.0, 0.0, outlineAlpha);  // 30% 黑色描边
            }
            if (inShape) {
                blendPixel(pixel, 255.0, 255.0, 255.0, whiteAlpha);  // 白色主体
            }
        }
    }

    // ---- 说明文字描述符（平台层用系统字体绘制）----
    if (items != nullptr && g.textW > 0 && !spec.caption.empty()) {
        SubtitleRenderItem item;
        item.text = spec.caption;
        // 锚点：文字区左缘、垂直居中（归一化 0..1）。
        item.anchorX = static_cast<double>(g.textX) / frameW;
        item.anchorY = static_cast<double>(g.textY + g.textH / 2.0) / frameH;
        item.alignment = 0;  // 左对齐
        item.fontSize = std::max(10.0, g.textH * kCaptionFontRatio);
        item.fillRgba = 0xFFFFFFFF;          // 白色文字
        item.outlineRgba = 0x0000004Du;      // 30% 黑色描边（0x4D ≈ 77/255）
        item.outlineWidth = std::max(1.0, g.textH * 0.08);
        items->push_back(std::move(item));
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
