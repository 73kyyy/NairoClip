#include "test_framework.h"
#include "nairoclip/core/motion_blur_directional.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr int kW = 48;
constexpr int kH = 48;

/// 竖条纹图（便于观察水平模糊）；条纹沿 x 方向周期变化。
std::vector<std::uint8_t> makeVerticalStripes(int w, int h, int period) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            const std::uint8_t v = ((x / period) % 2 == 0) ? 255 : 0;
            px[i + 0] = v;
            px[i + 1] = v;
            px[i + 2] = v;
            px[i + 3] = 255;
        }
    }
    return px;
}

/// 水平条纹图（便于观察垂直模糊）。
std::vector<std::uint8_t> makeHorizontalStripes(int w, int h, int period) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            const std::uint8_t v = ((y / period) % 2 == 0) ? 255 : 0;
            px[i + 0] = v;
            px[i + 1] = v;
            px[i + 2] = v;
            px[i + 3] = 255;
        }
    }
    return px;
}

/// 沿某一行的通道值方差（模糊会降低方差）。
double rowVariance(const std::vector<std::uint8_t>& px, int w, int y) {
    double sum = 0.0;
    double sumSq = 0.0;
    for (int x = 0; x < w; ++x) {
        const double v = px[(static_cast<std::size_t>(y) * w + x) * 4];
        sum += v;
        sumSq += v * v;
    }
    const double n = static_cast<double>(w);
    return sumSq / n - (sum / n) * (sum / n);
}

}  // namespace

// =============================================================================
// 方向性运动模糊（B1-4）
// =============================================================================
//
// 验收要点（重构动机）：
//   1. 单帧即可产生模糊（不依赖相邻帧）；
//   2. **方向正确**——水平运动会沿水平方向糊，垂直不会；
//   3. 静止时零改动；快门角度 0 时零改动。

NC_TEST(DirectionalBlur, ZeroVelocityIsNoOp) {
    std::vector<std::uint8_t> px = makeVerticalStripes(kW, kH, 4);
    const std::vector<std::uint8_t> original = px;
    DirectionalBlurParams p;
    p.velocityX = 0.0;
    p.velocityY = 0.0;
    NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);
}

NC_TEST(DirectionalBlur, ZeroShutterIsNoOp) {
    // 快门 0° = 无曝光时间 = 无模糊
    std::vector<std::uint8_t> px = makeVerticalStripes(kW, kH, 4);
    const std::vector<std::uint8_t> original = px;
    DirectionalBlurParams p;
    p.velocityX = 0.1;
    p.shutterAngleDegrees = 0.0;
    NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);
}

NC_TEST(DirectionalBlur, HorizontalMotionBlursHorizontally) {
    // 用竖条纹：水平模糊会把黑白条纹糊成灰 → 该行方差明显下降
    std::vector<std::uint8_t> px = makeVerticalStripes(kW, kH, 4);
    const double before = rowVariance(px, kW, kH / 2);
    NC_CHECK(before > 1000.0);  // 条纹应该方差很大

    DirectionalBlurParams p;
    p.velocityX = 0.12;  // 水平运动
    p.velocityY = 0.0;
    p.shutterAngleDegrees = 360.0;
    p.samples = 16;
    p.maxLength = 0.3;
    NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));

    const double after = rowVariance(px, kW, kH / 2);
    if (after >= before) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "水平运动未降低水平方差: before=" + std::to_string(before) +
                " after=" + std::to_string(after));
    }
}

NC_TEST(DirectionalBlur, HorizontalMotionDoesNotBlurVertically) {
    // 关键的方向性断言：水平运动不应该让「水平条纹」变糊
    // （若实现把方向搞错或各向同性处理，这里会红）
    std::vector<std::uint8_t> px = makeHorizontalStripes(kW, kH, 4);
    // 取某一列，看垂直方向的方差
    auto colVariance = [&]() {
        double sum = 0.0;
        double sumSq = 0.0;
        for (int y = 0; y < kH; ++y) {
            const double v = px[(static_cast<std::size_t>(y) * kW + kW / 2) * 4];
            sum += v;
            sumSq += v * v;
        }
        const double n = static_cast<double>(kH);
        return sumSq / n - (sum / n) * (sum / n);
    };
    const double before = colVariance();
    NC_CHECK(before > 1000.0);

    DirectionalBlurParams p;
    p.velocityX = 0.12;
    p.velocityY = 0.0;
    p.shutterAngleDegrees = 360.0;
    NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));

    const double after = colVariance();
    // 垂直方向方差应基本不变（允许采样带来的少量衰减，但不该大幅下降）
    if (after < before * 0.8) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "水平运动把垂直方向也糊了（方向错误）: before=" + std::to_string(before) +
                " after=" + std::to_string(after));
    }
}

NC_TEST(DirectionalBlur, VerticalMotionBlursVertically) {
    std::vector<std::uint8_t> px = makeHorizontalStripes(kW, kH, 4);
    auto colVariance = [&]() {
        double sum = 0.0;
        double sumSq = 0.0;
        for (int y = 0; y < kH; ++y) {
            const double v = px[(static_cast<std::size_t>(y) * kW + kW / 2) * 4];
            sum += v;
            sumSq += v * v;
        }
        const double n = static_cast<double>(kH);
        return sumSq / n - (sum / n) * (sum / n);
    };
    const double before = colVariance();
    NC_CHECK(before > 1000.0);

    DirectionalBlurParams p;
    p.velocityX = 0.0;
    p.velocityY = 0.12;
    p.shutterAngleDegrees = 360.0;
    NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));

    const double after = colVariance();
    if (after >= before) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "垂直运动未降低垂直方差: before=" + std::to_string(before) +
                " after=" + std::to_string(after));
    }
}

NC_TEST(DirectionalBlur, LongerShutterBlursMore) {
    // 快门角度越大，曝光时间越长，模糊越强
    auto varianceAfter = [](double angle) {
        std::vector<std::uint8_t> px = makeVerticalStripes(kW, kH, 4);
        DirectionalBlurParams p;
        p.velocityX = 0.1;
        p.shutterAngleDegrees = angle;
        NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));
        return rowVariance(px, kW, kH / 2);
    };
    const double shortShutter = varianceAfter(90.0);
    const double longShutter = varianceAfter(360.0);
    NC_CHECK(longShutter <= shortShutter);
}

NC_TEST(DirectionalBlur, MaxLengthClampsExtremeMotion) {
    // 极快运动时 maxLength 应限制模糊长度。
    //
    // 断言方式：同样荒谬的速度下，maxLength 越小 → 模糊越短 → 保留细节越多
    // （方差越大）。若实现忽略 maxLength，两者结果完全相同，差异为 0。
    //
    // 注：首版这条断言写的是「方差 < 1.0 才报警」，实测注入「删除 maxLength
    // 钳制」的变异后仍然通过——因为钳制失效时采样点被推到了画布边界，
    // 采样值互相混合后方差仍远大于 1.0。阈值型断言在极值处不可靠，
    // 改为「两种参数互相比」才能证伪。
    auto varianceWithMaxLength = [](double maxLength) {
        std::vector<std::uint8_t> px = makeVerticalStripes(kW, kH, 4);
        DirectionalBlurParams p;
        p.velocityX = 10.0;  // 荒谬的高速
        p.shutterAngleDegrees = 360.0;
        p.maxLength = maxLength;
        p.samples = 16;
        NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));
        return rowVariance(px, kW, kH / 2);
    };
    const double tight = varianceWithMaxLength(0.02);   // 很短
    const double loose = varianceWithMaxLength(0.5);    // 拉满
    if (!(tight > loose * 1.05)) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "maxLength 未影响模糊长度（tight=" + std::to_string(tight) +
                " loose=" + std::to_string(loose) + "）");
    }
}

NC_TEST(DirectionalBlur, AlphaPreserved) {
    std::vector<std::uint8_t> px = makeVerticalStripes(kW, kH, 4);
    for (std::size_t i = 3; i < px.size(); i += 4) {
        px[i] = static_cast<std::uint8_t>((i / 4) % 251);
    }
    std::vector<int> before;
    for (std::size_t i = 3; i < px.size(); i += 4) before.push_back(px[i]);

    DirectionalBlurParams p;
    p.velocityX = 0.1;
    NC_CHECK(applyDirectionalBlurToRgba(px.data(), kW, kH, p));

    std::vector<int> after;
    for (std::size_t i = 3; i < px.size(); i += 4) after.push_back(px[i]);
    NC_CHECK(before == after);
}

NC_TEST(DirectionalBlur, Deterministic) {
    DirectionalBlurParams p;
    p.velocityX = 0.08;
    p.velocityY = -0.05;
    p.shutterAngleDegrees = 200.0;
    std::vector<std::uint8_t> a = makeVerticalStripes(kW, kH, 4);
    std::vector<std::uint8_t> b = a;
    NC_CHECK(applyDirectionalBlurToRgba(a.data(), kW, kH, p));
    NC_CHECK(applyDirectionalBlurToRgba(b.data(), kW, kH, p));
    NC_CHECK(a == b);
}

NC_TEST(DirectionalBlur, InvalidParamsRejected) {
    std::vector<std::uint8_t> px = makeVerticalStripes(kW, kH, 4);
    const std::vector<std::uint8_t> original = px;

    DirectionalBlurParams p;
    p.shutterAngleDegrees = 400.0;  // 超过 360
    NC_CHECK(!directionalBlurParamsValid(p));
    NC_CHECK(!applyDirectionalBlurToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);

    DirectionalBlurParams p2;
    p2.samples = 1;  // 少于 2
    NC_CHECK(!directionalBlurParamsValid(p2));

    DirectionalBlurParams p3;
    p3.maxLength = 0.9;
    NC_CHECK(!directionalBlurParamsValid(p3));

    NC_CHECK(!applyDirectionalBlurToRgba(nullptr, kW, kH, DirectionalBlurParams{}));
    NC_CHECK(!applyDirectionalBlurToRgba(px.data(), 0, kH, DirectionalBlurParams{}));
}

NC_TEST(DirectionalBlur, VelocityFromPositionsConvertsUnits) {
    // 10 画幅/秒 × 1/30 秒 → 0.333 画幅/帧
    double vx = 0, vy = 0;
    NC_CHECK(velocityFromPositions(0.0, 0.0, 1.0, 0.0, 0.1, 1.0 / 30.0, vx, vy));
    NC_CHECK(std::fabs(vx - 10.0 / 30.0) < 1e-9);
    NC_CHECK(std::fabs(vy) < 1e-12);

    // dt <= 0 拒绝
    NC_CHECK(!velocityFromPositions(0.0, 0.0, 1.0, 0.0, 0.0, 1.0 / 30.0, vx, vy));
    // frameDuration <= 0 拒绝
    NC_CHECK(!velocityFromPositions(0.0, 0.0, 1.0, 0.0, 0.1, 0.0, vx, vy));
}

NC_TEST(DirectionalBlur, NegativeVelocityBlursOtherDirection) {
    // 负速度（反向运动）应产生同样的模糊量（模糊是偶函数于方向）
    std::vector<std::uint8_t> a = makeVerticalStripes(kW, kH, 4);
    std::vector<std::uint8_t> b = makeVerticalStripes(kW, kH, 4);
    DirectionalBlurParams p1;
    p1.velocityX = 0.1;
    DirectionalBlurParams p2;
    p2.velocityX = -0.1;
    NC_CHECK(applyDirectionalBlurToRgba(a.data(), kW, kH, p1));
    NC_CHECK(applyDirectionalBlurToRgba(b.data(), kW, kH, p2));
    // 在对称的条纹图上，正反方向模糊应得到接近的结果
    NC_CHECK(std::fabs(rowVariance(a, kW, kH / 2) - rowVariance(b, kW, kH / 2)) < 1.0);
}
