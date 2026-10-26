// =============================================================================
// 超分辨率 AI 测试（真实推理 + 分块/回退语义）
// =============================================================================
// 说明：
//   · 推理类用例需 ONNX Runtime + 模型文件（models/super_res_2x4x/model.onnx）；
//     缺任一自动跳过（CI 无模型时不失败）；
//   · 双线性兜底与分块参数推导为纯函数，始终执行。
#include "test_framework.h"
#include "nairoclip/core/super_res_ai.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>


using namespace nairoclip::core;

namespace {

/// 构造测试帧：左半亮右半暗，便于检验几何正确性。
std::vector<std::uint8_t> makeFrame(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            const std::uint8_t v = x < w / 2 ? 220 : 40;
            px[i] = px[i + 1] = px[i + 2] = v;
            px[i + 3] = 255;
        }
    }
    return px;
}

bool modelAvailable(const char* rel) {
    std::FILE* f = std::fopen(rel, "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// 纯函数：双线性放大
// ---------------------------------------------------------------------------

NC_TEST(SuperRes, BilinearDoublesSize) {
    const int w = 8, h = 6;
    const auto src = makeFrame(w, h);
    std::vector<std::uint8_t> out;
    upscaleBilinear(src.data(), w, h, 2, out);
    NC_CHECK_EQ(static_cast<int>(out.size()), w * 2 * h * 2 * 4);
}

NC_TEST(SuperRes, BilinearQuadruplesSize) {
    const int w = 5, h = 4;
    const auto src = makeFrame(w, h);
    std::vector<std::uint8_t> out;
    upscaleBilinear(src.data(), w, h, 4, out);
    NC_CHECK_EQ(static_cast<int>(out.size()), w * 4 * h * 4 * 4);
}

NC_TEST(SuperRes, BilinearPreservesColorRegions) {
    // 纯色区放大后中心仍应为该色（不产生越界混合）
    const int w = 16, h = 16;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t i = 0; i < src.size(); i += 4) {
        src[i] = 200; src[i + 1] = 100; src[i + 2] = 50; src[i + 3] = 255;
    }
    std::vector<std::uint8_t> out;
    upscaleBilinear(src.data(), w, h, 2, out);
    const int ow = w * 2;
    const std::size_t center = (static_cast<std::size_t>(h) * ow + w) * 4;
    NC_CHECK_EQ(static_cast<int>(out[center]), 200);
    NC_CHECK_EQ(static_cast<int>(out[center + 1]), 100);
    NC_CHECK_EQ(static_cast<int>(out[center + 2]), 50);
}

NC_TEST(SuperRes, BilinearRejectsBadInput) {
    std::vector<std::uint8_t> out;
    upscaleBilinear(nullptr, 8, 8, 2, out);
    NC_CHECK(out.empty());
    out.clear();
    const std::vector<std::uint8_t> dummy(64 * 4, 0);
    upscaleBilinear(dummy.data(), 0, 8, 2, out);
    NC_CHECK(out.empty());
    out.clear();
    upscaleBilinear(dummy.data(), 8, 8, 0, out);
    NC_CHECK(out.empty());
}

// ---------------------------------------------------------------------------
// 纯函数：分块尺寸推导
// ---------------------------------------------------------------------------

NC_TEST(SuperRes, SuggestedTileFromShape) {
    NC_CHECK_EQ(superResSuggestedTileSize({1, 3, 256, 256}), 256);
    NC_CHECK_EQ(superResSuggestedTileSize({1, 3, 128, 128}), 128);
    // 动态维度 → 回退默认
    NC_CHECK_EQ(superResSuggestedTileSize({1, 3, -1, -1}), 256);
    NC_CHECK_EQ(superResSuggestedTileSize({}), 256);
}

// ---------------------------------------------------------------------------
// 真实推理（需模型）
// ---------------------------------------------------------------------------

NC_TEST(SuperRes, RealInferenceProducesUpscaledFrame) {
    const char* model = "models/super_res_2x4x/model.onnx";
    if (!modelAvailable(model)) {
        std::printf("    [skip] 模型缺失：%s\n", model);
        return;
    }
    SuperResEstimator est;
    std::string err;
    if (!est.load(model, &err)) {
        std::printf("    [skip] 加载失败（引擎不可用）：%s\n", err.c_str());
        return;
    }
    NC_CHECK(est.loaded());

    const int w = 32, h = 24;
    const auto src = makeFrame(w, h);
    std::vector<std::uint8_t> out;
    SuperResStats stats;
    NC_CHECK(est.upscale(src.data(), w, h, SuperResScale::X2, SuperResTiling{}, out, &stats, &err));
    NC_CHECK_EQ(stats.outputWidth, w * 2);
    NC_CHECK_EQ(stats.outputHeight, h * 2);
    NC_CHECK_EQ(static_cast<int>(out.size()), w * 2 * h * 2 * 4);
    NC_CHECK(stats.tilesTotal > 0);
    NC_CHECK(stats.tilesFallback <= stats.tilesTotal);
    // 全不透明
    bool opaque = true;
    for (std::size_t i = 3; i < out.size(); i += 4) {
        if (out[i] != 255) { opaque = false; break; }
    }
    NC_CHECK(opaque);
}

NC_TEST(SuperRes, RejectsInvalidScaleAndUnloaded) {
    SuperResEstimator est;
    const auto src = makeFrame(8, 8);
    std::vector<std::uint8_t> out;
    std::string err;
    // 未加载
    NC_CHECK(!est.upscale(src.data(), 8, 8, SuperResScale::X2, SuperResTiling{}, out, nullptr, &err));
    NC_CHECK(!err.empty());
    // 非法倍数（用枚举外强转）
    NC_CHECK(!est.upscale(src.data(), 8, 8, static_cast<SuperResScale>(3), SuperResTiling{}, out,
                          nullptr, &err));
}
