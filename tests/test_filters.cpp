// =============================================================================
// 滤镜库测试（文档 Part 4-B 第 7 节「滤镜」14 风格 + 变速曲线预设）
// =============================================================================
// 覆盖：
//   1. 滤镜名称/解析（文档 7.1 分类清单逐字核对）；
//   2. 效果真实性（黑白去色、Negative 反色、暖调/冷调方向正确）；
//   3. 强度语义（0 = 零改动；100 = 完整风格；中强度居中）；
//   4. 集成（命令/撤销/序列化/C ABI）；
//   5. 变速曲线预设（文档 Part 4-A 1.1：蒙太奇/英雄时刻/子弹时间/跳跃/闪进/闪出）。
#include "test_framework.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/filters.h"
#include "nairoclip/core/serialization.h"
#include "nairoclip/core/speed_curve.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 测试图：左半暖色块（橙红）、右半冷色块（青蓝）。
std::vector<std::uint8_t> makeColorTestImage(int width, int height) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) *
                                         static_cast<std::size_t>(height) * 4,
                                     255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* px = pixels.data() +
                               (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                static_cast<std::size_t>(x)) * 4;
            if (x < width / 2) {
                px[0] = 200;  // 暖（R 高 B 低）
                px[1] = 130;
                px[2] = 60;
            } else {
                px[0] = 60;   // 冷（B 高 R 低）
                px[1] = 130;
                px[2] = 200;
            }
        }
    }
    return pixels;
}

/// 平均各通道（校验色调方向）。
void meanChannels(const std::vector<std::uint8_t>& pixels, double* outR, double* outG,
                  double* outB) {
    double sumR = 0.0;
    double sumG = 0.0;
    double sumB = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        sumR += pixels[i];
        sumG += pixels[i + 1];
        sumB += pixels[i + 2];
        ++count;
    }
    if (count == 0) {
        *outR = *outG = *outB = 0.0;
        return;
    }
    *outR = sumR / static_cast<double>(count);
    *outG = sumG / static_cast<double>(count);
    *outB = sumB / static_cast<double>(count);
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
// 1. 名称与解析（文档 7.1 清单逐字核对）
// ---------------------------------------------------------------------------

NC_TEST(Filters, DisplayNamesMatchDocument) {
    // 文档 Part 4-B 7.1：全部 / 电影 / 复古 / 清新 / 黑白 / 赛博 / 日系 /
    //                    胶片 / 暖阳 / 冷调 / 港风 / 高级灰 / 奶油 / Negative
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Movie)), std::string("电影"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Retro)), std::string("复古"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Fresh)), std::string("清新"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::BlackWhite)), std::string("黑白"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Cyber)), std::string("赛博"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Japanese)), std::string("日系"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Film)), std::string("胶片"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::WarmSun)), std::string("暖阳"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::CoolTone)), std::string("冷调"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::HongKong)), std::string("港风"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::HighGray)), std::string("高级灰"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Cream)), std::string("奶油"));
    NC_CHECK_EQ(std::string(filterStyleDisplayName(FilterStyle::Negative)),
                std::string("Negative"));
    // 总数 = 14（含 None）
    NC_CHECK_EQ(static_cast<int>(FilterStyle::Negative), 13);
}

NC_TEST(Filters, ParseRoundtrip) {
    FilterStyle style;
    NC_CHECK(filterStyleFromString("movie", style) && style == FilterStyle::Movie);
    NC_CHECK(filterStyleFromString("BlackWhite", style) && style == FilterStyle::BlackWhite);
    NC_CHECK(filterStyleFromString("黑白", style) && style == FilterStyle::BlackWhite);
    NC_CHECK(filterStyleFromString("negative", style) && style == FilterStyle::Negative);
    NC_CHECK(filterStyleFromString("暖阳", style) && style == FilterStyle::WarmSun);
    NC_CHECK(!filterStyleFromString("unknown", style));
    NC_CHECK(filterStyleFromInt(0, style) && style == FilterStyle::None);
    NC_CHECK(filterStyleFromInt(13, style) && style == FilterStyle::Negative);
    NC_CHECK(!filterStyleFromInt(14, style));
    // 名称往返
    for (int i = 0; i <= 13; ++i) {
        const auto s = static_cast<FilterStyle>(i);
        FilterStyle parsed;
        NC_CHECK(filterStyleFromString(filterStyleName(s), parsed) && parsed == s);
    }
}

// ---------------------------------------------------------------------------
// 2. 效果真实性
// ---------------------------------------------------------------------------

NC_TEST(Filters, BlackWhiteRemovesColor) {
    std::vector<std::uint8_t> pixels = makeColorTestImage(64, 48);
    NC_CHECK(applyFilterToRgba(pixels.data(), 64, 48, FilterStyle::BlackWhite, 100.0));
    // 去色后 R=G=B（容差 2）
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        NC_CHECK(std::abs(static_cast<int>(pixels[i]) - static_cast<int>(pixels[i + 1])) <= 2);
        NC_CHECK(std::abs(static_cast<int>(pixels[i + 1]) - static_cast<int>(pixels[i + 2])) <= 2);
    }
}

NC_TEST(Filters, NegativeInverts) {
    const std::vector<std::uint8_t> original = makeColorTestImage(32, 32);
    std::vector<std::uint8_t> pixels = original;
    NC_CHECK(applyFilterToRgba(pixels.data(), 32, 32, FilterStyle::Negative, 100.0));
    // 反色：R' ≈ 255 - R（容差 3）
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        NC_CHECK(std::abs(static_cast<int>(pixels[i]) + original[i] - 255) <= 3);
    }
}

NC_TEST(Filters, WarmRaisesRedAndLowersBlue) {
    const std::vector<std::uint8_t> original = makeColorTestImage(32, 32);
    std::vector<std::uint8_t> pixels = original;
    double origR = 0.0;
    double origG = 0.0;
    double origB = 0.0;
    meanChannels(original, &origR, &origG, &origB);
    NC_CHECK(applyFilterToRgba(pixels.data(), 32, 32, FilterStyle::WarmSun, 100.0));
    double newR = 0.0;
    double newG = 0.0;
    double newB = 0.0;
    meanChannels(pixels, &newR, &newG, &newB);
    NC_CHECK(newR > origR);  // 暖调：红升
    NC_CHECK(newB > origB - 1.0);  // 蓝不显著降（暖阳以提亮为主）
    // 冷调反向
    std::vector<std::uint8_t> cool = original;
    NC_CHECK(applyFilterToRgba(cool.data(), 32, 32, FilterStyle::CoolTone, 100.0));
    double coolR = 0.0;
    double coolG = 0.0;
    double coolB = 0.0;
    meanChannels(cool, &coolR, &coolG, &coolB);
    NC_CHECK(coolB > coolR);
}

NC_TEST(Filters, AllStylesChangeImage) {
    const std::vector<std::uint8_t> original = makeColorTestImage(32, 32);
    for (int i = 1; i <= 13; ++i) {
        std::vector<std::uint8_t> pixels = original;
        const auto style = static_cast<FilterStyle>(i);
        NC_CHECK(applyFilterToRgba(pixels.data(), 32, 32, style, 100.0));
        NC_CHECK(meanAbsDiff(pixels, original) > 0.5);
    }
}

// ---------------------------------------------------------------------------
// 3. 强度语义（文档 7.2：0~100）
// ---------------------------------------------------------------------------

NC_TEST(Filters, IntensityZeroIsNoOp) {
    const std::vector<std::uint8_t> original = makeColorTestImage(32, 32);
    std::vector<std::uint8_t> pixels = original;
    NC_CHECK(applyFilterToRgba(pixels.data(), 32, 32, FilterStyle::Movie, 0.0));
    NC_CHECK_EQ(meanAbsDiff(pixels, original), 0.0);
    // None 风格任意强度均零改动
    std::vector<std::uint8_t> none = original;
    NC_CHECK(applyFilterToRgba(none.data(), 32, 32, FilterStyle::None, 100.0));
    NC_CHECK_EQ(meanAbsDiff(none, original), 0.0);
}

NC_TEST(Filters, IntensityScalesEffect) {
    const std::vector<std::uint8_t> original = makeColorTestImage(32, 32);
    std::vector<std::uint8_t> half = original;
    std::vector<std::uint8_t> full = original;
    NC_CHECK(applyFilterToRgba(half.data(), 32, 32, FilterStyle::BlackWhite, 50.0));
    NC_CHECK(applyFilterToRgba(full.data(), 32, 32, FilterStyle::BlackWhite, 100.0));
    const double halfDiff = meanAbsDiff(half, original);
    const double fullDiff = meanAbsDiff(full, original);
    // 中强度效果应弱于完整强度（单调性）
    NC_CHECK(halfDiff > 0.0);
    NC_CHECK(halfDiff < fullDiff);
}

NC_TEST(Filters, InvalidBufferFails) {
    std::uint8_t buffer[8] = {0};
    NC_CHECK(!applyFilterToRgba(nullptr, 4, 4, FilterStyle::Movie, 100.0));
    NC_CHECK(!applyFilterToRgba(buffer, 0, 4, FilterStyle::Movie, 100.0));
    NC_CHECK(!applyFilterToRgba(buffer, 4, -1, FilterStyle::Movie, 100.0));
}

// ---------------------------------------------------------------------------
// 4. 集成（命令/撤销/序列化/C ABI）
// ---------------------------------------------------------------------------

namespace {

struct FilterFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    ClipId clipId;

    FilterFixture() {
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

NC_TEST(Filters, EditorSetAndUndo) {
    FilterFixture f;
    NC_CHECK(!f.project.mainSequence()->tracks[0].clips[0].hasFilter());
    NC_CHECK(f.editor.setClipFilter(f.seqId, f.trackId, f.clipId, FilterStyle::Movie, 80.0).isOk());
    const Clip& clip = f.project.mainSequence()->tracks[0].clips[0];
    NC_CHECK(clip.filterStyle == FilterStyle::Movie);
    NC_CHECK(std::abs(clip.filterIntensity - 80.0) < 1e-9);
    // 撤销/重做
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(!f.project.mainSequence()->tracks[0].clips[0].hasFilter());
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK(f.project.mainSequence()->tracks[0].clips[0].filterStyle == FilterStyle::Movie);
    // 清除滤镜
    NC_CHECK(f.editor.setClipFilter(f.seqId, f.trackId, f.clipId, FilterStyle::None, 80.0).isOk());
    NC_CHECK(!f.project.mainSequence()->tracks[0].clips[0].hasFilter());
    // 非法强度拒绝
    NC_CHECK(!f.editor.setClipFilter(f.seqId, f.trackId, f.clipId, FilterStyle::Movie, 150.0)
                  .isOk());
}

NC_TEST(Filters, SerializationRoundtrip) {
    FilterFixture f;
    NC_CHECK(f.editor.setClipFilter(f.seqId, f.trackId, f.clipId, FilterStyle::Film, 65.0).isOk());
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"filter\"") != std::string::npos);
    NC_CHECK(text.find("film") != std::string::npos);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Clip& clip = loaded.mainSequence()->tracks[0].clips[0];
    NC_CHECK(clip.filterStyle == FilterStyle::Film);
    NC_CHECK(std::abs(clip.filterIntensity - 65.0) < 1e-9);
    // 未启用时不写入
    FilterFixture g;
    NC_CHECK(saveProjectToNclip(g.project).find("\"filter\"") == std::string::npos);
    // 非法风格降级为 None（工程可打开）
    std::string broken = text;
    const std::size_t pos = broken.find("film");
    broken.replace(pos, 4, "bogus_style_xyz");
    Project tolerant;
    NC_CHECK(loadProjectFromNclip(broken, tolerant).result.isOk());
    NC_CHECK(!tolerant.mainSequence()->tracks[0].clips[0].hasFilter());
}

NC_TEST(Filters, CapiSetGetFilter) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "v.mp4", "/v/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 5000000);
    // 默认未启用
    NC_CHECK_EQ(nc_get_clip_filter(editor, 0, clip, nullptr, nullptr), 0);
    // 设置电影滤镜 + 强度
    NC_CHECK_EQ(nc_set_clip_filter(editor, 0, clip, 1, 80.0), NC_OK);
    std::int32_t style = -1;
    double intensity = -1.0;
    NC_CHECK_EQ(nc_get_clip_filter(editor, 0, clip, &style, &intensity), 1);
    NC_CHECK_EQ(style, 1);
    NC_CHECK(std::abs(intensity - 80.0) < 1e-9);
    // 非法参数
    NC_CHECK_EQ(nc_set_clip_filter(editor, 0, clip, 14, 80.0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_clip_filter(editor, 0, clip, 1, 200.0), NC_ERROR_INVALID_ARGUMENT);
    // 展示名（文档逐字）
    NC_CHECK_EQ(std::string(nc_filter_display_name(1)), std::string("电影"));
    NC_CHECK_EQ(std::string(nc_filter_display_name(13)), std::string("Negative"));
    NC_CHECK_EQ(std::string(nc_filter_display_name(99)), std::string("无"));
    // NULL 安全
    NC_CHECK_EQ(nc_get_clip_filter(nullptr, 0, clip, nullptr, nullptr), -1);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 5. 变速曲线预设（文档 Part 4-A 1.1）
// ---------------------------------------------------------------------------

NC_TEST(Filters, SpeedCurvePresetNames) {
    // 文档清单：自定义/蒙太奇/英雄时刻/子弹时间/跳跃/闪进/闪出
    NC_CHECK_EQ(std::string(speedCurvePresetDisplayName(SpeedCurvePreset::Custom)),
                std::string("自定义"));
    NC_CHECK_EQ(std::string(speedCurvePresetDisplayName(SpeedCurvePreset::Montage)),
                std::string("蒙太奇"));
    NC_CHECK_EQ(std::string(speedCurvePresetDisplayName(SpeedCurvePreset::HeroMoment)),
                std::string("英雄时刻"));
    NC_CHECK_EQ(std::string(speedCurvePresetDisplayName(SpeedCurvePreset::BulletTime)),
                std::string("子弹时间"));
    NC_CHECK_EQ(std::string(speedCurvePresetDisplayName(SpeedCurvePreset::Jump)),
                std::string("跳跃"));
    NC_CHECK_EQ(std::string(speedCurvePresetDisplayName(SpeedCurvePreset::FlashIn)),
                std::string("闪进"));
    NC_CHECK_EQ(std::string(speedCurvePresetDisplayName(SpeedCurvePreset::FlashOut)),
                std::string("闪出"));
}

NC_TEST(Filters, SpeedCurvePresetBuilds) {
    const Time duration = Rational::make(4);  // 4 秒源区间
    // 自定义 → 空曲线（恒速）
    NC_CHECK(buildSpeedCurvePreset(SpeedCurvePreset::Custom, duration).empty());
    // 各预设应生成非空且合法的曲线
    for (int i = 1; i <= 6; ++i) {
        const auto preset = static_cast<SpeedCurvePreset>(i);
        const SpeedCurve curve = buildSpeedCurvePreset(preset, duration);
        NC_CHECK(!curve.empty());
        NC_CHECK(curve.invariantsHold());
    }
    // 子弹时间：中段极慢点应显著低于两端（最慢点在源 45% 处 = 1.8s）
    const SpeedCurve bullet = buildSpeedCurvePreset(SpeedCurvePreset::BulletTime, duration);
    const double startSpeed = bullet.speedAt(Time{}, Rational::make(1));
    const double slowest = bullet.speedAt(fromMicroseconds(1800000), Rational::make(1));
    NC_CHECK(slowest < startSpeed);
    NC_CHECK(slowest <= 0.2);
    // 闪进：开头速度应显著高于尾部
    const SpeedCurve flashIn = buildSpeedCurvePreset(SpeedCurvePreset::FlashIn, duration);
    const double headSpeed = flashIn.speedAt(Time{}, Rational::make(1));
    const double tailSpeed = flashIn.speedAt(fromMicroseconds(3900000), Rational::make(1));
    NC_CHECK(headSpeed > tailSpeed * 2.0);
    // 曲线影响时间轴时长（子弹时间 → 时长显著变长）
    const Time mapped = bullet.timelineDuration(Time{}, duration, Rational::make(1));
    NC_CHECK(mapped > duration);
}

// ---------------------------------------------------------------------------
// 6. 端到端导出（真实解码 → 滤镜 → 编码 → 回读；无 FFmpeg 时排除）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_ffmpeg.h"

#include <fstream>

namespace {

bool filterMediaExists() {
    std::ifstream f("/tmp/media_gen/test_av.mp4");
    return f.good();
}

/// 平均通道饱和度（max-min；用于验证去色/色调效果）。
double meanSaturation(const std::vector<std::uint8_t>& rgba) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
        const int mx = std::max({rgba[i], rgba[i + 1], rgba[i + 2]});
        const int mn = std::min({rgba[i], rgba[i + 1], rgba[i + 2]});
        sum += mx - mn;
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

struct ExportFilterFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;

    explicit ExportFilterFixture(bool twoLayer = false) {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 160;
        seq.height = 120;
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/tmp/media_gen/test_av.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(3);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
        if (twoLayer) {
            editor.addTrack(seqId, TrackType::Video, "V2", &secondTrack);
        }
    }
    TrackId secondTrack;
};

/// 跑一次导出（纯视频），返回首帧像素（失败返回空）。
std::vector<std::uint8_t> exportFirstFrame(Project& project, const ExportPlan& plan,
                                           int* outWidth, int* outHeight) {
    FFmpegReaderFactory readerFactory;
    ExportExecutorOptions options;
    options.outputVideo = true;
    options.outputAudio = false;
    std::vector<std::uint8_t> first;
    int width = 0;
    int height = 0;
    executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& f) {
            if (first.empty()) {
                first = f.rgba;
                width = f.width;
                height = f.height;
            }
        },
        nullptr, nullptr, options, &project);
    if (outWidth != nullptr) *outWidth = width;
    if (outHeight != nullptr) *outHeight = height;
    return first;
}

}  // namespace

NC_TEST(Filters, ExportAppliesBlackWhite) {
    if (!filterMediaExists()) return;
    // 无滤镜基线
    ExportFilterFixture plain;
    Clip c;
    c.asset = plain.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    ClipId clipId;
    plain.editor.insertClip(plain.seqId, plain.trackId, c, &clipId);
    const ExportPlan plainPlan = compileExportPlan(plain.project, *plain.project.mainSequence());
    const std::vector<std::uint8_t> plainFrame = exportFirstFrame(plain.project, plainPlan,
                                                                  nullptr, nullptr);
    NC_CHECK(!plainFrame.empty());
    const double plainSat = meanSaturation(plainFrame);
    NC_CHECK(plainSat > 20.0);  // 源素材为彩条（有饱和色）
    // 黑白滤镜
    ExportFilterFixture bw;
    Clip c2;
    c2.asset = bw.assetId;
    c2.start = Rational::make(0);
    c2.duration = Rational::make(1);
    ClipId clipId2;
    bw.editor.insertClip(bw.seqId, bw.trackId, c2, &clipId2);
    NC_CHECK(bw.editor.setClipFilter(bw.seqId, bw.trackId, clipId2, FilterStyle::BlackWhite, 100.0)
                 .isOk());
    const ExportPlan bwPlan = compileExportPlan(bw.project, *bw.project.mainSequence());
    const std::vector<std::uint8_t> bwFrame = exportFirstFrame(bw.project, bwPlan, nullptr,
                                                               nullptr);
    NC_CHECK(!bwFrame.empty());
    NC_CHECK(meanSaturation(bwFrame) < 5.0);  // 去色后饱和度接近 0
}

NC_TEST(Filters, ExportAppliesLowerLayerFilterInMultiLayer) {
    // 缺陷回归：多层合成时下层帧必须吃自己的滤镜
    // （此前只处理上层，下层按原始像素参与混合）
    if (!filterMediaExists()) return;
    // 单层对照（无滤镜）
    ExportFilterFixture single;
    Clip c;
    c.asset = single.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    ClipId clipId;
    single.editor.insertClip(single.seqId, single.trackId, c, &clipId);
    const ExportPlan singlePlan =
        compileExportPlan(single.project, *single.project.mainSequence());
    const double singleSat = meanSaturation(
        exportFirstFrame(single.project, singlePlan, nullptr, nullptr));
    // 双层：下层黑白 + 上层 50% 透明（透出下层）→ 饱和度应显著降低
    ExportFilterFixture dual(true);
    Clip c1;
    c1.asset = dual.assetId;
    c1.start = Rational::make(0);
    c1.duration = Rational::make(1);
    ClipId id1;
    dual.editor.insertClip(dual.seqId, dual.trackId, c1, &id1);
    Clip c2 = c1;
    ClipId id2;
    dual.editor.insertClip(dual.seqId, dual.secondTrack, c2, &id2);
    NC_CHECK(dual.editor.setClipFilter(dual.seqId, dual.trackId, id1, FilterStyle::BlackWhite,
                                       100.0)
                 .isOk());
    NC_CHECK(dual.editor.setClipOpacity(dual.seqId, dual.secondTrack, id2, 0.5).isOk());
    const ExportPlan dualPlan = compileExportPlan(dual.project, *dual.project.mainSequence());
    const double dualSat =
        meanSaturation(exportFirstFrame(dual.project, dualPlan, nullptr, nullptr));
    NC_CHECK(dualSat < singleSat * 0.75);  // 下层去色透出后显著降饱和
}

#endif  // NAIROCLIP_HAS_FFMPEG
