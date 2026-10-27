#include "nairoclip/core/light_effects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "nairoclip/core/spatial_effects.h"

namespace nairoclip {
namespace core {

namespace {

/// 感知亮度（Rec.709 权重）。用亮度而非简单平均：
/// 纯蓝 (0,0,255) 的算术平均是 85，但它看起来明显比纯绿暗。
double luma(double r, double g, double b) {
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

}  // namespace

bool bloomParamsValid(const BloomParams& params) {
    if (!(params.threshold >= 0.0 && params.threshold <= 1.0)) return false;
    if (!(params.radiusPx >= 0.0 && params.radiusPx <= 64.0)) return false;
    if (!(params.intensity >= 0.0 && params.intensity <= 1.0)) return false;
    return true;
}

bool applyBloomToRgba(std::uint8_t* pixels, int width, int height, const BloomParams& params) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!bloomParamsValid(params)) {
        return false;
    }
    if (params.intensity <= 0.0 || params.radiusPx <= 0.0) {
        return true;  // 零改动
    }
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    // 1) 高光提取：只保留超过阈值的部分（软膝：给一段过渡，避免硬边）
    std::vector<std::uint8_t> highlight(count * 4, 0);
    bool anyHighlight = false;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = i * 4;
        const double r = static_cast<double>(pixels[idx + 0]) / 255.0;
        const double g = static_cast<double>(pixels[idx + 1]) / 255.0;
        const double b = static_cast<double>(pixels[idx + 2]) / 255.0;
        const double y = luma(r, g, b);
        if (y <= params.threshold) {
            continue;
        }
        // 强度 = (y - threshold) / (1 - threshold)，保持原色比例
        const double span = 1.0 - params.threshold;
        const double w = span > 1e-9 ? (y - params.threshold) / span : 1.0;
        highlight[idx + 0] = static_cast<std::uint8_t>(clamp01(r * w) * 255.0 + 0.5);
        highlight[idx + 1] = static_cast<std::uint8_t>(clamp01(g * w) * 255.0 + 0.5);
        highlight[idx + 2] = static_cast<std::uint8_t>(clamp01(b * w) * 255.0 + 0.5);
        highlight[idx + 3] = 255;
        anyHighlight = true;
    }
    if (!anyHighlight) {
        return true;  // 画面没有高光：Bloom 无事可做
    }

    // 2) 高斯模糊（复用既有实现，半径即扩散范围）
    applyGaussianBlurRgba(highlight.data(), width, height, params.radiusPx);

    // 3) 加法混合回原图（加法而非 alpha 混合：光应该"加上去"而不是"盖上去"）
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = i * 4;
        for (int c = 0; c < 3; ++c) {
            const double base = static_cast<double>(pixels[idx + static_cast<std::size_t>(c)]);
            const double add = static_cast<double>(highlight[idx + static_cast<std::size_t>(c)]);
            const double v = base + add * params.intensity;
            pixels[idx + static_cast<std::size_t>(c)] =
                static_cast<std::uint8_t>(std::clamp(v, 0.0, 255.0));
        }
    }
    return true;
}

bool lensFlareParamsValid(const LensFlareParams& params) {
    if (!(params.sourceX >= 0.0 && params.sourceX <= 1.0)) return false;
    if (!(params.sourceY >= 0.0 && params.sourceY <= 1.0)) return false;
    if (!(params.size >= 0.0 && params.size <= 1.0)) return false;
    if (!(params.temperature >= -1.0 && params.temperature <= 1.0)) return false;
    if (!(params.intensity >= 0.0 && params.intensity <= 1.0)) return false;
    if (params.ghostCount < 0 || params.ghostCount > 6) return false;
    return true;
}

bool applyLensFlareToRgba(std::uint8_t* pixels, int width, int height,
                          const LensFlareParams& params) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!lensFlareParamsValid(params)) {
        return false;
    }
    if (params.intensity <= 0.0) {
        return true;
    }

    const double cx = static_cast<double>(width) * 0.5;
    const double cy = static_cast<double>(height) * 0.5;
    const double sx = params.sourceX * static_cast<double>(width);
    const double sy = params.sourceY * static_cast<double>(height);
    const double shortSide = static_cast<double>(std::min(width, height));

    // 色温 → 色偏：暖偏橙金，冷偏青蓝。中性时三通道等权。
    double tintR = 1.0;
    double tintG = 1.0;
    double tintB = 1.0;
    const double t = params.temperature;
    if (t > 0.0) {
        tintR += 0.35 * t;   // 暖：红/绿升，蓝降
        tintG += 0.12 * t;
        tintB -= 0.30 * t;
    } else if (t < 0.0) {
        tintR += 0.30 * t;   // t 为负 → 红降
        tintG -= 0.05 * t;   // → 绿略升
        tintB -= 0.40 * t;   // → 蓝升
    }
    tintR = std::max(0.0, tintR);
    tintG = std::max(0.0, tintG);
    tintB = std::max(0.0, tintB);

    // 需要叠加的光量缓冲（先累加再统一混合，避免多次饱和截断）
    std::vector<double> addR(static_cast<std::size_t>(width) * height, 0.0);
    std::vector<double> addG(addR.size(), 0.0);
    std::vector<double> addB(addR.size(), 0.0);

    /// 往缓冲里画一个软边圆盘。
    auto addDisc = [&](double ccx, double ccy, double radius, double strength) {
        if (radius <= 0.5 || strength <= 0.0) {
            return;
        }
        const int x0 = std::max(0, static_cast<int>(ccx - radius) - 1);
        const int x1 = std::min(width - 1, static_cast<int>(ccx + radius) + 1);
        const int y0 = std::max(0, static_cast<int>(ccy - radius) - 1);
        const int y1 = std::min(height - 1, static_cast<int>(ccy + radius) + 1);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const double dx = static_cast<double>(x) - ccx;
                const double dy = static_cast<double>(y) - ccy;
                const double d = std::sqrt(dx * dx + dy * dy);
                if (d > radius) {
                    continue;
                }
                // 软边：中心 1 → 边缘 0（cos 曲线，比线性更自然）
                const double k = 0.5 + 0.5 * std::cos(d / radius * 3.14159265358979323846);
                const double a = strength * k;
                const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(x);
                addR[i] += a * tintR;
                addG[i] += a * tintG;
                addB[i] += a * tintB;
            }
        }
    };

    // 主光源
    const double srcRadius = shortSide * 0.5 * params.size;
    addDisc(sx, sy, srcRadius, params.intensity * 0.9);

    // 鬼影：沿「光源 → 中心」的延长线依次排布，越远越小越淡
    for (int i = 1; i <= params.ghostCount; ++i) {
        // 系数从 0.55 降到 0.35：0.55 时第一个鬼影就跑出画面
        // （光源在 0.8 → 鬼影落在 16 - 14.9 ≈ 1.1，正好贴边），
        // 观感上等于"没有鬼影"。0.35 让前两个鬼影落在画面内。
        const double f = 1.0 + static_cast<double>(i) * 0.35;
        const double gx = cx + (cx - sx) * f;
        const double gy = cy + (cy - sy) * f;
        const double r = srcRadius * std::max(0.15, 0.55 - 0.06 * static_cast<double>(i));
        const double s = params.intensity * std::max(0.05, 0.35 - 0.05 * static_cast<double>(i));
        addDisc(gx, gy, r, s);
    }

    // 统一叠加
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = i * 4;
        const double rr = static_cast<double>(pixels[idx + 0]) + addR[i] * 255.0;
        const double gg = static_cast<double>(pixels[idx + 1]) + addG[i] * 255.0;
        const double bb = static_cast<double>(pixels[idx + 2]) + addB[i] * 255.0;
        pixels[idx + 0] = static_cast<std::uint8_t>(std::clamp(rr, 0.0, 255.0));
        pixels[idx + 1] = static_cast<std::uint8_t>(std::clamp(gg, 0.0, 255.0));
        pixels[idx + 2] = static_cast<std::uint8_t>(std::clamp(bb, 0.0, 255.0));
    }
    return true;
}

const char* lightEffectsPurpose() {
    return "光效：Bloom（高光提取→模糊→叠加）与镜头光晕（光源 + 鬼影环）";
}

}  // namespace core
}  // namespace nairoclip
