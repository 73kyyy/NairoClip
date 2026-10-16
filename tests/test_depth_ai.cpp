// =============================================================================
// 深度图 AI 测试（文档 Part 4-C 13.2：真实推理 + 4 种应用效果）
// =============================================================================
// 说明：
//   · 推理类用例需 ONNX Runtime + 模型文件（/workspace/deps/depth_anything_v2_small.onnx）；
//     两者缺一自动跳过（CI 无模型时不失败）；
//   · 4 种效果为纯像素运算，始终执行（合成深度图驱动）。
#include "test_framework.h"
#include "nairoclip/core/depth_ai.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

bool depthModelAvailable() {
    if (!aiEngineAvailable()) {
        return false;
    }
    FILE* f = std::fopen("/workspace/deps/depth_anything_v2_small.onnx", "rb");
    if (f == nullptr) {
        return false;
    }
    std::fclose(f);
    return true;
}

/// 合成测试帧：左半暗（近）+ 右半亮（远）的渐变（供效果验证）。
std::vector<std::uint8_t> makeTestFrame(int w, int h) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = pixels.data() +
                               (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
            const std::uint8_t v = static_cast<std::uint8_t>(x * 255 / std::max(1, w - 1));
            px[0] = v;
            px[1] = static_cast<std::uint8_t>(128);
            px[2] = static_cast<std::uint8_t>(255 - v / 2);
        }
    }
    return pixels;
}

/// 合成深度图：左半近（255）+ 右半远（0），带过渡带。
std::vector<std::uint8_t> makeTestDepth(int w, int h) {
    std::vector<std::uint8_t> depth(static_cast<std::size_t>(w) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            // 从左到右：近 → 远
            depth[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(255 - x * 255 / std::max(1, w - 1));
        }
    }
    return depth;
}

double meanAbsDiff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i += 4) {
        sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 真实推理（需模型 + ORT；缺失自动跳过）
// ---------------------------------------------------------------------------

NC_TEST(DepthAI, RealInferenceProducesDepth) {
    if (!depthModelAvailable()) return;
    DepthEstimator estimator;
    std::string failure;
    NC_CHECK(estimator.load("/workspace/deps/depth_anything_v2_small.onnx", &failure));
    NC_CHECK(estimator.loaded());
    // 合成帧（左暗右亮 → 深度应有空间分布）
    const int W = 160;
    const int H = 120;
    const std::vector<std::uint8_t> frame = makeTestFrame(W, H);
    std::vector<std::uint8_t> depth;
    NC_CHECK(estimator.estimate(frame.data(), W, H, depth, &failure));
    NC_CHECK_EQ(depth.size(), std::size_t(W * H));
    // 深度图应有变化（非全同值）
    std::uint8_t mn = depth[0];
    std::uint8_t mx = depth[0];
    for (std::uint8_t v : depth) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    NC_CHECK(mx > mn);  // 有空间分布（真实推理）
    // 左右区域应有差异（输入左暗右亮 → 深度不同）
    double leftSum = 0.0;
    double rightSum = 0.0;
    int count = 0;
    for (int y = H / 4; y < H * 3 / 4; ++y) {
        for (int x = W / 8; x < W * 3 / 8; ++x) {
            leftSum += depth[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)];
        }
        for (int x = W * 5 / 8; x < W * 7 / 8; ++x) {
            rightSum += depth[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)];
        }
        ++count;
    }
    const double leftMean = leftSum / count;
    const double rightMean = rightSum / count;
    NC_CHECK(std::abs(leftMean - rightMean) > 1.0);  // 存在真实空间差异
}

NC_TEST(DepthAI, InferenceFailurePaths) {
    if (!depthModelAvailable()) return;
    DepthEstimator estimator;
    std::string failure;
    // 未加载 → 失败（fail-closed）
    std::vector<std::uint8_t> depth;
    const std::vector<std::uint8_t> frame = makeTestFrame(32, 32);
    NC_CHECK(!estimator.estimate(frame.data(), 32, 32, depth, &failure));
    NC_CHECK(!failure.empty());
    // 加载不存在的模型 → 失败
    NC_CHECK(!estimator.load("/tmp/nonexistent_model_xyz.onnx", &failure));
    NC_CHECK(!failure.empty());
    // 正常加载后非法帧 → 失败
    NC_CHECK(estimator.load("/workspace/deps/depth_anything_v2_small.onnx", &failure));
    NC_CHECK(!estimator.estimate(nullptr, 32, 32, depth, &failure));
    NC_CHECK(!estimator.estimate(frame.data(), 0, 32, depth, &failure));
    // 卸载后可重新加载
    estimator.unload();
    NC_CHECK(!estimator.loaded());
    NC_CHECK(estimator.load("/workspace/deps/depth_anything_v2_small.onnx", &failure));
}

// ---------------------------------------------------------------------------
// 2. 4 种应用效果（纯像素运算；合成深度图驱动）
// ---------------------------------------------------------------------------

NC_TEST(DepthAI, BokehEffect) {
    const int W = 64;
    const int H = 48;
    // 用高频棋盘图（模糊对线性渐变无效果——数学必然，必须用高频内容验证）
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(W) * H * 4, 255);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::uint8_t* px = frame.data() +
                               (static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)) * 4;
            const std::uint8_t v = ((x / 4 + y / 4) % 2) ? 240 : 20;
            px[0] = v;
            px[1] = v;
            px[2] = v;
        }
    }
    const std::vector<std::uint8_t> depth = makeTestDepth(W, H);
    std::vector<std::uint8_t> pixels = frame;
    BokehParams params;
    params.focusDistance = 1.0;  // 焦点在最近（左侧）
    params.aperture = 2.8;
    params.intensity = 100.0;
    NC_CHECK(applyBokehEffect(pixels.data(), W, H, depth, params));
    // 远处（右侧）应用模糊 → 与原始差异显著
    const double diff = meanAbsDiff(pixels, frame);
    NC_CHECK(diff > 5.0);
    // 焦点区（左侧）应基本清晰（差异远小于远处）
    double leftDiff = 0.0;
    double rightDiff = 0.0;
    int leftCount = 0;
    int rightCount = 0;
    for (int y = H / 4; y < H * 3 / 4; ++y) {
        for (int x = 0; x < W / 8; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x);
            leftDiff += std::abs(static_cast<int>(pixels[idx * 4]) -
                                 static_cast<int>(frame[idx * 4]));
            ++leftCount;
        }
        for (int x = W * 5 / 8; x < W * 7 / 8; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x);
            rightDiff += std::abs(static_cast<int>(pixels[idx * 4]) -
                                  static_cast<int>(frame[idx * 4]));
            ++rightCount;
        }
    }
    NC_CHECK(rightDiff / rightCount > leftDiff / leftCount * 3.0);  // 深度驱动差异化虚化
    // 零强度 → 零改动
    std::vector<std::uint8_t> untouched = frame;
    BokehParams zero;
    zero.intensity = 0.0;
    NC_CHECK(applyBokehEffect(untouched.data(), W, H, depth, zero));
    NC_CHECK_EQ(meanAbsDiff(untouched, frame), 0.0);
    // 深度图缺失 → 失败
    std::vector<std::uint8_t> bad = frame;
    NC_CHECK(!applyBokehEffect(bad.data(), W, H, {}, params));
}

NC_TEST(DepthAI, FogEffect) {
    const int W = 64;
    const int H = 48;
    const std::vector<std::uint8_t> frame = makeTestFrame(W, H);
    const std::vector<std::uint8_t> depth = makeTestDepth(W, H);
    std::vector<std::uint8_t> pixels = frame;
    FogParams params;
    params.intensity = 80.0;
    params.colorRgba = 0xFFFFFFF0;  // 白雾
    params.startDistance = 0.3;
    NC_CHECK(applyFogEffect(pixels.data(), W, H, depth, params));
    // 远处（右侧）变白 → 整体变亮
    double sumBefore = 0.0;
    double sumAfter = 0.0;
    for (std::size_t i = 0; i < frame.size(); i += 4) {
        sumBefore += frame[i];
        sumAfter += pixels[i];
    }
    NC_CHECK(sumAfter > sumBefore);  // 白雾叠加使画面变亮
    // 零强度 → 零改动
    std::vector<std::uint8_t> untouched = frame;
    FogParams zero;
    zero.intensity = 0.0;
    NC_CHECK(applyFogEffect(untouched.data(), W, H, depth, zero));
    NC_CHECK_EQ(meanAbsDiff(untouched, frame), 0.0);
}

NC_TEST(DepthAI, ColorBandsEffect) {
    const int W = 64;
    const int H = 48;
    const std::vector<std::uint8_t> frame = makeTestFrame(W, H);
    const std::vector<std::uint8_t> depth = makeTestDepth(W, H);
    std::vector<std::uint8_t> pixels = frame;
    ColorBandsParams params;
    params.smoothness = 100.0;  // 平滑过渡
    NC_CHECK(applyColorBandsEffect(pixels.data(), W, H, depth, params));
    NC_CHECK(meanAbsDiff(pixels, frame) > 2.0);  // 色调被改变
    // 硬边界模式（柔和度 0）
    std::vector<std::uint8_t> hard = frame;
    params.smoothness = 0.0;
    NC_CHECK(applyColorBandsEffect(hard.data(), W, H, depth, params));
    NC_CHECK(meanAbsDiff(hard, frame) > 2.0);
    // 两种模式结果不同（柔性差异）
    NC_CHECK(meanAbsDiff(hard, pixels) > 0.5);
}

NC_TEST(DepthAI, ParallaxEffect) {
    const int W = 64;
    const int H = 48;
    const std::vector<std::uint8_t> frame = makeTestFrame(W, H);
    const std::vector<std::uint8_t> depth = makeTestDepth(W, H);
    // 静态轨迹
    std::vector<std::uint8_t> staticPixels = frame;
    ParallaxParams params;
    params.intensity = 80.0;
    params.trajectory = 0;
    NC_CHECK(applyParallaxEffect(staticPixels.data(), W, H, depth, params, 0.0));
    NC_CHECK(meanAbsDiff(staticPixels, frame) > 0.5);
    // 摇摆轨迹：不同时刻结果不同（真实动画）
    std::vector<std::uint8_t> t0 = frame;
    std::vector<std::uint8_t> t1 = frame;
    params.trajectory = 1;
    NC_CHECK(applyParallaxEffect(t0.data(), W, H, depth, params, 0.0));
    NC_CHECK(applyParallaxEffect(t1.data(), W, H, depth, params, 0.5));
    NC_CHECK(meanAbsDiff(t0, t1) > 0.1);  // 时间驱动位移
    // 环绕轨迹
    std::vector<std::uint8_t> orbit = frame;
    params.trajectory = 2;
    NC_CHECK(applyParallaxEffect(orbit.data(), W, H, depth, params, 0.3));
    NC_CHECK(meanAbsDiff(orbit, frame) > 0.5);
    // 确定性：同参数两次 → 相同结果
    std::vector<std::uint8_t> repeat = frame;
    params.trajectory = 1;
    NC_CHECK(applyParallaxEffect(repeat.data(), W, H, depth, params, 0.5));
    NC_CHECK_EQ(meanAbsDiff(repeat, t1), 0.0);
    // 零强度 → 零改动
    std::vector<std::uint8_t> untouched = frame;
    ParallaxParams zero;
    zero.intensity = 0.0;
    NC_CHECK(applyParallaxEffect(untouched.data(), W, H, depth, zero, 0.0));
    NC_CHECK_EQ(meanAbsDiff(untouched, frame), 0.0);
}

NC_TEST(DepthAI, AllEffectsValidateInputs) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(32) * 32 * 4, 128);
    const std::vector<std::uint8_t> depth(static_cast<std::size_t>(32) * 32, 128);
    // 空指针 / 非法尺寸 / 深度尺寸不匹配 → false
    NC_CHECK(!applyBokehEffect(nullptr, 32, 32, depth, BokehParams{}));
    NC_CHECK(!applyBokehEffect(pixels.data(), 0, 32, depth, BokehParams{}));
    NC_CHECK(!applyBokehEffect(pixels.data(), 32, 32, {}, BokehParams{}));
    NC_CHECK(!applyFogEffect(nullptr, 32, 32, depth, FogParams{}));
    NC_CHECK(!applyFogEffect(pixels.data(), 32, 32, {}, FogParams{}));
    NC_CHECK(!applyColorBandsEffect(nullptr, 32, 32, depth, ColorBandsParams{}));
    NC_CHECK(!applyColorBandsEffect(pixels.data(), 32, 32, {}, ColorBandsParams{}));
    NC_CHECK(!applyParallaxEffect(nullptr, 32, 32, depth, ParallaxParams{}, 0.0));
    NC_CHECK(!applyParallaxEffect(pixels.data(), 32, 32, {}, ParallaxParams{}, 0.0));
}
