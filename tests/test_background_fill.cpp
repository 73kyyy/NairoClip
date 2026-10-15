// =============================================================================
// 背景填充测试（文档 Part 4-C 第 11 节「背景」：颜色/模糊/画布样式三 Tab）
//             + 变速曲线预设 C ABI（文档 Part 4-A 1.1）
// =============================================================================
// 覆盖：
//   1. 模式/样式名称与解析（文档逐字）；
//   2. 颜色填充（空白区域变目标色；素材区域不变）；
//   3. 模糊填充（空白区域为素材模糊放大；不再等于原背景色）；
//   4. 画布样式（纹理真实生成 + 填充；不同样式不同图案）；
//   5. 集成（命令/撤销/序列化/C ABI）+ 变速预设 C ABI。
#include "test_framework.h"
#include "nairoclip/core/background_fill.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/serialization.h"
#include "nairoclip/core/speed_curve.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr int kW = 64;
constexpr int kH = 48;

/// 构造画布 + 素材蒙版：素材区域为彩色块，留白区域为黑（模拟 Fit 信箱）。
struct CanvasFixture {
    std::vector<std::uint8_t> canvas;   // RGBA8（画布）
    std::vector<std::uint8_t> mask;     // 0 = 留白，255 = 素材
    int contentX0 = 0;                  // 素材区域范围（便于断言）
    int contentX1 = 0;

    CanvasFixture() {
        canvas.assign(static_cast<std::size_t>(kW) * kH * 4, 0);
        mask.assign(static_cast<std::size_t>(kW) * kH, 0);
        // 素材居中竖条（宽 1/3；左右留白）
        contentX0 = kW / 3;
        contentX1 = 2 * kW / 3;
        for (int y = 0; y < kH; ++y) {
            for (int x = 0; x < kW; ++x) {
                const std::size_t idx =
                    static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x);
                std::uint8_t* px = canvas.data() + idx * 4;
                if (x >= contentX0 && x < contentX1) {
                    // 素材：渐变彩色
                    px[0] = static_cast<std::uint8_t>(60 + x * 2);
                    px[1] = 140;
                    px[2] = static_cast<std::uint8_t>(220 - y);
                    mask[idx] = 255;
                } else {
                    px[0] = px[1] = px[2] = 0;  // 黑边（留白）
                    mask[idx] = 0;
                }
                px[3] = 255;
            }
        }
    }
    std::uint8_t* pixel(int x, int y) {
        return canvas.data() + (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 4;
    }
};

/// 左侧留白区域的平均色。
void meanLeftMargin(const std::vector<std::uint8_t>& canvas, double* outR, double* outG,
                    double* outB) {
    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    std::size_t count = 0;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW / 3; ++x) {
            const std::uint8_t* px =
                canvas.data() + (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 4;
            sumR += px[0];
            sumG += px[1];
            sumB += px[2];
            ++count;
        }
    }
    *outR = count > 0 ? sumR / static_cast<double>(count) : 0.0;
    *outG = count > 0 ? sumG / static_cast<double>(count) : 0.0;
    *outB = count > 0 ? sumB / static_cast<double>(count) : 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 名称与解析
// ---------------------------------------------------------------------------

NC_TEST(BackgroundFill, ModeNames) {
    NC_CHECK_EQ(std::string(backgroundModeName(BackgroundMode::None)), std::string("none"));
    NC_CHECK_EQ(std::string(backgroundModeName(BackgroundMode::Color)), std::string("color"));
    NC_CHECK_EQ(std::string(backgroundModeName(BackgroundMode::Blur)), std::string("blur"));
    NC_CHECK_EQ(std::string(backgroundModeName(BackgroundMode::Style)), std::string("style"));
    BackgroundMode mode;
    NC_CHECK(backgroundModeFromString("color", mode) && mode == BackgroundMode::Color);
    NC_CHECK(backgroundModeFromString("模糊", mode) && mode == BackgroundMode::Blur);
    NC_CHECK(backgroundModeFromString("画布样式", mode) && mode == BackgroundMode::Style);
    NC_CHECK(!backgroundModeFromString("bogus", mode));
    NC_CHECK(backgroundModeFromInt(0, mode) && mode == BackgroundMode::None);
    NC_CHECK(backgroundModeFromInt(3, mode) && mode == BackgroundMode::Style);
    NC_CHECK(!backgroundModeFromInt(4, mode));
}

NC_TEST(BackgroundFill, StyleNames) {
    NC_CHECK_EQ(std::string(canvasStyleDisplayName(CanvasStyle::Wood)), std::string("木纹"));
    NC_CHECK_EQ(std::string(canvasStyleDisplayName(CanvasStyle::Paper)), std::string("纸张"));
    CanvasStyle style;
    NC_CHECK(canvasStyleFromInt(1, style) && style == CanvasStyle::Wood);
    NC_CHECK(canvasStyleFromInt(15, style) && style == CanvasStyle::Neon);
    NC_CHECK(!canvasStyleFromInt(16, style));
    NC_CHECK(!canvasStyleFromInt(0, style) || style == CanvasStyle::None);
}

// ---------------------------------------------------------------------------
// 2. 颜色填充
// ---------------------------------------------------------------------------

NC_TEST(BackgroundFill, ColorFillsMarginsOnly) {
    CanvasFixture f;
    // 素材区原始像素（用于对照）
    const std::uint8_t contentBefore[3] = {f.pixel(32, 20)[0], f.pixel(32, 20)[1],
                                           f.pixel(32, 20)[2]};
    // 填充红色（0xFF0000FF = R=255,G=0,B=0）
    NC_CHECK(applyBackgroundFill(f.canvas.data(), f.canvas.data(), f.mask.data(), kW, kH,
                                 BackgroundMode::Color, 0xFF0000FF, 0.0));
    // 左侧留白 → 红
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    meanLeftMargin(f.canvas, &r, &g, &b);
    NC_CHECK(r > 250.0);
    NC_CHECK(g < 5.0);
    NC_CHECK(b < 5.0);
    // 素材区域不变
    NC_CHECK_EQ(f.pixel(32, 20)[0], contentBefore[0]);
    NC_CHECK_EQ(f.pixel(32, 20)[1], contentBefore[1]);
    NC_CHECK_EQ(f.pixel(32, 20)[2], contentBefore[2]);
    // None 模式零改动
    CanvasFixture g2;
    const std::uint8_t before = g2.pixel(5, 5)[0];
    NC_CHECK(applyBackgroundFill(g2.canvas.data(), g2.canvas.data(), g2.mask.data(), kW, kH,
                                 BackgroundMode::None, 0xFFFFFFFF, 50.0));
    NC_CHECK_EQ(g2.pixel(5, 5)[0], before);
}

// ---------------------------------------------------------------------------
// 3. 模糊填充
// ---------------------------------------------------------------------------

NC_TEST(BackgroundFill, BlurFillsWithBlurredSource) {
    CanvasFixture f;
    const double marginBefore = static_cast<double>(f.pixel(5, 24)[0]);  // 原留白 = 0
    NC_CHECK(marginBefore < 1.0);
    NC_CHECK(applyBackgroundFill(f.canvas.data(), f.canvas.data(), f.mask.data(), kW, kH,
                                 BackgroundMode::Blur, 0, 80.0));
    // 留白区域应被素材模糊内容填充（不再是纯黑）
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    meanLeftMargin(f.canvas, &r, &g, &b);
    NC_CHECK(r > 20.0 || g > 20.0 || b > 20.0);  // 有内容（非黑）
    // 模糊度越高 → 与低模糊度结果不同（真实模糊运算）
    CanvasFixture low;
    NC_CHECK(applyBackgroundFill(low.canvas.data(), low.canvas.data(), low.mask.data(), kW, kH,
                                 BackgroundMode::Blur, 0, 5.0));
    bool differs = false;
    for (std::size_t i = 0; i < f.canvas.size() && !differs; i += 4) {
        if (f.canvas[i] != low.canvas[i]) {
            differs = true;
        }
    }
    NC_CHECK(differs);
    // 模糊模式缺基底 → 失败
    NC_CHECK(!applyBackgroundFill(f.canvas.data(), nullptr, f.mask.data(), kW, kH,
                                  BackgroundMode::Blur, 0, 50.0));
}

// ---------------------------------------------------------------------------
// 4. 画布样式
// ---------------------------------------------------------------------------

NC_TEST(BackgroundFill, StyleTextureGenerated) {
    // 纹理生成（确定性）
    std::vector<std::uint8_t> a;
    std::vector<std::uint8_t> b;
    NC_CHECK(generateCanvasStyle(CanvasStyle::Wood, 32, 32, a));
    NC_CHECK(generateCanvasStyle(CanvasStyle::Wood, 32, 32, b));
    NC_CHECK_EQ(a.size(), std::size_t(32 * 32 * 4));
    bool identical = true;
    for (std::size_t i = 0; i < a.size() && identical; ++i) {
        if (a[i] != b[i]) identical = false;
    }
    NC_CHECK(identical);  // 同一样式 → 同一纹理（确定性）
    // 不同样式 → 不同纹理
    std::vector<std::uint8_t> paper;
    NC_CHECK(generateCanvasStyle(CanvasStyle::Paper, 32, 32, paper));
    bool differs = false;
    for (std::size_t i = 0; i < a.size() && !differs; i += 4) {
        if (a[i] != paper[i]) differs = true;
    }
    NC_CHECK(differs);
    // 非法尺寸
    std::vector<std::uint8_t> bad;
    NC_CHECK(!generateCanvasStyle(CanvasStyle::Wood, 0, 32, bad));
}

NC_TEST(BackgroundFill, StyleFillsMargins) {
    CanvasFixture f;
    // 木纹样式（编号 1）
    NC_CHECK(applyBackgroundFill(f.canvas.data(), f.canvas.data(), f.mask.data(), kW, kH,
                                 BackgroundMode::Style, 0, 1.0));
    double r = 0.0;
    double g = 0.0;
    double b = 0.0;
    meanLeftMargin(f.canvas, &r, &g, &b);
    NC_CHECK(r > 50.0);  // 木纹为暖棕（R 显著 > 0）
    NC_CHECK(r > b);     // 暖色调：R > B
    // 非法样式编号（0 = None / 99 越界）→ 失败
    NC_CHECK(!applyBackgroundFill(f.canvas.data(), f.canvas.data(), f.mask.data(), kW, kH,
                                  BackgroundMode::Style, 0, 0.0));
    NC_CHECK(!applyBackgroundFill(f.canvas.data(), f.canvas.data(), f.mask.data(), kW, kH,
                                  BackgroundMode::Style, 0, 99.0));
}

// ---------------------------------------------------------------------------
// 5. 集成（命令/撤销/序列化/C ABI）+ 变速预设 C ABI
// ---------------------------------------------------------------------------

namespace {

struct BackgroundFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    ClipId clipId;

    BackgroundFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/v/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(5);
        editor.insertClip(seqId, trackId, c, &clipId);
    }
};

}  // namespace

NC_TEST(BackgroundFill, EditorSetAndUndo) {
    BackgroundFixture f;
    NC_CHECK(!f.project.mainSequence()->tracks[0].clips[0].hasBackground());
    // 颜色模式
    NC_CHECK(f.editor.setClipBackground(f.seqId, f.trackId, f.clipId, BackgroundMode::Color,
                                        0xFF00FFFF, 0.0)
                 .isOk());
    const Clip& clip = f.project.mainSequence()->tracks[0].clips[0];
    NC_CHECK(clip.hasBackground());
    NC_CHECK(clip.backgroundMode == BackgroundMode::Color);
    NC_CHECK_EQ(clip.backgroundColorRgba, std::uint32_t(0xFF00FFFF));
    // 撤销/重做
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(!f.project.mainSequence()->tracks[0].clips[0].hasBackground());
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK(f.project.mainSequence()->tracks[0].clips[0].backgroundMode == BackgroundMode::Color);
    // 模糊模式（强度校验）
    NC_CHECK(f.editor.setClipBackground(f.seqId, f.trackId, f.clipId, BackgroundMode::Blur, 0,
                                        60.0)
                 .isOk());
    NC_CHECK(!f.editor.setClipBackground(f.seqId, f.trackId, f.clipId, BackgroundMode::Blur, 0,
                                         150.0)
                  .isOk());
    // 样式模式（编号校验）
    NC_CHECK(f.editor.setClipBackground(f.seqId, f.trackId, f.clipId, BackgroundMode::Style, 0,
                                        1.0)
                 .isOk());
    NC_CHECK(!f.editor.setClipBackground(f.seqId, f.trackId, f.clipId, BackgroundMode::Style, 0,
                                         99.0)
                  .isOk());
    // 清除
    NC_CHECK(f.editor.setClipBackground(f.seqId, f.trackId, f.clipId, BackgroundMode::None, 0,
                                        0.0)
                 .isOk());
    NC_CHECK(!f.project.mainSequence()->tracks[0].clips[0].hasBackground());
}

NC_TEST(BackgroundFill, SerializationRoundtrip) {
    BackgroundFixture f;
    NC_CHECK(f.editor.setClipBackground(f.seqId, f.trackId, f.clipId, BackgroundMode::Color,
                                        0x123456FF, 0.0)
                 .isOk());
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"background\"") != std::string::npos);
    NC_CHECK(text.find("color") != std::string::npos);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Clip& clip = loaded.mainSequence()->tracks[0].clips[0];
    NC_CHECK(clip.backgroundMode == BackgroundMode::Color);
    NC_CHECK_EQ(clip.backgroundColorRgba, std::uint32_t(0x123456FF));
    // 未启用不写入
    BackgroundFixture g;
    NC_CHECK(saveProjectToNclip(g.project).find("\"background\"") == std::string::npos);
    // 非法模式降级（工程可打开）
    std::string broken = text;
    const std::size_t pos = broken.find("color");
    broken.replace(pos, 5, "bogus_mode_x");
    Project tolerant;
    NC_CHECK(loadProjectFromNclip(broken, tolerant).result.isOk());
    NC_CHECK(!tolerant.mainSequence()->tracks[0].clips[0].hasBackground());
}

NC_TEST(BackgroundFill, CapiBackground) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "v.mp4", "/v/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 5000000);
    // 默认未设置
    NC_CHECK_EQ(nc_get_clip_background(editor, 0, clip, nullptr, nullptr, nullptr), 0);
    // 设置颜色
    NC_CHECK_EQ(nc_set_clip_background(editor, 0, clip, NC_BACKGROUND_COLOR, 0x00FF00FF, 0.0),
                NC_OK);
    std::int32_t mode = -1;
    std::uint32_t rgba = 0;
    NC_CHECK_EQ(nc_get_clip_background(editor, 0, clip, &mode, &rgba, nullptr), 1);
    NC_CHECK_EQ(mode, NC_BACKGROUND_COLOR);
    NC_CHECK_EQ(rgba, std::uint32_t(0x00FF00FF));
    // 样式模式
    NC_CHECK_EQ(nc_set_clip_background(editor, 0, clip, NC_BACKGROUND_STYLE, 0, 2.0), NC_OK);
    NC_CHECK_EQ(nc_get_clip_background(editor, 0, clip, &mode, nullptr, nullptr), 1);
    NC_CHECK_EQ(mode, NC_BACKGROUND_STYLE);
    // 非法模式
    NC_CHECK_EQ(nc_set_clip_background(editor, 0, clip, 9, 0, 0.0), NC_ERROR_INVALID_ARGUMENT);
    // NULL 安全
    NC_CHECK_EQ(nc_get_clip_background(nullptr, 0, clip, nullptr, nullptr, nullptr), -1);
    nc_editor_destroy(editor);
}

NC_TEST(BackgroundFill, CapiSpeedCurvePreset) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "v.mp4", "/v/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 4000000);
    // 预设名称（文档逐字）
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(1)), std::string("蒙太奇"));
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(2)), std::string("英雄时刻"));
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(3)), std::string("子弹时间"));
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(4)), std::string("跳跃"));
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(5)), std::string("闪进"));
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(6)), std::string("闪出"));
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(0)), std::string("自定义"));
    NC_CHECK_EQ(std::string(nc_speed_curve_preset_display_name(99)), std::string("自定义"));
    // 应用预设（子弹时间）
    NC_CHECK_EQ(nc_apply_speed_curve_preset(editor, 0, clip, 3, 0), NC_OK);
    NC_CHECK(nc_clip_speed_curve_count(editor, 0, clip) > 0);
    // 自定义 = 清空曲线
    NC_CHECK_EQ(nc_apply_speed_curve_preset(editor, 0, clip, 0, 0), NC_OK);
    NC_CHECK_EQ(nc_clip_speed_curve_count(editor, 0, clip), 0);
    // 非法预设
    NC_CHECK_EQ(nc_apply_speed_curve_preset(editor, 0, clip, 99, 0), NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 6. 端到端：背景填充经真实导出（Fit 留白 + 颜色填充）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_ffmpeg.h"

#include <fstream>

namespace {

bool bgMediaExists() {
    std::ifstream f("/tmp/media_gen/test_av.mp4");
    return f.good();
}

}  // namespace

NC_TEST(BackgroundFill, ExportFillColorInLetterbox) {
    if (!bgMediaExists()) return;
    // 素材 160x120（4:3）放入 160x160（1:1）画布 Fit 模式 → 上下留白 → 填红
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 160;
    seq.height = 160;
    seq.frameRate = fps30();
    seq.frameFit = FrameFit::Fit;
    seq.backgroundRgba = 0x000000FF;  // 默认黑边
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.path = "/tmp/media_gen/test_av.mp4";
    a.type = MediaType::Video;
    a.duration = Rational::make(3);
    project.assets.push_back(a);
    const AssetId assetId = project.assets.front().id;
    TrackId trackId;
    editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
    Clip c;
    c.asset = assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    ClipId clipId;
    editor.insertClip(seqId, trackId, c, &clipId);
    // 红色背景填充（0xFF0000FF）
    NC_CHECK(editor.setClipBackground(seqId, trackId, clipId, BackgroundMode::Color, 0xFF0000FF,
                                      0.0)
                 .isOk());
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    FFmpegReaderFactory readerFactory;
    ExportExecutorOptions options;
    options.outputVideo = true;
    options.outputAudio = false;
    std::vector<std::uint8_t> first;
    executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& f) {
            if (first.empty()) first = f.rgba;
        },
        nullptr, nullptr, options, &project);
    NC_CHECK(!first.empty());
    // 顶部留白区域（y = 2）应为红色
    const std::size_t topIdx = (static_cast<std::size_t>(2) * 160 + 80) * 4;
    NC_CHECK(first[topIdx] > 200);      // R 高
    NC_CHECK(first[topIdx + 1] < 60);   // G 低
    NC_CHECK(first[topIdx + 2] < 60);   // B 低
    // 中心素材区域应保留素材内容（非纯红）
    const std::size_t midIdx = (static_cast<std::size_t>(80) * 160 + 80) * 4;
    const bool notPureRed = first[midIdx + 1] > 60 || first[midIdx + 2] > 60;
    NC_CHECK(notPureRed);
}

#endif  // NAIROCLIP_HAS_FFMPEG
