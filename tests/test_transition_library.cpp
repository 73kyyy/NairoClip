// =============================================================================
// 转场库测试（文档 Part 4-B 第 6 节「转场」9 分类 / 30+ 预设）
// =============================================================================
// 覆盖：
//   1. 预设清单（文档逐字：基础/叠化/运镜/扭曲/故障/光效/几何/粒子/复古）；
//   2. 分类映射与方向选择器适用性（文档 6.3）；
//   3. 渲染真实性（每种转场都有真实像素输出；进度 0/1 端点语义正确）；
//   4. 旧 8 种转场向后兼容（枚举值未变）；
//   5. C ABI（类型/名称/分类查询）。
#include "test_framework.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/transition.h"
#include "nairoclip/core/transition_render.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr int kW = 48;
constexpr int kH = 32;

/// A = 纯红，B = 纯蓝（便于判定各转场是否真实参与混合）。
std::vector<std::uint8_t> makeFrameA() {
    std::vector<std::uint8_t> f(static_cast<std::size_t>(kW) * kH * 4, 255);
    for (std::size_t i = 0; i < f.size(); i += 4) {
        f[i] = 220;
        f[i + 1] = 30;
        f[i + 2] = 30;
    }
    return f;
}
std::vector<std::uint8_t> makeFrameB() {
    std::vector<std::uint8_t> f(static_cast<std::size_t>(kW) * kH * 4, 255);
    for (std::size_t i = 0; i < f.size(); i += 4) {
        f[i] = 30;
        f[i + 1] = 30;
        f[i + 2] = 220;
    }
    return f;
}

/// 与 A 的平均差异（0 = 完全是 A；> 0 = 有变化）。
double meanDiffFrom(const std::vector<std::uint8_t>& frame,
                    const std::vector<std::uint8_t>& reference) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < frame.size() && i < reference.size(); i += 4) {
        sum += std::abs(static_cast<int>(frame[i]) - static_cast<int>(reference[i]));
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 预设清单（文档逐字）
// ---------------------------------------------------------------------------

NC_TEST(TransitionLibrary, PresetNamesMatchDocument) {
    // 基础
    NC_CHECK_EQ(std::string(transitionName(TransitionType::CrossDissolve)),
                std::string("交叉溶解"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::FlashWhite)), std::string("闪白"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::FadeToBlack)),
                std::string("淡入黑场"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Cut)), std::string("硬切"));
    // 叠化
    NC_CHECK_EQ(std::string(transitionName(TransitionType::ColorDissolve)),
                std::string("色彩叠化"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::BlurDissolve)),
                std::string("模糊叠化"));
    // 运镜
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Zoom)), std::string("缩放"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::ZoomOut)), std::string("拉远"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Rotate)), std::string("旋转"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::WhipPan)), std::string("甩镜"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::ZoomPunch)),
                std::string("缩放冲击"));
    // 扭曲
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Ripple)), std::string("波纹"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Liquify)), std::string("液化"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Fisheye)), std::string("鱼眼"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::GlitchWarp)),
                std::string("故障扭曲"));
    // 故障
    NC_CHECK_EQ(std::string(transitionName(TransitionType::RgbSplit)), std::string("RGB分离"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::SignalNoise)),
                std::string("信号干扰"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::DigitalNoise)),
                std::string("数字噪点"));
    // 光效
    NC_CHECK_EQ(std::string(transitionName(TransitionType::LightSweep)),
                std::string("光晕扫过"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::LensFlare)),
                std::string("镜头光斑"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::ExposureFlicker)),
                std::string("曝光闪烁"));
    // 几何
    NC_CHECK_EQ(std::string(transitionName(TransitionType::CircleWipe)),
                std::string("圆形擦除"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Blinds)), std::string("百叶窗"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::MosaicSplit)),
                std::string("马赛克分割"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::Pixelate)), std::string("像素化"));
    // 粒子
    NC_CHECK_EQ(std::string(transitionName(TransitionType::ParticleDissolve)),
                std::string("粒子消散"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::SmokeTransition)),
                std::string("烟雾过渡"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::SparkleGather)),
                std::string("光点聚散"));
    // 复古
    NC_CHECK_EQ(std::string(transitionName(TransitionType::FilmFlicker)),
                std::string("胶片闪烁"));
    NC_CHECK_EQ(std::string(transitionName(TransitionType::OldFilmScratch)),
                std::string("老电影划痕转场"));
}

NC_TEST(TransitionLibrary, CategoryMapping) {
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::CrossDissolve)),
                std::string("基础"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::FlashWhite)), std::string("基础"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::Cut)), std::string("基础"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::ColorDissolve)),
                std::string("叠化"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::ZoomOut)), std::string("运镜"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::Rotate)), std::string("运镜"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::Ripple)), std::string("扭曲"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::RgbSplit)), std::string("故障"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::LightSweep)), std::string("光效"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::Blinds)), std::string("几何"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::SmokeTransition)),
                std::string("粒子"));
    NC_CHECK_EQ(std::string(transitionCategory(TransitionType::FilmFlicker)),
                std::string("复古"));
}

NC_TEST(TransitionLibrary, DirectionSupport) {
    // 文档 6.3：推拉/擦除/百叶窗类显示方向选择器
    NC_CHECK(transitionSupportsDirection(TransitionType::Zoom));
    NC_CHECK(transitionSupportsDirection(TransitionType::ZoomOut));
    NC_CHECK(transitionSupportsDirection(TransitionType::WipeLeft));
    NC_CHECK(transitionSupportsDirection(TransitionType::WipeRight));
    NC_CHECK(transitionSupportsDirection(TransitionType::Blinds));
    NC_CHECK(transitionSupportsDirection(TransitionType::CircleWipe));
    // 叠化/闪白等无方向
    NC_CHECK(!transitionSupportsDirection(TransitionType::CrossDissolve));
    NC_CHECK(!transitionSupportsDirection(TransitionType::FlashWhite));
    NC_CHECK(!transitionSupportsDirection(TransitionType::Ripple));
    NC_CHECK(!transitionSupportsDirection(TransitionType::FilmFlicker));
}

NC_TEST(TransitionLibrary, DurationRange) {
    // 文档 6.2：0.1s ~ 2.0s
    NC_CHECK(transitionDurationValid(Rational::make(1, 2)));   // 0.5s
    NC_CHECK(transitionDurationValid(Rational::make(1, 10)));  // 0.1s（边界）
    NC_CHECK(transitionDurationValid(Rational::make(2)));      // 2.0s（边界）
    NC_CHECK(!transitionDurationValid(Rational::make(1, 20))); // 0.05s（过短）
    NC_CHECK(!transitionDurationValid(Rational::make(3)));     // 3s（过长）
}

// ---------------------------------------------------------------------------
// 2. 渲染真实性（每种转场都有真实像素输出）
// ---------------------------------------------------------------------------

NC_TEST(TransitionLibrary, AllTypesRenderRealOutput) {
    const std::vector<std::uint8_t> a = makeFrameA();
    const std::vector<std::uint8_t> b = makeFrameB();
    const int total = static_cast<int>(TransitionType::OldFilmScratch) + 1;
    for (int typeValue = 0; typeValue < total; ++typeValue) {
        const auto type = static_cast<TransitionType>(typeValue);
        std::vector<std::uint8_t> out(a.size(), 0);
        // 中点进度（0.5）：所有转场都应产生真实输出
        NC_CHECK(TransitionRenderer::renderFrame(type, a.data(), b.data(), out.data(), kW, kH,
                                                 0.5));
        // 硬切为中点切换（输出为 A 或 B 之一）；淡入黑场中点为纯黑（语义正确）；
        // 其余应与 A 有差异
        if (type == TransitionType::Cut) {
            const double diffA = meanDiffFrom(out, a);
            const double diffB = meanDiffFrom(out, b);
            NC_CHECK(diffA < 1.0 || diffB < 1.0);  // 完全等于 A 或 B
        } else {
            const double diff = meanDiffFrom(out, a);
            NC_CHECK(diff > 0.5);  // 真实混合（非原始 A）
        }
        // 所有类型：不允许输出全透明
        // 说明：FadeToBlack 中点为纯黑（0,0,0）属**正确语义**（黑场），
        //       因此这里校验 alpha 通道有效（帧可用）而非 RGB 非零。
        bool alphaValid = true;
        for (std::size_t i = 3; i < out.size(); i += 4) {
            if (out[i] == 0) {
                alphaValid = false;
                break;
            }
        }
        NC_CHECK(alphaValid);
        // 非黑场类转场：输出不应全黑（真实内容）
        if (type != TransitionType::FadeToBlack) {
            bool allZero = true;
            for (std::size_t i = 0; i < out.size(); i += 4) {
                if (out[i] != 0 || out[i + 1] != 0 || out[i + 2] != 0) {
                    allZero = false;
                    break;
                }
            }
            NC_CHECK(!allZero);
        }
    }
}

NC_TEST(TransitionLibrary, EndpointSemantics) {
    const std::vector<std::uint8_t> a = makeFrameA();
    const std::vector<std::uint8_t> b = makeFrameB();
    // 进度 0：接近 A（允许轻度效果差异）
    const auto type = TransitionType::CrossDissolve;
    std::vector<std::uint8_t> out0(a.size(), 0);
    NC_CHECK(TransitionRenderer::renderFrame(type, a.data(), b.data(), out0.data(), kW, kH, 0.0));
    NC_CHECK(meanDiffFrom(out0, a) < 1.0);
    // 进度 1：等于 B
    std::vector<std::uint8_t> out1(a.size(), 0);
    NC_CHECK(TransitionRenderer::renderFrame(type, a.data(), b.data(), out1.data(), kW, kH, 1.0));
    NC_CHECK(meanDiffFrom(out1, b) < 1.0);
}

NC_TEST(TransitionLibrary, Deterministic) {
    const std::vector<std::uint8_t> a = makeFrameA();
    const std::vector<std::uint8_t> b = makeFrameB();
    // 取几个含噪声的转场（噪点/干扰/胶片）
    const TransitionType noisy[] = {TransitionType::DigitalNoise,
                                    TransitionType::SignalNoise,
                                    TransitionType::FilmFlicker,
                                    TransitionType::GlitchWarp};
    for (const auto type : noisy) {
        std::vector<std::uint8_t> out1(a.size(), 0);
        std::vector<std::uint8_t> out2(a.size(), 0);
        NC_CHECK(TransitionRenderer::renderFrame(type, a.data(), b.data(), out1.data(), kW, kH,
                                                 0.42));
        NC_CHECK(TransitionRenderer::renderFrame(type, a.data(), b.data(), out2.data(), kW, kH,
                                                 0.42));
        bool identical = true;
        for (std::size_t i = 0; i < out1.size() && identical; ++i) {
            if (out1[i] != out2[i]) identical = false;
        }
        NC_CHECK(identical);  // 同一 (a, b, t) → 同一输出
    }
}

NC_TEST(TransitionLibrary, BackwardCompatibleEnumValues) {
    // 既有 8 种枚举值不得变更（.nclip 向后兼容）
    NC_CHECK_EQ(static_cast<int>(TransitionType::CrossDissolve), 0);
    NC_CHECK_EQ(static_cast<int>(TransitionType::FadeToBlack), 1);
    NC_CHECK_EQ(static_cast<int>(TransitionType::FadeFromBlack), 2);
    NC_CHECK_EQ(static_cast<int>(TransitionType::FlashWhite), 3);
    NC_CHECK_EQ(static_cast<int>(TransitionType::WipeLeft), 4);
    NC_CHECK_EQ(static_cast<int>(TransitionType::WipeRight), 5);
    NC_CHECK_EQ(static_cast<int>(TransitionType::SlideUp), 6);
    NC_CHECK_EQ(static_cast<int>(TransitionType::Zoom), 7);
    // 新增区为追加（8..33）
    NC_CHECK_EQ(static_cast<int>(TransitionType::Cut), 8);
    NC_CHECK_EQ(static_cast<int>(TransitionType::OldFilmScratch), 33);
}

// ---------------------------------------------------------------------------
// 3. C ABI
// ---------------------------------------------------------------------------

NC_TEST(TransitionLibrary, CapiTypeQueries) {
    NC_CHECK_EQ(nc_transition_type_count(), 34);
    // 名称（文档逐字）
    NC_CHECK_EQ(std::string(nc_transition_type_name(0)), std::string("交叉溶解"));
    NC_CHECK_EQ(std::string(nc_transition_type_name(8)), std::string("硬切"));
    NC_CHECK_EQ(std::string(nc_transition_type_name(33)), std::string("老电影划痕转场"));
    NC_CHECK_EQ(std::string(nc_transition_type_name(99)), std::string("未知转场"));
    // 分类
    NC_CHECK_EQ(std::string(nc_transition_category(0)), std::string("基础"));
    NC_CHECK_EQ(std::string(nc_transition_category(13)), std::string("运镜"));
    NC_CHECK_EQ(std::string(nc_transition_category(33)), std::string("复古"));
    NC_CHECK_EQ(std::string(nc_transition_category(-1)), std::string(""));
    // 方向支持
    NC_CHECK_EQ(nc_transition_supports_direction(7), 1);   // 缩放（推近）
    NC_CHECK_EQ(nc_transition_supports_direction(0), 0);   // 交叉溶解
    NC_CHECK_EQ(nc_transition_supports_direction(99), 0);
}

NC_TEST(TransitionLibrary, CapiAddTransitionWithNewType) {
    // 新转场可经 C ABI 添加到时间轴（端到端链路）
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "v.mp4", "/v/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    // 两段相邻片段（带 0.5s 间隙供转场）
    const std::uint64_t clip1 = nc_insert_clip(editor, 0, asset, 0, 2000000);
    const std::uint64_t clip2 = nc_insert_clip(editor, 0, asset, 2500000, 2000000);
    NC_CHECK(clip1 != 0);
    NC_CHECK(clip2 != 0);
    // 添加「粒子消散」转场（类型 29；时长 0.5s）
    const std::uint64_t transition =
        nc_add_transition(editor, 0, 29, 500000, clip1, clip2);
    NC_CHECK(transition != 0);
    NC_CHECK_EQ(nc_transition_count(editor, 0), 1);
    // 回读名称
    std::int32_t type = -1;
    char* name = nullptr;
    NC_CHECK_EQ(nc_transition_get(editor, 0, 0, &type, nullptr, nullptr, nullptr, &name), NC_OK);
    NC_CHECK_EQ(type, 29);
    NC_CHECK_EQ(std::string(name != nullptr ? name : ""), std::string("粒子消散"));
    nc_free_string(name);
    nc_editor_destroy(editor);
}
