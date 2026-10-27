#include "nairoclip/core/motion_blur_directional.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

double clamp01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

/// 双线性采样（归一化坐标；越界钳制到边界）。
void sampleBilinear(const std::uint8_t* px, int w, int h, double u, double v,
                    std::uint8_t* outRgb) {
    u = clamp01(u);
    v = clamp01(v);
    const double fx = u * (w - 1);
    const double fy = v * (h - 1);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double tx = fx - x0;
    const double ty = fy - y0;
    for (int c = 0; c < 3; ++c) {
        const double p00 = px[(static_cast<std::size_t>(y0) * w + x0) * 4 + c];
        const double p10 = px[(static_cast<std::size_t>(y0) * w + x1) * 4 + c];
        const double p01 = px[(static_cast<std::size_t>(y1) * w + x0) * 4 + c];
        const double p11 = px[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
        const double top = p00 + (p10 - p00) * tx;
        const double bottom = p01 + (p11 - p01) * tx;
        outRgb[c] = static_cast<std::uint8_t>(
            std::clamp(top + (bottom - top) * ty + 0.5, 0.0, 255.0));
    }
}

}  // namespace

bool directionalBlurParamsValid(const DirectionalBlurParams& params) {
    if (!std::isfinite(params.velocityX) || !std::isfinite(params.velocityY)) return false;
    if (!(params.shutterAngleDegrees >= 0.0 && params.shutterAngleDegrees <= 360.0)) {
        return false;
    }
    if (params.samples < 2 || params.samples > 32) return false;
    if (!(params.maxLength >= 0.0 && params.maxLength <= 0.5)) return false;
    return true;
}

bool velocityFromPositions(double x0, double y0, double x1, double y1, double dtSeconds,
                           double frameDurationSeconds, double& outVx, double& outVy) {
    outVx = 0.0;
    outVy = 0.0;
    if (!(dtSeconds > 1e-9) || !(frameDurationSeconds > 1e-9)) {
        return false;
    }
    if (!std::isfinite(x0) || !std::isfinite(y0) || !std::isfinite(x1) ||
        !std::isfinite(y1)) {
        return false;
    }
    const double vxPerSec = (x1 - x0) / dtSeconds;
    const double vyPerSec = (y1 - y0) / dtSeconds;
    outVx = vxPerSec * frameDurationSeconds;
    outVy = vyPerSec * frameDurationSeconds;
    return true;
}

bool applyDirectionalBlurToRgba(std::uint8_t* pixels, int width, int height,
                                const DirectionalBlurParams& params) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!directionalBlurParamsValid(params)) {
        return false;
    }
    // velocity 已经是「本帧内位移」（归一化画幅），无需再换算帧率。
    // 单位约定见 header：模糊半长 = |位移| × (快门角度/360) / 2。
    const double vx = params.velocityX;
    const double vy = params.velocityY;
    const double speed = std::sqrt(vx * vx + vy * vy);
    if (speed < 1e-6) {
        return true;  // 静止：零改动
    }
    // 快门角度：180° → 半帧位移；360° → 全帧位移
    const double shutterFactor = params.shutterAngleDegrees / 360.0;
    double len = speed * shutterFactor;
    if (len <= 1e-9) {
        return true;  // 快门 0°：无曝光时间，无模糊
    }
    if (len > params.maxLength) {
        len = params.maxLength;
    }
    // 单位方向向量
    const double ux = vx / speed;
    const double uy = vy / speed;

    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    std::vector<std::uint8_t> source(pixels, pixels + count * 4);

    const int n = params.samples;
    // 半长：以当前帧为中心，向两侧各延伸 len/2（快门在帧时刻前后各开一半）
    const double half = len * 0.5;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // 归一化坐标
            const double nx = width > 1 ? static_cast<double>(x) / (width - 1) : 0.0;
            const double ny = height > 1 ? static_cast<double>(y) / (height - 1) : 0.0;
            double accR = 0.0;
            double accG = 0.0;
            double accB = 0.0;
            double weightSum = 0.0;
            for (int i = 0; i < n; ++i) {
                // t ∈ [-1, 1]：三角权重（中心最高）
                const double t = (n == 1) ? 0.0 : (2.0 * static_cast<double>(i) / (n - 1) - 1.0);
                const double w = 1.0 - std::fabs(t) * 0.9;  // 端点权重 0.1，不归零
                // 沿方向偏移。注意：画面坐标 y 向下，vy 的符号由调用方约定。
                const double su = nx + ux * half * t;
                const double sv = ny + uy * half * t;
                std::uint8_t rgb[3] = {0, 0, 0};
                sampleBilinear(source.data(), width, height, su, sv, rgb);
                accR += rgb[0] * w;
                accG += rgb[1] * w;
                accB += rgb[2] * w;
                weightSum += w;
            }
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            if (weightSum > 1e-9) {
                pixels[idx + 0] = static_cast<std::uint8_t>(
                    std::clamp(accR / weightSum + 0.5, 0.0, 255.0));
                pixels[idx + 1] = static_cast<std::uint8_t>(
                    std::clamp(accG / weightSum + 0.5, 0.0, 255.0));
                pixels[idx + 2] = static_cast<std::uint8_t>(
                    std::clamp(accB / weightSum + 0.5, 0.0, 255.0));
            }
            // alpha 不变
        }
    }
    return true;
}

const char* directionalBlurPurpose() {
    return "方向性运动模糊：沿运动矢量在单帧内多点采样（不依赖相邻帧）";
}

}  // namespace core
}  // namespace nairoclip
