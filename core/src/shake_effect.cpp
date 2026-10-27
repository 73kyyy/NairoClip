#include "nairoclip/core/shake_effect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;

/// 确定性哈希 → [0,1)。与项目内其它特效模块同一风格（murmur3 finalizer）。
/// 不用 rand()/random_device：那会让三端与预览/导出结果不一致。
double hashUnit(std::uint64_t seed, std::uint64_t salt) {
    std::uint64_t h = seed ^ (salt * 0x9E3779B97F4A7C15ull);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return static_cast<double>(h % 1000000ull) / 1000000.0;
}

/// 双线性采样（归一化坐标；越界钳制）。
void sampleBilinear(const std::uint8_t* pixels, int width, int height, double u, double v,
                    std::uint8_t* outRgb) {
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double fx = u * (width - 1);
    const double fy = v * (height - 1);
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const double tx = fx - x0;
    const double ty = fy - y0;
    for (int c = 0; c < 3; ++c) {
        const double p00 = pixels[(static_cast<std::size_t>(y0) * width + x0) * 4 + c];
        const double p10 = pixels[(static_cast<std::size_t>(y0) * width + x1) * 4 + c];
        const double p01 = pixels[(static_cast<std::size_t>(y1) * width + x0) * 4 + c];
        const double p11 = pixels[(static_cast<std::size_t>(y1) * width + x1) * 4 + c];
        const double top = p00 + (p10 - p00) * tx;
        const double bottom = p01 + (p11 - p01) * tx;
        outRgb[c] = static_cast<std::uint8_t>(std::clamp(top + (bottom - top) * ty + 0.5,
                                                        0.0, 255.0));
    }
}

}  // namespace

bool shakeParamsValid(const ShakeParams& params) {
    if (params.amplitude < 0.0 || params.amplitude > 0.2) return false;
    if (params.frequency < 0.0 || params.frequency > 20.0) return false;
    if (params.decaySeconds < 0.0 || params.decaySeconds > 10.0) return false;
    // 相位允许任意值（三角函数的周期语义），只拒绝非有限值
    if (!std::isfinite(params.phase)) return false;
    const int traj = static_cast<int>(params.trajectory);
    if (traj < 0 || traj > 3) return false;
    return true;
}

void shakeOffsetAt(const ShakeParams& params, double timeSeconds, int width, int height,
                   double& outDx, double& outDy) {
    outDx = 0.0;
    outDy = 0.0;
    if (width <= 0 || height <= 0 || !shakeParamsValid(params)) {
        return;
    }
    // 以短边为基准：同一 amplitude 在横屏/竖屏下观感一致。
    const double base = static_cast<double>(std::min(width, height)) * params.amplitude;
    if (base <= 0.0) {
        return;
    }
    const double t = std::max(0.0, timeSeconds);
    const double omega = 2.0 * kPi * params.frequency;
    const double phase = omega * t + params.phase;

    // 衰减包络：decaySeconds = 0 → 恒为 1；否则按 exp(-3t/T) 衰减，
    // 在 t = T 处约剩 5%（人眼基本看不出），效果「抖一下就停」。
    double envelope = 1.0;
    if (params.decaySeconds > 0.0) {
        envelope = std::exp(-3.0 * t / params.decaySeconds);
    }
    const double amp = base * envelope;
    if (amp <= 0.0) {
        return;
    }

    switch (params.trajectory) {
        case ShakeTrajectory::Horizontal:
            outDx = std::sin(phase) * amp;
            break;
        case ShakeTrajectory::Vertical:
            outDy = std::sin(phase) * amp;
            break;
        case ShakeTrajectory::Circular:
            outDx = std::cos(phase) * amp;
            outDy = std::sin(phase) * amp;
            break;
        case ShakeTrajectory::Random: {
            // 每个「震荡步」取一次随机偏移，步内保持不变。
            // 步长 = 半个周期；frequency = 0 时退化为单一常量偏移。
            const std::uint64_t step =
                params.frequency > 0.0
                    ? static_cast<std::uint64_t>(std::floor(t * params.frequency * 2.0))
                    : 0ull;
            const std::uint64_t s = static_cast<std::uint64_t>(params.seed);
            outDx = (hashUnit(step, s ^ 0xA5A5A5A5ull) * 2.0 - 1.0) * amp;
            outDy = (hashUnit(step, s ^ 0x5A5A5A5Aull) * 2.0 - 1.0) * amp;
            break;
        }
    }
}

bool applyShakeToRgba(std::uint8_t* pixels, int width, int height, const ShakeParams& params,
                      double timeSeconds, double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!shakeParamsValid(params)) {
        return false;
    }
    if (intensity <= 0.0) {
        return true;  // 强度 0：零改动
    }
    intensity = std::min(1.0, intensity);

    double dx = 0.0;
    double dy = 0.0;
    shakeOffsetAt(params, timeSeconds, width, height, dx, dy);
    dx *= intensity;
    dy *= intensity;
    // 亚像素位移无意义（采样结果与原地相同），直接跳过以免无谓拷贝
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9) {
        return true;
    }

    std::vector<std::uint8_t> source(
        pixels, pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            // 目标像素 (x,y) 采样源 (x - dx, y - dy)
            double sx = x - dx;
            double sy = y - dy;
            // 镜像填充：超出边界的部分折回，避免黑边
            if (sx < 0) sx = -sx;
            if (sx > width - 1) sx = 2.0 * (width - 1) - sx;
            if (sy < 0) sy = -sy;
            if (sy > height - 1) sy = 2.0 * (height - 1) - sy;
            sx = std::clamp(sx, 0.0, static_cast<double>(width - 1));
            sy = std::clamp(sy, 0.0, static_cast<double>(height - 1));
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            sampleBilinear(source.data(), width, height,
                           sx / std::max(1, width - 1), sy / std::max(1, height - 1),
                           pixels + idx);
            // alpha 保持原值
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
