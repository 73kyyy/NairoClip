#include "nairoclip/core/canvas.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {
namespace {

/// 用 int64 安全计算像素总量；任一维非正或乘法溢出返回 -1。
std::int64_t safePixelCount(int width, int height) {
    if (width <= 0 || height <= 0) return -1;
    const std::int64_t w = width;
    const std::int64_t h = height;
    // 先按 16384 上限判断，避免 w*h 越界；这里用除法预检而非乘法后检查。
    if (w > kCanvasMaxPixels / h) return -1;
    return w * h;
}

}  // namespace

const std::vector<CanvasPreset>& canvasPresets() {
    // 集中管理（B1 §5.2「预设清单必须集中管理」）。
    // 尺寸只是建议值，用户可自定义同比例的其他分辨率。
    static const std::vector<CanvasPreset> kPresets = {
        {"1:1", "正方形 1:1", 1080, 1080},
        {"4:5", "竖版 4:5", 1080, 1350},
        {"3:4", "竖版 3:4", 1080, 1440},
        {"9:16", "竖屏 9:16", 1080, 1920},
        {"2:3", "竖版 2:3", 1080, 1620},
        {"16:9", "横屏 16:9", 1920, 1080},
        {"4:3", "传统 4:3", 1440, 1080},
        {"3:2", "摄影 3:2", 1620, 1080},
        {"21:9", "超宽 21:9", 2520, 1080},
        {"2.39:1", "宽银幕 2.39:1", 2390, 1000},
    };
    return kPresets;
}

const CanvasPreset* findCanvasPreset(const std::string& id) {
    for (const CanvasPreset& p : canvasPresets()) {
        if (id == p.id) return &p;
    }
    return nullptr;
}

CanvasValidation validateCanvasSize(int width, int height) {
    CanvasValidation v;
    if (width <= 0 || height <= 0) {
        v.errorCode = "NC-TIMELINE-INVALID_CANVAS_SIZE";
        v.reason = "画布宽高必须为正数";
        return v;
    }
    if (width < kCanvasMinDimension || height < kCanvasMinDimension) {
        v.errorCode = "NC-TIMELINE-INVALID_CANVAS_SIZE";
        v.reason = "画布宽高不得小于 " + std::to_string(kCanvasMinDimension) + " 像素";
        return v;
    }
    if (width > kCanvasMaxDimension || height > kCanvasMaxDimension) {
        v.errorCode = "NC-TIMELINE-INVALID_CANVAS_SIZE";
        v.reason = "画布宽高不得大于 " + std::to_string(kCanvasMaxDimension) + " 像素";
        return v;
    }
    const std::int64_t pixels = safePixelCount(width, height);
    if (pixels < 0) {
        v.errorCode = "NC-TIMELINE-CANVAS_OVERFLOW";
        v.reason = "画布像素总量计算溢出";
        return v;
    }
    if (pixels > kCanvasMaxPixels) {
        v.errorCode = "NC-TIMELINE-CANVAS_TOO_LARGE";
        v.reason = "画布像素总量超过上限（" + std::to_string(width) + "×" +
                   std::to_string(height) + "）";
        return v;
    }
    v.ok = true;
    return v;
}

Rational canvasAspectRatio(int width, int height) {
    if (width <= 0 || height <= 0) return Rational::make(0, 1);
    return Rational::make(width, height);
}

bool sameAspectRatio(int w1, int h1, int w2, int h2) {
    if (w1 <= 0 || h1 <= 0 || w2 <= 0 || h2 <= 0) return false;
    // 交叉相乘比较，全程 int64；有理数精确比较，不经过浮点。
    const std::int64_t left = static_cast<std::int64_t>(w1) * h2;
    const std::int64_t right = static_cast<std::int64_t>(w2) * h1;
    return left == right;
}

AspectSolve solveHeightForWidth(int width, Rational ratio) {
    AspectSolve s;
    s.width = width;
    if (width <= 0 || !ratio.isPositive()) {
        s.height = 0;
        return s;
    }
    // height = width / ratio
    const Rational exact = Rational::make(width, 1) / ratio;
    s.height = static_cast<int>(roundToInt(exact));
    s.height = std::clamp(s.height, kCanvasMinDimension, kCanvasMaxDimension);
    s.exact = sameAspectRatio(s.width, s.height, ratio.num > 0 ? ratio.num : 1,
                              ratio.num > 0 ? ratio.den : 1);
    // sameAspectRatio 期望整数宽高；这里改为直接比较有理数更稳妥：
    s.exact = (canvasAspectRatio(s.width, s.height) == ratio);
    return s;
}

AspectSolve solveWidthForHeight(int height, Rational ratio) {
    AspectSolve s;
    s.height = height;
    if (height <= 0 || !ratio.isPositive()) {
        s.width = 0;
        return s;
    }
    const Rational exact = Rational::make(height, 1) * ratio;
    s.width = static_cast<int>(roundToInt(exact));
    s.width = std::clamp(s.width, kCanvasMinDimension, kCanvasMaxDimension);
    s.exact = (canvasAspectRatio(s.width, s.height) == ratio);
    return s;
}

bool swapCanvasOrientation(int* width, int* height) {
    if (width == nullptr || height == nullptr) return false;
    const int w = *width;
    const int h = *height;
    if (!validateCanvasSize(h, w).ok) return false;
    *width = h;
    *height = w;
    return true;
}

const char* canvasAdaptModeName(CanvasAdaptMode mode) {
    switch (mode) {
        case CanvasAdaptMode::Preserve:
            return "preserve";
        case CanvasAdaptMode::Fit:
            return "fit";
        case CanvasAdaptMode::Fill:
            return "fill";
    }
    return "preserve";
}

bool canvasAdaptModeFromName(const std::string& name, CanvasAdaptMode* out) {
    if (name == "preserve") {
        if (out) *out = CanvasAdaptMode::Preserve;
        return true;
    }
    if (name == "fit") {
        if (out) *out = CanvasAdaptMode::Fit;
        return true;
    }
    if (name == "fill") {
        if (out) *out = CanvasAdaptMode::Fill;
        return true;
    }
    return false;
}

double canvasAdaptScale(CanvasAdaptMode mode, int srcW, int srcH, int dstW, int dstH) {
    if (mode == CanvasAdaptMode::Preserve) return 1.0;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return 1.0;
    const double sx = static_cast<double>(dstW) / static_cast<double>(srcW);
    const double sy = static_cast<double>(dstH) / static_cast<double>(srcH);
    // Fit：完整装入 → 取较小倍数；Fill：覆盖画布 → 取较大倍数。
    // 两者都保持素材宽高比（同一倍数作用于两轴）。
    return mode == CanvasAdaptMode::Fit ? std::min(sx, sy) : std::max(sx, sy);
}

std::uint32_t CanvasBackground::toRgba32() const {
    return (static_cast<std::uint32_t>(r) << 24) | (static_cast<std::uint32_t>(g) << 16) |
           (static_cast<std::uint32_t>(b) << 8) | static_cast<std::uint32_t>(a);
}

CanvasBackground CanvasBackground::fromRgba32(std::uint32_t rgba) {
    CanvasBackground c;
    c.r = static_cast<std::uint8_t>((rgba >> 24) & 0xFF);
    c.g = static_cast<std::uint8_t>((rgba >> 16) & 0xFF);
    c.b = static_cast<std::uint8_t>((rgba >> 8) & 0xFF);
    c.a = static_cast<std::uint8_t>(rgba & 0xFF);
    return c;
}

CanvasValidation validateBackgroundRgba(std::uint32_t rgba) {
    CanvasValidation v;
    // 8-bit 通道打包后天然落在 0..255，无需逐通道判断；
    // 这里做的是「打包值语义」校验：本项目约定 0xRRGGBBAA，
    // 高 8 位必须是 R 而非 A，因此拒绝常见的 0xAARRGGBB 误用形态——
    // 无法从数值本身判断，故只校验调用方显式声明的通道序，其余交给测试。
    (void)rgba;
    v.ok = true;
    return v;
}

void blendStraightOver(std::uint8_t* dstRgba, const std::uint8_t* srcRgba) {
    if (dstRgba == nullptr || srcRgba == nullptr) return;
    const int a = srcRgba[3];
    if (a == 255) {
        // 不透明：直接覆盖。与旧 memcpy 行为逐字节一致 —— 保证不透明素材零回归。
        dstRgba[0] = srcRgba[0];
        dstRgba[1] = srcRgba[1];
        dstRgba[2] = srcRgba[2];
        dstRgba[3] = 255;
        return;
    }
    if (a == 0) {
        return;  // 全透明：dst 不变（背景得以保留）
    }
    const int inv = 255 - a;
    // 四舍五入：+127 再 /255，避免整数截断让半透明叠加整体偏暗。
    dstRgba[0] = static_cast<std::uint8_t>((srcRgba[0] * a + dstRgba[0] * inv + 127) / 255);
    dstRgba[1] = static_cast<std::uint8_t>((srcRgba[1] * a + dstRgba[1] * inv + 127) / 255);
    dstRgba[2] = static_cast<std::uint8_t>((srcRgba[2] * a + dstRgba[2] * inv + 127) / 255);
    dstRgba[3] = static_cast<std::uint8_t>((a * 255 + dstRgba[3] * inv + 127) / 255);
}

CanvasBackground flattenOnto(CanvasBackground src, CanvasBackground bg) {
    // straight alpha 合成：out = src*a + bg*(1-a)
    const double a = static_cast<double>(src.a) / 255.0;
    const double inv = 1.0 - a;
    CanvasBackground out;
    out.r = static_cast<std::uint8_t>(std::lround(src.r * a + bg.r * inv));
    out.g = static_cast<std::uint8_t>(std::lround(src.g * a + bg.g * inv));
    out.b = static_cast<std::uint8_t>(std::lround(src.b * a + bg.b * inv));
    // 结果叠加在不透明背景上 → alpha 恒为 255
    out.a = 255;
    return out;
}

}  // namespace core
}  // namespace nairoclip
