#include "test_framework.h"
#include "nairoclip/core/shake_effect.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 生成一张有梯度的测试图（便于检测位移：纯色图位移后完全看不出差别）。
std::vector<std::uint8_t> makeGradient(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            px[i + 0] = static_cast<std::uint8_t>((x * 255) / (w > 1 ? w - 1 : 1));
            px[i + 1] = static_cast<std::uint8_t>((y * 255) / (h > 1 ? h - 1 : 1));
            px[i + 2] = static_cast<std::uint8_t>(((x + y) * 127) / (w + h));
            px[i + 3] = 255;
        }
    }
    return px;
}

ShakeParams baseParams() {
    ShakeParams p;
    p.amplitude = 0.05;
    p.frequency = 5.0;
    return p;
}

}  // namespace

// =============================================================================
// 震动特效（B1-5）
// =============================================================================
//
// 核心约束：**确定性**。同参数同时间点必须逐像素一致——否则预览与导出不同、
// 三端成片不同、问题无法复现。这也是文档里唯一被标为「关键」的要求。

NC_TEST(Shake, SameParamsProduceBitIdenticalFrames) {
    const int w = 64, h = 48;
    const std::vector<std::uint8_t> original = makeGradient(w, h);
    const ShakeParams p = baseParams();

    std::vector<std::uint8_t> a = original;
    std::vector<std::uint8_t> b = original;
    NC_CHECK(applyShakeToRgba(a.data(), w, h, p, 0.37));
    NC_CHECK(applyShakeToRgba(b.data(), w, h, p, 0.37));
    // 逐像素一致
    NC_CHECK(a == b);
    // 且确实发生了位移（不是空操作）
    NC_CHECK(a != original);
}

NC_TEST(Shake, DeterminismAcrossManyFrames) {
    // 单帧一致可能碰巧；连续多帧都一致才能说明没有隐藏的全局状态
    const int w = 32, h = 32;
    const std::vector<std::uint8_t> original = makeGradient(w, h);
    const ShakeParams p = baseParams();
    for (int f = 0; f < 30; ++f) {
        const double t = f * 0.033;
        std::vector<std::uint8_t> a = original;
        std::vector<std::uint8_t> b = original;
        NC_CHECK(applyShakeToRgba(a.data(), w, h, p, t));
        NC_CHECK(applyShakeToRgba(b.data(), w, h, p, t));
        NC_CHECK(a == b);
    }
}

NC_TEST(Shake, AllTrajectoriesAreDeterministic) {
    const int w = 32, h = 24;
    const std::vector<std::uint8_t> original = makeGradient(w, h);
    for (int traj = 0; traj <= 3; ++traj) {
        ShakeParams p = baseParams();
        p.trajectory = static_cast<ShakeTrajectory>(traj);
        for (int f = 0; f < 10; ++f) {
            std::vector<std::uint8_t> a = original;
            std::vector<std::uint8_t> b = original;
            NC_CHECK(applyShakeToRgba(a.data(), w, h, p, f * 0.05));
            NC_CHECK(applyShakeToRgba(b.data(), w, h, p, f * 0.05));
            NC_CHECK(a == b);
        }
    }
}

NC_TEST(Shake, DifferentSeedsGiveDifferentRandomOffsets) {
    const int w = 64, h = 64;
    ShakeParams p1 = baseParams();
    p1.trajectory = ShakeTrajectory::Random;
    p1.seed = 1;
    ShakeParams p2 = p1;
    p2.seed = 2;

    double dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    shakeOffsetAt(p1, 0.1, w, h, dx1, dy1);
    shakeOffsetAt(p2, 0.1, w, h, dx2, dy2);
    // 种子不同 → 偏移不同（否则「随机」名不副实）
    NC_CHECK(std::fabs(dx1 - dx2) > 1e-9 || std::fabs(dy1 - dy2) > 1e-9);
}

NC_TEST(Shake, AmplitudeScalesWithShortSide) {
    ShakeParams p;
    p.amplitude = 0.1;
    p.frequency = 1.0;
    p.trajectory = ShakeTrajectory::Horizontal;
    // 相位 0：sin(0)=0。取 1/4 周期处偏移最大。
    const double quarterPeriod = 0.25;  // frequency=1Hz → 周期 1s
    double dx = 0, dy = 0;
    shakeOffsetAt(p, quarterPeriod, 200, 100, dx, dy);
    // 短边 100 × 0.1 = 10 像素
    NC_CHECK(std::fabs(std::fabs(dx) - 10.0) < 1e-6);
    NC_CHECK(std::fabs(dy) < 1e-12);

    // 同一 amplitude 在竖屏下也按短边 100 算（画面观感一致）
    shakeOffsetAt(p, quarterPeriod, 100, 200, dx, dy);
    NC_CHECK(std::fabs(std::fabs(dx) - 10.0) < 1e-6);
}

NC_TEST(Shake, HorizontalOnlyMovesX) {
    ShakeParams p = baseParams();
    p.trajectory = ShakeTrajectory::Horizontal;
    for (int i = 0; i < 20; ++i) {
        double dx = 0, dy = 0;
        shakeOffsetAt(p, i * 0.05, 100, 100, dx, dy);
        NC_CHECK(std::fabs(dy) < 1e-12);
    }
}

NC_TEST(Shake, VerticalOnlyMovesY) {
    ShakeParams p = baseParams();
    p.trajectory = ShakeTrajectory::Vertical;
    for (int i = 0; i < 20; ++i) {
        double dx = 0, dy = 0;
        shakeOffsetAt(p, i * 0.05, 100, 100, dx, dy);
        NC_CHECK(std::fabs(dx) < 1e-12);
    }
}

NC_TEST(Shake, CircularKeepsRadius) {
    ShakeParams p = baseParams();
    p.trajectory = ShakeTrajectory::Circular;
    p.amplitude = 0.1;
    const double r0 = 10.0;  // 100 * 0.1
    for (int i = 0; i < 12; ++i) {
        double dx = 0, dy = 0;
        shakeOffsetAt(p, i * 0.07, 100, 100, dx, dy);
        const double r = std::sqrt(dx * dx + dy * dy);
        NC_CHECK(std::fabs(r - r0) < 1e-6);
    }
}

NC_TEST(Shake, DecayMakesOffsetShrinkOverTime) {
    ShakeParams p;
    p.amplitude = 0.1;
    p.frequency = 0.0;  // 关掉振荡，只看包络
    p.trajectory = ShakeTrajectory::Horizontal;
    p.decaySeconds = 1.0;

    // frequency = 0 → sin(0 + 0) = 0，改用 Circular 的 cos 才非零。
    p.trajectory = ShakeTrajectory::Circular;
    double dx1 = 0, dy1 = 0, dx2 = 0, dy2 = 0;
    shakeOffsetAt(p, 0.0, 100, 100, dx1, dy1);
    shakeOffsetAt(p, 0.5, 100, 100, dx2, dy2);
    const double r1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
    const double r2 = std::sqrt(dx2 * dx2 + dy2 * dy2);
    NC_CHECK(r1 > r2);
    // t = 0.5T：exp(-3*0.5) ≈ 0.223 → 约剩 22%
    NC_CHECK(std::fabs(r2 / r1 - std::exp(-1.5)) < 1e-6);
    // t = T：exp(-3) ≈ 0.0498 → 约剩 5%
    double dxT = 0, dyT = 0;
    shakeOffsetAt(p, 1.0, 100, 100, dxT, dyT);
    NC_CHECK(std::fabs(std::sqrt(dxT * dxT + dyT * dyT) / r1 - std::exp(-3.0)) < 1e-6);
    // 更久之后趋近 0
    double dx3 = 0, dy3 = 0;
    shakeOffsetAt(p, 3.0, 100, 100, dx3, dy3);
    NC_CHECK(std::sqrt(dx3 * dx3 + dy3 * dy3) < 0.01);
}

NC_TEST(Shake, ZeroIntensityIsNoOp) {
    const int w = 32, h = 32;
    std::vector<std::uint8_t> px = makeGradient(w, h);
    const std::vector<std::uint8_t> original = px;
    NC_CHECK(applyShakeToRgba(px.data(), w, h, baseParams(), 0.3, 0.0));
    NC_CHECK(px == original);
}

NC_TEST(Shake, ZeroAmplitudeIsNoOp) {
    const int w = 32, h = 32;
    std::vector<std::uint8_t> px = makeGradient(w, h);
    const std::vector<std::uint8_t> original = px;
    ShakeParams p = baseParams();
    p.amplitude = 0.0;
    NC_CHECK(applyShakeToRgba(px.data(), w, h, p, 0.3));
    NC_CHECK(px == original);
}

NC_TEST(Shake, AlphaIsPreserved) {
    // 震动只改位移，不应改变透明度分布
    const int w = 16, h = 16;
    std::vector<std::uint8_t> px = makeGradient(w, h);
    // 造一块半透明区
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (x < 8) px[(static_cast<std::size_t>(y) * w + x) * 4 + 3] = 100;
        }
    }
    std::vector<std::uint8_t> beforeAlpha;
    for (std::size_t i = 3; i < px.size(); i += 4) beforeAlpha.push_back(px[i]);

    NC_CHECK(applyShakeToRgba(px.data(), w, h, baseParams(), 0.2));
    std::vector<std::uint8_t> afterAlpha;
    for (std::size_t i = 3; i < px.size(); i += 4) afterAlpha.push_back(px[i]);
    NC_CHECK(beforeAlpha == afterAlpha);
}

NC_TEST(Shake, InvalidParamsRejected) {
    const int w = 8, h = 8;
    std::vector<std::uint8_t> px = makeGradient(w, h);
    const std::vector<std::uint8_t> original = px;

    ShakeParams p = baseParams();
    p.amplitude = 0.5;  // > 0.2 上限
    NC_CHECK(!shakeParamsValid(p));
    NC_CHECK(!applyShakeToRgba(px.data(), w, h, p, 0.1));
    NC_CHECK(px == original);

    ShakeParams p2 = baseParams();
    p2.frequency = 100.0;
    NC_CHECK(!shakeParamsValid(p2));

    ShakeParams p3 = baseParams();
    p3.decaySeconds = -1.0;
    NC_CHECK(!shakeParamsValid(p3));

    // 缓冲区非法
    NC_CHECK(!applyShakeToRgba(nullptr, w, h, baseParams(), 0.1));
    NC_CHECK(!applyShakeToRgba(px.data(), 0, h, baseParams(), 0.1));
    NC_CHECK(!applyShakeToRgba(px.data(), w, -1, baseParams(), 0.1));
}

NC_TEST(Shake, NoBlackBordersFromMirrorPadding) {
    // 位移后四角不应出现纯黑（镜像填充的正确性）
    const int w = 40, h = 40;
    std::vector<std::uint8_t> px = makeGradient(w, h);
    ShakeParams p = baseParams();
    p.amplitude = 0.2;
    p.frequency = 3.0;
    NC_CHECK(applyShakeToRgba(px.data(), w, h, p, 0.13));
    const std::size_t corners[4] = {0, (static_cast<std::size_t>(w) - 1) * 4,
                                    (static_cast<std::size_t>(h) - 1) * w * 4,
                                    (static_cast<std::size_t>(w * h) - 1) * 4};
    for (std::size_t idx : corners) {
        // 纯黑 = 三个通道全 0
        const bool allBlack = px[idx] == 0 && px[idx + 1] == 0 && px[idx + 2] == 0;
        NC_CHECK(!allBlack);
    }
}
