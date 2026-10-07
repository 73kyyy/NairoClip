#include "nairoclip/core/frame_fit.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

/// 字符串比较（ASCII 大小写不敏感）。
bool equalsIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/// 取整（四舍五入到最近整数；用于几何计算，保证结果可预期）。
int roundToInt(double value) {
    return static_cast<int>(std::floor(value + 0.5));
}

}  // namespace

bool computeFrameFitRect(FrameFit mode, int srcW, int srcH, int dstW, int dstH,
                         FrameFitRect& out) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
        return false;
    }

    FrameFitRect rect;
    switch (mode) {
        case FrameFit::Fit: {
            // 等比内缩：完整保留源画面，余量留背景。
            const double scale = std::min(static_cast<double>(dstW) / srcW,
                                          static_cast<double>(dstH) / srcH);
            rect.scale = scale;
            rect.srcX = 0;
            rect.srcY = 0;
            rect.srcW = srcW;
            rect.srcH = srcH;
            rect.dstW = std::max(1, roundToInt(srcW * scale));
            rect.dstH = std::max(1, roundToInt(srcH * scale));
            // 不允许内缩结果超出画布（浮点舍入保护）。
            rect.dstW = std::min(rect.dstW, dstW);
            rect.dstH = std::min(rect.dstH, dstH);
            rect.dstX = (dstW - rect.dstW) / 2;
            rect.dstY = (dstH - rect.dstH) / 2;
            break;
        }
        case FrameFit::Fill: {
            // 等比铺满：放大到覆盖画布，溢出部分居中裁切（无黑边）。
            const double scale = std::max(static_cast<double>(dstW) / srcW,
                                          static_cast<double>(dstH) / srcH);
            rect.scale = scale;
            rect.srcX = 0;
            rect.srcY = 0;
            rect.srcW = srcW;
            rect.srcH = srcH;
            rect.dstW = std::max(1, roundToInt(srcW * scale));
            rect.dstH = std::max(1, roundToInt(srcH * scale));
            // 至少覆盖画布（浮点舍入保护）。
            rect.dstW = std::max(rect.dstW, dstW);
            rect.dstH = std::max(rect.dstH, dstH);
            rect.dstX = (dstW - rect.dstW) / 2;
            rect.dstY = (dstH - rect.dstH) / 2;
            break;
        }
        case FrameFit::Crop: {
            // 1:1 居中裁切（不缩放）：像素精确，用于等比例中心构图。
            rect.scale = 1.0;
            rect.srcW = std::min(srcW, dstW);
            rect.srcH = std::min(srcH, dstH);
            // 源小于画布时整体居中原样放置；大于时取中心。
            rect.srcX = std::max(0, (srcW - rect.srcW) / 2);
            rect.srcY = std::max(0, (srcH - rect.srcH) / 2);
            rect.dstW = rect.srcW;
            rect.dstH = rect.srcH;
            rect.dstX = (dstW - rect.dstW) / 2;
            rect.dstY = (dstH - rect.dstH) / 2;
            break;
        }
        default:
            return false;
    }
    out = rect;
    return true;
}

bool renderFrameFit(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                    std::uint8_t* dst, int dstW, int dstH, FrameFit mode,
                    std::uint32_t backgroundRgba) {
    if (src == nullptr || dst == nullptr || srcW <= 0 || srcH <= 0 || dstW <= 0 ||
        dstH <= 0) {
        return false;
    }
    const int stride = srcStride > 0 ? srcStride : srcW * 4;
    if (stride < srcW * 4) {
        return false;  // stride 过小：行数据不足
    }

    FrameFitRect rect;
    if (!computeFrameFitRect(mode, srcW, srcH, dstW, dstH, rect)) {
        return false;
    }

    // ---- 背景填充（Fit 模式下即信箱黑边；Fill/Crop 会被绘制区覆盖）----
    const std::uint8_t bgR = static_cast<std::uint8_t>((backgroundRgba >> 24) & 0xFF);
    const std::uint8_t bgG = static_cast<std::uint8_t>((backgroundRgba >> 16) & 0xFF);
    const std::uint8_t bgB = static_cast<std::uint8_t>((backgroundRgba >> 8) & 0xFF);
    const std::uint8_t bgA = static_cast<std::uint8_t>(backgroundRgba & 0xFF);
    const bool bgOpaque = (bgR == bgG && bgG == bgB && bgB == bgA);
    if (bgOpaque) {
        std::memset(dst, bgR, static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH) * 4);
    } else {
        for (std::size_t i = 0; i < static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH);
             ++i) {
            std::uint8_t* px = dst + i * 4;
            px[0] = bgR;
            px[1] = bgG;
            px[2] = bgB;
            px[3] = bgA;
        }
    }

    // 绘制区与画布求交（Fill 的负偏移 / 溢出在此裁掉）。
    const int clipX0 = std::max(0, rect.dstX);
    const int clipY0 = std::max(0, rect.dstY);
    const int clipX1 = std::min(dstW, rect.dstX + rect.dstW);
    const int clipY1 = std::min(dstH, rect.dstY + rect.dstH);
    if (clipX0 >= clipX1 || clipY0 >= clipY1) {
        return true;  // 完全溢出：结果为纯背景（合法输出）
    }

    const bool nearest = (mode == FrameFit::Crop);
    if (nearest) {
        // ---- 1:1 精确拷贝（无插值）----
        for (int y = clipY0; y < clipY1; ++y) {
            const int srcY = rect.srcY + (y - rect.dstY);
            if (srcY < 0 || srcY >= srcH) continue;
            const std::uint8_t* srcRow =
                src + static_cast<std::size_t>(srcY) * static_cast<std::size_t>(stride);
            std::uint8_t* dstRow =
                dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) * 4;
            for (int x = clipX0; x < clipX1; ++x) {
                const int srcX = rect.srcX + (x - rect.dstX);
                if (srcX < 0 || srcX >= srcW) continue;
                std::memcpy(dstRow + static_cast<std::size_t>(x) * 4,
                            srcRow + static_cast<std::size_t>(srcX) * 4, 4);
            }
        }
        return true;
    }

    // ---- 双线性缩放 ----
    // x 方向映射与行无关，预先算好（性能：大图每帧省重复浮点运算）。
    // 采样坐标以「像素中心」为基准：srcPos = (dstPos + 0.5) * srcSize / dstSize - 0.5
    const std::size_t spanW = static_cast<std::size_t>(clipX1 - clipX0);
    std::vector<int> x0(spanW);
    std::vector<int> x1(spanW);
    std::vector<float> fx(spanW);
    const double sxStep = static_cast<double>(rect.srcW) / static_cast<double>(rect.dstW);
    for (std::size_t i = 0; i < spanW; ++i) {
        const int dstX = clipX0 + static_cast<int>(i);
        const double localX = static_cast<double>(dstX - rect.dstX) + 0.5;
        double sx = rect.srcX + localX * sxStep - 0.5;
        sx = std::max(0.0, std::min(static_cast<double>(srcW - 1), sx));
        const int ix = static_cast<int>(std::floor(sx));
        const int ixNext = std::min(srcW - 1, ix + 1);
        x0[i] = ix;
        x1[i] = ixNext;
        fx[i] = static_cast<float>(sx - ix);
    }

    const double syStep = static_cast<double>(rect.srcH) / static_cast<double>(rect.dstH);
    for (int y = clipY0; y < clipY1; ++y) {
        const double localY = static_cast<double>(y - rect.dstY) + 0.5;
        double sy = rect.srcY + localY * syStep - 0.5;
        sy = std::max(0.0, std::min(static_cast<double>(srcH - 1), sy));
        const int iy = static_cast<int>(std::floor(sy));
        const int iyNext = std::min(srcH - 1, iy + 1);
        const float fy = static_cast<float>(sy - iy);

        const std::uint8_t* rowTop =
            src + static_cast<std::size_t>(iy) * static_cast<std::size_t>(stride);
        const std::uint8_t* rowBottom =
            src + static_cast<std::size_t>(iyNext) * static_cast<std::size_t>(stride);
        std::uint8_t* dstRow =
            dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) * 4;

        for (std::size_t i = 0; i < spanW; ++i) {
            const std::uint8_t* p00 = rowTop + static_cast<std::size_t>(x0[i]) * 4;
            const std::uint8_t* p10 = rowTop + static_cast<std::size_t>(x1[i]) * 4;
            const std::uint8_t* p01 = rowBottom + static_cast<std::size_t>(x0[i]) * 4;
            const std::uint8_t* p11 = rowBottom + static_cast<std::size_t>(x1[i]) * 4;
            const float wx0 = 1.0f - fx[i];
            const float wx1 = fx[i];
            const float wy0 = 1.0f - fy;
            const float wy1 = fy;
            const float w00 = wx0 * wy0;
            const float w10 = wx1 * wy0;
            const float w01 = wx0 * wy1;
            const float w11 = wx1 * wy1;

            std::uint8_t* out = dstRow + (static_cast<std::size_t>(clipX0) + i) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const float value = p00[ch] * w00 + p10[ch] * w10 + p01[ch] * w01 +
                                    p11[ch] * w11;
                out[ch] = static_cast<std::uint8_t>(value + 0.5f);
            }
        }
    }
    return true;
}

const char* frameFitName(FrameFit mode) {
    switch (mode) {
        case FrameFit::Fit:
            return "fit";
        case FrameFit::Fill:
            return "fill";
        case FrameFit::Crop:
            return "crop";
        default:
            return "fit";
    }
}

bool frameFitFromInt(int value, FrameFit& out) {
    switch (value) {
        case 0:
            out = FrameFit::Fit;
            return true;
        case 1:
            out = FrameFit::Fill;
            return true;
        case 2:
            out = FrameFit::Crop;
            return true;
        default:
            return false;
    }
}

bool frameFitFromString(const char* text, FrameFit& out) {
    if (text == nullptr) {
        return false;
    }
    if (equalsIgnoreCase(text, "fit")) {
        out = FrameFit::Fit;
        return true;
    }
    if (equalsIgnoreCase(text, "fill")) {
        out = FrameFit::Fill;
        return true;
    }
    if (equalsIgnoreCase(text, "crop")) {
        out = FrameFit::Crop;
        return true;
    }
    return false;
}

}  // namespace core
}  // namespace nairoclip
