#include "nairoclip/core/atmosphere_effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "nairoclip/core/particle_engine.h"
#include "nairoclip/core/spatial_effects.h"

namespace nairoclip {
namespace core {

namespace {

/// 确定性伪随机（同项目其他模块技术：hash_combine + murmur3 finalizer）。
std::uint32_t hashValue(std::uint64_t a, std::uint64_t b) {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    };
    mix(a);
    mix(b);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<std::uint32_t>(h >> 32);
}

double rand01(std::uint64_t a, std::uint64_t b) {
    return static_cast<double>(hashValue(a, b)) / 4294967296.0;
}

/// 在像素坐标 (cx, cy) 处以 radiusPx 绘制软边圆点（加法混合，白色）。
void drawSoftDot(std::uint8_t* pixels, int width, int height, double cx, double cy,
                 double radiusPx, double strength) {
    if (radiusPx < 0.5) {
        radiusPx = 0.5;
    }
    const int minX = std::max(0, static_cast<int>(cx - radiusPx));
    const int maxX = std::min(width - 1, static_cast<int>(cx + radiusPx));
    const int minY = std::max(0, static_cast<int>(cy - radiusPx));
    const int maxY = std::min(height - 1, static_cast<int>(cy + radiusPx));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const double dx = static_cast<double>(x) - cx;
            const double dy = static_cast<double>(y) - cy;
            const double dist = std::sqrt(dx * dx + dy * dy);
            if (dist > radiusPx) {
                continue;
            }
            const double t = dist / radiusPx;
            const double falloff = (1.0 - t * t) * (1.0 - t * t);  // 软边
            const double add = falloff * strength * 255.0;
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            for (int c = 0; c < 3; ++c) {
                const double v = pixels[idx + static_cast<std::size_t>(c)] + add;
                pixels[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, v)));
            }
        }
    }
}

/// 粒子数按质量分级缩放（预览简化；导出完整）。
int scaledParticleCount(int fullCount, const RenderQuality& quality) {
    if (quality.path == RenderPath::Export || quality.sampleBudget >= 1.0) {
        return fullCount;
    }
    const int scaled = static_cast<int>(static_cast<double>(fullCount) *
                                        quality.sampleBudget + 0.5);
    return scaled < 4 ? 4 : scaled;
}

}  // namespace

const char* atmosphereEffectPurpose() {
    return "氛围特效（雪花/下雨/光斑/老电影/星光）：基于通用粒子引擎与确定性噪声，"
           "无需帧间状态；导出完整精度、预览可按质量分级简化";
}

// =============================================================================
// 雪花 / 下雨（粒子引擎）
// =============================================================================

bool applySnowRgba(std::uint8_t* pixels, int width, int height, double intensity,
                   double timeSeconds, const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;  // 关闭（零改动）
    }
    intensity = std::min(1.0, intensity);
    ParticleConfig config;
    config.count = scaledParticleCount(static_cast<int>(120 + 280 * intensity), quality);
    config.velocityX = 0.03;   // 缓慢向右飘
    config.velocityY = 0.10;   // 缓慢下落
    config.velocitySpread = 0.05;
    config.gravityY = 0.0;
    config.lifetimeSeconds = 8.0;
    config.size = 0.004 + 0.004 * intensity;
    config.seed = 0x534E4F57ULL;  // "SNOW"
    const double shortSide = static_cast<double>(std::min(width, height));
    for (int i = 0; i < config.count; ++i) {
        ParticleSample sample;
        if (!sampleParticle(i, timeSeconds, config, sample)) {
            continue;
        }
        const double px = sample.x * static_cast<double>(width);
        const double py = sample.y * static_cast<double>(height);
        const double radius = sample.size * shortSide;
        drawSoftDot(pixels, width, height, px, py, radius,
                    sample.opacity * (0.5 + 0.5 * intensity));
    }
    return true;
}

bool applyRainRgba(std::uint8_t* pixels, int width, int height, double intensity,
                   double timeSeconds, const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    ParticleConfig config;
    config.count = scaledParticleCount(static_cast<int>(200 + 600 * intensity), quality);
    config.velocityX = 0.02;   // 轻微斜向
    config.velocityY = 0.55;   // 快速下落
    config.velocitySpread = 0.08;
    config.gravityY = 0.0;
    config.lifetimeSeconds = 2.5;
    config.size = 0.002;
    config.seed = 0x5241494EULL;  // "RAIN"
    const double shortSide = static_cast<double>(std::min(width, height));
    for (int i = 0; i < config.count; ++i) {
        ParticleSample sample;
        if (!sampleParticle(i, timeSeconds, config, sample)) {
            continue;
        }
        const double px = sample.x * static_cast<double>(width);
        const double py = sample.y * static_cast<double>(height);
        // 雨滴：小圆点 + 纵向拖尾近似（两个偏移点）
        const double radius = std::max(1.0, sample.size * shortSide * 2.0);
        const double trail = 0.012 * static_cast<double>(height) * intensity;
        const double alpha = sample.opacity * 0.55 * intensity;
        drawSoftDot(pixels, width, height, px, py, radius, alpha);
        drawSoftDot(pixels, width, height, px, py + trail, radius * 0.8, alpha * 0.7);
    }
    return true;
}

// =============================================================================
// 光斑（LightLeak）
// =============================================================================

bool applyLightLeakRgba(std::uint8_t* pixels, int width, int height, double intensity,
                        double timeSeconds) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 3 个确定性光斑（暖色径向渐变）+ 闪烁（时间驱动透明度）
    const int leakCount = 3;
    const double shortSide = static_cast<double>(std::min(width, height));
    for (int i = 0; i < leakCount; ++i) {
        // 每 4 秒一个闪烁周期（相位错开）
        const double flickerPhase =
            std::fmod(timeSeconds / 4.0 + rand01(static_cast<std::uint64_t>(i), 11), 1.0);
        const double flicker =
            0.55 + 0.45 * std::sin(flickerPhase * 2.0 * 3.141592653589793);
        const double cx = rand01(static_cast<std::uint64_t>(i), 21) * 1.2 * width -
                          0.1 * width;
        const double cy = rand01(static_cast<std::uint64_t>(i), 22) * 1.2 * height -
                          0.1 * height;
        const double radius =
            shortSide * (0.25 + 0.3 * rand01(static_cast<std::uint64_t>(i), 23));
        const int minX = std::max(0, static_cast<int>(cx - radius));
        const int maxX = std::min(width - 1, static_cast<int>(cx + radius));
        const int minY = std::max(0, static_cast<int>(cy - radius));
        const int maxY = std::min(height - 1, static_cast<int>(cy + radius));
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const double dx = static_cast<double>(x) - cx;
                const double dy = static_cast<double>(y) - cy;
                const double dist = std::sqrt(dx * dx + dy * dy);
                if (dist > radius) {
                    continue;
                }
                const double t = dist / radius;
                const double falloff = (1.0 - t) * (1.0 - t);
                const double add = falloff * flicker * intensity * 90.0;
                const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
                pixels[idx] = static_cast<std::uint8_t>(
                    std::min(255.0, static_cast<double>(pixels[idx]) + add));
                pixels[idx + 1] = static_cast<std::uint8_t>(
                    std::min(255.0, static_cast<double>(pixels[idx + 1]) + add * 0.62));
                pixels[idx + 2] = static_cast<std::uint8_t>(
                    std::min(255.0, static_cast<double>(pixels[idx + 2]) + add * 0.22));
            }
        }
    }
    return true;
}

// =============================================================================
// 老电影划痕（OldFilm）
// =============================================================================

bool applyOldFilmRgba(std::uint8_t* pixels, int width, int height, double intensity,
                      double timeSeconds) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    const std::uint64_t tick = static_cast<std::uint64_t>(timeSeconds * 1000.0);
    // 1) 确定性竖线划痕（随机 x / 长度 / 亮度；每帧变化）
    const int scratchCount = static_cast<int>(2 + 6 * intensity);
    for (int i = 0; i < scratchCount; ++i) {
        const int sx = static_cast<int>(
            rand01(tick, static_cast<std::uint64_t>(i) * 3 + 1) * width);
        const double y0 =
            rand01(tick, static_cast<std::uint64_t>(i) * 3 + 2) * height * 0.5;
        const double len =
            height * (0.2 + 0.6 * rand01(tick, static_cast<std::uint64_t>(i) * 3 + 3));
        const bool bright = rand01(tick, static_cast<std::uint64_t>(i) * 7) > 0.5;
        const double add = (bright ? 1.0 : -0.7) * intensity * 120.0;
        const int y1 = std::max(0, static_cast<int>(y0));
        const int y2 = std::min(height, static_cast<int>(y0 + len));
        const int px = std::min(width - 1, std::max(0, sx));
        for (int y = y1; y < y2; ++y) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + px) * 4;
            for (int c = 0; c < 3; ++c) {
                const double v = pixels[idx + static_cast<std::size_t>(c)] + add;
                pixels[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, v)));
            }
        }
    }
    // 2) 画面抖动（确定性小幅位移）
    const int jitterX =
        static_cast<int>((rand01(tick, 100) * 2.0 - 1.0) * 2.0 * intensity);
    const int jitterY =
        static_cast<int>((rand01(tick, 101) * 2.0 - 1.0) * 1.5 * intensity);
    if (jitterX != 0 || jitterY != 0) {
        std::vector<std::uint8_t> source(
            pixels, pixels + static_cast<std::size_t>(width) * height * 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const int sx = std::min(width - 1, std::max(0, x + jitterX));
                const int sy = std::min(height - 1, std::max(0, y + jitterY));
                const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 4;
                const std::size_t src = (static_cast<std::size_t>(sy) * width + sx) * 4;
                for (int c = 0; c < 3; ++c) {
                    pixels[dst + static_cast<std::size_t>(c)] =
                        source[src + static_cast<std::size_t>(c)];
                }
            }
        }
    }
    // 3) 胶片颗粒（时间变化噪声；复用 spatial_effects 的噪点实现）
    applyGrainRgba(pixels, width, height, 0.35 * intensity,
                   static_cast<std::int64_t>(tick));
    return true;
}

// =============================================================================
// 星光（Starburst）
// =============================================================================

bool applyStarburstRgba(std::uint8_t* pixels, int width, int height, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 找高光点（亮度 > 阈值；网格降采样保证性能）
    const double threshold = 235.0;
    const int gridStep = 8;
    std::vector<std::pair<int, int>> highlights;
    for (int y = 0; y < height && highlights.size() < 64; y += gridStep) {
        for (int x = 0; x < width; x += gridStep) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            const double luma = 0.299 * pixels[idx] + 0.587 * pixels[idx + 1] +
                                0.114 * pixels[idx + 2];
            if (luma > threshold) {
                highlights.emplace_back(x, y);
                if (highlights.size() >= 64) {
                    break;
                }
            }
        }
    }
    const double shortSide = static_cast<double>(std::min(width, height));
    const double rayLength = shortSide * 0.06 * (0.5 + intensity);
    for (const auto& point : highlights) {
        // 十字星芒：水平与垂直两条射线（逐像素加法衰减）
        for (int d = 1; d <= static_cast<int>(rayLength); ++d) {
            const double falloff = (1.0 - static_cast<double>(d) / rayLength);
            const double add = falloff * falloff * intensity * 110.0;
            for (int sign : {-1, 1}) {
                const int px = point.first + sign * d;
                if (px >= 0 && px < width) {
                    const std::size_t idx =
                        (static_cast<std::size_t>(point.second) * width + px) * 4;
                    for (int c = 0; c < 3; ++c) {
                        pixels[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                            std::min(255.0, static_cast<double>(
                                                pixels[idx + static_cast<std::size_t>(c)]) +
                                                add));
                    }
                }
                const int py = point.second + sign * d;
                if (py >= 0 && py < height) {
                    const std::size_t idx =
                        (static_cast<std::size_t>(py) * width + point.first) * 4;
                    for (int c = 0; c < 3; ++c) {
                        pixels[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                            std::min(255.0, static_cast<double>(
                                                pixels[idx + static_cast<std::size_t>(c)]) +
                                                add));
                    }
                }
            }
        }
    }
    return true;
}

// =============================================================================
// 分发
// =============================================================================

bool applyAtmosphereEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                                 double value, double timeSeconds,
                                 const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    switch (effectType) {
        case static_cast<int>(AtmosphereEffectType::Snow):
            return applySnowRgba(pixels, width, height, value, timeSeconds, quality);
        case static_cast<int>(AtmosphereEffectType::Rain):
            return applyRainRgba(pixels, width, height, value, timeSeconds, quality);
        case static_cast<int>(AtmosphereEffectType::LightLeak):
            return applyLightLeakRgba(pixels, width, height, value, timeSeconds);
        case static_cast<int>(AtmosphereEffectType::OldFilm):
            return applyOldFilmRgba(pixels, width, height, value, timeSeconds);
        case static_cast<int>(AtmosphereEffectType::Starburst):
            return applyStarburstRgba(pixels, width, height, value);
        default:
            return true;  // 非氛围特效：由其他模块处理（非错误）
    }
}

}  // namespace core
}  // namespace nairoclip
