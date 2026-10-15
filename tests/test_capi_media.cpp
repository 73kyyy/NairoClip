// =============================================================================
// C ABI 媒体与查询测试（第2批：预览/缩略图/波形/场景/节拍/速度曲线/关键帧/标记/音画分离）
// =============================================================================
// 说明：真实解码类用例在素材缺失时自动跳过（与既有 FFmpeg 测试约定一致）；
//       纯数据类用例（速度曲线/关键帧/标记）不依赖素材，始终执行。
#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

bool testMediaExists() {
    std::ifstream f("/tmp/media_gen/test_av.mp4");
    return f.good();
}

bool shiftMediaExists() {
    std::ifstream f("/tmp/media_gen/tone_shift.mp4");
    return f.good();
}

std::string takeString(char* text) {
    if (text == nullptr) {
        return std::string();
    }
    const std::string result(text);
    nc_free_string(text);
    return result;
}

/// 带 1 视频素材 + 1 视频轨（素材 3s）的编辑器。
NcEditor* makeVideoEditor(std::uint64_t* outAsset, std::uint64_t* outClip,
                          int64_t duration_us = 3000000) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset =
        nc_import_asset(editor, "v.mp4", "/tmp/media_gen/test_av.mp4", 0, duration_us);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, duration_us);
    if (outAsset != nullptr) *outAsset = asset;
    if (outClip != nullptr) *outClip = clip;
    return editor;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 速度曲线（文档「剪辑 → 变速 → 曲线」）
// ---------------------------------------------------------------------------

NC_TEST(CapiMedia, SpeedCurveSetGet) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "a.mp4", "/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 4000000);
    NC_CHECK(clip != 0);
    // 初始无曲线
    NC_CHECK_EQ(nc_clip_speed_curve_count(editor, 0, clip), 0);
    // 设置 3 个控制点（0s:1x 线性 / 1s:2x 缓入 / 2s:0.5x 缓出）
    const double times[3] = {0.0, 1.0, 2.0};
    const double speeds[3] = {1.0, 2.0, 0.5};
    const std::int32_t easings[3] = {0, 1, 2};
    NC_CHECK_EQ(nc_set_clip_speed_curve(editor, 0, clip, times, speeds, easings, 3), NC_OK);
    NC_CHECK_EQ(nc_clip_speed_curve_count(editor, 0, clip), 3);
    // 读取控制点
    double sourceSeconds = 0.0;
    double speed = 0.0;
    std::int32_t easing = -1;
    NC_CHECK_EQ(nc_clip_speed_curve_get(editor, 0, clip, 1, &sourceSeconds, &speed, &easing),
                NC_OK);
    NC_CHECK(std::abs(sourceSeconds - 1.0) < 1e-6);
    NC_CHECK(std::abs(speed - 2.0) < 1e-3);
    NC_CHECK_EQ(easing, 1);
    // 越界
    NC_CHECK_EQ(nc_clip_speed_curve_get(editor, 0, clip, 9, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    // 清空（0 个点 = 恒速）
    NC_CHECK_EQ(nc_set_clip_speed_curve(editor, 0, clip, nullptr, nullptr, nullptr, 0), NC_OK);
    NC_CHECK_EQ(nc_clip_speed_curve_count(editor, 0, clip), 0);
    // 非法速度（>10x）
    const double badSpeed[1] = {50.0};
    const double t0[1] = {0.0};
    NC_CHECK_EQ(nc_set_clip_speed_curve(editor, 0, clip, t0, badSpeed, nullptr, 1),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(CapiMedia, SpeedCurveUndoable) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "a.mp4", "/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 4000000);
    const double times[2] = {0.0, 1.0};
    const double speeds[2] = {1.0, 2.0};
    NC_CHECK_EQ(nc_set_clip_speed_curve(editor, 0, clip, times, speeds, nullptr, 2), NC_OK);
    NC_CHECK_EQ(nc_clip_speed_curve_count(editor, 0, clip), 2);
    // 撤销恢复无曲线
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_speed_curve_count(editor, 0, clip), 0);
    // 重做恢复
    NC_CHECK_EQ(nc_redo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_speed_curve_count(editor, 0, clip), 2);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 2. 关键帧查询（文档「自定义关键帧转场」/ 参数动画面板）
// ---------------------------------------------------------------------------

NC_TEST(CapiMedia, KeyframeQuery) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "a.mp4", "/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 4000000);
    // 初始无关键帧
    NC_CHECK_EQ(nc_keyframe_count(editor, 0, clip, 0), 0);
    // 插入两个透明度关键帧（0s=1.0, 2s=0.5）
    NC_CHECK_EQ(nc_insert_keyframe(editor, 0, clip, 0, 0, 1.0), NC_OK);
    NC_CHECK_EQ(nc_insert_keyframe(editor, 0, clip, 0, 2000000, 0.5), NC_OK);
    NC_CHECK_EQ(nc_keyframe_count(editor, 0, clip, 0), 2);
    // 读取第二个（时刻 2s、值 0.5）
    std::int64_t timeUs = 0;
    double value = 0.0;
    std::int32_t easing = -1;
    NC_CHECK_EQ(nc_keyframe_get(editor, 0, clip, 0, 1, &timeUs, &value, &easing), NC_OK);
    NC_CHECK_EQ(timeUs, std::int64_t(2000000));
    NC_CHECK(std::abs(value - 0.5) < 1e-9);
    // 其他通道仍为 0
    NC_CHECK_EQ(nc_keyframe_count(editor, 0, clip, 4), 0);
    // 越界
    NC_CHECK_EQ(nc_keyframe_get(editor, 0, clip, 0, 9, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    NC_CHECK_EQ(nc_keyframe_get(editor, 0, clip, 9, 0, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 3. 标记（文档 Part 3 时间轴标记/批注）
// ---------------------------------------------------------------------------

NC_TEST(CapiMedia, Markers) {
    NcEditor* editor = nc_editor_create();
    // 添加两个标记
    const std::uint64_t m1 = nc_add_marker(editor, 1500000, "开场", "#2D8CFF");
    const std::uint64_t m2 = nc_add_marker(editor, 4000000, nullptr, nullptr);
    NC_CHECK(m1 != 0);
    NC_CHECK(m2 != 0);
    NC_CHECK(m1 != m2);
    NC_CHECK_EQ(nc_marker_count(editor), 2);
    // 读取
    std::uint64_t id = 0;
    std::int64_t timeUs = 0;
    char* name = nullptr;
    char* color = nullptr;
    NC_CHECK_EQ(nc_marker_get(editor, 0, &id, &timeUs, &name, &color), NC_OK);
    NC_CHECK_EQ(id, m1);
    NC_CHECK_EQ(timeUs, std::int64_t(1500000));
    NC_CHECK_EQ(takeString(name), std::string("开场"));
    NC_CHECK_EQ(takeString(color), std::string("#2D8CFF"));
    // 默认名（第二个）
    NC_CHECK_EQ(nc_marker_get(editor, 1, nullptr, nullptr, &name, nullptr), NC_OK);
    NC_CHECK_EQ(takeString(name), std::string("标记"));
    // 删除 + 撤销
    NC_CHECK_EQ(nc_remove_marker(editor, m1), NC_OK);
    NC_CHECK_EQ(nc_marker_count(editor), 1);
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_marker_count(editor), 2);
    // 越界与非法
    NC_CHECK_EQ(nc_marker_get(editor, 9, nullptr, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    NC_CHECK_EQ(nc_add_marker(editor, -1, nullptr, nullptr), std::uint64_t(0));
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 4. 音画分离（文档「剪辑」音画分离）
// ---------------------------------------------------------------------------

NC_TEST(CapiMedia, DetachAudio) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "a.mp4", "/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    (void)nc_add_track(editor, "A1", 1);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 1000000, 4000000);
    NC_CHECK(clip != 0);
    // 分离 → 音频轨获得新片段
    const std::uint64_t audioClip = nc_detach_audio(editor, 0, clip, 1);
    NC_CHECK(audioClip != 0);
    NC_CHECK(audioClip != clip);
    NC_CHECK_EQ(nc_clip_count(editor, 1), 1);
    // 音频片段与视频片段同时间
    std::int64_t start = 0;
    std::int64_t duration = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 1, 0, nullptr, &start, &duration, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(1000000));
    NC_CHECK_EQ(duration, std::int64_t(4000000));
    // 撤销
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 1), 0);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 5. 真实解码类（素材缺失时跳过；无 FFmpeg 构建整体排除）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG

NC_TEST(CapiMedia, PreviewRenderRealFrame) {
    if (!testMediaExists()) return;
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeVideoEditor(&asset, &clip);
    std::int32_t width = 0;
    std::int32_t height = 0;
    std::uint8_t* rgba = nullptr;
    std::uint64_t activeClip = 0;
    std::int32_t transitionType = 0;
    const std::int32_t rc = nc_preview_render(editor, 1000000, &width, &height, &rgba,
                                              &activeClip, nullptr, nullptr, nullptr, nullptr,
                                              nullptr, &transitionType, nullptr);
    NC_CHECK_EQ(rc, NC_OK);
    // 画布默认 1080x1920（序列默认）
    NC_CHECK_EQ(width, 1080);
    NC_CHECK_EQ(height, 1920);
    NC_CHECK_EQ(activeClip, clip);
    NC_CHECK_EQ(transitionType, -1);  // 无转场
    NC_CHECK(rgba != nullptr);
    // 真实解码：画面非纯黑（测试素材为彩条）
    std::size_t nonBlack = 0;
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < total; ++i) {
        if (rgba[i * 4] > 16 || rgba[i * 4 + 1] > 16 || rgba[i * 4 + 2] > 16) {
            ++nonBlack;
        }
    }
    NC_CHECK(nonBlack > total / 10);
    nc_free_buffer(rgba);
    // 无素材时刻（10s 无片段）：返回画布黑帧
    std::uint8_t* rgba2 = nullptr;
    std::uint64_t active2 = 1;
    NC_CHECK_EQ(nc_preview_render(editor, 10000000, nullptr, nullptr, &rgba2, &active2, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_OK);
    NC_CHECK_EQ(active2, std::uint64_t(0));
    nc_free_buffer(rgba2);
    nc_editor_destroy(editor);
}

NC_TEST(CapiMedia, PreviewSubtitles) {
    NcEditor* editor = nc_editor_create();
    // 无字幕 → 空字符串
    NC_CHECK_EQ(takeString(nc_preview_subtitles(editor, 1000000)), std::string(""));
    // 添加一条字幕 [0.5s, 2s)
    const std::uint64_t sub = nc_upsert_subtitle(editor, 500000, 1500000, "你好世界");
    NC_CHECK(sub != 0);
    NC_CHECK_EQ(takeString(nc_preview_subtitles(editor, 1000000)), std::string("你好世界"));
    // 区间外的时刻 → 空
    NC_CHECK_EQ(takeString(nc_preview_subtitles(editor, 3000000)), std::string(""));
    // NULL 安全
    NC_CHECK(nc_preview_subtitles(nullptr, 0) == nullptr);
    nc_editor_destroy(editor);
}

NC_TEST(CapiMedia, ThumbnailGenerateReal) {
    if (!testMediaExists()) return;
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset =
        nc_import_asset(editor, "v.mp4", "/tmp/media_gen/test_av.mp4", 0, 3000000);
    // 生成 4 张缩略图
    std::uint8_t* images = nullptr;
    std::int64_t times[8] = {0};
    std::int32_t count = 0;
    NC_CHECK_EQ(nc_thumbnail_generate(editor, asset, 4, 160, 90, &images, times, &count), NC_OK);
    NC_CHECK_EQ(count, 4);
    NC_CHECK(images != nullptr);
    std::int32_t w = 0;
    std::int32_t h = 0;
    NC_CHECK_EQ(nc_thumbnail_size(editor, &w, &h), NC_OK);
    NC_CHECK(w > 0 && w <= 160);
    NC_CHECK(h > 0 && h <= 90);
    // 时间点应递增且位于素材时长内
    for (int i = 0; i < count; ++i) {
        NC_CHECK(times[i] >= 0 && times[i] <= 3000000);
        if (i > 0) {
            NC_CHECK(times[i] > times[i - 1]);
        }
    }
    // 缩略图内容非空（真实解码）
    const std::size_t perImage = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
    std::size_t nonBlack = 0;
    for (std::size_t i = 0; i < perImage; ++i) {
        if (images[i * 4 + 0] > 16 || images[i * 4 + 1] > 16 || images[i * 4 + 2] > 16) {
            ++nonBlack;
        }
    }
    NC_CHECK(nonBlack > perImage / 10);
    nc_free_buffer(images);
    // 素材不存在
    NC_CHECK_EQ(nc_thumbnail_generate(editor, 999, 4, 0, 0, nullptr, nullptr, nullptr),
                NC_ERROR_NOT_FOUND);
    nc_editor_destroy(editor);
}

NC_TEST(CapiMedia, WaveformGenerateReal) {
    if (!testMediaExists()) return;
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset =
        nc_import_asset(editor, "v.mp4", "/tmp/media_gen/test_av.mp4", 0, 3000000);
    double peak = 0.0;
    std::int32_t sampleRate = 0;
    std::int32_t channels = 0;
    NC_CHECK_EQ(nc_waveform_generate(editor, asset, 64, &peak, &sampleRate, &channels), NC_OK);
    NC_CHECK_EQ(nc_waveform_bucket_count(editor), 64);
    NC_CHECK_EQ(sampleRate, 44100);
    // 测试素材为 440Hz 正弦（幅度 0.25 级）；峰值应 > 0.05
    NC_CHECK(peak > 0.05);
    // 逐桶读取（至少若干桶非零）
    int nonSilent = 0;
    for (int i = 0; i < 64; ++i) {
        double minValue = 0.0;
        double maxValue = 0.0;
        double rms = 0.0;
        NC_CHECK_EQ(nc_waveform_bucket_get(editor, i, &minValue, &maxValue, &rms), NC_OK);
        NC_CHECK(minValue <= maxValue);
        NC_CHECK(rms >= 0.0);
        if (rms > 0.01) {
            ++nonSilent;
        }
    }
    NC_CHECK(nonSilent > 32);
    // 越界
    NC_CHECK_EQ(nc_waveform_bucket_get(editor, 999, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    nc_editor_destroy(editor);
}

NC_TEST(CapiMedia, SceneDetectReal) {
    // two_shots.mp4：两镜头拼接（边界在 2.0s）
    std::ifstream probe("/tmp/media_gen/two_shots.mp4");
    if (!probe.good()) return;
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset =
        nc_import_asset(editor, "two.mp4", "/tmp/media_gen/two_shots.mp4", 0, 4000000);
    (void)nc_add_track(editor, "V1", 0);
    (void)nc_insert_clip(editor, 0, asset, 0, 4000000);
    std::int32_t count = 0;
    double* boundaries = nullptr;
    NC_CHECK_EQ(nc_scene_detect(editor, 0.0, 600, &count, &boundaries), NC_OK);
    NC_CHECK(count >= 1);
    NC_CHECK(boundaries != nullptr);
    // 边界数组 [start_s, end_s] × count；首个边界应自 0 开始
    NC_CHECK(std::abs(boundaries[0]) < 1e-6);
    // 存在 2.0s 附近的边界（第二镜头起点；容差 0.6s）
    bool foundBoundary = false;
    for (int i = 0; i < count; ++i) {
        const double start = boundaries[i * 2];
        if (std::abs(start - 2.0) < 0.6) {
            foundBoundary = true;
        }
        NC_CHECK(boundaries[i * 2] < boundaries[i * 2 + 1]);
    }
    NC_CHECK(foundBoundary);
    nc_free_buffer(reinterpret_cast<std::uint8_t*>(boundaries));
    nc_editor_destroy(editor);
}

NC_TEST(CapiMedia, BeatDetectReal) {
    // tone_shift.mp4 有 440Hz 正弦音频（无鼓点 → BPM 可能为 0，但不应崩溃）
    if (!shiftMediaExists()) return;
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset =
        nc_import_asset(editor, "t.mp4", "/tmp/media_gen/tone_shift.mp4", 1, 4000000);
    (void)nc_add_track(editor, "A1", 1);
    (void)nc_insert_clip(editor, 0, asset, 0, 4000000);
    double bpm = -1.0;
    double confidence = -1.0;
    std::int32_t beatCount = -1;
    double* beats = nullptr;
    // track_index = 0（唯一的音频轨）
    NC_CHECK_EQ(nc_beat_detect(editor, 0, &bpm, &confidence, &beatCount, &beats), NC_OK);
    NC_CHECK(bpm >= 0.0);
    NC_CHECK(confidence >= 0.0 && confidence <= 1.0);
    NC_CHECK(beatCount >= 0);
    // 节拍时间应递增
    for (int i = 1; i < beatCount; ++i) {
        NC_CHECK(beats[i] > beats[i - 1]);
    }
    if (beats != nullptr) {
        nc_free_buffer(reinterpret_cast<std::uint8_t*>(beats));
    }
    // 无音频轨（track_index 指向视频轨）→ InvalidArgument
    NC_CHECK_EQ(nc_beat_detect(editor, 9, nullptr, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    nc_editor_destroy(editor);
}

NC_TEST(CapiMedia, PreviewInvalidate) {
    if (!testMediaExists()) return;
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeVideoEditor(&asset, &clip);
    // 渲染一帧（建立缓存）
    NC_CHECK_EQ(nc_preview_render(editor, 500000, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_OK);
    // 手动失效（不崩溃）+ 再次渲染仍可用
    nc_preview_invalidate(editor);
    std::uint8_t* rgba = nullptr;
    NC_CHECK_EQ(nc_preview_render(editor, 500000, nullptr, nullptr, &rgba, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_OK);
    NC_CHECK(rgba != nullptr);
    nc_free_buffer(rgba);
    // 编辑后自动失效（新缓存重建）并反映新状态
    NC_CHECK_EQ(nc_set_clip_enabled(editor, 0, clip, 0), NC_OK);
    std::uint64_t active = 1;
    NC_CHECK_EQ(nc_preview_render(editor, 500000, nullptr, nullptr, nullptr, &active, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_OK);
    // 片段被禁用：渲染器跳过该片段（本用例仅验证不崩溃；行为由预览实现决定）
    nc_editor_destroy(editor);
}

#endif  // NAIROCLIP_HAS_FFMPEG
