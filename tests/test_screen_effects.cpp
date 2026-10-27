// =============================================================================
// 画面特效 + 混合模式 + 渲染质量分级 + 蒙版扩展 测试（M2.30）
//   —— 文档第二段 5.1（画面特效六种）/ 5.8（蒙版星形爱心）/ 5.9（画中画混合）/
//      4.4（预览 vs 导出质量分级）
// =============================================================================
// 覆盖：
//   · 六种画面特效：零强度零改动、强度生效、确定性（同帧同输出）、
//     帧间时变（故障/RGB 分离）；
//   · 混合模式：五种模式的标准公式精确值（Screen/Overlay/Darken/HardLight/Normal）、
//     帧级混合（alpha 权重）、名称解析往返；
//   · 渲染质量分级：导出/预览校验（fail-closed）、核半径/采样数分级、
//     导出 + 降分辨率必须被拒绝（文档 4.4 强制要求）；
//   · 蒙版星形/爱心：几何正确性（角尖/谷/凹口/下尖）、解析往返；
//   · 管线集成：编辑器命令（撤销重做）、导出计划快照、序列化往返、C ABI。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/blend_modes.h"
#include "nairoclip/core/mask.h"
#include "nairoclip/core/render_engine.h"
#include "nairoclip/core/screen_effects.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 高频测试图（三通道都有变化；移位类效果需要可检测差异）。
std::vector<std::uint8_t> makeDetailImage(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            px[i] = static_cast<std::uint8_t>((x * 11 + y * 3) % 256);
            px[i + 1] = static_cast<std::uint8_t>((y * 13 + x) % 256);
            px[i + 2] = static_cast<std::uint8_t>((x * 3 + y * 7) % 256);
        }
    }
    return px;
}

/// 三通道差异计数（只比 R 会漏检 RGB 分离等仅动 B 通道的情形）。
int diffCount(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    int n = 0;
    for (std::size_t i = 0; i < a.size() && i + 2 < b.size(); i += 4) {
        if (a[i] != b[i] || a[i + 1] != b[i + 1] || a[i + 2] != b[i + 2]) ++n;
    }
    return n;
}

}  // namespace

// ---------------------------------------------------------------------------
// 画面特效（六种）
// ---------------------------------------------------------------------------

NC_TEST(ScreenEffects, AllEffectsZeroIntensityNoOp) {
    const int w = 64, h = 48;
    const int types[] = {13, 14, 15, 16, 17, 18};
    for (int type : types) {
        auto image = makeDetailImage(w, h);
        const auto original = image;
        NC_CHECK(applyScreenEffectToRgba(image.data(), w, h, type, 0.0, 0));
        NC_CHECK_EQ(diffCount(original, image), 0);  // 零强度：零改动
    }
}

NC_TEST(ScreenEffects, AllEffectsChangeImageAtFullIntensity) {
    const int w = 64, h = 48;
    const int types[] = {13, 14, 15, 16, 17, 18};
    for (int type : types) {
        auto image = makeDetailImage(w, h);
        const auto original = image;
        NC_CHECK(applyScreenEffectToRgba(image.data(), w, h, type, 1.0, 0));
        NC_CHECK(diffCount(original, image) > 100);  // 效果真实生效
    }
}

NC_TEST(ScreenEffects, DeterministicAcrossRuns) {
    const int w = 64, h = 48;
    const int types[] = {13, 14, 15, 16, 17, 18};
    for (int type : types) {
        auto a = makeDetailImage(w, h);
        auto b = makeDetailImage(w, h);
        applyScreenEffectToRgba(a.data(), w, h, type, 1.0, 5);
        applyScreenEffectToRgba(b.data(), w, h, type, 1.0, 5);
        for (std::size_t i = 0; i < a.size(); ++i) {
            NC_CHECK_EQ(a[i], b[i]);  // 同帧同输出（确定性）
        }
    }
}

NC_TEST(ScreenEffects, GlitchAndRgbSplitVaryAcrossFrames) {
    const int w = 64, h = 48;
    // 故障与 RGB 分离的位移/色块随时变（帧间差异）
    for (int type : {13, 18}) {
        auto f0 = makeDetailImage(w, h);
        auto f1 = makeDetailImage(w, h);
        applyScreenEffectToRgba(f0.data(), w, h, type, 1.0, 0);
        applyScreenEffectToRgba(f1.data(), w, h, type, 1.0, 1);
        NC_CHECK(diffCount(f0, f1) > 10);  // 帧间存在差异（时变）
    }
}

NC_TEST(ScreenEffects, MirrorSwapsSides) {
    const int w = 64, h = 48;
    auto before = makeDetailImage(w, h);
    auto image = before;
    NC_CHECK(applyMirrorRgba(image.data(), w, h, 1.0));
    // 镜像后 at(x) == 镜像前 at(w-1-x)
    auto at = [&](const std::vector<std::uint8_t>& f, int x, int y) {
        return static_cast<int>(f[(static_cast<std::size_t>(y) * w + x) * 4]);
    };
    NC_CHECK_EQ(at(image, 0, 10), at(before, w - 1, 10));
    NC_CHECK_EQ(at(image, w - 1, 10), at(before, 0, 10));
    NC_CHECK_EQ(at(image, 7, 20), at(before, w - 8, 20));
}

NC_TEST(ScreenEffects, ScanlinesDarkenAlternateRows) {
    const int w = 32, h = 32;
    std::vector<std::uint8_t> image(static_cast<std::size_t>(w) * h * 4, 200);
    NC_CHECK(applyScanlinesRgba(image.data(), w, h, 1.0));
    auto v = [&](int y) { return static_cast<int>(image[(static_cast<std::size_t>(y) * w + 1) * 4]); };
    // period = 4：y0/y1 亮；y2/y3 暗
    NC_CHECK(v(0) > v(2));
    NC_CHECK(v(1) > v(3));
    NC_CHECK_EQ(v(0), 200);
    NC_CHECK(v(2) < 200);
}

NC_TEST(ScreenEffects, SplitScreenShowsFullFrameInEachQuadrant) {
    const int w = 64, h = 48;
    // 构造可识别的图案（左上角亮、其它区域暗）
    std::vector<std::uint8_t> image(static_cast<std::size_t>(w) * h * 4, 20);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 8; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            image[i] = 220;
            image[i + 1] = 220;
            image[i + 2] = 220;
        }
    }
    NC_CHECK(applySplitScreenRgba(image.data(), w, h, 1.0));
    // 分屏后：每个象限的左上角都应显示源帧左上角（亮区）→ 每格左上像素应亮
    auto at = [&](int x, int y) {
        return static_cast<int>(image[(static_cast<std::size_t>(y) * w + x) * 4]);
    };
    NC_CHECK(at(1, 1) > 200);           // 左上格
    NC_CHECK(at(w / 2 + 1, 1) > 200);   // 右上格
    NC_CHECK(at(1, h / 2 + 1) > 200);   // 左下格
    NC_CHECK(at(w / 2 + 1, h / 2 + 1) > 200);  // 右下格
    // 每格右下角应是源帧右下角（暗区）
    NC_CHECK(at(w / 2 - 2, h / 2 - 2) < 100);
}

NC_TEST(ScreenEffects, RejectsInvalidArgs) {
    auto image = makeDetailImage(16, 16);
    const auto original = image;
    NC_CHECK(!applyScreenEffectToRgba(nullptr, 16, 16, 13, 1.0, 0));
    NC_CHECK(!applyScreenEffectToRgba(image.data(), 0, 16, 13, 1.0, 0));
    NC_CHECK(!applyScreenEffectToRgba(image.data(), 16, -1, 13, 1.0, 0));
    NC_CHECK(!applyGlitchRgba(nullptr, 16, 16, 1.0, 0));
    NC_CHECK(!applySplitScreenRgba(nullptr, 16, 16, 1.0));
    NC_CHECK(!applyMirrorRgba(nullptr, 16, 16, 1.0));
    NC_CHECK(!applyRadialBlurRgba(nullptr, 16, 16, 1.0, RenderQuality::exportQuality()));
    NC_CHECK(!applyScanlinesRgba(nullptr, 16, 16, 1.0));
    NC_CHECK(!applyRgbSplitRgba(nullptr, 16, 16, 1.0, 0));
    // 非画面特效类型（如颜色类）→ 零改动成功
    NC_CHECK(applyScreenEffectToRgba(image.data(), 16, 16, 3, 1.0, 0));
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
}

// ---------------------------------------------------------------------------
// 混合模式（文档 5.9）
// ---------------------------------------------------------------------------

NC_TEST(BlendModes, StandardFormulasExact) {
    // 行业标准公式的精确值
    NC_CHECK(std::fabs(blendChannel(BlendMode::Normal, 0.3, 0.9) - 0.9) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Screen, 0.5, 0.5) - 0.75) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Screen, 0.0, 0.5) - 0.5) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Overlay, 0.25, 0.5) - 0.25) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Overlay, 0.75, 0.5) - 0.75) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Darken, 0.3, 0.7) - 0.3) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Darken, 0.8, 0.2) - 0.2) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::HardLight, 0.5, 0.25) - 0.25) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::HardLight, 0.5, 0.75) - 0.75) < 1e-9);
    // 输入钳制（防御）
    NC_CHECK(std::fabs(blendChannel(BlendMode::Screen, -0.5, 0.5) - 0.5) < 1e-9);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Screen, 1.5, 0.5) - 1.0) < 1e-9);
}

NC_TEST(BlendModes, NamesAndParsing) {
    NC_CHECK_EQ(std::string(blendModeName(BlendMode::Normal)), std::string("normal"));
    NC_CHECK_EQ(std::string(blendModeName(BlendMode::Screen)), std::string("screen"));
    NC_CHECK_EQ(std::string(blendModeName(BlendMode::Overlay)), std::string("overlay"));
    NC_CHECK_EQ(std::string(blendModeName(BlendMode::Darken)), std::string("darken"));
    NC_CHECK_EQ(std::string(blendModeName(BlendMode::HardLight)), std::string("hard_light"));
    BlendMode mode;
    NC_CHECK(blendModeFromString("screen", mode));
    NC_CHECK(mode == BlendMode::Screen);
    NC_CHECK(blendModeFromString("HARD-LIGHT", mode));  // 别名 + 大小写不敏感
    NC_CHECK(mode == BlendMode::HardLight);
    NC_CHECK(blendModeFromString("hardlight", mode));
    NC_CHECK(mode == BlendMode::HardLight);
    NC_CHECK(!blendModeFromString("bogus", mode));
    NC_CHECK(!blendModeFromString(nullptr, mode));
    // B1-1 后共 16 种（0..15）。上界跟随 kBlendModeCount，避免再次硬编码。
    for (int i = 0; i < kBlendModeCount; ++i) {
        NC_CHECK(blendModeFromInt(i, mode));
        NC_CHECK_EQ(static_cast<int>(mode), i);
    }
    NC_CHECK(!blendModeFromInt(kBlendModeCount, mode));
    NC_CHECK(!blendModeFromInt(-1, mode));
}

NC_TEST(BlendModes, FrameBlendRespectsAlpha) {
    const int w = 8, h = 8;
    // 上层全透明 → 底层不变
    {
        std::vector<std::uint8_t> base(static_cast<std::size_t>(w) * h * 4, 100);
        std::vector<std::uint8_t> top(static_cast<std::size_t>(w) * h * 4, 200);
        for (std::size_t i = 3; i < top.size(); i += 4) top[i] = 0;
        const auto original = base;
        NC_CHECK(blendFrameRgba(base.data(), top.data(), w, h, BlendMode::Screen, 1.0));
        for (std::size_t i = 0; i < base.size(); ++i) {
            NC_CHECK_EQ(base[i], original[i]);  // 全透明 → 零改动
        }
    }
    // 上层不透明 → 结果为模式公式输出
    {
        std::vector<std::uint8_t> base(static_cast<std::size_t>(w) * h * 4, 128);
        std::vector<std::uint8_t> top(static_cast<std::size_t>(w) * h * 4, 128);
        for (std::size_t i = 3; i < top.size(); i += 4) top[i] = 255;
        NC_CHECK(blendFrameRgba(base.data(), top.data(), w, h, BlendMode::Screen, 1.0));
        // Screen(128/255, 128/255) = 1 - (127/255)² = 0.7520 → 192
        NC_CHECK_EQ(base[0], std::uint8_t(192));
        NC_CHECK_EQ(base[3], std::uint8_t(255));  // 合成后不透明
    }
    // opacity 权重
    {
        std::vector<std::uint8_t> base(static_cast<std::size_t>(w) * h * 4, 100);
        std::vector<std::uint8_t> top(static_cast<std::size_t>(w) * h * 4, 200);
        for (std::size_t i = 3; i < top.size(); i += 4) top[i] = 255;
        NC_CHECK(blendFrameRgba(base.data(), top.data(), w, h, BlendMode::Screen, 0.5));
        // 半权重：结果介于 base(100) 与 Screen 全量(~236) 之间
        NC_CHECK(base[0] > 100 && base[0] < 236);
    }
}

NC_TEST(BlendModes, FrameBlendRejectsInvalid) {
    std::vector<std::uint8_t> buf(64, 100);
    NC_CHECK(!blendFrameRgba(nullptr, buf.data(), 4, 4, BlendMode::Screen, 1.0));
    NC_CHECK(!blendFrameRgba(buf.data(), nullptr, 4, 4, BlendMode::Screen, 1.0));
    NC_CHECK(!blendFrameRgba(buf.data(), buf.data(), 0, 4, BlendMode::Screen, 1.0));
    NC_CHECK(!blendFrameRgba(buf.data(), buf.data(), 4, -1, BlendMode::Screen, 1.0));
    // opacity 0 → 零改动成功
    const auto original = buf;
    NC_CHECK(blendFrameRgba(buf.data(), buf.data(), 4, 4, BlendMode::Screen, 0.0));
    for (std::size_t i = 0; i < buf.size(); ++i) {
        NC_CHECK_EQ(buf[i], original[i]);
    }
}

// ---------------------------------------------------------------------------
// 渲染质量分级（文档 4.4）
// ---------------------------------------------------------------------------

NC_TEST(RenderEngine, ExportQualityRequiresFullPrecision) {
    const auto qe = RenderQuality::exportQuality();
    NC_CHECK(qe.valid());
    NC_CHECK(validateRenderQuality(qe).allowed);
    // 导出 + 降分辨率 → 必须拒绝（文档 4.4 强制：导出质量不可缩水）
    RenderQuality bad = qe;
    bad.scale = 0.5;
    NC_CHECK(!bad.valid());
    NC_CHECK(!validateRenderQuality(bad).allowed);
    // 导出 + 降采样 → 拒绝
    bad = qe;
    bad.sampleBudget = 0.5;
    NC_CHECK(!validateRenderQuality(bad).allowed);
    // 导出 + 允许丢帧 → 拒绝
    bad = qe;
    bad.allowFrameDrop = true;
    NC_CHECK(!validateRenderQuality(bad).allowed);
}

NC_TEST(RenderEngine, PreviewQualityAllowsOptimization) {
    const auto qp = RenderQuality::previewQuality();
    NC_CHECK(qp.valid());
    NC_CHECK(validateRenderQuality(qp).allowed);
    NC_CHECK(qp.path == RenderPath::Preview);
    NC_CHECK(qp.scale < 1.0);        // 预览允许降分辨率
    NC_CHECK(qp.allowFrameDrop);     // 预览允许丢帧
    // 非法值（负缩放）拒绝
    RenderQuality bad = qp;
    bad.scale = -1.0;
    NC_CHECK(!validateRenderQuality(bad).allowed);
    // 名称
    NC_CHECK_EQ(std::string(renderPathName(RenderPath::Preview)), std::string("preview"));
    NC_CHECK_EQ(std::string(renderPathName(RenderPath::Export)), std::string("export"));
}

NC_TEST(RenderEngine, KernelRadiusAndSampleCountScale) {
    const auto qe = RenderQuality::exportQuality();
    const auto qp = RenderQuality::previewQuality(0.5, 0.5);
    // 核半径：导出不变；预览按 sampleBudget 缩放（下限 1）
    NC_CHECK(std::fabs(effectiveKernelRadius(qe, 8.0) - 8.0) < 1e-9);
    NC_CHECK(std::fabs(effectiveKernelRadius(qp, 8.0) - 4.0) < 1e-9);
    NC_CHECK(std::fabs(effectiveKernelRadius(qp, 1.0) - 1.0) < 1e-9);  // 下限
    NC_CHECK(std::fabs(effectiveKernelRadius(qe, 0.0) - 0.0) < 1e-9);
    // 采样数：导出不变；预览缩放（下限 4）
    NC_CHECK_EQ(effectiveSampleCount(qe, 24), 24);
    NC_CHECK_EQ(effectiveSampleCount(qp, 24), 12);
    NC_CHECK_EQ(effectiveSampleCount(qp, 4), 4);   // 下限（4*0.5=2 → 4）
    NC_CHECK_EQ(effectiveSampleCount(qp, 100), 50);
}

NC_TEST(RenderEngine, RadialBlurUsesQualityGrading) {
    const int w = 64, h = 48;
    // 预览与导出质量下的旋转模糊结果应不同（采样数不同）
    auto exportImage = makeDetailImage(w, h);
    auto previewImage = makeDetailImage(w, h);
    NC_CHECK(applyRadialBlurRgba(exportImage.data(), w, h, 1.0,
                                 RenderQuality::exportQuality()));
    NC_CHECK(applyRadialBlurRgba(previewImage.data(), w, h, 1.0,
                                 RenderQuality::previewQuality(0.5, 0.5)));
    NC_CHECK(diffCount(exportImage, previewImage) > 0);  // 分级真实生效
    // 两者都改变了画面
    const auto original = makeDetailImage(w, h);
    NC_CHECK(diffCount(original, exportImage) > 100);
    NC_CHECK(diffCount(original, previewImage) > 100);
}

// ---------------------------------------------------------------------------
// 蒙版扩展（文档 5.8：星形/爱心）
// ---------------------------------------------------------------------------

NC_TEST(MaskShapes, StarAndHeartParsing) {
    NC_CHECK_EQ(std::string(maskShapeName(MaskShape::Star)), std::string("star"));
    NC_CHECK_EQ(std::string(maskShapeName(MaskShape::Heart)), std::string("heart"));
    MaskShape shape;
    NC_CHECK(maskShapeFromString("star", shape));
    NC_CHECK(shape == MaskShape::Star);
    NC_CHECK(maskShapeFromString("HEART", shape));
    NC_CHECK(shape == MaskShape::Heart);
    NC_CHECK(maskShapeFromString("星形", shape));
    NC_CHECK(shape == MaskShape::Star);
    NC_CHECK(maskShapeFromString("爱心", shape));
    NC_CHECK(shape == MaskShape::Heart);
    NC_CHECK(maskShapeFromInt(5, shape));
    NC_CHECK(shape == MaskShape::Star);
    NC_CHECK(maskShapeFromInt(6, shape));
    NC_CHECK(shape == MaskShape::Heart);
    NC_CHECK(!maskShapeFromInt(7, shape));
    NC_CHECK(!maskShapeFromString("triangle", shape));
}

NC_TEST(MaskShapes, StarGeometry) {
    const int W = 200, H = 200;
    Mask star;
    star.shape = MaskShape::Star;
    star.width = 0.35;  // 外接半径 70px；内半径 31.5px
    star.centerX = 0.5;
    star.centerY = 0.5;
    // 中心：可见
    NC_CHECK(maskCoverageAt(star, 100, 100, W, H) > 0.99);
    // 外围：不可见
    NC_CHECK(maskCoverageAt(star, 195, 195, W, H) < 0.01);
    // 角尖方向（θ=0，r=65 < 70）：可见
    NC_CHECK(maskCoverageAt(star, 165, 100, W, H) > 0.9);
    // 谷方向（θ=36°，r=45 > 31.5）：不可见
    NC_CHECK(maskCoverageAt(star, 136, 73, W, H) < 0.1);
    // 正上方（θ=90°，谷方向，r=60）：不可见
    NC_CHECK(maskCoverageAt(star, 100, 40, W, H) < 0.1);
    // 反转
    Mask inverted = star;
    inverted.invert = true;
    NC_CHECK(maskCoverageAt(inverted, 100, 100, W, H) < 0.01);
    NC_CHECK(maskCoverageAt(inverted, 195, 195, W, H) > 0.99);
}

NC_TEST(MaskShapes, HeartGeometry) {
    const int W = 200, H = 200;
    Mask heart;
    heart.shape = MaskShape::Heart;
    heart.width = 0.35;  // scale = 70px；ny = -dy/70×1.15
    heart.centerX = 0.5;
    heart.centerY = 0.5;
    // 主体（中心）：可见
    NC_CHECK(maskCoverageAt(heart, 100, 110, W, H) > 0.99);
    // 下部主体（ny≈-0.66）：可见
    NC_CHECK(maskCoverageAt(heart, 100, 140, W, H) > 0.99);
    // 左圆弧（nx≈-0.43, ny≈0.33）：可见
    NC_CHECK(maskCoverageAt(heart, 70, 80, W, H) > 0.99);
    // 下尖以下（ny≈-1.23）：不可见
    NC_CHECK(maskCoverageAt(heart, 100, 175, W, H) < 0.01);
    // 顶部凹口（ny≈1.10，方程为边界外）：不可见
    NC_CHECK(maskCoverageAt(heart, 100, 33, W, H) < 0.01);
    // 角落：不可见
    NC_CHECK(maskCoverageAt(heart, 10, 10, W, H) < 0.01);
    NC_CHECK(maskCoverageAt(heart, 190, 190, W, H) < 0.01);
    // 羽化产生渐变（边界附近值介于 0 与 1 之间）
    Mask feather = heart;
    feather.feather = 0.1;
    bool sawGradient = false;
    for (int y = 20; y < 60 && !sawGradient; ++y) {
        const double cov = maskCoverageAt(feather, 100, y, W, H);
        if (cov > 0.05 && cov < 0.95) sawGradient = true;
    }
    NC_CHECK(sawGradient);
}

// ---------------------------------------------------------------------------
// 管线集成（编辑器 / 导出计划 / 序列化 / C ABI）
// ---------------------------------------------------------------------------

#include "nairoclip/core/capi.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/serialization.h"

namespace {

struct ScreenFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    ClipId clipId;
    ScreenFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 64;
        seq.height = 48;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/videos/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(10);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(1);
        editor.insertClip(seqId, trackId, c, &clipId);
    }
};

}  // namespace

NC_TEST(ScreenPipeline, BlendModeCommandsWithUndo) {
    ScreenFixture f;
    const Clip* clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip->blendMode == BlendMode::Normal);  // 默认
    NC_CHECK(f.editor.setClipBlendMode(f.seqId, f.trackId, f.clipId, BlendMode::Screen).isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip->blendMode == BlendMode::Screen);
    // 撤销
    NC_CHECK(f.editor.undo().isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip->blendMode == BlendMode::Normal);
    // 重做
    NC_CHECK(f.editor.redo().isOk());
    clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip->blendMode == BlendMode::Screen);
    // 锁定轨道拒绝
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    NC_CHECK(!f.editor.setClipBlendMode(f.seqId, f.trackId, f.clipId, BlendMode::Darken).isOk());
}

NC_TEST(ScreenPipeline, ExportPlanCarriesBlendMode) {
    ScreenFixture f;
    NC_CHECK(f.editor.setClipBlendMode(f.seqId, f.trackId, f.clipId,
                                       BlendMode::Overlay).isOk());
    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(1));
    NC_CHECK(plan.tracks[0].clips[0].blendMode == BlendMode::Overlay);
}

NC_TEST(ScreenPipeline, SerializationRoundtrip) {
    ScreenFixture f;
    NC_CHECK(f.editor.setClipBlendMode(f.seqId, f.trackId, f.clipId,
                                       BlendMode::HardLight).isOk());
    // 新特效类型（画面特效）也应能持久化
    Effect glitch;
    glitch.id = EffectId{f.project.ids.next()};
    glitch.type = EffectType::Glitch;
    glitch.value = 0.7;
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId, glitch).isOk());
    const std::string text = saveProjectToNclip(f.project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    const Clip* clip = loaded.sequences[0].tracks[0].findClip(f.clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK(clip->blendMode == BlendMode::HardLight);
    NC_CHECK_EQ(clip->effects.size(), std::size_t(1));
    NC_CHECK(clip->effects.effects()[0].type == EffectType::Glitch);
    NC_CHECK(std::fabs(clip->effects.effects()[0].value - 0.7) < 1e-9);
}

NC_TEST(ScreenPipeline, DefaultOmitsBlendModeFromFile) {
    ScreenFixture f;
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"blendMode\"") == std::string::npos);  // 默认不写
}

NC_TEST(ScreenPipeline, CapisBlendModeSetGet) {
    NcEditor* editor = nc_editor_create();
    const uint64_t assetId = nc_import_asset(editor, "a.mp4", "/videos/a.mp4", 0, 10000000);
    nc_add_track(editor, "V1", 0);
    const uint64_t clipId = nc_insert_clip(editor, 0, assetId, 0, 1000000);
    NC_CHECK(clipId != 0);
    // 默认 Normal
    NC_CHECK_EQ(nc_get_clip_blend_mode(editor, 0, clipId), 0);
    // 设置 Screen
    NC_CHECK_EQ(nc_set_clip_blend_mode(editor, 0, clipId, 1), NC_OK);
    NC_CHECK_EQ(nc_get_clip_blend_mode(editor, 0, clipId), 1);
    // 四种非默认模式往返
    for (int mode : {1, 2, 3, 4}) {
        NC_CHECK_EQ(nc_set_clip_blend_mode(editor, 0, clipId, mode), NC_OK);
        NC_CHECK_EQ(nc_get_clip_blend_mode(editor, 0, clipId), mode);
    }
    // B1-1 后新增模式（5..15）也应能往返
    for (int mode : {5, 10, 15}) {
        NC_CHECK_EQ(nc_set_clip_blend_mode(editor, 0, clipId, mode), NC_OK);
        NC_CHECK_EQ(nc_get_clip_blend_mode(editor, 0, clipId), mode);
    }
    // 非法值：上界之外
    NC_CHECK_EQ(nc_set_clip_blend_mode(editor, 0, clipId, kBlendModeCount),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_clip_blend_mode(editor, 0, clipId, 99), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_clip_blend_mode(nullptr, 0, clipId, 1), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_clip_blend_mode(nullptr, 0, clipId), -1);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 多层合成 + 混合模式（真实管线；文档 5.9 的画中画核心场景）
// ---------------------------------------------------------------------------

namespace {

/// Mock 读取器：按路径返回不同纯色（验证混合公式在合成路径真实生效）。
class ColorReader : public MediaReader {
public:
    ColorReader() {
        probe_.type = MediaType::Video;
        probe_.frameRate = fps30();
        probe_.width = 8;
        probe_.height = 8;
        probe_.duration = Rational::make(10);
    }
    Result open(const std::string& p) override {
        if (p.empty()) return Result::fail(ErrorCode::InvalidArgument, "路径为空");
        // 路径含 "bright" → 亮色；含 "dark" → 暗色
        if (p.find("bright") != std::string::npos) {
            value_ = 200;
        } else if (p.find("dark") != std::string::npos) {
            value_ = 100;
        } else {
            value_ = 150;
        }
        open_ = true;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time&) override {
        if (!open_) return Result::fail(ErrorCode::InvalidArgument, "未打开");
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame& o) override {
        if (!open_) return false;
        o.timestamp = Rational::make(0);
        o.width = 8;
        o.height = 8;
        o.stride = 8 * 4;
        o.pixels.assign(8 * 8 * 4, 255);
        for (std::size_t i = 0; i + 3 < o.pixels.size(); i += 4) {
            o.pixels[i] = value_;
            o.pixels[i + 1] = value_;
            o.pixels[i + 2] = value_;
        }
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    MediaProbeResult probe_;
    bool open_ = false;
    std::uint8_t value_ = 150;
};

class ColorFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<ColorReader>(); }
};

struct LayerFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId bottomTrack;
    TrackId topTrack;
    LayerFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 8;
        seq.height = 8;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        // 底层：亮色素材
        MediaAsset bright;
        bright.id = AssetId{project.ids.next()};
        bright.path = "/videos/bright.mp4";
        bright.type = MediaType::Video;
        bright.duration = Rational::make(10);
        project.assets.push_back(bright);
        // 上层：暗色素材
        MediaAsset dark;
        dark.id = AssetId{project.ids.next()};
        dark.path = "/videos/dark.mp4";
        dark.type = MediaType::Video;
        dark.duration = Rational::make(10);
        project.assets.push_back(dark);
        editor.addTrack(seqId, TrackType::Video, "V1", &bottomTrack);
        editor.addTrack(seqId, TrackType::Video, "V2", &topTrack);
        Clip bottom;
        bottom.asset = bright.id;
        bottom.start = Rational::make(0);
        bottom.duration = Rational::make(1);
        editor.insertClip(seqId, bottomTrack, bottom, nullptr);
        Clip top;
        top.asset = dark.id;
        top.start = Rational::make(0);
        top.duration = Rational::make(1);
        editor.insertClip(seqId, topTrack, top, nullptr);
    }
};

}  // namespace

NC_TEST(ScreenPipeline, BlendModeAppliesInMultiLayerExport) {
    // 底层 bright(200) + 上层 dark(100)，与 Normal 对比：
    //  - Normal：上层不透明 → 输出 = 100（遮挡）
    //  - Screen：1 - (1-200/255)(1-100/255) = 1 - 0.2157*0.6078 ≈ 0.869 → 222（更亮）
    //  - Darken：min(200,100) = 100（与 Normal 相同）
    //  - Overlay：底 200/255=0.784 > 0.5 → 1-2(0.216)(0.608) = 0.737 → 188
    auto runBlend = [&](BlendMode mode) {
        LayerFixture f;
        // 找到上层片段（V2）
        const Track& track = f.project.sequences[0].tracks[1];
        NC_CHECK_EQ(track.clips.size(), std::size_t(1));
        NC_CHECK(f.editor.setClipBlendMode(f.seqId, f.topTrack, track.clips[0].id, mode).isOk());
        const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
        ColorFactory factory;
        int result = -1;
        executeExport(
            plan, factory,
            [&](const OutputVideoFrame& frame) {
                if (result < 0) result = frame.rgba[0];
            },
            nullptr, nullptr, ExportExecutorOptions{}, &f.project);
        return result;
    };
    const int normal = runBlend(BlendMode::Normal);
    const int screen = runBlend(BlendMode::Screen);
    const int darken = runBlend(BlendMode::Darken);
    const int overlay = runBlend(BlendMode::Overlay);
    const int hardLight = runBlend(BlendMode::HardLight);
    std::printf("[实测] 多层混合: normal=%d screen=%d darken=%d overlay=%d hardLight=%d\n",
                normal, screen, darken, overlay, hardLight);
    // Normal：上层遮挡 → 输出 = 上层值 100
    NC_CHECK_EQ(normal, 100);
    // Screen：明显更亮（滤色提亮语义）
    NC_CHECK(screen > 200);
    // Darken：min(200,100) = 100（与 Normal 巧合相同——上层更暗）
    NC_CHECK_EQ(darken, 100);
    // Overlay：底图亮 → 结果应比纯遮挡(100)亮
    NC_CHECK(overlay > 150);
    // HardLight：上层暗(<0.5) → 2*top*base = 2*(100/255)*(200/255) ≈ 0.615 → 157
    NC_CHECK(hardLight > 100 && hardLight < 200);
}
