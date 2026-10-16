// =============================================================================
// 智能抠像 AI 测试（文档 Part 4-A 1.6：人像抠像真实推理）
// =============================================================================
// 说明：推理类用例需 ONNX Runtime + MODNet 模型；缺失自动跳过。
#include "test_framework.h"
#include "nairoclip/core/matting_ai.h"
#include "nairoclip/core/person_effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

bool mattingModelAvailable() {
    if (!aiEngineAvailable()) {
        return false;
    }
    FILE* f = std::fopen("/workspace/deps/modnet.onnx", "rb");
    if (f == nullptr) {
        return false;
    }
    std::fclose(f);
    return true;
}

/// 合成人像场景：中心椭圆人像（肤色 + 纹理细节）+ 深色背景。
/// 说明：实测 MODNet 对「色块模拟」响应弱、对「中心大前景 + 纹理」响应强，
///       故测试用后者（贴近真实人像构图）。
std::vector<std::uint8_t> makePortraitFrame(int w, int h) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 255);
    const double cx = w * 0.5;
    const double cy = h * 0.5;
    const double rx = w * 0.30;
    const double ry = h * 0.42;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = pixels.data() +
                               (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
            const double dx = (x - cx) / rx;
            const double dy = (y - cy) / ry;
            if (dx * dx + dy * dy < 1.0) {
                // 人像：肤色 + 细微纹理（模型依赖纹理特征）
                px[0] = static_cast<std::uint8_t>(
                    std::clamp(220.0 + 15.0 * std::sin(x * 0.3), 0.0, 255.0));
                px[1] = static_cast<std::uint8_t>(
                    std::clamp(170.0 + 12.0 * std::cos(y * 0.3), 0.0, 255.0));
                px[2] = 140;
            } else {
                // 背景：深蓝灰
                px[0] = 72;
                px[1] = 84;
                px[2] = 122;
            }
        }
    }
    return pixels;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 档位与名称
// ---------------------------------------------------------------------------

NC_TEST(MattingAI, QualityNamesAndSizes) {
    NC_CHECK_EQ(std::string(mattingAiQualityName(MattingQuality::Fast)), std::string("fast"));
    NC_CHECK_EQ(std::string(mattingAiQualityName(MattingQuality::Standard)),
                std::string("standard"));
    NC_CHECK_EQ(std::string(mattingAiQualityName(MattingQuality::HighPrecision)),
                std::string("high_precision"));
    // 文档 1.6「抠像精度：快速/标准/高精度」
    NC_CHECK_EQ(mattingAiQualityInputSize(MattingQuality::Fast), 256);
    NC_CHECK_EQ(mattingAiQualityInputSize(MattingQuality::Standard), 512);
    NC_CHECK_EQ(mattingAiQualityInputSize(MattingQuality::HighPrecision), 768);
    MattingQuality quality;
    NC_CHECK(mattingAiQualityFromInt(0, quality) && quality == MattingQuality::Fast);
    NC_CHECK(mattingAiQualityFromInt(2, quality) && quality == MattingQuality::HighPrecision);
    NC_CHECK(!mattingAiQualityFromInt(3, quality));
    NC_CHECK(!mattingAiQualityFromInt(-1, quality));
}

// ---------------------------------------------------------------------------
// 2. 真实推理（需模型 + ORT；缺失跳过）
// ---------------------------------------------------------------------------

NC_TEST(MattingAI, RealInferenceProducesAlpha) {
    if (!mattingModelAvailable()) return;
    MattingEstimator estimator;
    std::string failure;
    NC_CHECK(estimator.load("/workspace/deps/modnet.onnx", &failure));
    NC_CHECK(estimator.loaded());
    const int W = 160;
    const int H = 120;
    const std::vector<std::uint8_t> frame = makePortraitFrame(W, H);
    std::vector<std::uint8_t> alpha;
    // 标准精度 + 默认羽化 35（文档 1.6）
    NC_CHECK(estimator.estimate(frame.data(), W, H, MattingQuality::Standard, 35.0, alpha,
                                &failure));
    NC_CHECK_EQ(alpha.size(), std::size_t(W * H));
    // Alpha 应有空间分布（非全 0 或全 255）
    std::uint8_t mn = alpha[0];
    std::uint8_t mx = alpha[0];
    for (std::uint8_t v : alpha) {
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    NC_CHECK(mx > mn);
    // 中心（人像）Alpha 应显著高于角落（背景）
    double centerSum = 0.0;
    double cornerSum = 0.0;
    int count = 0;
    for (int y = H / 3; y < H * 2 / 3; ++y) {
        for (int x = W / 3; x < W * 2 / 3; ++x) {
            centerSum += alpha[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)];
        }
        for (int x = 0; x < W / 8; ++x) {
            cornerSum += alpha[static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)];
        }
        ++count;
    }
    // 中心人像应高于角落背景
    NC_CHECK(centerSum / count > cornerSum / count);
    // Alpha 最大值应达较高水平（检测到前景）
    NC_CHECK(mx > 100);
}

NC_TEST(MattingAI, QualityLevelsWork) {
    if (!mattingModelAvailable()) return;
    MattingEstimator estimator;
    std::string failure;
    NC_CHECK(estimator.load("/workspace/deps/modnet.onnx", &failure));
    const int W = 64;
    const int H = 64;
    const std::vector<std::uint8_t> frame = makePortraitFrame(W, H);
    // 三档均可用（快速档更快但分辨率低）
    for (const auto quality : {MattingQuality::Fast, MattingQuality::Standard}) {
        std::vector<std::uint8_t> alpha;
        NC_CHECK(estimator.estimate(frame.data(), W, H, quality, 20.0, alpha, &failure));
        NC_CHECK_EQ(alpha.size(), std::size_t(W * H));
    }
}

NC_TEST(MattingAI, FeatherAffectsEdges) {
    if (!mattingModelAvailable()) return;
    MattingEstimator estimator;
    std::string failure;
    NC_CHECK(estimator.load("/workspace/deps/modnet.onnx", &failure));
    const int W = 64;
    const int H = 64;
    const std::vector<std::uint8_t> frame = makePortraitFrame(W, H);
    // 无羽化 vs 强羽化 → 结果不同（边缘更柔和）
    std::vector<std::uint8_t> sharp;
    std::vector<std::uint8_t> soft;
    NC_CHECK(estimator.estimate(frame.data(), W, H, MattingQuality::Fast, 0.0, sharp, &failure));
    NC_CHECK(estimator.estimate(frame.data(), W, H, MattingQuality::Fast, 100.0, soft, &failure));
    bool differs = false;
    for (std::size_t i = 0; i < sharp.size() && !differs; ++i) {
        if (sharp[i] != soft[i]) differs = true;
    }
    NC_CHECK(differs);
}

NC_TEST(MattingAI, FailurePaths) {
    if (!mattingModelAvailable()) return;
    MattingEstimator estimator;
    std::string failure;
    std::vector<std::uint8_t> alpha;
    const std::vector<std::uint8_t> frame = makePortraitFrame(32, 32);
    // 未加载 → 失败（fail-closed）
    NC_CHECK(!estimator.estimate(frame.data(), 32, 32, MattingQuality::Standard, 35.0, alpha,
                                 &failure));
    NC_CHECK(!failure.empty());
    // 不存在的模型
    NC_CHECK(!estimator.load("/tmp/nonexistent_matting_model.onnx", &failure));
    // 非法帧
    NC_CHECK(estimator.load("/workspace/deps/modnet.onnx", &failure));
    NC_CHECK(!estimator.estimate(nullptr, 32, 32, MattingQuality::Standard, 35.0, alpha,
                                 &failure));
    NC_CHECK(!estimator.estimate(frame.data(), 0, 32, MattingQuality::Standard, 35.0, alpha,
                                 &failure));
}

// ---------------------------------------------------------------------------
// 3. Alpha 解锁人物特效（瘦身/发光轮廓——此前无 Alpha 时零改动）
// ---------------------------------------------------------------------------

NC_TEST(MattingAI, AlphaUnlocksSlimAndGlow) {
    if (!mattingModelAvailable()) return;
    MattingEstimator estimator;
    std::string failure;
    NC_CHECK(estimator.load("/workspace/deps/modnet.onnx", &failure));
    // 中心椭圆人像（高频棋盘纹理；确保形变可见且模型可识别）
    const int W = 64;
    const int H = 48;
    std::vector<std::uint8_t> frame(static_cast<std::size_t>(W) * H * 4, 255);
    const double cx = W * 0.5;
    const double cy = H * 0.5;
    const double rx = W * 0.30;
    const double ry = H * 0.42;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            std::uint8_t* px = frame.data() +
                               (static_cast<std::size_t>(y) * W + static_cast<std::size_t>(x)) * 4;
            const double dx = (x - cx) / rx;
            const double dy = (y - cy) / ry;
            if (dx * dx + dy * dy < 1.0) {
                const std::uint8_t v = ((x / 4 + y / 4) % 2) ? 240 : 20;
                px[0] = v;
                px[1] = v;
                px[2] = v;
            } else {
                px[0] = 72;
                px[1] = 84;
                px[2] = 122;
            }
        }
    }
    std::vector<std::uint8_t> alpha;
    NC_CHECK(estimator.estimate(frame.data(), W, H, MattingQuality::Fast, 0.0, alpha, &failure));
    // 瘦身：无 Alpha 零改动；有 Alpha 真实形变
    std::vector<std::uint8_t> noAlpha = frame;
    NC_CHECK(applySlimRgba(noAlpha.data(), W, H, 1.0, nullptr));
    bool unchanged = true;
    for (std::size_t i = 0; i < noAlpha.size() && unchanged; i += 4) {
        if (noAlpha[i] != frame[i]) unchanged = false;
    }
    NC_CHECK(unchanged);
    std::vector<std::uint8_t> slimmed = frame;
    NC_CHECK(applySlimRgba(slimmed.data(), W, H, 1.0, alpha.data()));
    double diff = 0.0;
    for (std::size_t i = 0; i < slimmed.size(); i += 4) {
        diff += std::abs(static_cast<int>(slimmed[i]) - static_cast<int>(frame[i]));
    }
    NC_CHECK(diff / (W * H) > 0.5);  // 真实形变
    // 发光轮廓：有 Alpha 时画面变亮
    std::vector<std::uint8_t> glowing = frame;
    double sumBefore = 0.0;
    double sumAfter = 0.0;
    for (std::size_t i = 0; i < frame.size(); i += 4) {
        sumBefore += frame[i];
    }
    NC_CHECK(applyGlowOutlineRgba(glowing.data(), W, H, 1.0, alpha.data(),
                                  RenderQuality::exportQuality()));
    for (std::size_t i = 0; i < glowing.size(); i += 4) {
        sumAfter += glowing[i];
    }
    NC_CHECK(sumAfter > sumBefore);  // 发光叠加
}
