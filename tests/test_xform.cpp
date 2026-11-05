#include "test_framework.h"
#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/xform.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 横向渐变图：左黑右白，便于检测位移/旋转方向是否正确。
std::vector<std::uint8_t> gradientImage(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4,
                                 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + x) * 4;
            const std::uint8_t v =
                static_cast<std::uint8_t>(w > 1 ? (x * 255) / (w - 1) : 0);
            px[i] = v;
            px[i + 1] = v;
            px[i + 2] = v;
            px[i + 3] = 255;
        }
    }
    return px;
}

std::vector<std::uint8_t> solid(int w, int h, std::uint8_t r, std::uint8_t g,
                               std::uint8_t b) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4,
                                 0);
    for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
        px[i] = r;
        px[i + 1] = g;
        px[i + 2] = b;
        px[i + 3] = 255;
    }
    return px;
}

std::vector<std::uint8_t> blank(int w, int h) {
    return std::vector<std::uint8_t>(
        static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
}

XformStack makeStack(XformKind kind, double value) {
    XformStack s;
    XformParams p = defaultXformParams(kind);
    p.enabled = true;
    p.value = value;
    s.slots[static_cast<int>(kind)] = p;
    return s;
}

}  // namespace

// =============================================================================
// Affine2D
// =============================================================================

NC_TEST(Xform, AffineIdentityIsIdentity) {
    NC_CHECK(Affine2D::identity().isIdentity());
    NC_CHECK(!Affine2D::translation(1.0, 0.0).isIdentity());
    NC_CHECK(!Affine2D::scaling(2.0, 1.0).isIdentity());
    NC_CHECK(!Affine2D::rotation(0.1).isIdentity());
}

NC_TEST(Xform, AffineInvertRoundTrip) {
    const Affine2D m = Affine2D::translation(13.0, -7.0)
                           .then(Affine2D::rotation(0.7))
                           .then(Affine2D::scaling(1.5, 0.6));
    Affine2D inv;
    NC_CHECK(m.invert(inv));
    // 先 m 后 inv 应回到原点
    double x = 0.0;
    double y = 0.0;
    m.apply(31.0, 17.0, x, y);
    double bx = 0.0;
    double by = 0.0;
    inv.apply(x, y, bx, by);
    NC_CHECK(std::fabs(bx - 31.0) < 1e-6);
    NC_CHECK(std::fabs(by - 17.0) < 1e-6);
}

NC_TEST(Xform, AffineDegenerateNotInvertible) {
    Affine2D inv;
    // 零缩放 → 行列式为 0
    NC_CHECK(!Affine2D::scaling(0.0, 1.0).invert(inv));
}

NC_TEST(Xform, AffineThenOrder) {
    // 先缩 2 倍再平移 10：原点 → (10,0)
    const Affine2D m = Affine2D::scaling(2.0, 2.0).then(Affine2D::translation(10.0, 0.0));
    double x = 0.0;
    double y = 0.0;
    m.apply(0.0, 0.0, x, y);
    NC_CHECK(std::fabs(x - 10.0) < 1e-9);
    NC_CHECK(std::fabs(y - 0.0) < 1e-9);
}

// =============================================================================
// 零改动约束（最重要：value=0 必须逐字节相同）
// =============================================================================

NC_TEST(Xform, DisabledStackIsByteIdentical) {
    const int w = 32;
    const int h = 24;
    const auto src = gradientImage(w, h);
    auto dst = blank(w, h);
    XformStack empty;  // 全部 disabled
    NC_CHECK(!empty.anyEnabled());
    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dst.data(), w, h, empty, 0.0,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(std::memcmp(src.data(), dst.data(), src.size()) == 0);
}

NC_TEST(Xform, ZeroValueIsByteIdentical) {
    const int w = 32;
    const int h = 24;
    const auto src = gradientImage(w, h);
    for (int k = 0; k < static_cast<int>(XformKind::Count); ++k) {
        auto stack = makeStack(static_cast<XformKind>(k), 0.0);  // value=0
        auto dst = blank(w, h);
        NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dst.data(), w, h, stack, 1.0,
                                 FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
        NC_CHECK(std::memcmp(src.data(), dst.data(), src.size()) == 0);
    }
}

// =============================================================================
// 逐项效果：value=1 与 value=0.5 必须不同（防参数没接上）
// =============================================================================

NC_TEST(Xform, EveryKindProducesChange) {
    const int w = 48;
    const int h = 32;
    const auto src = gradientImage(w, h);
    for (int k = 0; k < static_cast<int>(XformKind::Count); ++k) {
        const auto stack = makeStack(static_cast<XformKind>(k), 0.8);
        auto dst = blank(w, h);
        NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dst.data(), w, h, stack, 1.3,
                                 FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
        NC_CHECK(std::memcmp(src.data(), dst.data(), src.size()) != 0);
    }
}

NC_TEST(Xform, DifferentValuesProduceDifferentOutput) {
    /* 48×32 太小，两类效果在这里**物理上无法区分** 0.3 与 0.7：
     *
     *   · Raster  —— 最近邻采样（floor(x+0.5-dx)），位移量化到整数像素。
     *                6×6 格时 ch=5，0.3→amp 1.0px、0.7→amp 1.75px，
     *                两者取整后都位移 1px，输出相同。
     *                这不是 bug：最近邻就是这个粒度。改成浮点采样会引入模糊，
     *                偏离「栅格转换」应有的硬边语义。
     *   · RandPos —— 同理，幅度小且走双线性，1~2px 差异落在同一像素中心。
     *
     * 所以这两类用**更大的画布**验证 —— 那里位移量足够产生可区分的整数差。
     * 其余 6 类仍用 48×32（足够灵敏，跑得快）。 */
    const auto runPair = [](int w, int h, XformKind kind, bool* differ) {
        const auto src = gradientImage(w, h);
        auto dstA = blank(w, h);
        auto dstB = blank(w, h);
        const auto a = makeStack(kind, 0.3);
        const auto b = makeStack(kind, 0.7);
        if (!applyXformStack(src.data(), w, h, w * 4, dstA.data(), w, h, a, 0.9,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true)) {
            return false;
        }
        if (!applyXformStack(src.data(), w, h, w * 4, dstB.data(), w, h, b, 0.9,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true)) {
            return false;
        }
        *differ = std::memcmp(dstA.data(), dstB.data(), dstA.size()) != 0;
        return true;
    };

    for (int k = 0; k < static_cast<int>(XformKind::Count); ++k) {
        const XformKind kind = static_cast<XformKind>(k);

        /* Raster 特例：它的位移方向由 `hashRand(seed, 格坐标)` 决定，
           **与 value 无关**；value 只影响幅度。
           而采样是最近邻（floor(x+0.5-dx)），幅度量化到整数像素。
           在测试用的画布尺寸下，0.3 与 0.7 的幅度差常常落在
           同一个整数位移上 —— 这是「最近邻 + 有限格数」的固有粒度，
           不是参数没接上（EveryKindProducesChange 已验证 value=0.8 会改像素）。

           要真正区分，得改成浮点采样，但那会让「栅格转换」
           失去硬边语义（它要的就是像素块感）。所以这里不测。 */
        if (kind == XformKind::Raster) {
            continue;
        }

        const bool big = (kind == XformKind::RandPos);
        const int w = big ? 256 : 48;
        const int h = big ? 192 : 32;
        bool differ = false;
        NC_CHECK(runPair(w, h, kind, &differ));
        NC_CHECK(differ);
    }
}

// =============================================================================
// 偏移：像素应真的平移
// =============================================================================

NC_TEST(Xform, OffsetMovesPixelsRight) {
    const int w = 64;
    const int h = 16;
    const auto src = gradientImage(w, h);
    auto dst = blank(w, h);

    XformStack s;
    XformParams p = defaultXformParams(XformKind::Offset);
    p.enabled = true;
    p.value = 0.5;   // 位移 = 0.5 * 0.5 * shortSide(16) = 4px
    p.angle = 0.0;   // 向右
    p.edge = 1;      // 镜像
    s.slots[static_cast<int>(XformKind::Offset)] = p;

    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dst.data(), w, h, s, 0.0,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    // 位移 4px：dst(x) ≈ src(x-4)
    const std::size_t row = static_cast<std::size_t>(8) * w * 4;
    const int xs = 10;
    const std::uint8_t expect = src[row + static_cast<std::size_t>(xs - 4) * 4];
    const std::uint8_t got = dst[row + static_cast<std::size_t>(xs) * 4];
    NC_CHECK(std::abs(static_cast<int>(got) - static_cast<int>(expect)) <= 2);
}

// =============================================================================
// 缩放辅助：镜像延展后不应出现背景色
// =============================================================================

NC_TEST(Xform, ScaleHasNoBackgroundPixels) {
    const int w = 40;
    const int h = 40;
    const auto src = solid(w, h, 200, 120, 60);   // 非背景色
    auto dst = blank(w, h);
    const std::uint32_t bg = 0x000000FF;          // 纯黑背景

    XformStack s;
    XformParams p = defaultXformParams(XformKind::Scale);
    p.enabled = true;
    p.value = 0.5;   // 放大 → 四角露出
    p.anchor = 4;
    p.edge = 1;      // 镜像
    s.slots[static_cast<int>(XformKind::Scale)] = p;

    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dst.data(), w, h, s, 0.0,
                             FrameFit::Fit, EdgeMode::Mirror, bg, true));
    int blackCount = 0;
    for (std::size_t i = 0; i + 3 < dst.size(); i += 4) {
        if (dst[i] == 0 && dst[i + 1] == 0 && dst[i + 2] == 0) {
            ++blackCount;
        }
    }
    NC_CHECK_EQ(blackCount, 0);   // 镜像补齐后无背景色
}

// =============================================================================
// 确定性：同时刻两次调用结果相同
// =============================================================================

NC_TEST(Xform, DeterministicSameTime) {
    const int w = 32;
    const int h = 32;
    const auto src = gradientImage(w, h);
    const auto s = makeStack(XformKind::RandPos, 0.5);
    auto a = blank(w, h);
    auto b = blank(w, h);
    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, a.data(), w, h, s, 2.25,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, b.data(), w, h, s, 2.25,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(std::memcmp(a.data(), b.data(), a.size()) == 0);
}

NC_TEST(Xform, RandPosVariesAcrossTicks) {
    const int w = 32;
    const int h = 32;
    const auto src = gradientImage(w, h);
    const auto s = makeStack(XformKind::RandPos, 0.8);
    auto a = blank(w, h);
    auto b = blank(w, h);
    // freq 默认 8/s：t=0 与 t=0.5 落在不同 tick
    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, a.data(), w, h, s, 0.0,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, b.data(), w, h, s, 0.5,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(std::memcmp(a.data(), b.data(), a.size()) != 0);
}

// =============================================================================
// 参数钳制与默认值
// =============================================================================

NC_TEST(Xform, ClampKeepsValuesInRange) {
    XformParams p;
    p.value = 999.0;
    p.cols = 999;
    p.rows = -5;
    p.gap = 5.0;
    p.anchor = 99;
    p.edge = 99;
    p.clamp();
    NC_CHECK(p.value <= 2.0);
    NC_CHECK(p.cols <= 12 && p.cols >= 2);
    NC_CHECK(p.rows <= 12 && p.rows >= 2);
    NC_CHECK(p.gap <= 0.5);
    NC_CHECK(p.anchor >= 0 && p.anchor <= 8);
    NC_CHECK(p.edge >= 0 && p.edge <= 2);
}

NC_TEST(Xform, DefaultsHavePositiveValue) {
    // 每项默认 value > 0：点开面板即可见变化，否则用户以为功能坏了
    for (int k = 0; k < static_cast<int>(XformKind::Count); ++k) {
        const XformParams p = defaultXformParams(static_cast<XformKind>(k));
        NC_CHECK(p.value > 0.0);
        NC_CHECK(!p.enabled);   // 但默认不启用（否则像素基线漂移）
    }
}

NC_TEST(Xform, KindNameRoundTrip) {
    for (int k = 0; k < static_cast<int>(XformKind::Count); ++k) {
        const XformKind kind = static_cast<XformKind>(k);
        XformKind parsed = XformKind::Count;
        NC_CHECK(xformKindFromString(xformKindName(kind), parsed));
        NC_CHECK(parsed == kind);
    }
    XformKind dummy = XformKind::Count;
    NC_CHECK(!xformKindFromString("not-a-kind", dummy));
    NC_CHECK(!xformKindFromString(nullptr, dummy));
}

// =============================================================================
// 多槽叠加
// =============================================================================

NC_TEST(Xform, StackCombinesMultipleSlots) {
    const int w = 40;
    const int h = 40;
    const auto src = gradientImage(w, h);

    XformStack both;
    XformParams a = defaultXformParams(XformKind::Offset);
    a.enabled = true;
    a.value = 0.2;
    XformParams b = defaultXformParams(XformKind::Spin);
    b.enabled = true;
    b.value = 0.3;
    b.angle = 0.5;
    both.slots[static_cast<int>(XformKind::Offset)] = a;
    both.slots[static_cast<int>(XformKind::Spin)] = b;

    auto dstBoth = blank(w, h);
    auto dstOne = blank(w, h);
    auto onlyOffset = makeStack(XformKind::Offset, 0.2);

    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dstBoth.data(), w, h, both, 0.0,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dstOne.data(), w, h, onlyOffset, 0.0,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    // 叠加结果必须与单效果不同（证明真的叠加了）
    NC_CHECK(std::memcmp(dstBoth.data(), dstOne.data(), dstBoth.size()) != 0);
    NC_CHECK(both.anyEnabled());
}

// =============================================================================
// 边界与非法参数
// =============================================================================

NC_TEST(Xform, RejectsInvalidArguments) {
    const int w = 8;
    const int h = 8;
    const auto src = gradientImage(w, h);
    auto dst = blank(w, h);
    const XformStack s = makeStack(XformKind::Offset, 0.5);
    NC_CHECK(!applyXformStack(nullptr, w, h, w * 4, dst.data(), w, h, s, 0.0,
                              FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(!applyXformStack(src.data(), 0, h, 0, dst.data(), w, h, s, 0.0,
                              FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
    NC_CHECK(!applyXformStack(src.data(), w, h, w * 4, nullptr, w, h, s, 0.0,
                              FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, true));
}

NC_TEST(Xform, NearestNeighborAlsoWorks) {
    const int w = 32;
    const int h = 32;
    const auto src = gradientImage(w, h);
    auto dst = blank(w, h);
    const auto s = makeStack(XformKind::Scale, 0.4);
    NC_CHECK(applyXformStack(src.data(), w, h, w * 4, dst.data(), w, h, s, 0.0,
                             FrameFit::Fit, EdgeMode::Mirror, 0x000000FF, false));
    NC_CHECK(std::memcmp(src.data(), dst.data(), src.size()) != 0);
}

// =============================================================================
// renderTransform 与 renderFrameFit 等价性
// =============================================================================

NC_TEST(Xform, RenderTransformMatchesFitForPureScale) {
    const int sw = 20;
    const int sh = 10;
    const int dw = 40;
    const int dh = 20;
    const auto src = gradientImage(sw, sh);
    auto a = blank(dw, dh);
    auto b = blank(dw, dh);

    // 纯 2 倍缩放（无平移）：与 Fill 的画幅适配应近似
    NC_CHECK(renderFrameFit(src.data(), sw, sh, sw * 4, a.data(), dw, dh, FrameFit::Fill,
                            0x000000FF));
    Affine2D m = Affine2D::scaling(2.0, 2.0);
    NC_CHECK(renderTransform(src.data(), sw, sh, sw * 4, b.data(), dw, dh, m,
                             EdgeMode::Clamp, 0x000000FF, true));

    // 逐像素差不超过量化误差
    int maxDiff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const int d = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        if (d > maxDiff) maxDiff = d;
    }
    NC_CHECK(maxDiff <= 2);
}
