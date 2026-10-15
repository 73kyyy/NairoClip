#include "nairoclip/core/screen_effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

/// 确定性伪随机（同 spatial_effects 技术：hash_combine + murmur3 finalizer）。
std::uint32_t hashValue(std::uint64_t seed, std::uint64_t salt) {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL;  // 黄金比例种子
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);  // boost::hash_combine
    };
    mix(seed);
    mix(salt);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<std::uint32_t>(h >> 32);
}

/// 取 [0, 1) 的确定性随机。
double rand01(std::uint64_t seed, std::uint64_t salt) {
    return static_cast<double>(hashValue(seed, salt)) / 4294967296.0;
}

}  // namespace

const char* screenEffectPurpose() {
    return "画面特效（故障/分屏/镜像/旋转模糊/扫描线/RGB 分离）：单帧像素处理，"
           "无需帧间状态；CPU 参考实现与 GPU 后端语义一致";
}

// =============================================================================
// 故障（Glitch）
// =============================================================================

bool applyGlitchRgba(std::uint8_t* pixels, int width, int height, double intensity,
                     std::int64_t frameIndex) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;  // 关闭（零改动）
    }
    intensity = std::min(1.0, intensity);
    // 每行 RGB 通道独立水平偏移（幅度 ±8% 宽 × 强度）
    // 修复：用 lround 而非 int 截断（小幅度下截断会导致偏移恒为 0，效果消失）。
    const double maxShift = static_cast<double>(width) * 0.08 * intensity;
    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const double rowRand = rand01(static_cast<std::uint64_t>(y), frameIndex * 7919ULL);
        // 仅部分行发生偏移（故障的"条带状"外观）
        if (rowRand > 0.35 + 0.5 * (1.0 - intensity)) {
            continue;
        }
        const int shiftR = static_cast<int>(
            std::lround((rand01(y, frameIndex * 104729ULL) * 2.0 - 1.0) * maxShift));
        const int shiftG = static_cast<int>(
            std::lround((rand01(y, frameIndex * 15485863ULL) * 2.0 - 1.0) * maxShift));
        const int shiftB = static_cast<int>(
            std::lround((rand01(y, frameIndex * 32452843ULL) * 2.0 - 1.0) * maxShift));
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            const std::size_t srcRow = static_cast<std::size_t>(y) * width * 4;
            auto sample = [&](int sx, int c) -> std::uint8_t {
                sx = std::min(width - 1, std::max(0, sx));
                return source[srcRow + static_cast<std::size_t>(sx) * 4 +
                              static_cast<std::size_t>(c)];
            };
            pixels[idx] = sample(x + shiftR, 0);
            pixels[idx + 1] = sample(x + shiftG, 1);
            pixels[idx + 2] = sample(x + shiftB, 2);
        }
    }
    // 随机色块替换（少量矩形块用随机纯色覆盖）
    const int blockCount = static_cast<int>(4 + 12 * intensity);
    const int blockW = std::max(3, width / 12);
    const int blockH = std::max(3, height / 24);
    for (int i = 0; i < blockCount; ++i) {
        const int bx = static_cast<int>(rand01(i * 2, frameIndex * 31ULL + 1) *
                                        std::max(1, width - blockW));
        const int by = static_cast<int>(rand01(i * 2, frameIndex * 37ULL + 2) *
                                        std::max(1, height - blockH));
        const std::uint8_t r =
            static_cast<std::uint8_t>(rand01(i, frameIndex * 41ULL + 3) * 255.0);
        const std::uint8_t g =
            static_cast<std::uint8_t>(rand01(i, frameIndex * 43ULL + 4) * 255.0);
        const std::uint8_t b =
            static_cast<std::uint8_t>(rand01(i, frameIndex * 47ULL + 5) * 255.0);
        for (int y = by; y < std::min(height, by + blockH); ++y) {
            for (int x = bx; x < std::min(width, bx + blockW); ++x) {
                const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
                pixels[idx] = r;
                pixels[idx + 1] = g;
                pixels[idx + 2] = b;
            }
        }
    }
    return true;
}

// =============================================================================
// 分屏（SplitScreen）
// =============================================================================

bool applySplitScreenRgba(std::uint8_t* pixels, int width, int height, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    const int halfW = width / 2;
    const int halfH = height / 2;
    if (halfW <= 0 || halfH <= 0) {
        return true;  // 过小画面：无法分屏（零改动）
    }
    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    // 分屏（文档 5.1「同一帧纹理多次采样+坐标变换后拼接显示」）：
    // 2x2 四宫格，每格展示**完整画面的缩小版**（纹理多次采样 + 缩放坐标变换）。
    // 修复记录：此前实现每格显示自身象限（原地映射）→ 输出与输入几乎相同
    // （仅分隔线变化），是「无效效果」。现每格对完整帧降采样填充。
    for (int y = 0; y < height; ++y) {
        const int ly = y >= halfH ? y - halfH : y;                 // 格内 y
        const int sy = std::min(height - 1, ly * height / halfH);  // 全帧 → 格 的缩放采样
        for (int x = 0; x < width; ++x) {
            const int lx = x >= halfW ? x - halfW : x;             // 格内 x
            const int sx = std::min(width - 1, lx * width / halfW);
            const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 4;
            const std::size_t src = (static_cast<std::size_t>(sy) * width + sx) * 4;
            // 强度混合：0 = 原图，1 = 完整分屏（每格完整帧缩小版）
            for (int c = 0; c < 3; ++c) {
                const double mixed = source[src + static_cast<std::size_t>(c)] * intensity +
                                     source[dst + static_cast<std::size_t>(c)] *
                                         (1.0 - intensity);
                pixels[dst + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, mixed + 0.5)));
            }
            // 分屏分隔线（1px 暗线；强度 > 0.3 时显示）
            if ((x == halfW || y == halfH) && intensity > 0.3) {
                for (int c = 0; c < 3; ++c) {
                    pixels[dst + static_cast<std::size_t>(c)] =
                        static_cast<std::uint8_t>(pixels[dst + static_cast<std::size_t>(c)] * 0.4);
                }
            }
        }
    }
    return true;
}

// =============================================================================
// 镜像（Mirror）
// =============================================================================

bool applyMirrorRgba(std::uint8_t* pixels, int width, int height, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 左右翻转：右半 50% 完全翻转（强度 1）；强度 < 1 时按比例混合
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int mx = width - 1 - x;
            if (mx <= x) {
                break;  // 每对只处理一次（左半 + 中轴）
            }
            const std::size_t a = (static_cast<std::size_t>(y) * width + x) * 4;
            const std::size_t b = (static_cast<std::size_t>(y) * width + mx) * 4;
            for (int c = 0; c < 3; ++c) {
                const std::uint8_t va = pixels[a + static_cast<std::size_t>(c)];
                const std::uint8_t vb = pixels[b + static_cast<std::size_t>(c)];
                // 强度混合：0 = 不变，1 = 完整镜像（左右交换）
                const double newA = va * (1.0 - intensity) + vb * intensity;
                const double newB = vb * (1.0 - intensity) + va * intensity;
                pixels[a + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, newA + 0.5)));
                pixels[b + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, newB + 0.5)));
            }
        }
    }
    return true;
}

// =============================================================================
// 旋转模糊（RadialBlur）
// =============================================================================

bool applyRadialBlurRgba(std::uint8_t* pixels, int width, int height, double intensity,
                         const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 最大旋转角度 30° × 强度；采样数按质量分级（导出 24；预览按 sampleBudget）
    const double maxAngle = 30.0 * intensity * 3.141592653589793 / 180.0;
    const int samples = effectiveSampleCount(quality, 24);
    if (samples <= 1) {
        return true;
    }
    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    const double cx = static_cast<double>(width) * 0.5;
    const double cy = static_cast<double>(height) * 0.5;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double accR = 0.0, accG = 0.0, accB = 0.0;
            for (int s = 0; s < samples; ++s) {
                // 角度从 -maxAngle/2 到 +maxAngle/2 均匀采样
                const double t = static_cast<double>(s) / static_cast<double>(samples - 1);
                const double angle = (t - 0.5) * maxAngle;
                const double cosA = std::cos(angle);
                const double sinA = std::sin(angle);
                const double dx = static_cast<double>(x) + 0.5 - cx;
                const double dy = static_cast<double>(y) + 0.5 - cy;
                const int sx = static_cast<int>(cx + dx * cosA - dy * sinA);
                const int sy = static_cast<int>(cy + dx * sinA + dy * cosA);
                const int cxClamped = std::min(width - 1, std::max(0, sx));
                const int cyClamped = std::min(height - 1, std::max(0, sy));
                const std::size_t idx = (static_cast<std::size_t>(cyClamped) * width +
                                         cxClamped) * 4;
                accR += source[idx];
                accG += source[idx + 1];
                accB += source[idx + 2];
            }
            const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 4;
            pixels[dst] = static_cast<std::uint8_t>(accR / samples + 0.5);
            pixels[dst + 1] = static_cast<std::uint8_t>(accG / samples + 0.5);
            pixels[dst + 2] = static_cast<std::uint8_t>(accB / samples + 0.5);
        }
    }
    return true;
}

// =============================================================================
// 扫描线（Scanlines）
// =============================================================================

bool applyScanlinesRgba(std::uint8_t* pixels, int width, int height, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 每 4 行一组：奇偶行交替变暗（CRT 扫描线外观）
    const double darkFactor = 1.0 - 0.45 * intensity;  // 暗行最多暗 45%
    const int period = 4;
    for (int y = 0; y < height; ++y) {
        const int phase = y % period;
        const bool darkRow = phase >= period / 2;
        const double factor = darkRow ? darkFactor : 1.0;
        if (factor >= 0.999999) {
            continue;
        }
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const double v = pixels[idx + static_cast<std::size_t>(c)] * factor;
                pixels[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, v + 0.5)));
            }
        }
    }
    return true;
}

// =============================================================================
// RGB 分离（RgbSplit）
// =============================================================================

bool applyRgbSplitRgba(std::uint8_t* pixels, int width, int height, double intensity,
                       std::int64_t frameIndex) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 通道位移幅度：±3% 宽 × 强度（时变抖动方向）
    // 修复：此前用 int 截断（static_cast<int>），当幅度 < 1px 时结果恒为 0
    // （小分辨率/低强度下效果完全消失）；改用 lround 保证小幅度下仍产生位移。
    const double maxShift = static_cast<double>(width) * 0.03 * intensity;
    const int shiftR = static_cast<int>(
        std::lround((rand01(frameIndex, 1) * 2.0 - 1.0) * maxShift));
    const int shiftB = static_cast<int>(
        std::lround((rand01(frameIndex, 2) * 2.0 - 1.0) * maxShift));
    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            const std::size_t row = static_cast<std::size_t>(y) * width * 4;
            const int rx = std::min(width - 1, std::max(0, x + shiftR));
            const int bx = std::min(width - 1, std::max(0, x + shiftB));
            // R 通道从 +shiftR 采样；G 保持原位；B 通道从 +shiftB 采样
            pixels[idx] = source[row + static_cast<std::size_t>(rx) * 4];
            pixels[idx + 2] = source[row + static_cast<std::size_t>(bx) * 4 + 2];
        }
    }
    return true;
}

// =============================================================================
// 分发
// =============================================================================

bool applyScreenEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                             double value, std::int64_t frameIndex,
                             const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    switch (effectType) {
        case static_cast<int>(ScreenEffectType::Glitch):
            return applyGlitchRgba(pixels, width, height, value, frameIndex);
        case static_cast<int>(ScreenEffectType::SplitScreen):
            return applySplitScreenRgba(pixels, width, height, value);
        case static_cast<int>(ScreenEffectType::Mirror):
            return applyMirrorRgba(pixels, width, height, value);
        case static_cast<int>(ScreenEffectType::RadialBlur):
            return applyRadialBlurRgba(pixels, width, height, value, quality);
        case static_cast<int>(ScreenEffectType::Scanlines):
            return applyScanlinesRgba(pixels, width, height, value);
        case static_cast<int>(ScreenEffectType::RgbSplit):
            return applyRgbSplitRgba(pixels, width, height, value, frameIndex);
        case static_cast<int>(ScreenEffectType::Flash):
            // 闪光为时变脉冲：用 frameIndex 折算时间（1/30s 步进，与调用约定一致）
            return applyFlashRgba(pixels, width, height, value,
                                  static_cast<double>(frameIndex) / 30.0);
        case static_cast<int>(ScreenEffectType::Mosaic):
            return applyMosaicRgba(pixels, width, height, value);
        default:
            return true;  // 非画面特效：由其他模块处理（非错误）
    }
}

bool applyFlashRgba(std::uint8_t* pixels, int width, int height, double intensity,
                    double timeSeconds) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 文档「闪光」：亮度瞬间提升再恢复（时间脉冲）。
    // 脉冲波形：以 0.2s 周期做「快升慢降」（闪一下），峰值增益 = 强度 × 1.0。
    const double phase = std::fmod(timeSeconds, 0.2) / 0.2;
    const double pulse = phase < 0.15 ? (1.0 - phase / 0.15) : 0.0;
    const double boost = intensity * pulse;
    if (boost <= 0.0) {
        return true;  // 非脉冲时刻：零改动
    }
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* px = pixels + i * 4;
        for (int c = 0; c < 3; ++c) {
            // 亮度提升（趋向白）：out = v + (255 - v) × boost
            const double value = px[c] + (255.0 - px[c]) * boost;
            px[c] = static_cast<std::uint8_t>(std::clamp(value, 0.0, 255.0));
        }
    }
    return true;
}

bool applyMosaicRgba(std::uint8_t* pixels, int width, int height, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 文档「马赛克」：像素化块替换（块尺寸随强度增大；最大约 1/16 画布宽）
    const int blockSize = std::max(2, static_cast<int>(std::lround(width * 0.0625 * intensity)));
    std::vector<std::uint8_t> source(pixels, pixels + static_cast<std::size_t>(width) *
                                                        static_cast<std::size_t>(height) * 4);
    for (int by = 0; by < height; by += blockSize) {
        for (int bx = 0; bx < width; bx += blockSize) {
            // 块内均值颜色
            double sum[3] = {0.0, 0.0, 0.0};
            int count = 0;
            for (int y = by; y < std::min(height, by + blockSize); ++y) {
                for (int x = bx; x < std::min(width, bx + blockSize); ++x) {
                    const std::uint8_t* px =
                        source.data() + (static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x)) *
                                            4;
                    for (int c = 0; c < 3; ++c) {
                        sum[c] += px[c];
                    }
                    ++count;
                }
            }
            if (count == 0) {
                continue;
            }
            std::uint8_t avg[3];
            for (int c = 0; c < 3; ++c) {
                avg[c] = static_cast<std::uint8_t>(sum[c] / count);
            }
            // 填充块
            for (int y = by; y < std::min(height, by + blockSize); ++y) {
                for (int x = bx; x < std::min(width, bx + blockSize); ++x) {
                    std::uint8_t* px =
                        pixels + (static_cast<std::size_t>(y) *
                                      static_cast<std::size_t>(width) +
                                  static_cast<std::size_t>(x)) *
                                     4;
                    px[0] = avg[0];
                    px[1] = avg[1];
                    px[2] = avg[2];
                }
            }
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
