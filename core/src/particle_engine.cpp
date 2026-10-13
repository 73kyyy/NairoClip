#include "nairoclip/core/particle_engine.h"

#include <cmath>

namespace nairoclip {
namespace core {

namespace {

/// 确定性伪随机（hash_combine + murmur3 finalizer；同 spatial/screen 特效技术）。
std::uint32_t hashValue(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL;
    const auto mix = [&h](std::uint64_t v) {
        h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
    };
    mix(a);
    mix(b);
    mix(c);
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return static_cast<std::uint32_t>(h >> 32);
}

double rand01(std::uint64_t a, std::uint64_t b, std::uint64_t c) {
    return static_cast<double>(hashValue(a, b, c)) / 4294967296.0;
}

/// 整数帧序号（用于时变种子的稳定量化）。
std::uint64_t frameTick(double timeSeconds) {
    const double t = timeSeconds < 0.0 ? 0.0 : timeSeconds;
    return static_cast<std::uint64_t>(t * 1000.0);  // 毫秒粒度
}

}  // namespace

bool sampleParticle(int index, double timeSeconds, const ParticleConfig& config,
                    ParticleSample& out) {
    if (index < 0 || !config.valid()) {
        return false;
    }
    const double time = timeSeconds < 0.0 ? 0.0 : timeSeconds;
    const std::uint64_t idx = static_cast<std::uint64_t>(index);
    const std::uint64_t seed = config.seed;
    const std::uint64_t tick = frameTick(time);

    // 初始位置（确定性）：x ∈ [0,1]，y ∈ [-0.1, 0.1]（顶部附近生成）
    const double startX = rand01(seed, idx, 1);
    const double startY = rand01(seed, idx, 2) * 0.2 - 0.1;
    // 相位：错开各粒子的生命周期起点（避免同时重置）
    const double phase = rand01(seed, idx, 3);
    // 速度扰动
    const double vx = config.velocityX +
                      (rand01(seed, idx, 4) * 2.0 - 1.0) * config.velocitySpread;
    const double vy = config.velocityY +
                      (rand01(seed, idx, 5) * 2.0 - 1.0) * config.velocitySpread;
    // 尺寸扰动
    const double size =
        config.size * (1.0 + (rand01(seed, idx, 6) * 2.0 - 1.0) * config.sizeSpread);
    // 生命周期进度：t = fmod(time/寿命 + 相位, 1)
    double lifePhase = std::fmod(time / config.lifetimeSeconds + phase, 1.0);
    if (lifePhase < 0.0) lifePhase += 1.0;
    // 位置：初位置 + 速度 × 实际秒数 + 0.5 × 重力 × t²
    const double elapsed = lifePhase * config.lifetimeSeconds;
    const double x = startX + vx * elapsed;
    const double y = startY + vy * elapsed + 0.5 * config.gravityY * elapsed * elapsed;
    // 不透明度：淡入（前 10%）+ 淡出（后 20%）
    double opacity = 1.0;
    if (lifePhase < 0.10) {
        opacity = lifePhase / 0.10;
    } else if (lifePhase > 0.80) {
        opacity = (1.0 - lifePhase) / 0.20;
    }
    if (opacity < 0.0) opacity = 0.0;
    if (opacity > 1.0) opacity = 1.0;

    out.x = x;
    out.y = y;
    out.size = size > 0.0005 ? size : 0.0005;
    out.lifePhase = lifePhase;
    out.opacity = opacity;
    return true;
}

const char* particleEnginePurpose() {
    return "通用粒子引擎（数量/初速度/重力/生命周期/种子参数化）：为雪花/下雨/"
           "光斑/星光等氛围特效提供确定性粒子采样；GPU 后端按同一语义实现";
}

}  // namespace core
}  // namespace nairoclip
