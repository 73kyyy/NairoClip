#include "nairoclip/core/spatial_effects.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

/// 高斯核（sigma → 归一化权重数组，半径 = ceil(3σ)）。
std::vector<double> gaussianKernel(double sigma, int& radiusOut) {
    double s = sigma;
    if (s < 0.5) s = 0.5;
    int radius = static_cast<int>(std::ceil(3.0 * s));
    if (radius < 1) radius = 1;
    if (radius > 512) radius = 512;  // 防御上限（radius 参数最大 100 → sigma 33 → 半径 100）
    std::vector<double> kernel(static_cast<std::size_t>(radius) * 2 + 1);
    double sum = 0.0;
    const double twoSigmaSq = 2.0 * s * s;
    for (int i = -radius; i <= radius; ++i) {
        const double w = std::exp(-static_cast<double>(i * i) / twoSigmaSq);
        kernel[static_cast<std::size_t>(i + radius)] = w;
        sum += w;
    }
    for (auto& w : kernel) w /= sum;
    radiusOut = radius;
    return kernel;
}

/// 可分离高斯模糊（水平 + 垂直两趟；double 中间缓冲；边缘钳制）。
bool blurRgbaInternal(std::uint8_t* pixels, int width, int height, double radiusPixels) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (radiusPixels <= 0.0) {
        return true;  // 无模糊（零改动成功）
    }
    // sigma = r/3（行业惯例：3σ 覆盖半径）；下限 0.5 避免退化为恒等
    const double sigma = std::max(0.5, radiusPixels / 3.0);
    int radius = 0;
    const std::vector<double> kernel = gaussianKernel(sigma, radius);

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // 中间缓冲（水平趟输出；RGB 三通道）
    std::vector<double> temp(pixelCount * 3, 0.0);

    // 水平趟：temp[y][x][c] = Σ kernel[k] * src[y][x+k][c]
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = pixels + static_cast<std::size_t>(y) * width * 4;
        double* outRow = temp.data() + static_cast<std::size_t>(y) * width * 3;
        for (int x = 0; x < width; ++x) {
            double acc[3] = {0.0, 0.0, 0.0};
            for (int k = -radius; k <= radius; ++k) {
                const int sx = std::min(width - 1, std::max(0, x + k));
                const std::uint8_t* px = row + static_cast<std::size_t>(sx) * 4;
                const double w = kernel[static_cast<std::size_t>(k + radius)];
                acc[0] += w * px[0];
                acc[1] += w * px[1];
                acc[2] += w * px[2];
            }
            double* out = outRow + static_cast<std::size_t>(x) * 3;
            out[0] = acc[0];
            out[1] = acc[1];
            out[2] = acc[2];
        }
    }

    // 垂直趟：dst[x][y][c] = Σ kernel[k] * temp[y+k][x][c]
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double acc[3] = {0.0, 0.0, 0.0};
            for (int k = -radius; k <= radius; ++k) {
                const int sy = std::min(height - 1, std::max(0, y + k));
                const double* src =
                    temp.data() + (static_cast<std::size_t>(sy) * width + x) * 3;
                const double w = kernel[static_cast<std::size_t>(k + radius)];
                acc[0] += w * src[0];
                acc[1] += w * src[1];
                acc[2] += w * src[2];
            }
            std::uint8_t* px =
                pixels + (static_cast<std::size_t>(y) * width + x) * 4;
            px[0] = static_cast<std::uint8_t>(
                std::max(0.0, std::min(255.0, acc[0] + 0.5)));
            px[1] = static_cast<std::uint8_t>(
                std::max(0.0, std::min(255.0, acc[1] + 0.5)));
            px[2] = static_cast<std::uint8_t>(
                std::max(0.0, std::min(255.0, acc[2] + 0.5)));
        }
    }
    return true;
}

/// 确定性伪随机（同一 (x, y, frame) → 同一输出；murmur3 雪崩混合保证
/// 任意输入位的差异充分扩散到全部输出位——FNV 直接取高位曾导致相邻帧
/// 哈希差异过小（仅 256），帧间噪声相同）。
std::uint32_t hashPixel(int x, int y, std::int64_t frameIndex, int channel) {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL;  // 黄金比例种子
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);  // boost::hash_combine
    };
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)));
    mix(static_cast<std::uint64_t>(frameIndex));
    mix(static_cast<std::uint64_t>(static_cast<std::uint32_t>(channel)));
    // murmur3 finalizer（雪崩）
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<std::uint32_t>(h >> 32);
}

}  // namespace

// =============================================================================
// 单个效果
// =============================================================================

bool applyVignetteRgba(std::uint8_t* pixels, int width, int height, double strength) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (strength <= 0.0) {
        return true;  // 关闭（零改动）
    }
    if (strength > 1.0) strength = 1.0;
    const double cx = static_cast<double>(width) * 0.5;
    const double cy = static_cast<double>(height) * 0.5;
    // 归一化半径：以画面短边为基准，保证不同画幅观感一致
    const double normRadius = static_cast<double>(std::min(width, height)) * 0.5;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double dx = static_cast<double>(x) + 0.5 - cx;
            const double dy = static_cast<double>(y) + 0.5 - cy;
            const double dist = std::sqrt(dx * dx + dy * dy) / normRadius;
            // 平滑衰减：>0.5 后开始变暗，到边缘（~1.4）最暗
            double t = (dist - 0.5) / 0.9;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            const double smooth = t * t * (3.0 - 2.0 * t);  // smoothstep
            const double factor = 1.0 - strength * 0.85 * smooth;  // 最多暗 85%
            std::uint8_t* px =
                pixels + (static_cast<std::size_t>(y) * width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                px[c] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, px[c] * factor + 0.5)));
            }
        }
    }
    return true;
}

bool applyGaussianBlurRgba(std::uint8_t* pixels, int width, int height, double radiusPixels) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    return blurRgbaInternal(pixels, width, height, radiusPixels);
}

bool applySharpenRgba(std::uint8_t* pixels, int width, int height, double strength) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (strength <= 0.0) {
        return true;  // 关闭（零改动）
    }
    if (strength > 5.0) strength = 5.0;
    // 非锐化掩模：orig + strength × (orig − blur)；模糊半径 1px 的 3x3 高斯近似
    // （σ = 0.5 → 核 [1,2,1]/4 可分离）。
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> blurred(pixels, pixels + pixelCount * 4);
    if (!blurRgbaInternal(blurred.data(), width, height, 1.5)) {
        return false;
    }
    const double mix = strength;  // 线性权重
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::size_t base = i * 4;
        for (int c = 0; c < 3; ++c) {
            const double orig = pixels[base + static_cast<std::size_t>(c)];
            const double blur = blurred[base + static_cast<std::size_t>(c)];
            const double sharpened = orig + mix * (orig - blur);
            pixels[base + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                std::max(0.0, std::min(255.0, sharpened + 0.5)));
        }
    }
    return true;
}

bool applyGrainRgba(std::uint8_t* pixels, int width, int height, double strength,
                    std::int64_t frameIndex) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (strength <= 0.0) {
        return true;  // 关闭（零改动）
    }
    if (strength > 1.0) strength = 1.0;
    // 噪声幅值：±32 灰度（强度 1.0 时）；0.x 强度按比例
    const double amplitude = 32.0 * strength;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx =
                (static_cast<std::size_t>(y) * width + x) * 4;
            std::uint8_t* px = pixels + idx;
            // 每像素独立噪声（三通道不同 → 彩色颗粒感，与胶片颗粒一致）
            for (int c = 0; c < 3; ++c) {
                const std::uint32_t rand = hashPixel(x, y, frameIndex, c);
                // 映射到 [-1, 1]（以 0.5 为中心）
                const double noise =
                    (static_cast<double>(rand) / 4294967295.0) * 2.0 - 1.0;
                const double v = px[c] + noise * amplitude;
                px[c] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, v + 0.5)));
            }
        }
    }
    return true;
}

// =============================================================================
// 分发
// =============================================================================

bool applySpatialEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                              double value, std::int64_t frameIndex) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    switch (effectType) {
        case 8:  // EffectType::Vignette
            return applyVignetteRgba(pixels, width, height, value);
        case 9:  // EffectType::GaussianBlur
            return applyGaussianBlurRgba(pixels, width, height, value);
        case 10:  // EffectType::Sharpen
            return applySharpenRgba(pixels, width, height, value);
        case 11:  // EffectType::Grain
            return applyGrainRgba(pixels, width, height, value, frameIndex);
        default:
            return true;  // 颜色类/未知：由 ColorTransform 处理（非错误）
    }
}

}  // namespace core
}  // namespace nairoclip
