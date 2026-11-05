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

bool renderFrameFitMasked(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                          std::uint8_t* dst, int dstW, int dstH, FrameFit mode,
                          std::uint32_t backgroundRgba, std::uint8_t* sourceMask) {
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
    // 素材区域蒙版：先整体置 0（留白），随后绘制区置 255（素材像素）。
    // 说明：仅当调用方需要背景填充时才写蒙版（sourceMask 非空）。
    if (sourceMask != nullptr) {
        std::memset(sourceMask, 0,
                    static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH));
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
                if (sourceMask != nullptr) {
                    sourceMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) +
                               static_cast<std::size_t>(x)] = 255;
                }
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

            const int outX = clipX0 + static_cast<int>(i);
            std::uint8_t* out = dstRow + static_cast<std::size_t>(outX) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const float value = p00[ch] * w00 + p10[ch] * w10 + p01[ch] * w01 +
                                    p11[ch] * w11;
                out[ch] = static_cast<std::uint8_t>(value + 0.5f);
            }
            if (sourceMask != nullptr) {
                sourceMask[static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) +
                           static_cast<std::size_t>(outX)] = 255;
            }
        }
    }
    return true;
}

bool renderFrameFit(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                    std::uint8_t* dst, int dstW, int dstH, FrameFit mode,
                    std::uint32_t backgroundRgba) {
    return renderFrameFitMasked(src, srcW, srcH, srcStride, dst, dstW, dstH, mode,
                                backgroundRgba, nullptr);
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

// =============================================================================
// Affine2D
// =============================================================================
Affine2D Affine2D::identity() { return Affine2D{}; }

Affine2D Affine2D::translation(double tx, double ty) {
    Affine2D a;
    a.m[2] = tx;
    a.m[5] = ty;
    return a;
}

Affine2D Affine2D::scaling(double sx, double sy) {
    Affine2D a;
    a.m[0] = sx;
    a.m[4] = sy;
    return a;
}

Affine2D Affine2D::rotation(double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    Affine2D a;
    a.m[0] = c;
    a.m[1] = -s;
    a.m[3] = s;
    a.m[4] = c;
    return a;
}

Affine2D Affine2D::then(const Affine2D& next) const {
    // 结果 = next ∘ this（先 this，再 next）
    Affine2D r;
    r.m[0] = next.m[0] * m[0] + next.m[1] * m[3];
    r.m[1] = next.m[0] * m[1] + next.m[1] * m[4];
    r.m[2] = next.m[0] * m[2] + next.m[1] * m[5] + next.m[2];
    r.m[3] = next.m[3] * m[0] + next.m[4] * m[3];
    r.m[4] = next.m[3] * m[1] + next.m[4] * m[4];
    r.m[5] = next.m[3] * m[2] + next.m[4] * m[5] + next.m[5];
    return r;
}

bool Affine2D::invert(Affine2D& out) const {
    const double det = m[0] * m[4] - m[1] * m[3];
    if (std::fabs(det) < 1e-12) {
        return false;
    }
    const double inv = 1.0 / det;
    Affine2D r;
    r.m[0] = m[4] * inv;
    r.m[1] = -m[1] * inv;
    r.m[3] = -m[3] * inv;
    r.m[4] = m[0] * inv;
    r.m[2] = -(r.m[0] * m[2] + r.m[1] * m[5]);
    r.m[5] = -(r.m[3] * m[2] + r.m[4] * m[5]);
    out = r;
    return true;
}

void Affine2D::apply(double x, double y, double& outX, double& outY) const {
    outX = m[0] * x + m[1] * y + m[2];
    outY = m[3] * x + m[4] * y + m[5];
}

bool Affine2D::isIdentity() const {
    const double k = 1e-9;
    return std::fabs(m[0] - 1.0) < k && std::fabs(m[1]) < k && std::fabs(m[2]) < k &&
           std::fabs(m[3]) < k && std::fabs(m[4] - 1.0) < k && std::fabs(m[5]) < k;
}

// =============================================================================
// 变换渲染（单次重采样）
// =============================================================================
namespace {

/// 按 EdgeMode 把越界坐标折回有效范围。返回 false 表示该像素应填背景。
bool wrapCoord(int& v, int size, EdgeMode edge) {
    if (v >= 0 && v < size) {
        return true;
    }
    switch (edge) {
        case EdgeMode::Clamp:
            v = std::max(0, std::min(size - 1, v));
            return true;
        case EdgeMode::Mirror: {
            const int period = 2 * size;
            int t = v % period;
            if (t < 0) {
                t += period;
            }
            v = (t < size) ? t : (period - 1 - t);
            return true;
        }
        case EdgeMode::Background:
        default:
            return false;
    }
}

}  // namespace

bool renderTransform(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                     std::uint8_t* dst, int dstW, int dstH, const Affine2D& srcToDst,
                     EdgeMode edge, std::uint32_t backgroundRgba, bool bilinear) {
    if (src == nullptr || dst == nullptr || srcW <= 0 || srcH <= 0 || dstW <= 0 ||
        dstH <= 0) {
        return false;
    }
    const int stride = srcStride > 0 ? srcStride : srcW * 4;
    if (stride < srcW * 4) {
        return false;
    }

    Affine2D dstToSrc;
    if (!srcToDst.invert(dstToSrc)) {
        return false;
    }

    const std::uint8_t bgR = static_cast<std::uint8_t>((backgroundRgba >> 24) & 0xFF);
    const std::uint8_t bgG = static_cast<std::uint8_t>((backgroundRgba >> 16) & 0xFF);
    const std::uint8_t bgB = static_cast<std::uint8_t>((backgroundRgba >> 8) & 0xFF);
    const std::uint8_t bgA = static_cast<std::uint8_t>(backgroundRgba & 0xFF);

    for (int y = 0; y < dstH; ++y) {
        std::uint8_t* dstRow =
            dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) * 4;
        for (int x = 0; x < dstW; ++x) {
            std::uint8_t* out = dstRow + static_cast<std::size_t>(x) * 4;
            double sx = 0.0;
            double sy = 0.0;
            dstToSrc.apply(static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5, sx,
                           sy);

            if (!bilinear) {
                int ix = static_cast<int>(std::floor(sx));
                int iy = static_cast<int>(std::floor(sy));
                const bool okX = wrapCoord(ix, srcW, edge);
                const bool okY = wrapCoord(iy, srcH, edge);
                if (!okX || !okY) {
                    out[0] = bgR;
                    out[1] = bgG;
                    out[2] = bgB;
                    out[3] = bgA;
                    continue;
                }
                std::memcpy(out,
                            src + static_cast<std::size_t>(iy) *
                                      static_cast<std::size_t>(stride) +
                                static_cast<std::size_t>(ix) * 4,
                            4);
                continue;
            }

            const int ix0 = static_cast<int>(std::floor(sx - 0.5));
            const int iy0 = static_cast<int>(std::floor(sy - 0.5));
            const double fx = (sx - 0.5) - static_cast<double>(ix0);
            const double fy = (sy - 0.5) - static_cast<double>(iy0);

            /* 双线性采样的 4 邻域。
               ------------------------------------------------------------------
               **这里曾经有个 bug**：把 4 个邻居都交给 wrapCoord，
               Background 模式下任一邻居越界就整像素填成背景色。

               后果：只要目标像素映射到源的**边缘半像素带**
               （sy < 0.5 或 sy > srcH - 1.5），iy0 会是 -1 或 iy0+1 == srcH，
               整行/整列被判出界 → 画面顶部和底部各有一条黑边。
               实测：160x120 源 Fill 到 160x160，第 0 行与第 159 行全黑。

               正确做法：**越界的邻居钳制到最近的有效像素**，
               而不是丢弃整个像素。理由：
                 · 边缘半像素是采样的正常情况（不是「画面外」）；
                 · 用钳制后的值参与插值，等价于「把边缘像素向外延伸」，
                   这正是 Clamp 语义，也是所有图像处理库（OpenCV /
                   libswscale）在边缘的标准做法；
                 · 真正的「画面外」（映射点完全落在源之外）由下面的
                   outside 判定处理，仍填背景色。
               这样「Fill 无黑边」才真正成立。 */
            int cx[2] = {ix0, ix0 + 1};
            int cy[2] = {iy0, iy0 + 1};

            /* 真正的画面外：映射点落在源图**像素覆盖范围**之外。
               ------------------------------------------------------------------
               源图像素 i 覆盖坐标区间 [i - 0.5, i + 0.5)。
               所以整幅图覆盖 [-0.5, srcW - 0.5)。

               判据要区分「边界半像素」与「真的在图外」：
                 · sy = 119.628, srcH = 120 → 119.628 < 119.5 不成立，
                   但它在 [−0.5, 119.5) 之外 —— 按上面的定义是「图外」。
                   可它明明落在最后一个像素（119）的覆盖范围
                   [118.5, 119.5) 之外半像素处……

               **这里必须务实**：只要映射点在 [0, srcH) 内就应当采样。
               因为缩小时（Fill 放大是缩小采样）最后一行映射到 119.6
               是**必然**的 —— 那是几何事实，不是「跑出画面」。
               把它判成图外，等于「放大后底部少一行像素」，
               视觉上就是一条黑边。

               实测症状（修复前）：160x120 Fill 到 160x160，
               第 0 行与第 159 行各有一条黑线。

               所以判据用 [−0.5, srcW) 半开区间：允许到最后一像素
               右侧半像素，再往外才填背景。 */
            const bool outside = (sx < -0.5 || sx >= static_cast<double>(srcW) ||
                                  sy < -0.5 || sy >= static_cast<double>(srcH));
            if (outside) {
                out[0] = bgR;
                out[1] = bgG;
                out[2] = bgB;
                out[3] = bgA;
                continue;
            }

            /* 边缘半像素：用 clamp 把邻域索引拉回有效范围。
               Mirror / Background 等其它 edge 模式仍走原 wrapCoord 逻辑 ——
               只有 Background 需要这个特例，因为它的「越界」语义
               在边缘半像素处不该生效。 */
            if (edge == EdgeMode::Background) {
                for (int k = 0; k < 2; ++k) {
                    cx[k] = std::clamp(cx[k], 0, srcW - 1);
                    cy[k] = std::clamp(cy[k], 0, srcH - 1);
                }
            } else {
                bool ok = true;
                for (int k = 0; k < 2 && ok; ++k) ok = wrapCoord(cx[k], srcW, edge);
                for (int k = 0; k < 2 && ok; ++k) ok = wrapCoord(cy[k], srcH, edge);
                if (!ok) {
                    out[0] = bgR;
                    out[1] = bgG;
                    out[2] = bgB;
                    out[3] = bgA;
                    continue;
                }
            }

            const std::uint8_t* p00 =
                src + static_cast<std::size_t>(cy[0]) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(cx[0]) * 4;
            const std::uint8_t* p10 =
                src + static_cast<std::size_t>(cy[0]) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(cx[1]) * 4;
            const std::uint8_t* p01 =
                src + static_cast<std::size_t>(cy[1]) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(cx[0]) * 4;
            const std::uint8_t* p11 =
                src + static_cast<std::size_t>(cy[1]) * static_cast<std::size_t>(stride) +
                static_cast<std::size_t>(cx[1]) * 4;

            const double w00 = (1.0 - fx) * (1.0 - fy);
            const double w10 = fx * (1.0 - fy);
            const double w01 = (1.0 - fx) * fy;
            const double w11 = fx * fy;

            for (int ch = 0; ch < 4; ++ch) {
                const double value =
                    p00[ch] * w00 + p10[ch] * w10 + p01[ch] * w01 + p11[ch] * w11;
                out[ch] = static_cast<std::uint8_t>(value + 0.5);
            }
        }
    }
    return true;
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
