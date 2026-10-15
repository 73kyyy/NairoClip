#include "nairoclip/core/person_effects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

/// 确定性时间噪声（与 spatial_effects / screen_effects 同一风格：
/// murmur3 finalizer 扩散，保证相邻时刻差异充分、同一时刻可复现）。
double hashNoise(std::uint64_t seed, std::uint64_t salt) {
    std::uint64_t h = seed ^ (salt * 0x9E3779B97F4A7C15ull);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return static_cast<double>(h % 1000000ull) / 1000000.0;
}

/// 双线性采样（归一化坐标 u,v；越界钳制到边界）。
void sampleBilinear(const std::uint8_t* pixels, int width, int height, double u, double v,
                    std::uint8_t* outRgb) {
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double x = u * (width - 1);
    const double y = v * (height - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const double fx = x - x0;
    const double fy = y - y0;
    for (int c = 0; c < 3; ++c) {
        const auto at = [&](int xx, int yy) {
            return static_cast<double>(
                pixels[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(xx)) * 4 + static_cast<std::size_t>(c)]);
        };
        const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
        const double bot = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
        const double value = top * (1.0 - fy) + bot * fy;
        outRgb[c] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
    }
}

}  // namespace

bool applyJitterRgba(std::uint8_t* pixels, int width, int height, double intensity,
                     double timeSeconds) {
    if (pixels == nullptr || width <= 0 || height <= 0 || intensity <= 0.0) {
        return pixels != nullptr && width > 0 && height > 0;
    }
    intensity = std::min(1.0, intensity);
    // 文档 5.2「抖动」：整帧随机小幅度位移（时间噪声函数驱动）。
    // 幅度：最大 4% 画布宽/高（强度 1 时）。
    const double maxShiftX = width * 0.04 * intensity;
    const double maxShiftY = height * 0.04 * intensity;
    const auto frameIndex = static_cast<std::uint64_t>(std::llround(timeSeconds * 1000.0));
    const double dx = (hashNoise(frameIndex, 1) * 2.0 - 1.0) * maxShiftX;
    const double dy = (hashNoise(frameIndex, 2) * 2.0 - 1.0) * maxShiftY;
    if (std::abs(dx) < 0.5 && std::abs(dy) < 0.5) {
        return true;  // 幅度不足 1 像素：零改动（避免无意义拷贝）
    }
    std::vector<std::uint8_t> source(pixels, pixels + static_cast<std::size_t>(width) *
                                                        static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // 目标像素 (x,y) 采样源 (x - dx, y - dy)（镜像填充边界）
            double sx = x - dx;
            double sy = y - dy;
            if (sx < 0) sx = -sx;
            if (sx > width - 1) sx = 2.0 * (width - 1) - sx;
            if (sy < 0) sy = -sy;
            if (sy > height - 1) sy = 2.0 * (height - 1) - sy;
            sampleBilinear(source.data(), width, height,
                           sx / std::max(1, width - 1), sy / std::max(1, height - 1),
                           pixels + (static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(x)) *
                                        4);
        }
    }
    return true;
}

bool applyZoomInRgba(std::uint8_t* pixels, int width, int height, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 文档 5.2「放大」：画面中心区域局部放大采样。
    const double scale = 1.0 + 0.5 * intensity;  // 1.0 → 1.5 倍
    std::vector<std::uint8_t> source(pixels, pixels + static_cast<std::size_t>(width) *
                                                        static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double u = (x - width * 0.5) / scale / (width * 0.5) + 0.5;
            const double v = (y - height * 0.5) / scale / (height * 0.5) + 0.5;
            sampleBilinear(source.data(), width, height, u, v,
                           pixels + (static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(x)) *
                                        4);
        }
    }
    return true;
}

bool applyEdgeDetectRgba(std::uint8_t* pixels, int width, int height, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 文档 5.2「边缘检测」：Sobel 边缘检测。
    // 亮度图
    std::vector<double> luma(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            const std::uint8_t* px = pixels + idx * 4;
            luma[idx] = (px[0] * 0.299 + px[1] * 0.587 + px[2] * 0.114) / 255.0;
        }
    }
    // Sobel 梯度幅值
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int xm = std::max(0, x - 1);
            const int xp = std::min(width - 1, x + 1);
            const int ym = std::max(0, y - 1);
            const int yp = std::min(height - 1, y + 1);
            const auto at = [&](int xx, int yy) {
                return luma[static_cast<std::size_t>(yy) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(xx)];
            };
            const double gx = (at(xp, ym) + 2.0 * at(xp, y) + at(xp, yp)) -
                              (at(xm, ym) + 2.0 * at(xm, y) + at(xm, yp));
            const double gy = (at(xm, yp) + 2.0 * at(x, yp) + at(xp, yp)) -
                              (at(xm, ym) + 2.0 * at(x, ym) + at(xp, ym));
            const double magnitude = std::min(1.0, std::sqrt(gx * gx + gy * gy));
            const std::size_t idx = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            std::uint8_t* px = pixels + idx * 4;
            const double edge = magnitude * 255.0;
            for (int c = 0; c < 3; ++c) {
                px[c] = static_cast<std::uint8_t>(
                    std::clamp(px[c] * (1.0 - intensity) + edge * intensity, 0.0, 255.0));
            }
        }
    }
    return true;
}

bool applySlimRgba(std::uint8_t* pixels, int width, int height, double intensity,
                   const std::uint8_t* alpha) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (alpha == nullptr || intensity <= 0.0) {
        return true;  // 无 Alpha：零改动（不伪造人像区域）
    }
    intensity = std::min(1.0, intensity);
    // 文档 5.2「瘦身」：复用抠像 Alpha 作为形变权重，对区域做局部 UV 坐标压缩。
    // 实现：以每行 Alpha 质心为锚点，Alpha 权重越高压缩越强（最大 50%）。
    std::vector<std::uint8_t> source(pixels, pixels + static_cast<std::size_t>(width) *
                                                        static_cast<std::size_t>(height) * 4);
    const double maxSqueeze = 0.5 * intensity;
    for (int y = 0; y < height; ++y) {
        // 该行 Alpha 质心与总权重
        double weightSum = 0.0;
        double centerX = 0.0;
        for (int x = 0; x < width; ++x) {
            const double w = alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                   static_cast<std::size_t>(x)] /
                             255.0;
            weightSum += w;
            centerX += w * x;
        }
        if (weightSum < 1e-6) {
            continue;  // 该行无人像：零改动
        }
        centerX /= weightSum;
        const double rowWeight = std::min(1.0, weightSum / width);
        const double squeeze = maxSqueeze * rowWeight;
        for (int x = 0; x < width; ++x) {
            const double w = alpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                   static_cast<std::size_t>(x)] /
                             255.0;
            // 局部压缩：越靠近人像质心，横向读出坐标越外扩（视觉变窄）
            const double local = squeeze * w;
            const double sx = centerX + (x - centerX) / std::max(0.5, 1.0 - local);
            sampleBilinear(source.data(), width, height,
                           sx / std::max(1, width - 1),
                           static_cast<double>(y) / std::max(1, height - 1),
                           pixels + (static_cast<std::size_t>(y) *
                                         static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(x)) *
                                        4);
        }
    }
    return true;
}

bool applyGlowOutlineRgba(std::uint8_t* pixels, int width, int height, double intensity,
                          const std::uint8_t* alpha, const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (alpha == nullptr || intensity <= 0.0) {
        return true;  // 无 Alpha：零改动
    }
    intensity = std::min(1.0, intensity);
    // 文档 5.2「发光轮廓」：基于抠像 Alpha 蒙版边缘做高斯模糊 + 叠加发光色。
    // 步骤：① Alpha 边缘强度（Sobel）；② 盒式模糊（近似高斯，按质量分级半径）；
    //       ③ 暖白发光色叠加（加性混合）。
    const int radius = std::max(1, static_cast<int>(std::lround(
                                       3.0 * intensity * quality.sampleBudget)));
    std::vector<double> edge(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                             0.0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int xm = std::max(0, x - 1);
            const int xp = std::min(width - 1, x + 1);
            const int ym = std::max(0, y - 1);
            const int yp = std::min(height - 1, y + 1);
            const auto a = [&](int xx, int yy) {
                return alpha[static_cast<std::size_t>(yy) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(xx)] /
                       255.0;
            };
            const double gx = (a(xp, ym) + 2.0 * a(xp, y) + a(xp, yp)) -
                              (a(xm, ym) + 2.0 * a(xm, y) + a(xm, yp));
            const double gy = (a(xm, yp) + 2.0 * a(x, yp) + a(xp, yp)) -
                              (a(xm, ym) + 2.0 * a(x, ym) + a(xp, ym));
            const std::size_t idx = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            edge[idx] = std::min(1.0, std::sqrt(gx * gx + gy * gy));
        }
    }
    // 盒式模糊（可分两步；输出到 blurred）
    std::vector<double> blurred(edge.size(), 0.0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double sum = 0.0;
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int yy = std::clamp(y + dy, 0, height - 1);
                for (int dx = -radius; dx <= radius; ++dx) {
                    const int xx = std::clamp(x + dx, 0, width - 1);
                    sum += edge[static_cast<std::size_t>(yy) * static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(xx)];
                    ++count;
                }
            }
            blurred[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)] = count > 0 ? sum / count : 0.0;
        }
    }
    // 叠加发光（暖白 255,240,200；加性混合）
    constexpr double kGlow[3] = {255.0, 240.0, 200.0};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            const double glow = std::min(1.0, blurred[idx] * 8.0 * intensity);
            if (glow <= 0.0) {
                continue;
            }
            std::uint8_t* px = pixels + idx * 4;
            for (int c = 0; c < 3; ++c) {
                px[c] = static_cast<std::uint8_t>(
                    std::clamp(px[c] + kGlow[c] * glow, 0.0, 255.0));
            }
        }
    }
    return true;
}

bool applyPersonEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                             double value, double timeSeconds, const std::uint8_t* alpha,
                             const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    switch (effectType) {
        case static_cast<int>(PersonEffectType::Jitter):
            return applyJitterRgba(pixels, width, height, value, timeSeconds);
        case static_cast<int>(PersonEffectType::ZoomIn):
            return applyZoomInRgba(pixels, width, height, value);
        case static_cast<int>(PersonEffectType::EdgeDetect):
            return applyEdgeDetectRgba(pixels, width, height, value);
        case static_cast<int>(PersonEffectType::Slim):
            return applySlimRgba(pixels, width, height, value, alpha);
        case static_cast<int>(PersonEffectType::GlowOutline):
            return applyGlowOutlineRgba(pixels, width, height, value, alpha, quality);
        default:
            return true;  // 非人物特效：由其他模块处理（非错误）
    }
}

}  // namespace core
}  // namespace nairoclip
