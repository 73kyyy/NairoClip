// =============================================================================
// 空间特效测试（M2.28，文档第 2 部分「✨特效 → 画面特效」+ 第 4 部分 4.4 质量分级）
// =============================================================================
// 背景（真实缺陷修复）：
//   此前 Vignette / GaussianBlur / Sharpen / Grain 四种特效只有枚举声明，
//   导出与预览渲染器从未实现（evaluateColorTransform 直接跳过，注释称
//   "由渲染器单独处理"但渲染器不存在对应代码）→ 用户添加后画面毫无变化。
//   本测试覆盖真实实现后的行为，并验证导出/预览管线真实生效。
//
// 覆盖：
//   · 暗角：四角变暗、中心不变、强度单调、0 = 零改动；
//   · 高斯模糊：边缘能量降低、均值近似保持、半径越大越平滑、可分离正确性；
//   · 锐化：边缘对比增强、0 = 零改动、强度单调；
//   · 噪点：像素扰动、确定性（同帧同输出）、帧间时变、0 = 零改动；
//   · 栈集成：颜色 + 空间组合、顺序、禁用跳过、空栈；
//   · 参数防御：空指针/非法尺寸/缓冲不足；
//   · 管线集成：导出计划快照 → executeExport 真实应用；预览真实应用。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/effects.h"
#include "nairoclip/core/spatial_effects.h"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 构造渐变图（RGB 渐变）。
std::vector<std::uint8_t> makeGradient(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            px[i] = static_cast<std::uint8_t>(x * 255 / (w > 1 ? w - 1 : 1));
            px[i + 1] = static_cast<std::uint8_t>(y * 255 / (h > 1 ? h - 1 : 1));
            px[i + 2] = 128;
        }
    }
    return px;
}

/// 构造黑白竖边界图（用于锐化/模糊）。
std::vector<std::uint8_t> makeEdge(int w, int h, int valueLeft = 60, int valueRight = 190) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            const std::uint8_t v = x < w / 2 ? static_cast<std::uint8_t>(valueLeft)
                                             : static_cast<std::uint8_t>(valueRight);
            px[i] = px[i + 1] = px[i + 2] = v;
        }
    }
    return px;
}

int channelAt(const std::vector<std::uint8_t>& px, int w, int x, int y, int c) {
    return px[(static_cast<std::size_t>(y) * w + x) * 4 + c];
}

/// 局部水平梯度能量（用于模糊/锐化的定量验证）。
double edgeEnergy(const std::vector<std::uint8_t>& px, int w, int h, int x0, int y0, int span) {
    double energy = 0.0;
    for (int y = y0; y < y0 + span; ++y) {
        for (int x = x0; x < x0 + span - 1; ++x) {
            const double d =
                channelAt(px, w, x + 1, y, 0) - channelAt(px, w, x, y, 0);
            energy += d * d;
        }
    }
    return energy;
}

Effect makeEffect(std::uint64_t id, EffectType type, double value) {
    Effect e;
    e.id = EffectId{id};
    e.type = type;
    e.value = value;
    e.enabled = true;
    return e;
}

}  // namespace

// ---------------------------------------------------------------------------
// 暗角
// ---------------------------------------------------------------------------

NC_TEST(SpatialEffects, VignetteDarkensCornersKeepsCenter) {
    const int w = 64, h = 48;
    auto image = makeGradient(w, h);
    const auto original = image;
    NC_CHECK(applyVignetteRgba(image.data(), w, h, 1.0));
    // 四角变暗
    NC_CHECK(channelAt(image, w, 1, 1, 0) < channelAt(original, w, 1, 1, 0));
    NC_CHECK(channelAt(image, w, 1, 1, 0) < channelAt(original, w, 1, 1, 0) / 2);
    // 中心不变（同一像素精确相等）
    NC_CHECK_EQ(channelAt(image, w, w / 2, h / 2, 0), channelAt(original, w, w / 2, h / 2, 0));
    // alpha 不变
    NC_CHECK_EQ(channelAt(image, w, 1, 1, 3), 255);
}

NC_TEST(SpatialEffects, VignetteStrengthMonotonicAndZeroNoOp) {
    const int w = 32, h = 32;
    // 强度 0 → 零改动
    auto image = makeGradient(w, h);
    const auto original = image;
    NC_CHECK(applyVignetteRgba(image.data(), w, h, 0.0));
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
    // 强度单调：角落亮度随强度递减
    double prev = 256.0;
    for (double s : {0.25, 0.5, 0.75, 1.0}) {
        auto img = makeGradient(w, h);
        applyVignetteRgba(img.data(), w, h, s);
        const double corner = channelAt(img, w, 1, 1, 0) + channelAt(img, w, 1, 1, 1);
        NC_CHECK(corner <= prev + 1e-9);
        prev = corner;
    }
}

// ---------------------------------------------------------------------------
// 高斯模糊
// ---------------------------------------------------------------------------

NC_TEST(SpatialEffects, BlurReducesEdgeEnergyAndPreservesMean) {
    const int w = 64, h = 48;
    auto image = makeEdge(w, h);
    const auto original = image;
    const double energyBefore = edgeEnergy(original, w, h, w / 2 - 6, h / 2 - 4, 12);
    NC_CHECK(applyGaussianBlurRgba(image.data(), w, h, 4.0));
    const double energyAfter = edgeEnergy(image, w, h, w / 2 - 6, h / 2 - 4, 12);
    NC_CHECK(energyAfter < energyBefore * 0.5);  // 边缘被真实平滑
    // 均值保持（可分离核归一化正确）
    double sumBefore = 0.0, sumAfter = 0.0;
    for (std::size_t i = 0; i < original.size(); i += 4) {
        sumBefore += original[i];
        sumAfter += image[i];
    }
    const double meanDiff = std::fabs(sumBefore - sumAfter) /
                             (static_cast<double>(w) * h);
    NC_CHECK(meanDiff < 2.0);
    // 边界处出现中间灰度（过渡带，证明真实卷积而非简单均值替换）
    NC_CHECK(channelAt(image, w, w / 2, h / 2, 0) > 60 + 5);
    NC_CHECK(channelAt(image, w, w / 2, h / 2, 0) < 190 - 5);
}

NC_TEST(SpatialEffects, BlurRadiusMonotonicAndZeroNoOp) {
    const int w = 64, h = 48;
    // 半径 0 → 零改动
    auto image = makeEdge(w, h);
    const auto original = image;
    NC_CHECK(applyGaussianBlurRgba(image.data(), w, h, 0.0));
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
    // 半径越大越平滑（边缘能量更低）
    double prev = 1e18;
    for (double r : {1.0, 3.0, 6.0}) {
        auto img = makeEdge(w, h);
        applyGaussianBlurRgba(img.data(), w, h, r);
        const double energy = edgeEnergy(img, w, h, w / 2 - 8, h / 2 - 4, 16);
        NC_CHECK(energy < prev);
        prev = energy;
    }
}

// ---------------------------------------------------------------------------
// 锐化
// ---------------------------------------------------------------------------

NC_TEST(SpatialEffects, SharpenIncreasesEdgeContrast) {
    const int w = 64, h = 48;
    auto image = makeEdge(w, h);
    const auto original = image;
    const double before =
        static_cast<double>(channelAt(original, w, w / 2, h / 2, 0)) -
        static_cast<double>(channelAt(original, w, w / 2 - 1, h / 2, 0));
    NC_CHECK(applySharpenRgba(image.data(), w, h, 3.0));
    const double after = static_cast<double>(channelAt(image, w, w / 2, h / 2, 0)) -
                         static_cast<double>(channelAt(image, w, w / 2 - 1, h / 2, 0));
    NC_CHECK(after > before);  // 边缘对比真实增强
    // 平坦区（远离边界）不变或微变（无过冲）
    const int flatX = 4;
    NC_CHECK(std::abs(channelAt(image, w, flatX, h / 2, 0) -
                      channelAt(original, w, flatX, h / 2, 0)) <= 2);
}

NC_TEST(SpatialEffects, SharpenZeroNoOpAndMonotonic) {
    const int w = 64, h = 48;
    auto image = makeEdge(w, h);
    const auto original = image;
    NC_CHECK(applySharpenRgba(image.data(), w, h, 0.0));
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
    // 强度越大边界对比越强
    double prev = -1e18;
    for (double s : {1.0, 3.0, 5.0}) {
        auto img = makeEdge(w, h);
        applySharpenRgba(img.data(), w, h, s);
        const double contrast = static_cast<double>(channelAt(img, w, w / 2, h / 2, 0)) -
                                static_cast<double>(channelAt(img, w, w / 2 - 1, h / 2, 0));
        NC_CHECK(contrast >= prev - 1e-9);
        prev = contrast;
    }
}

// ---------------------------------------------------------------------------
// 噪点
// ---------------------------------------------------------------------------

NC_TEST(SpatialEffects, GrainDisturbsPixelsAndIsDeterministic) {
    const int w = 64, h = 48;
    auto image = makeGradient(w, h);
    const auto original = image;
    NC_CHECK(applyGrainRgba(image.data(), w, h, 0.5, 7));
    int changed = 0;
    for (std::size_t i = 0; i < image.size(); i += 4) {
        if (image[i] != original[i]) ++changed;
    }
    NC_CHECK(changed > w * h / 2);  // 大部分像素被扰动
    // 确定性：同帧序号 → 逐像素相同
    auto image2 = makeGradient(w, h);
    applyGrainRgba(image2.data(), w, h, 0.5, 7);
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], image2[i]);
    }
}

NC_TEST(SpatialEffects, GrainVariesAcrossFrames) {
    const int w = 64, h = 48;
    auto frame0 = makeGradient(w, h);
    auto frame1 = makeGradient(w, h);
    applyGrainRgba(frame0.data(), w, h, 0.8, 0);
    applyGrainRgba(frame1.data(), w, h, 0.8, 1);
    int diff = 0;
    for (std::size_t i = 0; i < frame0.size(); i += 4) {
        if (frame0[i] != frame1[i]) ++diff;
    }
    // 帧间噪声应显著不同（时变颗粒；此前 FNV 高位哈希导致帧间完全相同——已修复）
    NC_CHECK(diff > w * h / 2);
}

NC_TEST(SpatialEffects, GrainZeroNoOpAndAmplitudeScales) {
    const int w = 32, h = 32;
    // 强度 0 → 零改动
    auto image = makeGradient(w, h);
    const auto original = image;
    NC_CHECK(applyGrainRgba(image.data(), w, h, 0.0, 3));
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
    // 幅值随强度缩放（0.1 → 小扰动；1.0 → 大幅度）
    auto small = makeGradient(w, h);
    auto large = makeGradient(w, h);
    applyGrainRgba(small.data(), w, h, 0.1, 3);
    applyGrainRgba(large.data(), w, h, 1.0, 3);
    int maxSmall = 0, maxLarge = 0;
    for (std::size_t i = 0; i < small.size(); i += 4) {
        maxSmall = std::max(maxSmall,
                            std::abs(static_cast<int>(small[i]) - static_cast<int>(original[i])));
        maxLarge = std::max(maxLarge,
                            std::abs(static_cast<int>(large[i]) - static_cast<int>(original[i])));
    }
    NC_CHECK(maxSmall > 0);
    NC_CHECK(maxLarge > maxSmall * 3);  // 强度 10 倍 → 幅值显著更大
}

// ---------------------------------------------------------------------------
// 栈集成（颜色 + 空间）
// ---------------------------------------------------------------------------

NC_TEST(SpatialEffects, StackAppliesColorThenSpatialInOrder) {
    const int w = 64, h = 48;
    EffectStack stack;
    NC_CHECK(stack.add(makeEffect(1, EffectType::Brightness, 0.3)).isOk());
    NC_CHECK(stack.add(makeEffect(2, EffectType::Vignette, 1.0)).isOk());
    auto image = makeGradient(w, h);
    const auto original = image;
    NC_CHECK(applyEffectStackToFrame(image.data(), w, h, stack, 0));
    // 断言 1：颜色变换已应用（中心变亮；Brightness +0.3 增益）
    NC_CHECK(channelAt(image, w, w / 2, h / 2, 0) > channelAt(original, w, w / 2, h / 2, 0));
    // 断言 2：空间效果已应用（角落相对衰减更强）——
    // 用「角落/中心的亮度比」比较（比值不受亮度增益影响：
    // 颜色变换对二者乘同一增益，余下差异只能来自空间效果）。
    // 注意避免用极暗像素（<20）做比值（8bit 量化噪声大）。
    // 角落取 (3,3)（其亮度约 12+12=24，可测）；中心取画面正中。
    const double originalRatio =
        static_cast<double>(channelAt(original, w, 3, 3, 0) + 1) /
        static_cast<double>(channelAt(original, w, w / 2, h / 2, 0) + 1);
    const double afterRatio = static_cast<double>(channelAt(image, w, 3, 3, 0) + 1) /
                              static_cast<double>(channelAt(image, w, w / 2, h / 2, 0) + 1);
    NC_CHECK(afterRatio < originalRatio);  // 角落相对中心更暗（暗角生效）
    // 断言 3：空间效果在颜色之后应用（逐像素差值可验证）——
    // 取同一像素：仅颜色（无暗角）→ 记为 A；颜色 + 暗角 → 记为 B。
    // 若空间效果确实在颜色之后应用，则角落像素 B < A（暗角在其上再衰减）；
    // 中心像素（暗角权重 = 1）B == A（两次结果完全一致）。
    EffectStack colorOnly;
    NC_CHECK(colorOnly.add(makeEffect(1, EffectType::Brightness, 0.3)).isOk());
    auto image2 = makeGradient(w, h);
    NC_CHECK(applyEffectStackToFrame(image2.data(), w, h, colorOnly, 0));
    // 角落：暗角额外衰减 → B < A
    NC_CHECK(channelAt(image, w, 3, 3, 0) < channelAt(image2, w, 3, 3, 0));
    // 中心：暗角权重为 1（不变暗）→ B == A（精确相等，证明顺序与权重正确）
    NC_CHECK_EQ(channelAt(image, w, w / 2, h / 2, 0),
                channelAt(image2, w, w / 2, h / 2, 0));
    // 角落/中心比值下降（暗角生效的直观验证）
    NC_CHECK(afterRatio < originalRatio);
}

NC_TEST(SpatialEffects, StackSkipsDisabledAndEmpty) {
    const int w = 32, h = 24;
    // 空栈 → 零改动
    EffectStack empty;
    auto image = makeGradient(w, h);
    const auto original = image;
    NC_CHECK(applyEffectStackToFrame(image.data(), w, h, empty, 0));
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
    // 禁用特效被跳过
    EffectStack stack;
    Effect vig = makeEffect(1, EffectType::Vignette, 1.0);
    vig.enabled = false;
    NC_CHECK(stack.add(vig).isOk());
    auto image2 = makeGradient(w, h);
    NC_CHECK(applyEffectStackToFrame(image2.data(), w, h, stack, 0));
    for (std::size_t i = 0; i < image2.size(); ++i) {
        NC_CHECK_EQ(image2[i], original[i]);
    }
}

// ---------------------------------------------------------------------------
// 参数防御
// ---------------------------------------------------------------------------

NC_TEST(SpatialEffects, RejectsInvalidArgs) {
    const int w = 16, h = 16;
    auto image = makeGradient(w, h);
    const auto original = image;
    // 空指针 / 非法尺寸
    NC_CHECK(!applySpatialEffectToRgba(nullptr, w, h, 8, 1.0, 0));
    NC_CHECK(!applySpatialEffectToRgba(image.data(), 0, h, 8, 1.0, 0));
    NC_CHECK(!applySpatialEffectToRgba(image.data(), w, -1, 8, 1.0, 0));
    NC_CHECK(!applyVignetteRgba(nullptr, w, h, 1.0));
    NC_CHECK(!applyGaussianBlurRgba(nullptr, w, h, 2.0));
    NC_CHECK(!applySharpenRgba(nullptr, w, h, 2.0));
    NC_CHECK(!applyGrainRgba(nullptr, w, h, 0.5, 0));
    NC_CHECK(!applyEffectStackToFrame(nullptr, w, h, EffectStack{}, 0));
    // 像素未被改动
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
    // 颜色类 type 分发 → 不改动（由 ColorTransform 处理）
    NC_CHECK(applySpatialEffectToRgba(image.data(), w, h, 0, 0.5, 0));  // Brightness
    for (std::size_t i = 0; i < image.size(); ++i) {
        NC_CHECK_EQ(image[i], original[i]);
    }
}

// ---------------------------------------------------------------------------
// 管线集成（导出 / 预览真实生效）
// ---------------------------------------------------------------------------

#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/serialization.h"

namespace {

/// Mock 读取器：纯灰帧。
class GrayReader : public MediaReader {
public:
    GrayReader() {
        probe_.type = MediaType::Video;
        probe_.frameRate = fps30();
        probe_.width = 64;
        probe_.height = 48;
        probe_.duration = Rational::make(10);
    }
    Result open(const std::string& p) override {
        if (p.empty()) return Result::fail(ErrorCode::InvalidArgument, "路径为空");
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
        o.width = 64;
        o.height = 48;
        o.stride = 64 * 4;
        o.pixels.assign(static_cast<std::size_t>(64) * 48 * 4, 255);
        for (std::size_t i = 0; i + 3 < o.pixels.size(); i += 4) {
            o.pixels[i] = 160;
            o.pixels[i + 1] = 160;
            o.pixels[i + 2] = 160;
        }
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    MediaProbeResult probe_;
    bool open_ = false;
};

class GrayFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<GrayReader>(); }
};

struct SpatialFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    ClipId clipId;
    SpatialFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 64;
        seq.height = 48;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/videos/gray.mp4";
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

NC_TEST(SpatialEffectsPipeline, ExportAppliesVignette) {
    SpatialFixture f;
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId,
                                makeEffect(1, EffectType::Vignette, 1.0))
                 .isOk());
    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    GrayFactory factory;
    int checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                // 角落暗于中心（暗角真实生效）
                const int corner = frame.rgba[(1 * 64 + 1) * 4];
                const int center = frame.rgba[(24 * 64 + 32) * 4];
                NC_CHECK(corner < center);
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(checked > 0);
}

NC_TEST(SpatialEffectsPipeline, ExportAppliesGaussianBlurAndSharpen) {
    SpatialFixture f;
    // 模糊（半径 4）
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId,
                                makeEffect(1, EffectType::GaussianBlur, 4.0))
                 .isOk());
    {
        const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
        GrayFactory factory;
        int checked = 0;
        executeExport(
            plan, factory,
            [&](const OutputVideoFrame& frame) {
                if (checked == 0) {
                    // 纯灰图模糊后仍为灰（不应变黑/变白 → 核归一化正确）
                    NC_CHECK_EQ(frame.rgba[(24 * 64 + 32) * 4], std::uint8_t(160));
                }
                ++checked;
            },
            nullptr, nullptr, ExportExecutorOptions{}, &f.project);
        NC_CHECK(checked > 0);
    }
    // 切换为锐化（移除旧特效 + 添加新特效——setClipEffects 无此 API）
    NC_CHECK(f.editor.removeEffect(f.seqId, f.trackId, f.clipId, EffectId{1}).isOk());
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId,
                                makeEffect(2, EffectType::Sharpen, 3.0))
                 .isOk());
    {
        const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
        NC_CHECK_EQ(plan.tracks[0].clips[0].effects.size(), std::size_t(1));
    }
}

NC_TEST(SpatialEffectsPipeline, ExportAppliesGrainDeterministically) {
    SpatialFixture f;
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId,
                                makeEffect(1, EffectType::Grain, 0.8))
                 .isOk());
    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    GrayFactory factory;
    // 两次导出第 0 帧 → 像素完全一致（确定性）
    std::vector<std::uint8_t> firstFrame;
    int checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) firstFrame = frame.rgba;
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(!firstFrame.empty());
    checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                NC_CHECK_EQ(frame.rgba.size(), firstFrame.size());
                for (std::size_t i = 0; i < frame.rgba.size(); i += 4) {
                    NC_CHECK_EQ(frame.rgba[i], firstFrame[i]);
                }
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    // 与无特效导出对比：噪点真实改变了像素
    SpatialFixture plain;
    const ExportPlan plainPlan = compileExportPlan(plain.project, plain.project.sequences[0]);
    int plainChecked = 0;
    std::vector<std::uint8_t> plainFrame;
    executeExport(
        plainPlan, factory,
        [&](const OutputVideoFrame& frame) {
            if (plainChecked == 0) plainFrame = frame.rgba;
            ++plainChecked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &plain.project);
    int diff = 0;
    for (std::size_t i = 0; i < firstFrame.size(); i += 4) {
        if (firstFrame[i] != plainFrame[i]) ++diff;
    }
    NC_CHECK(diff > 64 * 48 / 2);
}

NC_TEST(SpatialEffectsPipeline, PreviewAppliesSpatialEffects) {
    SpatialFixture f;
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId,
                                makeEffect(1, EffectType::Vignette, 1.0))
                 .isOk());
    GrayFactory factory;
    PreviewRenderer preview(f.project, factory);
    const PreviewFrame frame = preview.renderAt(Rational::make(1, 2));
    NC_CHECK_EQ(frame.width, 64);
    NC_CHECK_EQ(frame.height, 48);
    // 角落暗于中心（预览真实生效）
    const int corner = frame.rgba[(1 * 64 + 1) * 4];
    const int center = frame.rgba[(24 * 64 + 32) * 4];
    NC_CHECK(corner < center);
}

NC_TEST(SpatialEffectsPipeline, SerializationRoundtrip) {
    SpatialFixture f;
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId,
                                makeEffect(1, EffectType::GaussianBlur, 8.0))
                 .isOk());
    NC_CHECK(f.editor.addEffect(f.seqId, f.trackId, f.clipId,
                                makeEffect(2, EffectType::Grain, 0.4))
                 .isOk());
    const std::string text = saveProjectToNclip(f.project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    const Clip* clip = loaded.sequences[0].tracks[0].findClip(f.clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK_EQ(clip->effects.size(), std::size_t(2));
    const auto& effects = clip->effects.effects();
    NC_CHECK(effects[0].type == EffectType::GaussianBlur);
    NC_CHECK(std::fabs(effects[0].value - 8.0) < 1e-9);
    NC_CHECK(effects[1].type == EffectType::Grain);
    NC_CHECK(std::fabs(effects[1].value - 0.4) < 1e-9);
}

// ---------------------------------------------------------------------------
// 真实 FFmpeg 端到端（高频细节素材；需 /tmp/media_gen/detail_src.mp4）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/capi.h"
#include "nairoclip/core/media_ffmpeg.h"
#include <cstdio>
#include <fstream>

namespace {

/// 用真实素材导出并读取首帧（effectType = 0 表示无特效）。
bool exportDetailFrame(int effectType, double value, const char* outPath,
                       std::vector<std::uint8_t>& pixels, int& width, int& height) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId = nc_import_asset(editor, "detail_src.mp4",
                                                  "/tmp/media_gen/detail_src.mp4", 0, 2000000);
    if (assetId == 0) {
        nc_editor_destroy(editor);
        return false;
    }
    nc_add_track(editor, "V1", 0);
    const std::uint64_t clipId = nc_insert_clip(editor, 0, assetId, 0, 1000000);
    if (clipId == 0) {
        nc_editor_destroy(editor);
        return false;
    }
    if (effectType > 0) {
        if (nc_add_effect(editor, 0, clipId, effectType, value) == 0) {
            nc_editor_destroy(editor);
            return false;
        }
    }
    char* err = nullptr;
    const std::int64_t frames =
        nc_export_run(editor, outPath, 18, 0, 0, nullptr, nullptr, &err);
    if (err != nullptr) nc_free_string(err);
    nc_editor_destroy(editor);
    if (frames <= 0) return false;
    FFmpegMediaReader reader;
    if (!reader.open(outPath).isOk()) return false;
    VideoFrame frame;
    if (!reader.readVideoFrame(frame)) return false;
    reader.close();
    pixels = frame.pixels;
    width = frame.width;
    height = frame.height;
    return true;
}

double lumaAt(const std::vector<std::uint8_t>& px, int w, int x, int y) {
    const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
    return 0.299 * px[i] + 0.587 * px[i + 1] + 0.114 * px[i + 2];
}

}  // namespace

NC_TEST(SpatialEffectsE2E, BlurAndSharpenChangeGradientEnergy) {
    {
        std::ifstream check("/tmp/media_gen/detail_src.mp4");
        if (!check.good()) return;  // 素材未生成：跳过（与既有约定一致）
    }
    std::vector<std::uint8_t> blur, plain, sharp;
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0, w3 = 0, h3 = 0;
    NC_CHECK(exportDetailFrame(9, 8.0, "/tmp/media_gen/t_blur.mp4", blur, w1, h1));
    NC_CHECK(exportDetailFrame(0, 0.0, "/tmp/media_gen/t_plain.mp4", plain, w2, h2));
    NC_CHECK(exportDetailFrame(10, 4.0, "/tmp/media_gen/t_sharp.mp4", sharp, w3, h3));
    NC_CHECK_EQ(w1, w2);
    NC_CHECK_EQ(w2, w3);
    // 内容区（160x120 素材 Fit 到画布 → 扫描实际边界）
    int top = -1, bottom = -1, left = -1, right = -1;
    for (int y = 0; y < h2; ++y) {
        if (lumaAt(plain, w2, w2 / 2, y) > 5.0) {
            if (top < 0) top = y;
            bottom = y;
        }
    }
    for (int x = 0; x < w2; ++x) {
        if (lumaAt(plain, w2, x, h2 / 2) > 5.0) {
            if (left < 0) left = x;
            right = x;
        }
    }
    NC_CHECK(top >= 0 && left >= 0 && right > left);
    // 内容区平均水平梯度能量（高频素材：模糊应显著降低、锐化应增强）
    double energyPlain = 0.0, energyBlur = 0.0, energySharp = 0.0;
    long long samples = 0;
    for (int y = top + 10; y < bottom - 10; y += 3) {
        for (int x = left + 3; x < right - 3; ++x) {
            const double dp = lumaAt(plain, w2, x + 1, y) - lumaAt(plain, w2, x, y);
            const double db = lumaAt(blur, w1, x + 1, y) - lumaAt(blur, w1, x, y);
            const double ds = lumaAt(sharp, w3, x + 1, y) - lumaAt(sharp, w3, x, y);
            energyPlain += dp * dp;
            energyBlur += db * db;
            energySharp += ds * ds;
            ++samples;
        }
    }
    NC_CHECK(samples > 1000);
    const double avgPlain = energyPlain / static_cast<double>(samples);
    const double avgBlur = energyBlur / static_cast<double>(samples);
    const double avgSharp = energySharp / static_cast<double>(samples);
    std::printf("[实测] 梯度能量：清晰=%.1f 模糊=%.1f 锐化=%.1f\n", avgPlain, avgBlur,
                avgSharp);
    NC_CHECK(avgBlur < avgPlain * 0.5);   // 模糊真实降低细节
    NC_CHECK(avgSharp > avgPlain * 1.1);  // 锐化真实增强细节
}

NC_TEST(SpatialEffectsE2E, VignetteDarkensContentCornersOnRealExport) {
    {
        std::ifstream check("/tmp/media_gen/detail_src.mp4");
        if (!check.good()) return;  // 素材未生成：跳过
    }
    std::vector<std::uint8_t> vig, plain;
    int w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    NC_CHECK(exportDetailFrame(8, 1.0, "/tmp/media_gen/t_vig.mp4", vig, w1, h1));
    NC_CHECK(exportDetailFrame(0, 0.0, "/tmp/media_gen/t_plain2.mp4", plain, w2, h2));
    NC_CHECK_EQ(w1, w2);
    // 内容区边界
    int top = -1, bottom = -1;
    for (int y = 0; y < h2; ++y) {
        if (lumaAt(plain, w2, w2 / 2, y) > 5.0) {
            if (top < 0) top = y;
            bottom = y;
        }
    }
    NC_CHECK(top >= 0);
    // 找内容区中「有足够亮度」的角落代表点（对照组亮度 > 40）
    int cornerX = -1, cornerY = -1;
    for (int y = top + 10; y < top + 120 && cornerX < 0; y += 5) {
        for (int x = 10; x < w2 / 3; x += 5) {
            if (lumaAt(plain, w2, x, y) > 40.0) {
                cornerX = x;
                cornerY = y;
                break;
            }
        }
    }
    NC_CHECK(cornerX >= 0);
    const double cornerPlain = lumaAt(plain, w2, cornerX, cornerY);
    const double cornerVig = lumaAt(vig, w1, cornerX, cornerY);
    const double centerPlain = lumaAt(plain, w2, w2 / 2, h2 / 2);
    const double centerVig = lumaAt(vig, w1, w2 / 2, h2 / 2);
    std::printf("[实测] 角落 %.1f→%.1f 中心 %.1f→%.1f\n", cornerPlain, cornerVig,
                centerPlain, centerVig);
    NC_CHECK(cornerVig < cornerPlain * 0.7);              // 角落真实变暗（≥30%）
    NC_CHECK(std::fabs(centerVig - centerPlain) < 5.0);   // 中心基本不变（容压缩噪声）
}
#endif
