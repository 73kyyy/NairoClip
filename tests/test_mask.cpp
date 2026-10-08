#include "test_framework.h"
#include "nairoclip/core/mask.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 构造纯色帧（不透明）。
std::vector<std::uint8_t> solidFrame(int w, int h, std::uint8_t r, std::uint8_t g,
                                     std::uint8_t b) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        pixels[i] = r;
        pixels[i + 1] = g;
        pixels[i + 2] = b;
        pixels[i + 3] = 255;
    }
    return pixels;
}

void pixelAt(const std::vector<std::uint8_t>& frame, int width, int x, int y, int out[4]) {
    const std::size_t idx =
        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
         static_cast<std::size_t>(x)) * 4;
    out[0] = frame[idx];
    out[1] = frame[idx + 1];
    out[2] = frame[idx + 2];
    out[3] = frame[idx + 3];
}

}  // namespace

// =============================================================================
// 形状名称与解析
// =============================================================================

NC_TEST(Mask, ShapeNamesAndParsing) {
    NC_CHECK_EQ(std::string(maskShapeName(MaskShape::Linear)), std::string("linear"));
    NC_CHECK_EQ(std::string(maskShapeName(MaskShape::Mirror)), std::string("mirror"));
    NC_CHECK_EQ(std::string(maskShapeName(MaskShape::Circle)), std::string("circle"));
    NC_CHECK_EQ(std::string(maskShapeName(MaskShape::Rectangle)), std::string("rectangle"));
    MaskShape shape;
    NC_CHECK(maskShapeFromString("circle", shape));
    NC_CHECK(shape == MaskShape::Circle);
    NC_CHECK(maskShapeFromString("RECT", shape));  // 别名 + 大小写不敏感
    NC_CHECK(shape == MaskShape::Rectangle);
    NC_CHECK(maskShapeFromString("LINEAR", shape));
    NC_CHECK(shape == MaskShape::Linear);
    NC_CHECK(!maskShapeFromString("oval", shape));
    NC_CHECK(!maskShapeFromString(nullptr, shape));
}

// =============================================================================
// 覆盖度（几何精确性）
// =============================================================================

NC_TEST(Mask, RectangleCenterVisibleCornersHidden) {
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.centerX = 0.5;
    params.centerY = 0.5;
    params.width = 0.25;   // 半宽 25% 画面宽（覆盖 50% 宽）
    params.height = 0.25;  // 半高 25% 画面高
    params.feather = 0.0;
    // 中心可见
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0, 640, 480) > 0.99);
    // 四角遮蔽
    NC_CHECK(maskCoverageAt(params, 2.0, 2.0, 640, 480) < 0.01);
    NC_CHECK(maskCoverageAt(params, 638.0, 478.0, 640, 480) < 0.01);
    // 边界外一点（x = 中心 + 半宽 + 10px）
    const double boundaryX = 320.0 + 0.25 * 640.0;
    NC_CHECK(maskCoverageAt(params, boundaryX + 10.0, 240.0, 640, 480) < 0.01);
    NC_CHECK(maskCoverageAt(params, boundaryX - 10.0, 240.0, 640, 480) > 0.99);
}

NC_TEST(Mask, RectangleInvertSwaps) {
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.width = 0.25;
    params.height = 0.25;
    params.feather = 0.0;
    const double inside = maskCoverageAt(params, 320.0, 240.0, 640, 480);
    params.invert = true;
    const double invertedInside = maskCoverageAt(params, 320.0, 240.0, 640, 480);
    NC_CHECK(std::fabs((1.0 - inside) - invertedInside) < 0.01);
    // 反转后中心遮蔽、角落可见
    NC_CHECK(invertedInside < 0.01);
    NC_CHECK(maskCoverageAt(params, 2.0, 2.0, 640, 480) > 0.99);
}

NC_TEST(Mask, CircleRoundAndEllipse) {
    Mask params;
    params.shape = MaskShape::Circle;
    params.width = 0.25;   // 半径 = 25% 宽
    params.height = 0.25;  // 半径 = 25% 高 → 160x120 → 椭圆（40x30 像素）
    params.feather = 0.0;
    // 中心可见
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0, 640, 480) > 0.99);
    // 水平半径内可见
    NC_CHECK(maskCoverageAt(params, 320.0 + 30.0, 240.0, 640, 480) > 0.99);
    // 水平半径外遮蔽（水平半径 = 0.25 * 640 = 160px）
    NC_CHECK(maskCoverageAt(params, 320.0 + 180.0, 240.0, 640, 480) < 0.01);
    // 垂直半径外遮蔽（垂直半径 = 0.25 * 480 = 120px）
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0 + 140.0, 640, 480) < 0.01);
}

NC_TEST(Mask, MirrorKeepsBand) {
    Mask params;
    params.shape = MaskShape::Mirror;
    params.height = 0.2;  // |y 中心偏移| <= 20% 画面高 → 96px
    params.feather = 0.0;
    // 中心可见
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0, 640, 480) > 0.99);
    // 带内可见（偏移 80px < 96px）
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0 + 80.0, 640, 480) > 0.99);
    // 带外遮蔽（偏移 120px > 96px）
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0 + 120.0, 640, 480) < 0.01);
    // 上下对称
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0 + 80.0, 640, 480) >
             0.99 - 1e-9);
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0 - 80.0, 640, 480) > 0.99);
}

NC_TEST(Mask, LinearHalfPlane) {
    Mask params;
    params.shape = MaskShape::Linear;
    params.height = 0.0;  // 边界过中心
    params.feather = 0.0;
    // 上半可见，下半遮蔽
    NC_CHECK(maskCoverageAt(params, 320.0, 100.0, 640, 480) > 0.99);
    NC_CHECK(maskCoverageAt(params, 320.0, 380.0, 640, 480) < 0.01);
}

NC_TEST(Mask, FeatherCreatesGradient) {
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.width = 0.25;
    params.height = 0.25;
    params.feather = 0.05;  // 5% 短边 → 24px 过渡带（480 短边）
    // 羽化居中于边界（边界处 ≈ 50%）；过渡带内应有中间值
    const double boundaryX = 320.0 + 0.25 * 640.0;
    const double atBoundary = maskCoverageAt(params, boundaryX, 240.0, 640, 480);
    NC_CHECK(atBoundary > 0.35 && atBoundary < 0.65);  // 边界 ≈ 50%
    // 过渡带内（边界内侧 8px ≈ 有效渐变区）
    const double insideEdge = maskCoverageAt(params, boundaryX - 4.0, 240.0, 640, 480);
    NC_CHECK(insideEdge > 0.55 && insideEdge < 1.0);
    // 远离边界 → 饱和（内侧全可见 / 外侧全遮蔽）
    NC_CHECK(maskCoverageAt(params, boundaryX - 40.0, 240.0, 640, 480) > 0.99);
    NC_CHECK(maskCoverageAt(params, boundaryX + 40.0, 240.0, 640, 480) < 0.01);
}

NC_TEST(Mask, RotationRotatesRectangle) {
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.width = 0.05;   // 很窄的竖条（半宽 32px）
    params.height = 0.3;   // 高条（半高 144px）
    params.rotationDegrees = 90.0;  // 旋转 90° → 变成横条
    params.feather = 0.0;
    // 旋转后：水平方向变宽（原半高 144px），垂直方向变窄（原半宽 32px）
    NC_CHECK(maskCoverageAt(params, 320.0 + 100.0, 240.0, 640, 480) > 0.99);
    NC_CHECK(maskCoverageAt(params, 320.0, 240.0 + 100.0, 640, 480) < 0.01);
}

NC_TEST(Mask, CornerRadiusShrinksCorners) {
    Mask square;
    square.shape = MaskShape::Rectangle;
    square.width = 0.25;
    square.height = 0.25;
    square.feather = 0.0;
    Mask rounded = square;
    rounded.cornerRadius = 0.1;  // 48px 圆角
    // 矩形角落（对角内侧）在圆角下应被遮蔽
    const double cornerX = 320.0 + 0.25 * 640.0 - 8.0;
    const double cornerY = 240.0 + 0.25 * 480.0 - 8.0;
    NC_CHECK(maskCoverageAt(square, cornerX, cornerY, 640, 480) > 0.99);
    NC_CHECK(maskCoverageAt(rounded, cornerX, cornerY, 640, 480) < 0.5);
    // 中心仍可见
    NC_CHECK(maskCoverageAt(rounded, 320.0, 240.0, 640, 480) > 0.99);
}

NC_TEST(Mask, CoverageRejectsInvalidParams) {
    Mask params;
    params.feather = 0.9;  // 超范围（> 0.5）
    NC_CHECK(maskCoverageAt(params, 10.0, 10.0, 640, 480) < 0.0);
    params.feather = 0.1;
    NC_CHECK(maskCoverageAt(params, 10.0, 10.0, 0, 480) < 0.0);   // 尺寸非法
    NC_CHECK(maskCoverageAt(params, 10.0, 10.0, 640, -1) < 0.0);
}

// =============================================================================
// 帧应用（像素验证）
// =============================================================================

NC_TEST(MaskApply, BackgroundModeFillsOutside) {
    const int w = 64;
    const int h = 48;
    auto frame = solidFrame(w, h, 200, 100, 50);
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.width = 0.25;   // 半宽 16px
    params.height = 0.25;  // 半高 12px
    params.feather = 0.0;
    NC_CHECK(applyMaskToFrame(frame.data(), w, h, params, MaskBlendMode::Background,
                              0x000000FF));
    int px[4];
    // 中心保留源色
    pixelAt(frame, w, 32, 24, px);
    NC_CHECK_EQ(px[0], 200);
    NC_CHECK_EQ(px[1], 100);
    NC_CHECK_EQ(px[2], 50);
    // 角落变为黑色背景
    pixelAt(frame, w, 2, 2, px);
    NC_CHECK_EQ(px[0], 0);
    NC_CHECK_EQ(px[1], 0);
    NC_CHECK_EQ(px[2], 0);
    NC_CHECK_EQ(px[3], 255);
}

NC_TEST(MaskApply, CustomBackgroundColor) {
    const int w = 32;
    const int h = 32;
    auto frame = solidFrame(w, h, 255, 255, 255);
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.width = 0.1;
    params.height = 0.1;
    params.feather = 0.0;
    // 深灰背景（面板层级色 0x1E1E1EFF）
    NC_CHECK(applyMaskToFrame(frame.data(), w, h, params, MaskBlendMode::Background,
                              0x1E1E1EFF));
    int px[4];
    pixelAt(frame, w, 1, 1, px);
    NC_CHECK_EQ(px[0], 0x1E);
    NC_CHECK_EQ(px[1], 0x1E);
    NC_CHECK_EQ(px[2], 0x1E);
    // 中心仍是白色
    pixelAt(frame, w, 16, 16, px);
    NC_CHECK_EQ(px[0], 255);
}

NC_TEST(MaskApply, TransparentModeZeroesAlpha) {
    const int w = 64;
    const int h = 48;
    auto frame = solidFrame(w, h, 120, 140, 160);
    Mask params;
    params.shape = MaskShape::Circle;
    params.width = 0.2;
    params.height = 0.2;
    params.feather = 0.0;
    NC_CHECK(applyMaskToFrame(frame.data(), w, h, params, MaskBlendMode::Transparent));
    int px[4];
    // 中心：可见（alpha 保持 255，RGB 不变）
    pixelAt(frame, w, 32, 24, px);
    NC_CHECK_EQ(px[3], 255);
    NC_CHECK_EQ(px[0], 120);
    // 角落：alpha = 0（透出下层）
    pixelAt(frame, w, 1, 1, px);
    NC_CHECK_EQ(px[3], 0);
}

NC_TEST(MaskApply, FeatherProducesGradientInFrame) {
    const int w = 128;
    const int h = 96;
    auto frame = solidFrame(w, h, 255, 255, 255);
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.width = 0.3;
    params.height = 0.4;
    params.feather = 0.1;  // 10% 短边 → 约 9.6px
    NC_CHECK(applyMaskToFrame(frame.data(), w, h, params, MaskBlendMode::Background,
                              0x000000FF));
    // 沿水平方向扫描：应有中间灰阶（羽化过渡）
    int gradientPixels = 0;
    for (int x = 0; x < w; ++x) {
        int px[4];
        pixelAt(frame, w, x, h / 2, px);
        if (px[0] > 20 && px[0] < 235) {
            ++gradientPixels;
        }
    }
    NC_CHECK(gradientPixels > 5);  // 存在过渡带（非硬边）
}

NC_TEST(MaskApply, RejectsInvalidArgs) {
    auto frame = solidFrame(16, 16, 100, 100, 100);
    Mask params;
    NC_CHECK(!applyMaskToFrame(nullptr, 16, 16, params, MaskBlendMode::Background));
    NC_CHECK(!applyMaskToFrame(frame.data(), 0, 16, params, MaskBlendMode::Background));
    params.feather = 1.5;  // 非法
    NC_CHECK(!applyMaskToFrame(frame.data(), 16, 16, params, MaskBlendMode::Background));
}

NC_TEST(MaskApply, FullVisibleIsNoOp) {
    // 蒙版覆盖整个画面 → 像素不应改变（零开销快路径）
    auto frame = solidFrame(32, 32, 77, 88, 99);
    const auto original = frame;
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.width = 2.0;   // 远超画面
    params.height = 2.0;
    params.feather = 0.0;
    NC_CHECK(applyMaskToFrame(frame.data(), 32, 32, params, MaskBlendMode::Background));
    NC_CHECK(frame == original);
}

// =============================================================================
// 覆盖率统计（UI「蒙版覆盖 X%」）
// =============================================================================

NC_TEST(Mask, CoverageRatioComputation) {
    Mask params;
    params.shape = MaskShape::Rectangle;
    params.centerX = 0.5;
    params.centerY = 0.5;
    params.width = 0.25;   // 半宽 25% → 覆盖 50% 宽
    params.height = 0.25;  // 半高 25% → 覆盖 50% 高
    params.feather = 0.0;
    const double ratio = maskCoverageRatio(params, 640, 480, 2);
    // 面积约 50% × 50% = 25%（采样与边缘略有偏差）
    NC_CHECK(ratio > 0.22 && ratio < 0.28);
    // 全覆盖
    params.width = 2.0;
    params.height = 2.0;
    NC_CHECK(maskCoverageRatio(params, 640, 480, 4) > 0.99);
    // 反转 → 1 - 原比例
    params.width = 0.25;
    params.height = 0.25;
    params.invert = true;
    NC_CHECK(maskCoverageRatio(params, 640, 480, 2) > 0.70);
}

// =============================================================================
// 管线集成（导出/预览/序列化/撤销/C ABI 真实生效）
// =============================================================================

#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/serialization.h"

namespace {

/// Mock 读取器：固定纯色帧（便于验证蒙版裁切）。
class SolidReader : public MediaReader {
public:
    SolidReader() {
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
        o.pixels.assign(static_cast<std::size_t>(64) * 48 * 4, 0);
        for (std::size_t i = 0; i + 3 < o.pixels.size(); i += 4) {
            o.pixels[i] = 200;
            o.pixels[i + 1] = 100;
            o.pixels[i + 2] = 50;
            o.pixels[i + 3] = 255;
        }
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    MediaProbeResult probe_;
    bool open_ = false;
};

class SolidFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<SolidReader>(); }
};

struct MaskFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    ClipId clipId;
    MaskFixture() {
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
    Mask rectMask() {
        Mask mask;
        mask.shape = MaskShape::Rectangle;
        mask.width = 0.25;   // 半宽 16px
        mask.height = 0.25;  // 半高 12px
        mask.feather = 0.0;
        return mask;
    }
};

}  // namespace

NC_TEST(MaskPipeline, EditorSetAndUndo) {
    MaskFixture f;
    NC_CHECK(f.editor.setClipMask(f.seqId, f.trackId, f.clipId, f.rectMask()).isOk());
    const Clip* clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK(clip->hasMask);
    NC_CHECK(clip->mask.shape == MaskShape::Rectangle);
    // 撤销
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(!clip->hasMask);
    // 重做
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK(clip->hasMask);
}

NC_TEST(MaskPipeline, EditorRejectsInvalidAndLocked) {
    MaskFixture f;
    Mask bad;
    bad.shape = MaskShape::Rectangle;
    bad.feather = 0.9;  // 超范围
    NC_CHECK(!f.editor.setClipMask(f.seqId, f.trackId, f.clipId, bad).isOk());
    NC_CHECK(!f.editor.setClipMask(f.seqId, f.trackId, ClipId{9999}, f.rectMask()).isOk());
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    NC_CHECK(!f.editor.setClipMask(f.seqId, f.trackId, f.clipId, f.rectMask()).isOk());
}

NC_TEST(MaskPipeline, ExportPlanCarriesMask) {
    MaskFixture f;
    NC_CHECK(f.editor.setClipMask(f.seqId, f.trackId, f.clipId, f.rectMask()).isOk());
    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    NC_CHECK(!plan.tracks.empty());
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(1));
    NC_CHECK(plan.tracks[0].clips[0].hasMask);
    NC_CHECK(plan.tracks[0].clips[0].mask.shape == MaskShape::Rectangle);
}

NC_TEST(MaskPipeline, ExportAppliesMaskToFrames) {
    MaskFixture f;
    NC_CHECK(f.editor.setClipMask(f.seqId, f.trackId, f.clipId, f.rectMask()).isOk());
    const ExportPlan plan = compileExportPlan(f.project, f.project.sequences[0]);
    SolidFactory factory;
    int checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                NC_CHECK_EQ(frame.width, 64);
                NC_CHECK_EQ(frame.height, 48);
                // 中心保留源色
                const std::size_t centerIdx = (24 * 64 + 32) * 4;
                NC_CHECK_EQ(frame.rgba[centerIdx], std::uint8_t(200));
                // 角落为背景色（默认不透明黑）
                const std::size_t cornerIdx = (2 * 64 + 2) * 4;
                NC_CHECK_EQ(frame.rgba[cornerIdx], std::uint8_t(0));
                NC_CHECK_EQ(frame.rgba[cornerIdx + 3], std::uint8_t(255));  // 不透明
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(checked > 0);
}

NC_TEST(MaskPipeline, PreviewAppliesMask) {
    MaskFixture f;
    NC_CHECK(f.editor.setClipMask(f.seqId, f.trackId, f.clipId, f.rectMask()).isOk());
    SolidFactory factory;
    PreviewRenderer preview(f.project, factory);
    const PreviewFrame frame = preview.renderAt(Rational::make(1, 2));
    NC_CHECK_EQ(frame.width, 64);
    NC_CHECK_EQ(frame.height, 48);
    // 中心保留源色；角落为背景色
    const std::size_t centerIdx = (24 * 64 + 32) * 4;
    NC_CHECK_EQ(frame.rgba[centerIdx], std::uint8_t(200));
    const std::size_t cornerIdx = (2 * 64 + 2) * 4;
    NC_CHECK_EQ(frame.rgba[cornerIdx], std::uint8_t(0));
}

NC_TEST(MaskPipeline, SerializationRoundtrip) {
    MaskFixture f;
    Mask mask = f.rectMask();
    mask.centerX = 0.3;
    mask.rotationDegrees = 30.0;
    mask.feather = 0.15;
    mask.cornerRadius = 0.05;
    mask.invert = true;
    NC_CHECK(f.editor.setClipMask(f.seqId, f.trackId, f.clipId, mask).isOk());

    const std::string text = saveProjectToNclip(f.project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    const Clip* clip = loaded.sequences[0].tracks[0].findClip(f.clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK(clip->hasMask);
    NC_CHECK(clip->mask.shape == MaskShape::Rectangle);
    NC_CHECK(std::fabs(clip->mask.centerX - 0.3) < 1e-9);
    NC_CHECK(std::fabs(clip->mask.rotationDegrees - 30.0) < 1e-9);
    NC_CHECK(std::fabs(clip->mask.feather - 0.15) < 1e-9);
    NC_CHECK(std::fabs(clip->mask.cornerRadius - 0.05) < 1e-9);
    NC_CHECK(clip->mask.invert);
}

NC_TEST(MaskPipeline, DefaultOmitsMaskFromFile) {
    MaskFixture f;
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"mask\"") == std::string::npos);  // 未启用不写入
}

NC_TEST(MaskPipeline, NoneShapeClearsMask) {
    MaskFixture f;
    NC_CHECK(f.editor.setClipMask(f.seqId, f.trackId, f.clipId, f.rectMask()).isOk());
    Mask none;
    none.shape = MaskShape::None;
    NC_CHECK(f.editor.setClipMask(f.seqId, f.trackId, f.clipId, none).isOk());
    const Clip* clip = findClip(f.project, f.seqId, f.trackId, f.clipId);
    NC_CHECK(!clip->hasMask);
}
