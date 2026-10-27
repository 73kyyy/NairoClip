#include "test_framework.h"
#include "nairoclip/core/light_effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr int kW = 32;
constexpr int kH = 32;

std::vector<std::uint8_t> makeImage(int w, int h, std::uint8_t r, std::uint8_t g,
                                    std::uint8_t b) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i + 0] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

/// 在纯黑底上放一个白色方块（模拟高光）。
std::vector<std::uint8_t> makeSpotImage(int w, int h, int cx, int cy, int radius) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        px[i + 3] = 255;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int dx = x - cx;
            const int dy = y - cy;
            if (dx * dx + dy * dy <= radius * radius) {
                const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                px[i + 0] = 255;
                px[i + 1] = 255;
                px[i + 2] = 255;
            }
        }
    }
    return px;
}

int channelAt(const std::vector<std::uint8_t>& px, int w, int x, int y, int c) {
    return px[(static_cast<std::size_t>(y) * w + x) * 4 + static_cast<std::size_t>(c)];
}

}  // namespace

// =============================================================================
// Bloom（B1-3）
// =============================================================================

NC_TEST(Bloom, DarkImageIsUnchanged) {
    // 没有高光 → Bloom 无事可做，必须零改动。
    // （如果实现漏掉"高光提取"，暗部会被模糊提亮，这条就会红。）
    std::vector<std::uint8_t> px = makeImage(kW, kH, 20, 20, 20);
    const std::vector<std::uint8_t> original = px;
    BloomParams p;
    NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);
}

NC_TEST(Bloom, HighlightSpreadsToNeighbors) {
    // 亮部溢出断言（文档明确要求）：高光周围的暗像素必须被提亮。
    std::vector<std::uint8_t> px = makeSpotImage(kW, kH, 16, 16, 3);
    const int before = channelAt(px, kW, 16, 23, 0);  // 光斑正下方 7 像素处
    NC_CHECK_EQ(before, 0);                           // 原本是纯黑

    BloomParams p;
    p.threshold = 0.8;
    p.radiusPx = 8.0;
    p.intensity = 0.8;
    NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p));

    const int after = channelAt(px, kW, 16, 23, 0);
    if (after <= before) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "高光未扩散到邻近暗区: before=" + std::to_string(before) +
                " after=" + std::to_string(after));
    }
}

NC_TEST(Bloom, MidToneRegionDoesNotGlow) {
    // 这条专门区分「是否真的做了高光提取」：
    // 图里有一块亮白（该发光）和一片中灰（不该发光，但会因模糊而扩散）。
    // 若实现偷懒跳过高光提取、直接模糊整帧，中灰区会把亮度糊到邻近暗区，
    // 那里就会出现不该有的提亮。
    //
    // 为什么之前的 DarkImageIsUnchanged 拦不住这个错误：
    // 纯暗图 (20,20,20) 的 luma≈0.078，即便参与发光，
    // (0.078-0.8)/0.2 为负、被 clamp 到 0，结果仍是零改动。
    // 必须用「中灰」才暴露问题。
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kW) * kH * 4, 0);
    for (std::size_t i = 3; i < px.size(); i += 4) px[i] = 255;
    // 左半边中灰 (128)，右半边纯黑；顶部放一小块纯白高光
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW / 2; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4;
            px[i + 0] = 128;
            px[i + 1] = 128;
            px[i + 2] = 128;
        }
    }
    for (int y = 2; y < 6; ++y) {
        for (int x = 2; x < 6; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4;
            px[i + 0] = 255;
            px[i + 1] = 255;
            px[i + 2] = 255;
        }
    }
    // 取样点：中灰区与黑区交界处偏黑侧（x = kW/2 + 2），y 离高光很远
    const int probeX = kW / 2 + 2;
    const int probeY = 24;
    const int before = channelAt(px, kW, probeX, probeY, 0);
    NC_CHECK_EQ(before, 0);

    BloomParams p;
    p.threshold = 0.8;
    p.radiusPx = 8.0;
    p.intensity = 1.0;
    NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p));

    const int after = channelAt(px, kW, probeX, probeY, 0);
    if (after > 2) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "中灰区参与了发光（说明未做高光提取）: before=" +
                std::to_string(before) + " after=" + std::to_string(after));
    }
}

NC_TEST(Bloom, HighlightItselfGetsBrighterOrSaturates) {
    // 高光中心本身应更亮（或已饱和在 255）
    std::vector<std::uint8_t> px = makeSpotImage(kW, kH, 16, 16, 3);
    // 把光斑核心降到 200，留出提亮空间
    for (int y = 14; y <= 18; ++y) {
        for (int x = 14; x <= 18; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4;
            px[i + 0] = 200;
            px[i + 1] = 200;
            px[i + 2] = 200;
        }
    }
    const int before = channelAt(px, kW, 16, 16, 0);
    BloomParams p;
    p.threshold = 0.5;
    p.radiusPx = 4.0;
    p.intensity = 1.0;
    NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p));
    NC_CHECK(channelAt(px, kW, 16, 16, 0) >= before);
}

NC_TEST(Bloom, HigherThresholdMeansLessSpread) {
    // 阈值应真正起作用：阈值越高，参与发光的高光越少 → 扩散越弱
    auto run = [](double threshold) {
        std::vector<std::uint8_t> px = makeSpotImage(kW, kH, 16, 16, 3);
        BloomParams p;
        p.threshold = threshold;
        p.radiusPx = 6.0;
        p.intensity = 1.0;
        NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p));
        return channelAt(px, kW, 16, 22, 0);
    };
    const int low = run(0.3);
    const int high = run(0.95);
    NC_CHECK(low >= high);
}

NC_TEST(Bloom, ZeroIntensityOrRadiusIsNoOp) {
    std::vector<std::uint8_t> px = makeSpotImage(kW, kH, 16, 16, 3);
    const std::vector<std::uint8_t> original = px;

    BloomParams p;
    p.intensity = 0.0;
    NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);

    BloomParams p2;
    p2.radiusPx = 0.0;
    NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p2));
    NC_CHECK(px == original);
}

NC_TEST(Bloom, InvalidParamsRejected) {
    std::vector<std::uint8_t> px = makeSpotImage(kW, kH, 16, 16, 3);
    const std::vector<std::uint8_t> original = px;

    BloomParams p;
    p.threshold = 1.5;
    NC_CHECK(!bloomParamsValid(p));
    NC_CHECK(!applyBloomToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);

    BloomParams p2;
    p2.radiusPx = -1.0;
    NC_CHECK(!bloomParamsValid(p2));

    NC_CHECK(!applyBloomToRgba(nullptr, kW, kH, BloomParams{}));
    NC_CHECK(!applyBloomToRgba(px.data(), 0, kH, BloomParams{}));
}

NC_TEST(Bloom, AlphaPreserved) {
    std::vector<std::uint8_t> px = makeSpotImage(kW, kH, 16, 16, 3);
    std::vector<int> alpha;
    for (std::size_t i = 3; i < px.size(); i += 4) alpha.push_back(px[i]);
    BloomParams p;
    NC_CHECK(applyBloomToRgba(px.data(), kW, kH, p));
    std::vector<int> after;
    for (std::size_t i = 3; i < px.size(); i += 4) after.push_back(px[i]);
    NC_CHECK(alpha == after);
}

// =============================================================================
// 镜头光晕（B1-3）
// =============================================================================

NC_TEST(LensFlare, AddsLightAtSource) {
    std::vector<std::uint8_t> px = makeImage(kW, kH, 0, 0, 0);
    const int before = channelAt(px, kW, 22, 10, 0);

    LensFlareParams p;
    p.sourceX = 22.0 / kW;
    p.sourceY = 10.0 / kH;
    p.size = 0.3;
    p.intensity = 1.0;
    p.temperature = 0.0;
    p.ghostCount = 0;
    NC_CHECK(applyLensFlareToRgba(px.data(), kW, kH, p));

    const int after = channelAt(px, kW, 22, 10, 0);
    if (after <= before) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                         "光源位置没有变亮: " + std::to_string(after));
    }
}

NC_TEST(LensFlare, WarmTintFavorsRed) {
    LensFlareParams p;
    p.sourceX = 0.5;
    p.sourceY = 0.5;
    p.size = 0.4;
    p.intensity = 1.0;
    p.temperature = 0.9;  // 暖
    p.ghostCount = 0;

    std::vector<std::uint8_t> px = makeImage(kW, kH, 0, 0, 0);
    NC_CHECK(applyLensFlareToRgba(px.data(), kW, kH, p));
    const int r = channelAt(px, kW, 16, 16, 0);
    const int b = channelAt(px, kW, 16, 16, 2);
    NC_CHECK(r > b);
}

NC_TEST(LensFlare, CoolTintFavorsBlue) {
    LensFlareParams p;
    p.sourceX = 0.5;
    p.sourceY = 0.5;
    p.size = 0.4;
    p.intensity = 1.0;
    p.temperature = -0.9;  // 冷
    p.ghostCount = 0;

    std::vector<std::uint8_t> px = makeImage(kW, kH, 0, 0, 0);
    NC_CHECK(applyLensFlareToRgba(px.data(), kW, kH, p));
    const int r = channelAt(px, kW, 16, 16, 0);
    const int b = channelAt(px, kW, 16, 16, 2);
    NC_CHECK(b > r);
}

NC_TEST(LensFlare, GhostsAppearAlongAxis) {
    // 鬼影应出现在「光源 → 中心」的延长线上，而不是随机位置
    LensFlareParams p;
    p.sourceX = 0.8;  // 光源在右侧
    p.sourceY = 0.5;
    p.size = 0.1;
    p.intensity = 1.0;
    p.temperature = 0.0;
    p.ghostCount = 3;

    std::vector<std::uint8_t> px = makeImage(kW, kH, 0, 0, 0);
    NC_CHECK(applyLensFlareToRgba(px.data(), kW, kH, p));

    // 光源在 0.8*32≈25.6，鬼影沿「光源→中心」延长线落在中心左侧。
    // 第一个鬼影：16 + (16-25.6)*1.35 ≈ 3.0；第二个 ≈ 16 - 14.9*2 ≈ -14（出画面）
    // 因此取 x=3 附近作为检测点（用 3 而不是 6，后者已超出第一个鬼影半径）。
    int leftSide = 0;
    for (int x = 0; x <= 6; ++x) {
        leftSide = std::max(leftSide, channelAt(px, kW, x, 16, 0));
    }
    const int rightFar = channelAt(px, kW, 30, 16, 0);
    if (leftSide <= 0) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "鬼影未出现在轴线延长线（左侧）: " + std::to_string(leftSide) +
                " 右侧远端: " + std::to_string(rightFar));
    }
}

NC_TEST(LensFlare, ZeroIntensityIsNoOp) {
    std::vector<std::uint8_t> px = makeImage(kW, kH, 50, 60, 70);
    const std::vector<std::uint8_t> original = px;
    LensFlareParams p;
    p.intensity = 0.0;
    NC_CHECK(applyLensFlareToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);
}

NC_TEST(LensFlare, InvalidParamsRejected) {
    std::vector<std::uint8_t> px = makeImage(kW, kH, 0, 0, 0);
    const std::vector<std::uint8_t> original = px;

    LensFlareParams p;
    p.sourceX = 1.5;
    NC_CHECK(!lensFlareParamsValid(p));
    NC_CHECK(!applyLensFlareToRgba(px.data(), kW, kH, p));
    NC_CHECK(px == original);

    LensFlareParams p2;
    p2.ghostCount = 9;
    NC_CHECK(!lensFlareParamsValid(p2));

    LensFlareParams p3;
    p3.temperature = 3.0;
    NC_CHECK(!lensFlareParamsValid(p3));
}

NC_TEST(LensFlare, BrightnessDoesNotOverflow) {
    // 亮图 + 强光晕：结果必须仍是合法 8bit（不溢出/不环绕）
    std::vector<std::uint8_t> px = makeImage(kW, kH, 250, 250, 250);
    LensFlareParams p;
    p.intensity = 1.0;
    p.size = 1.0;
    p.ghostCount = 6;
    NC_CHECK(applyLensFlareToRgba(px.data(), kW, kH, p));
    for (std::size_t i = 0; i < px.size(); i += 4) {
        // 所有通道都应 >= 250（加法混合只会变亮）且 <= 255
        NC_CHECK(px[i + 0] >= 250);
        NC_CHECK(px[i + 1] >= 250);
        NC_CHECK(px[i + 2] >= 250);
    }
}

NC_TEST(LensFlare, Deterministic) {
    // 同一输入两次调用结果必须完全一致（无未受控随机）
    LensFlareParams p;
    p.sourceX = 0.3;
    p.sourceY = 0.7;
    p.temperature = -0.4;
    std::vector<std::uint8_t> a = makeImage(kW, kH, 10, 20, 30);
    std::vector<std::uint8_t> b = a;
    NC_CHECK(applyLensFlareToRgba(a.data(), kW, kH, p));
    NC_CHECK(applyLensFlareToRgba(b.data(), kW, kH, p));
    NC_CHECK(a == b);
}
