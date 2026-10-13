#include "nairoclip/core/motion_effects.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.141592653589793;

/// 找 timeSeconds 之前最近的节拍（无则为 -1）。
int nearestBeatBefore(double timeSeconds, const std::vector<Time>& beats) {
    int best = -1;
    for (std::size_t i = 0; i < beats.size(); ++i) {
        if (beats[i].toDouble() <= timeSeconds) {
            best = static_cast<int>(i);
        } else {
            break;  // 升序
        }
    }
    return best;
}

}  // namespace

const char* motionEffectPurpose() {
    return "动感特效（缩放冲击/甩镜/变速闪烁/节奏律动/运动模糊）：时间函数与节拍"
           "驱动，或相邻帧加权叠加；导出完整精度、预览可分级简化";
}

// =============================================================================
// 缩放冲击（ZoomPunch）
// =============================================================================

bool applyZoomPunchRgba(std::uint8_t* pixels, int width, int height, double intensity,
                        double timeSeconds) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 周期脉冲：每 2 秒一个周期，前 25% 快速放大（到达峰值后 75% 恢复）
    const double period = 2.0;
    const double phase = std::fmod(timeSeconds, period) / period;  // 0..1
    double pulse = 0.0;
    if (phase < 0.25) {
        // 快速放大（sin 半周期）
        pulse = std::sin(phase / 0.25 * kPi);
    } else if (phase < 0.75) {
        // 恢复
        pulse = std::sin((phase - 0.25) / 0.5 * kPi);
    }
    const double zoom = 1.0 + pulse * 0.12 * intensity;  // 最多放大 12%
    if (std::fabs(zoom - 1.0) < 1e-6) {
        return true;  // 无缩放（零改动）
    }
    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    const double cx = static_cast<double>(width) * 0.5;
    const double cy = static_cast<double>(height) * 0.5;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // 以画面中心为原点缩放采样
            const double sx = cx + (static_cast<double>(x) + 0.5 - cx) / zoom;
            const double sy = cy + (static_cast<double>(y) + 0.5 - cy) / zoom;
            const int px = std::min(width - 1, std::max(0, static_cast<int>(sx)));
            const int py = std::min(height - 1, std::max(0, static_cast<int>(sy)));
            const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 4;
            const std::size_t src = (static_cast<std::size_t>(py) * width + px) * 4;
            for (int c = 0; c < 3; ++c) {
                pixels[dst + static_cast<std::size_t>(c)] =
                    source[src + static_cast<std::size_t>(c)];
            }
        }
    }
    return true;
}

// =============================================================================
// 甩镜（WhipPan：方向性模糊）
// =============================================================================

bool applyWhipPanRgba(std::uint8_t* pixels, int width, int height, double intensity,
                      double timeSeconds, double directionDegrees,
                      const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    // 方向性模糊（文档 5.4「甩镜：方向性模糊Shader实现」）：
    // 沿 directionDegrees 方向的多采样线性平均；采样数按质量分级
    // （导出 16；预览按 sampleBudget）；模糊长度 = 画面短边 × 12% × 强度。
    // 说明：初版实现加了时间脉冲调制且长度仅 8%（实测 2.7px 对视差边缘平滑不足，
    // 效果近似无效）→ 改为静态长度 12%（仍可被「缩放/律动」等时间驱动效果叠加）。
    const double length =
        static_cast<double>(std::min(width, height)) * 0.12 * intensity;
    const int samples = effectiveSampleCount(quality, 16);
    const double rad = directionDegrees * kPi / 180.0;
    const double dx = std::cos(rad);
    const double dy = std::sin(rad);
    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double accR = 0.0, accG = 0.0, accB = 0.0;
            for (int s = 0; s < samples; ++s) {
                const double t =
                    (static_cast<double>(s) / static_cast<double>(samples - 1) - 0.5) * length;
                const int px = std::min(width - 1, std::max(0, static_cast<int>(x + dx * t)));
                const int py = std::min(height - 1, std::max(0, static_cast<int>(y + dy * t)));
                const std::size_t idx = (static_cast<std::size_t>(py) * width + px) * 4;
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
// 变速闪烁（BeatFlicker：节拍驱动亮度脉冲）
// =============================================================================

bool applyBeatFlickerRgba(std::uint8_t* pixels, int width, int height, double intensity,
                          double timeSeconds, const std::vector<Time>& beats) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    const int beatIndex = nearestBeatBefore(timeSeconds, beats);
    if (beatIndex < 0) {
        return true;  // 无节拍数据：零改动（不伪造闪烁）
    }
    const double beatTime = beats[static_cast<std::size_t>(beatIndex)].toDouble();
    const double delta = timeSeconds - beatTime;
    // 闪烁窗口 0.25 秒（指数衰减）
    const double window = 0.25;
    if (delta < 0.0 || delta > window) {
        return true;  // 不在节拍窗口内：零改动
    }
    const double pulse = std::exp(-delta / (window * 0.4));
    const double gain = 1.0 + pulse * 0.35 * intensity;  // 最多提亮 35%
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::size_t idx = i * 4;
        for (int c = 0; c < 3; ++c) {
            const double v = pixels[idx + static_cast<std::size_t>(c)] * gain;
            pixels[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                std::min(255.0, std::max(0.0, v + 0.5)));
        }
    }
    return true;
}

// =============================================================================
// 节奏律动（BeatRhythm：节拍驱动局部缩放）
// =============================================================================

bool applyBeatRhythmRgba(std::uint8_t* pixels, int width, int height, double intensity,
                         double timeSeconds, const std::vector<Time>& beats) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;
    }
    intensity = std::min(1.0, intensity);
    const int beatIndex = nearestBeatBefore(timeSeconds, beats);
    if (beatIndex < 0) {
        return true;  // 无节拍：零改动
    }
    const double beatTime = beats[static_cast<std::size_t>(beatIndex)].toDouble();
    const double delta = timeSeconds - beatTime;
    const double window = 0.3;
    if (delta < 0.0 || delta > window) {
        return true;
    }
    const double pulse = std::exp(-delta / (window * 0.45));
    // 棋盘式局部缩放：8x8 网格，按 (格索引 + 节拍序号) 确定性选择是否律动
    const int gridSize = 8;
    const int cellW = std::max(1, width / gridSize);
    const int cellH = std::max(1, height / gridSize);
    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    for (int cy = 0; cy < gridSize; ++cy) {
        for (int cx = 0; cx < gridSize; ++cx) {
            // 确定性选择：超过一半格子参与（奇偶棋盘 + 节拍序号扰动）
            if (((cx + cy + beatIndex) & 1) != 0) {
                continue;
            }
            const double zoom = 1.0 + pulse * 0.08 * intensity;  // 局部最多放大 8%
            const double centerX = (cx + 0.5) * cellW;
            const double centerY = (cy + 0.5) * cellH;
            const int x0 = cx * cellW;
            const int y0 = cy * cellH;
            const int x1 = std::min(width, x0 + cellW);
            const int y1 = std::min(height, y0 + cellH);
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const double sx = centerX + (static_cast<double>(x) + 0.5 - centerX) / zoom;
                    const double sy = centerY + (static_cast<double>(y) + 0.5 - centerY) / zoom;
                    const int px = std::min(width - 1, std::max(0, static_cast<int>(sx)));
                    const int py = std::min(height - 1, std::max(0, static_cast<int>(sy)));
                    const std::size_t dst = (static_cast<std::size_t>(y) * width + x) * 4;
                    const std::size_t src = (static_cast<std::size_t>(py) * width + px) * 4;
                    for (int c = 0; c < 3; ++c) {
                        pixels[dst + static_cast<std::size_t>(c)] =
                            source[src + static_cast<std::size_t>(c)];
                    }
                }
            }
        }
    }
    return true;
}

// =============================================================================
// 运动模糊（MotionBlur：多帧加权叠加）
// =============================================================================

int blurFrameCountForShutterAngle(double shutterAngleDegrees) {
    if (shutterAngleDegrees <= 0.0) {
        return 1;
    }
    if (shutterAngleDegrees > 360.0) {
        shutterAngleDegrees = 360.0;
    }
    // 快门角度 180°（电影标准）= 3 帧叠加；360° = 5 帧；90° = 1 帧。
    // 线性映射：N = 1 + round(角度/90)；奇数化（中央帧对称）。
    int frames = 1 + static_cast<int>(shutterAngleDegrees / 90.0 + 0.5);
    if (frames > 9) frames = 9;
    if (frames % 2 == 0) ++frames;  // 奇数（对称加权）
    if (frames > 9) frames = 7;
    return frames;
}

bool applyMotionBlurFrames(const std::vector<std::vector<std::uint8_t>>& frames, int width,
                           int height, double shutterAngleDegrees,
                           std::vector<std::uint8_t>& out) {
    if (frames.empty() || width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t pixelBytes =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    for (const auto& frame : frames) {
        if (frame.size() < pixelBytes) {
            return false;  // 帧尺寸不足
        }
    }
    // 叠帧数换算（文档 5.5）+ 实际可用帧数限制
    int count = blurFrameCountForShutterAngle(shutterAngleDegrees);
    if (count > static_cast<int>(frames.size())) {
        count = static_cast<int>(frames.size());
    }
    if (count <= 1) {
        out = frames[frames.size() / 2];  // 无模糊：中央帧
        return true;
    }
    // 取以中央为基准的连续 count 帧（文档：「相邻帧」）
    const int total = static_cast<int>(frames.size());
    const int center = total / 2;
    int start = center - count / 2;
    if (start < 0) start = 0;
    if (start + count > total) start = total - count;
    // 权重：中央帧最高，两端递减（三角权重）
    out.assign(pixelBytes, 0);
    double weightSum = 0.0;
    std::vector<double> weights(static_cast<std::size_t>(count), 0.0);
    for (int i = 0; i < count; ++i) {
        // 距离中央的归一化距离 d ∈ [0, 1]
        const double d = count > 1 ? std::fabs(static_cast<double>(i) -
                                               static_cast<double>(count - 1) / 2.0) /
                                         (static_cast<double>(count - 1) / 2.0)
                                   : 0.0;
        weights[static_cast<std::size_t>(i)] = 1.0 - 0.8 * d;  // 1.0 → 0.2
        weightSum += weights[static_cast<std::size_t>(i)];
    }
    for (int i = 0; i < count; ++i) {
        const std::vector<std::uint8_t>& frame = frames[static_cast<std::size_t>(start + i)];
        const double w = weights[static_cast<std::size_t>(i)] / weightSum;
        for (std::size_t p = 0; p < pixelBytes; ++p) {
            const double v = out[p] + frame[p] * w;
            out[p] = static_cast<std::uint8_t>(std::min(255.0, v + 0.5));
        }
    }
    return true;
}

// =============================================================================
// 分发
// =============================================================================

bool applyMotionEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                             double value, double timeSeconds,
                             const std::vector<Time>& beats,
                             const RenderQuality& quality) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    switch (effectType) {
        case static_cast<int>(MotionEffectType::ZoomPunch):
            return applyZoomPunchRgba(pixels, width, height, value, timeSeconds);
        case static_cast<int>(MotionEffectType::WhipPan):
            return applyWhipPanRgba(pixels, width, height, value, timeSeconds, 0.0, quality);
        case static_cast<int>(MotionEffectType::BeatFlicker):
            return applyBeatFlickerRgba(pixels, width, height, value, timeSeconds, beats);
        case static_cast<int>(MotionEffectType::BeatRhythm):
            return applyBeatRhythmRgba(pixels, width, height, value, timeSeconds, beats);
        case static_cast<int>(MotionEffectType::MotionBlur):
            // 单帧接口下运动模糊为占位（真实多帧叠加走 applyMotionBlurFrames；
            // 导出管线在帧序列层面调用）；返回 true 不改动。
            return true;
        default:
            return true;
    }
}

}  // namespace core
}  // namespace nairoclip
