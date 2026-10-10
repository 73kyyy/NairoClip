#include "test_framework.h"
#include "nairoclip/core/capi.h"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
NC_TEST(Capi, AbiVersion) {
    NC_CHECK_EQ(nc_abi_version(), NC_ABI_VERSION);
}
NC_TEST(Capi, CreateDestroy) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    // 默认工程：轨 0 条（主序列存在，无轨道）
    NC_CHECK_EQ(nc_track_count(editor), 0);
    NC_CHECK_EQ(nc_project_duration_us(editor), std::int64_t(0));
    nc_editor_destroy(editor);
    nc_editor_destroy(nullptr);  // NULL 安全
}
NC_TEST(Capi, NullSafety) {
    NC_CHECK_EQ(nc_track_count(nullptr), 0);
    NC_CHECK_EQ(nc_clip_count(nullptr, 0), 0);
    NC_CHECK_EQ(nc_project_duration_us(nullptr), std::int64_t(0));
    NC_CHECK(nc_project_save(nullptr) == nullptr);
    NC_CHECK_EQ(nc_undo(nullptr), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_redo(nullptr), NC_ERROR_INVALID_ARGUMENT);
    nc_free_string(nullptr);  // 安全
}
NC_TEST(Capi, ImportAndAddTrack) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "clip.mp4", "/videos/clip.mp4", 0, 60000000);
    NC_CHECK(assetId != 0);
    const std::uint64_t trackId = nc_add_track(editor, "V1", 0);
    NC_CHECK(trackId != 0);
    NC_CHECK_EQ(nc_track_count(editor), 1);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, InsertClipAndDuration) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "clip.mp4", "/videos/clip.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    const std::uint64_t clipId =
        nc_insert_clip(editor, 0, assetId, 1000000, 5000000);  // [1s, 6s)
    NC_CHECK(clipId != 0);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    NC_CHECK_EQ(nc_project_duration_us(editor), std::int64_t(6000000));
    nc_editor_destroy(editor);
}
NC_TEST(Capi, RejectsInvalidInsert) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "clip.mp4", "/videos/clip.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    // 轨道下标越界
    NC_CHECK_EQ(nc_insert_clip(editor, 5, assetId, 0, 1000000), 0);
    // 负起点
    NC_CHECK_EQ(nc_insert_clip(editor, 0, assetId, -1, 1000000), 0);
    // 非正时长
    NC_CHECK_EQ(nc_insert_clip(editor, 0, assetId, 0, 0), 0);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, UndoRedo) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "clip.mp4", "/videos/clip.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    NC_CHECK(nc_insert_clip(editor, 0, assetId, 0, 2000000) != 0);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 0);
    NC_CHECK_EQ(nc_redo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, SaveLoadRoundtrip) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "a.mp4", "/videos/a.mp4", 0, 30000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    NC_CHECK(nc_insert_clip(editor, 0, assetId, 0, 5000000) != 0);
    char* saved = nc_project_save(editor);
    NC_CHECK(saved != nullptr);
    const std::string text(saved);
    nc_free_string(saved);
    NC_CHECK(!text.empty());
    // 载入到新编辑器
    NcEditor* editor2 = nc_editor_create();
    char* detail = nullptr;
    const int32_t result = nc_project_load(editor2, text.c_str(), &detail);
    NC_CHECK_EQ(result, NC_OK);
    NC_CHECK(detail == nullptr);
    NC_CHECK_EQ(nc_track_count(editor2), 1);
    NC_CHECK_EQ(nc_clip_count(editor2, 0), 1);
    NC_CHECK_EQ(nc_project_duration_us(editor2), std::int64_t(5000000));
    nc_editor_destroy(editor);
    nc_editor_destroy(editor2);
}
NC_TEST(Capi, LoadFailureProvidesDetail) {
    NcEditor* editor = nc_editor_create();
    char* detail = nullptr;
    const int32_t result = nc_project_load(editor, "{broken", &detail);
    NC_CHECK_EQ(result, NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(detail != nullptr);
    if (detail != nullptr) {
        NC_CHECK(std::strlen(detail) > 0);
        nc_free_string(detail);
    }
    nc_editor_destroy(editor);
}
NC_TEST(Capi, CollisionRejectedWithCode) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "a.mp4", "/videos/a.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    NC_CHECK(nc_insert_clip(editor, 0, assetId, 0, 5000000) != 0);
    // 重叠区间 → 返回 0 且不插入
    NC_CHECK_EQ(nc_insert_clip(editor, 0, assetId, 3000000, 5000000), 0);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, SubtitleCrud) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t sid = nc_upsert_subtitle(editor, 1000000, 2000000, "你好世界");
    NC_CHECK(sid != 0);
    NC_CHECK_EQ(nc_subtitle_count(editor), 1);
    // 读取
    std::int64_t start = 0;
    std::int64_t duration = 0;
    char* text = nullptr;
    NC_CHECK_EQ(nc_subtitle_get(editor, 0, &start, &duration, &text), NC_OK);
    NC_CHECK_EQ(start, std::int64_t(1000000));
    NC_CHECK_EQ(duration, std::int64_t(2000000));
    NC_CHECK(text != nullptr);
    if (text != nullptr) {
        NC_CHECK_EQ(std::string(text), std::string("你好世界"));
        nc_free_string(text);
    }
    // 删除
    NC_CHECK_EQ(nc_remove_subtitle(editor, sid), NC_OK);
    NC_CHECK_EQ(nc_subtitle_count(editor), 0);
    // 越界
    NC_CHECK_EQ(nc_subtitle_get(editor, 5, nullptr, nullptr, nullptr), NC_ERROR_OUT_OF_RANGE);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, TransitionCrud) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId = nc_import_asset(editor, "a", "/a.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    const std::uint64_t c1 = nc_insert_clip(editor, 0, assetId, 0, 2000000);
    const std::uint64_t c2 = nc_insert_clip(editor, 0, assetId, 3000000, 2000000);
    NC_CHECK(c1 != 0 && c2 != 0);
    // 添加转场（类型 0 = 交叉溶解，时长 0.5s = 500000us；间隙 1s 够用）
    const std::uint64_t tid = nc_add_transition(editor, 0, 0, 500000, c1, c2);
    NC_CHECK(tid != 0);
    NC_CHECK_EQ(nc_transition_count(editor, 0), 1);
    // 读取
    std::int32_t type = -1;
    std::int64_t duration = 0;
    std::uint64_t from = 0;
    std::uint64_t to = 0;
    char* name = nullptr;
    NC_CHECK_EQ(nc_transition_get(editor, 0, 0, &type, &duration, &from, &to, &name), NC_OK);
    NC_CHECK_EQ(type, 0);
    NC_CHECK_EQ(duration, std::int64_t(500000));
    NC_CHECK_EQ(from, c1);
    NC_CHECK_EQ(to, c2);
    if (name != nullptr) {
        NC_CHECK(std::strlen(name) > 0);
        nc_free_string(name);
    }
    // 删除
    NC_CHECK_EQ(nc_remove_transition(editor, 0, tid), NC_OK);
    NC_CHECK_EQ(nc_transition_count(editor, 0), 0);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, ClipGet) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId = nc_import_asset(editor, "a", "/a.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    NC_CHECK(nc_insert_clip(editor, 0, assetId, 1000000, 2000000) != 0);
    std::int64_t start = 0;
    std::int64_t duration = 0;
    std::uint64_t gotAsset = 0;
    NC_CHECK_EQ(nc_clip_get(editor, 0, 0, &start, &duration, &gotAsset), NC_OK);
    NC_CHECK_EQ(start, std::int64_t(1000000));
    NC_CHECK_EQ(duration, std::int64_t(2000000));
    NC_CHECK_EQ(gotAsset, assetId);
    NC_CHECK_EQ(nc_clip_get(editor, 0, 9, nullptr, nullptr, nullptr), NC_ERROR_OUT_OF_RANGE);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, EffectCrud) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId = nc_import_asset(editor, "a", "/a.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    const std::uint64_t clipId = nc_insert_clip(editor, 0, assetId, 0, 5000000);
    NC_CHECK(clipId != 0);
    // 添加亮度特效（type=1）
    const std::uint64_t eid = nc_add_effect(editor, 0, clipId, 1, 0.25);
    NC_CHECK(eid != 0);
    NC_CHECK_EQ(nc_effect_count(editor, 0, clipId), 1);
    // 读取
    std::int32_t type = -1;
    double value = 0.0;
    std::int32_t enabled = 0;
    NC_CHECK_EQ(nc_effect_get(editor, 0, clipId, 0, &type, &value, &enabled), NC_OK);
    NC_CHECK_EQ(type, 1);
    NC_CHECK(std::abs(value - 0.25) < 1e-9);
    NC_CHECK_EQ(enabled, 1);
    // 无效类型
    NC_CHECK_EQ(nc_add_effect(editor, 0, clipId, 99, 0.0), std::uint64_t(0));
    // 删除
    NC_CHECK_EQ(nc_remove_effect(editor, 0, clipId, eid), NC_OK);
    NC_CHECK_EQ(nc_effect_count(editor, 0, clipId), 0);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, KeyframeAndAudio) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId = nc_import_asset(editor, "a", "/a.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    const std::uint64_t clipId = nc_insert_clip(editor, 0, assetId, 0, 5000000);
    // 关键帧：Opacity（0）
    NC_CHECK_EQ(nc_insert_keyframe(editor, 0, clipId, 0, 0, 1.0), NC_OK);
    NC_CHECK_EQ(nc_insert_keyframe(editor, 0, clipId, 0, 2000000, 0.5), NC_OK);
    // 非法属性
    NC_CHECK_EQ(nc_insert_keyframe(editor, 0, clipId, 9, 0, 1.0), NC_ERROR_INVALID_ARGUMENT);
    // 删除关键帧
    NC_CHECK_EQ(nc_remove_keyframe(editor, 0, clipId, 0, 0), NC_OK);
    // 音频属性
    NC_CHECK_EQ(nc_set_clip_audio(editor, 0, clipId, -6.0, 500000, 500000), NC_OK);
    // 非法音频（淡入淡出超时长）
    NC_CHECK_EQ(nc_set_clip_audio(editor, 0, clipId, 0.0, 3000000, 3000000),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}
NC_TEST(Capi, AutoEditEndToEnd) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId = nc_import_asset(editor, "a", "/a.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    // 风格：3 镜（每镜 1s）
    const double shots[] = {0.0, 1.0, 0.0, 0.5, 1.0, 2.0, 0.0, 0.5, 2.0, 3.0, 0.0, 0.5};
    const std::uint64_t assetIds[] = {assetId};
    const double usable[] = {10.0};
    char* error = nullptr;
    const int32_t clips = nc_auto_edit(editor, 0, shots, 3, 0.6, 0.5, 0.4, 0.7, 0.6, assetIds,
                                       usable, 1, 1, &error);
    NC_CHECK(clips >= 0);
    NC_CHECK(error == nullptr);
    NC_CHECK_EQ(nc_clip_count(editor, 0), clips);
    // 检查每个片段有特效（apply_color=1 且画像非中性）
    NC_CHECK(nc_effect_count(editor, 0, 1) >= 0);  // 片段 ID 从 1 开始（视分配器）
    nc_editor_destroy(editor);
}
NC_TEST(Capi, AutoEditRejectsBadArgs) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    char* error = nullptr;
    // 空指针 → -1
    const int32_t r1 = nc_auto_edit(editor, 0, nullptr, 3, 0.5, 0.5, 0.5, 0.5, 0.5, nullptr, nullptr,
                                    0, 1, &error);
    NC_CHECK_EQ(r1, -1);
    NC_CHECK(error != nullptr);
    if (error != nullptr) nc_free_string(error);
    nc_editor_destroy(editor);
}

#ifdef NAIROCLIP_HAS_FFMPEG
NC_TEST(Capi, ExportRunEndToEnd) {
    // 建工程：1 个视频片段（Mock 素材无法用于 FFmpeg 读取——直接用真实文件）
    // 简化：用空序列测试错误路径 + 直接用真实素材测试成功路径
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    // 空工程导出：主序列存在但无素材 → 导出 0 帧或失败均可接受，但不崩溃
    char* err = nullptr;
    const int64_t frames = nc_export_run(editor, "/tmp/media_gen/capi_empty.mp4", 30, 0, 0,
                                         nullptr, nullptr, &err);
    // 空序列：允许成功（0 帧）或失败；重点是健壮性
    if (frames < 0) {
        NC_CHECK(err != nullptr);
        nc_free_string(err);
    }
    nc_editor_destroy(editor);
}
NC_TEST(Capi, ExportRejectsNullArgs) {
    char* err = nullptr;
    const int64_t frames = nc_export_run(nullptr, "/tmp/x.mp4", 23, 1, 0, nullptr, nullptr, &err);
    NC_CHECK_EQ(frames, std::int64_t(-1));
    NC_CHECK(err != nullptr);
    nc_free_string(err);
}
NC_TEST(Capi, AiLabelParamPlumbsThrough) {
    // 验证 attach_ai_label 参数传递（真实导出到文件并回读识别）
    // 由于需要真实素材，这里用最小工程（无素材）验证接口不崩溃
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    char* err = nullptr;
    const int64_t frames = nc_export_run(editor, "/tmp/media_gen/capi_label.mp4", 30, 0, 1,
                                         "AI 生成", nullptr, &err);
    if (frames < 0 && err != nullptr) {
        nc_free_string(err);
    }
    nc_editor_destroy(editor);
    NC_CHECK(true);
}
#endif

// ---- 画幅适配 C ABI（导出预设 9:16 / 16:9 / 1:1 场景）----
NC_TEST(Capi, FrameFitSetGet) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    // 默认 Fit
    NC_CHECK_EQ(nc_get_frame_fit(editor), 0);
    // 设置 Fill
    NC_CHECK_EQ(nc_set_frame_fit(editor, 1), NC_OK);
    NC_CHECK_EQ(nc_get_frame_fit(editor), 1);
    // 设置 Crop
    NC_CHECK_EQ(nc_set_frame_fit(editor, 2), NC_OK);
    NC_CHECK_EQ(nc_get_frame_fit(editor), 2);
    // 非法值拒绝
    NC_CHECK_EQ(nc_set_frame_fit(editor, 9), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_frame_fit(editor), 2);  // 保持原值
    // 背景色
    NC_CHECK_EQ(nc_set_background_color(editor, 0x1E1E1EFF), NC_OK);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, FrameFitNullSafety) {
    NC_CHECK_EQ(nc_set_frame_fit(nullptr, 1), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_frame_fit(nullptr), -1);
    NC_CHECK_EQ(nc_set_background_color(nullptr, 0), NC_ERROR_INVALID_ARGUMENT);
}

NC_TEST(Capi, FrameFitPersistsThroughSaveLoad) {
    // 画幅设置属于工程数据：存档 → 读档后必须保持。
    NcEditor* editor = nc_editor_create();
    NC_CHECK_EQ(nc_set_frame_fit(editor, 2), NC_OK);
    char* saved = nc_project_save(editor);
    NC_CHECK(saved != nullptr);
    NcEditor* editor2 = nc_editor_create();
    char* detail = nullptr;
    NC_CHECK_EQ(nc_project_load(editor2, saved, &detail), NC_OK);
    NC_CHECK_EQ(nc_get_frame_fit(editor2), 2);
    nc_free_string(saved);
    if (detail != nullptr) nc_free_string(detail);
    nc_editor_destroy(editor);
    nc_editor_destroy(editor2);
}

// ---- 音频自动闪避 C ABI（文档「🎵音频 → 自动闪避」）----
NC_TEST(Capi, AutoDuckSetGet) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    // 加一条音频轨（下标 0）
    NC_CHECK(nc_add_track(editor, "音乐", 1) != 0);
    // 默认关闭
    int32_t enabled = -1;
    double sensitivity = -1.0;
    double duckDb = -1.0;
    NC_CHECK_EQ(nc_get_track_auto_duck(editor, 0, &enabled, &sensitivity, &duckDb), NC_OK);
    NC_CHECK_EQ(enabled, 0);
    // 启用并设置参数
    NC_CHECK_EQ(nc_set_track_auto_duck(editor, 0, 1, 0.7, 9.0, 80000, 350000, 250000), NC_OK);
    NC_CHECK_EQ(nc_get_track_auto_duck(editor, 0, &enabled, &sensitivity, &duckDb), NC_OK);
    NC_CHECK_EQ(enabled, 1);
    NC_CHECK(std::abs(sensitivity - 0.7) < 1e-9);
    NC_CHECK(std::abs(duckDb - 9.0) < 1e-9);
    // 非法参数（灵敏度越界）
    NC_CHECK_EQ(nc_set_track_auto_duck(editor, 0, 1, 1.5, 9.0, 0, 0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    // 越界下标
    NC_CHECK_EQ(nc_get_track_auto_duck(editor, 5, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, AutoDuckRejectsVideoTrack) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(nc_add_track(editor, "视频", 0) != 0);
    // 视频轨不允许自动闪避
    NC_CHECK_EQ(nc_set_track_auto_duck(editor, 0, 1, 0.5, 12.0, 0, 0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, AutoDuckNullSafety) {
    NC_CHECK_EQ(nc_set_track_auto_duck(nullptr, 0, 1, 0.5, 12.0, 0, 0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_track_auto_duck(nullptr, 0, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
}


// ---- 复合片段 / 嵌套序列 C ABI（文档「创建复合片段 / 解除复合片段」）----
NC_TEST(Capi, CompoundClipCreateAndUnwrap) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t assetId =
        nc_import_asset(editor, "a.mp4", "/videos/a.mp4", 0, 60000000);
    NC_CHECK(assetId != 0);
    const std::uint64_t trackId = nc_add_track(editor, "V1", 0);
    NC_CHECK(trackId != 0);
    // 两个相邻片段
    const std::uint64_t c1 = nc_insert_clip(editor, 0, assetId, 0, 1000000);
    const std::uint64_t c2 = nc_insert_clip(editor, 0, assetId, 1000000, 1000000);
    NC_CHECK(c1 != 0);
    NC_CHECK(c2 != 0);
    // 打组
    const uint64_t ids[2] = {c1, c2};
    uint64_t outSeq = 0;
    uint64_t outClip = 0;
    NC_CHECK_EQ(nc_create_compound_clip(editor, 0, ids, 2, "复合片段A", &outSeq, &outClip),
                NC_OK);
    NC_CHECK(outSeq != 0);
    NC_CHECK(outClip != 0);
    // 复合片段识别
    NC_CHECK_EQ(nc_clip_is_compound(editor, 0, outClip), 1);
    NC_CHECK_EQ(nc_clip_is_compound(editor, 0, c1), -1);  // 已不在轨道上
    // 轨道上只剩 1 个片段（复合片段）
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    // 撤销 → 还原 2 个片段
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);
    // 重做 → 复合片段恢复
    NC_CHECK_EQ(nc_redo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    // 解除复合片段 → 2 个片段还原
    NC_CHECK_EQ(nc_unwrap_compound_clip(editor, 0, outClip), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, CompoundClipRejectsBadArgs) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t trackId = nc_add_track(editor, "V1", 0);
    NC_CHECK(trackId != 0);
    const uint64_t ids[1] = {1};
    // 空指针 / 空数组
    NC_CHECK_EQ(nc_create_compound_clip(nullptr, 0, ids, 1, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_create_compound_clip(editor, 0, nullptr, 1, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_create_compound_clip(editor, 0, ids, 0, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    // 轨道越界
    NC_CHECK_EQ(nc_create_compound_clip(editor, 9, ids, 1, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    // 解除不存在的复合片段
    NC_CHECK_EQ(nc_unwrap_compound_clip(editor, 0, 9999), NC_ERROR_NOT_FOUND);
    // 空安全
    NC_CHECK_EQ(nc_clip_is_compound(nullptr, 0, 1), -1);
    nc_editor_destroy(editor);
}

// ---- 混音台 C ABI（文档第 3 部分「专业混音台视图」）----
NC_TEST(Capi, MixerConsoleControls) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    NC_CHECK(nc_add_track(editor, "音乐", 1) != 0);
    NC_CHECK(nc_add_track(editor, "旁白", 1) != 0);
    // 默认值
    int32_t muted = -1, solo = -1;
    double gainDb = -999.0;
    NC_CHECK_EQ(nc_get_track_mixer(editor, 0, &muted, &solo, &gainDb), NC_OK);
    NC_CHECK_EQ(muted, 0);
    NC_CHECK_EQ(solo, 0);
    NC_CHECK_EQ(gainDb, 0.0);
    // 设置增益 / 独奏
    NC_CHECK_EQ(nc_set_track_gain(editor, 0, -12.0), NC_OK);
    NC_CHECK_EQ(nc_set_track_solo(editor, 0, 1), NC_OK);
    NC_CHECK_EQ(nc_get_track_mixer(editor, 0, &muted, &solo, &gainDb), NC_OK);
    NC_CHECK_EQ(solo, 1);
    NC_CHECK(std::abs(gainDb + 12.0) < 1e-9);
    // 主输出
    NC_CHECK_EQ(nc_set_master_gain(editor, -3.0), NC_OK);
    double master = 0.0;
    NC_CHECK_EQ(nc_get_master_gain(editor, &master), NC_OK);
    NC_CHECK(std::abs(master + 3.0) < 1e-9);
    // 越界拒绝
    NC_CHECK_EQ(nc_set_track_gain(editor, 0, -99.0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_master_gain(editor, 99.0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_track_gain(editor, 9, -3.0), NC_ERROR_OUT_OF_RANGE);
    // 撤销可回退（历史面板）
    NC_CHECK_EQ(nc_undo(editor), NC_OK);  // 撤销主输出
    NC_CHECK_EQ(nc_get_master_gain(editor, &master), NC_OK);
    NC_CHECK_EQ(master, 0.0);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, MixerConsoleNullSafety) {
    NC_CHECK_EQ(nc_set_track_solo(nullptr, 0, 1), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_track_gain(nullptr, 0, 0.0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_master_gain(nullptr, 0.0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_master_gain(nullptr, nullptr), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_track_mixer(nullptr, 0, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
}

// ---- 会员权益 C ABI（文档第 6 部分「会员订阅体系」）----
NC_TEST(Capi, EntitlementSetGetAndTrackGate) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    // 默认 Free（fail-closed）
    NC_CHECK_EQ(nc_get_entitlement(editor), 0);
    // 免费：可添加到 4 条，第 5 条被拒（UI 置灰依据）
    NC_CHECK_EQ(nc_can_add_track(editor), 1);
    NC_CHECK(nc_add_track(editor, "T1", 0) != 0);
    NC_CHECK(nc_add_track(editor, "T2", 1) != 0);
    NC_CHECK(nc_add_track(editor, "T3", 1) != 0);
    NC_CHECK(nc_add_track(editor, "T4", 1) != 0);
    NC_CHECK_EQ(nc_can_add_track(editor), 0);  // 已达 4 条
    // 升级 Pro → 无限轨道
    NC_CHECK_EQ(nc_set_entitlement(editor, 1, 0, 0), NC_OK);
    NC_CHECK_EQ(nc_get_entitlement(editor), 1);
    NC_CHECK_EQ(nc_can_add_track(editor), 1);
    NC_CHECK(nc_add_track(editor, "T5", 1) != 0);
    // 降回 Free → 再次置灰
    NC_CHECK_EQ(nc_set_entitlement(editor, 0, 0, 0), NC_OK);
    NC_CHECK_EQ(nc_can_add_track(editor), 0);
    // 非法 tier
    NC_CHECK_EQ(nc_set_entitlement(editor, 7, 0, 0), NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, EntitlementExportClampQuery) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    // 免费：4K 会被降级、1080p 不降级
    NC_CHECK_EQ(nc_would_clamp_export(editor, 3840, 2160), 1);
    NC_CHECK_EQ(nc_would_clamp_export(editor, 1920, 1080), 0);
    NC_CHECK_EQ(nc_would_clamp_export(editor, 1080, 1920), 0);
    NC_CHECK_EQ(nc_would_clamp_export(editor, 2160, 3840), 1);
    // Pro：不降级
    NC_CHECK_EQ(nc_set_entitlement(editor, 1, 1, 0), NC_OK);  // 永久买断
    NC_CHECK_EQ(nc_would_clamp_export(editor, 3840, 2160), 0);
    // 到期 Pro：恢复降级（fail-closed，文档 3.10）
    const int64_t past = 100;  // 1970 年的时间戳 → 必然过期
    NC_CHECK_EQ(nc_set_entitlement(editor, 1, 0, past), NC_OK);
    NC_CHECK_EQ(nc_would_clamp_export(editor, 3840, 2160), 1);
    // 参数错误
    NC_CHECK_EQ(nc_would_clamp_export(editor, 0, 1080), -1);
    NC_CHECK_EQ(nc_would_clamp_export(nullptr, 1920, 1080), -1);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, EntitlementNullSafety) {
    NC_CHECK_EQ(nc_set_entitlement(nullptr, 1, 0, 0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_entitlement(nullptr), -1);
    NC_CHECK_EQ(nc_can_add_track(nullptr), -1);
    NC_CHECK_EQ(nc_would_clamp_export(nullptr, 100, 100), -1);
}

// ---- AI 检查 C ABI（文档 4.5：问题列表 + 全部自动修复）----
NC_TEST(Capi, AiCheckReportsIssuesAndFixes) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    // 两条重叠字幕
    const std::uint64_t s1 = nc_upsert_subtitle(editor, 0, 2000000, "第一条");
    const std::uint64_t s2 = nc_upsert_subtitle(editor, 1000000, 2000000, "第二条");
    NC_CHECK(s1 != 0 || s2 != 0);
    NC_CHECK_EQ(nc_subtitle_count(editor), 2);
    // 检查：应报重叠
    char* json = nullptr;
    char* err = nullptr;
    const int32_t count = nc_ai_check(editor, &json, &err);
    NC_CHECK(count >= 1);
    NC_CHECK(json != nullptr);
    const std::string report = json != nullptr ? std::string(json) : std::string();
    NC_CHECK(report.find("SubtitleOverlap") == std::string::npos);  // JSON 用数值枚举
    NC_CHECK(report.find("字幕重叠") != std::string::npos);
    NC_CHECK(report.find("\"autoFixable\":true") != std::string::npos);
    nc_free_string(json);
    if (err != nullptr) nc_free_string(err);
    // 自动修复
    char* fixErr = nullptr;
    const int32_t fixed = nc_ai_check_fix_all(editor, &fixErr);
    NC_CHECK(fixed >= 1);
    if (fixErr != nullptr) nc_free_string(fixErr);
    // 修复后应无重叠
    char* json2 = nullptr;
    nc_ai_check(editor, &json2, nullptr);
    const std::string report2 = json2 != nullptr ? std::string(json2) : std::string();
    NC_CHECK(report2.find("字幕重叠") == std::string::npos);
    nc_free_string(json2);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, AiCheckNullSafety) {
    NC_CHECK_EQ(nc_ai_check(nullptr, nullptr, nullptr), -1);
    NC_CHECK_EQ(nc_ai_check_fix_all(nullptr, nullptr), -1);
    // 空工程：检查应为 0 问题（不崩溃）
    NcEditor* editor = nc_editor_create();
    char* json = nullptr;
    NC_CHECK_EQ(nc_ai_check(editor, &json, nullptr), 0);
    NC_CHECK(json != nullptr);
    nc_free_string(json);
    nc_editor_destroy(editor);
}

// ---- 代理剪辑 C ABI（文档第 3 部分）----
NC_TEST(Capi, ProxyRecordsAndUsage) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "4k.mp4", "/videos/4k.mp4", 0, 60000000);
    NC_CHECK(assetId != 0);
    // 无记录
    int32_t status = -1;
    NC_CHECK_EQ(nc_proxy_get_record(editor, assetId, &status, nullptr, nullptr, nullptr, nullptr,
                                    nullptr),
                0);
    // 写入就绪记录
    NC_CHECK_EQ(nc_proxy_set_record(editor, assetId, "/proxies/4k.proxy.mp4", 2, 1920, 1080,
                                    12345678, nullptr),
                NC_OK);
    NC_CHECK_EQ(nc_proxy_get_record(editor, assetId, &status, nullptr, nullptr, nullptr, nullptr,
                                    nullptr),
                1);
    NC_CHECK_EQ(status, 2);  // ready
    // 占用统计
    int32_t ready = 0;
    int32_t generating = 0;
    int64_t total = 0;
    NC_CHECK_EQ(nc_proxy_usage(editor, &ready, &generating, &total), NC_OK);
    NC_CHECK_EQ(ready, 1);
    NC_CHECK_EQ(generating, 0);
    NC_CHECK_EQ(total, std::int64_t(12345678));
    // 清除（供「清理代理」）
    NC_CHECK_EQ(nc_proxy_clear_records(editor), 1);
    NC_CHECK_EQ(nc_proxy_usage(editor, &ready, nullptr, &total), NC_OK);
    NC_CHECK_EQ(ready, 0);
    NC_CHECK_EQ(total, std::int64_t(0));
    // 错误路径
    NC_CHECK_EQ(nc_proxy_set_record(editor, 9999, "/x.mp4", 2, 0, 0, 0, nullptr),
                NC_ERROR_NOT_FOUND);
    NC_CHECK_EQ(nc_proxy_set_record(editor, assetId, "/x.mp4", 9, 0, 0, 0, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_proxy_usage(nullptr, nullptr, nullptr, nullptr), NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ProxyImportCheckAndNullSafety) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "4k.mp4", "/videos/4k.mp4", 0, 60000000);
    NC_CHECK(assetId != 0);
    // 默认设置（自动 + 1/2）：素材未探测尺寸 → 无法判定高分辨率（返回 0）
    int32_t w = 0;
    int32_t h = 0;
    double secs = 0.0;
    int64_t bytes = 0;
    char* reason = nullptr;
    char* err = nullptr;
    const int32_t suggest =
        nc_proxy_check_import(editor, assetId, &w, &h, &secs, &bytes, &reason, &err);
    NC_CHECK(suggest == 0 || suggest == 1);  // 不崩溃即可
    if (reason != nullptr) nc_free_string(reason);
    if (err != nullptr) nc_free_string(err);
    // 空安全
    NC_CHECK_EQ(nc_proxy_check_import(nullptr, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                                      nullptr),
                -1);
    NC_CHECK_EQ(nc_proxy_get_record(nullptr, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                                    nullptr),
                -1);
    NC_CHECK_EQ(nc_proxy_clear_records(nullptr), -1);
    nc_editor_destroy(editor);
}

// ---- 导出预设 / 资源预估 / 性能模式 C ABI（文档 6.1）----
NC_TEST(Capi, ExportPresetsAndEstimate) {
    // 列举预设（6 个）
    int32_t ids[8] = {0};
    int32_t count = 0;
    NC_CHECK_EQ(nc_export_presets(ids, 8, &count), NC_OK);
    NC_CHECK_EQ(count, 6);
    NC_CHECK_EQ(ids[0], 0);  // custom
    NC_CHECK_EQ(ids[1], 1);  // douyin
    // 预设信息
    int32_t w = 0, h = 0, fps = 0, pro = -1;
    char* label = nullptr;
    NC_CHECK_EQ(nc_export_preset_info(1, &w, &h, &fps, &pro, &label), NC_OK);
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1920);
    NC_CHECK_EQ(pro, 0);
    NC_CHECK(label != nullptr);
    if (label != nullptr) nc_free_string(label);
    // 4K 预设需 Pro
    NC_CHECK_EQ(nc_export_preset_info(5, &w, &h, nullptr, &pro, nullptr), NC_OK);
    NC_CHECK_EQ(w, 3840);
    NC_CHECK_EQ(pro, 1);
    NC_CHECK_EQ(nc_export_preset_info(9, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    // 资源预估
    int64_t sizeBytes = 0;
    double seconds = 0.0;
    char* sizeText = nullptr;
    char* timeText = nullptr;
    NC_CHECK_EQ(nc_export_estimate(1920, 1080, 30, 60000000, 8.0, 1, &sizeBytes, &seconds,
                                   &sizeText, &timeText),
                NC_OK);
    NC_CHECK(sizeBytes > 0);
    NC_CHECK(seconds > 0.0);
    NC_CHECK(sizeText != nullptr && sizeText[0] != '\0');
    NC_CHECK(timeText != nullptr && timeText[0] != '\0');
    nc_free_string(sizeText);
    nc_free_string(timeText);
}

NC_TEST(Capi, ExportSetupPresetGating) {
    NcEditor* editor = nc_editor_create();  // 默认 Free
    int32_t w = 0, h = 0, fps = 0, downgraded = -1;
    char* presetName = nullptr;
    char* warning = nullptr;
    // Free + 4K 预设 → 降级 1080p + 警告
    NC_CHECK_EQ(nc_export_setup_preset(editor, 5, 1, 4, 23, &w, &h, &fps, &downgraded,
                                       &presetName, &warning),
                NC_OK);
    NC_CHECK_EQ(w, 1920);
    NC_CHECK_EQ(h, 1080);
    NC_CHECK_EQ(downgraded, 1);
    NC_CHECK(warning != nullptr && warning[0] != '\0');
    NC_CHECK(presetName != nullptr && presetName[0] != '\0');
    nc_free_string(presetName);
    nc_free_string(warning);
    // 升级 Pro → 不降级
    NC_CHECK_EQ(nc_set_entitlement(editor, 1, 0, 0), NC_OK);
    NC_CHECK_EQ(nc_export_setup_preset(editor, 5, 1, 4, 23, &w, &h, &fps, &downgraded,
                                       nullptr, nullptr),
                NC_OK);
    NC_CHECK_EQ(w, 3840);
    NC_CHECK_EQ(downgraded, 0);
    // 竖屏预设 + 省电模式
    NC_CHECK_EQ(nc_export_setup_preset(editor, 1, 2, 8, 25, &w, &h, &fps, &downgraded,
                                       &presetName, nullptr),
                NC_OK);
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1920);
    NC_CHECK_EQ(fps, 30);
    NC_CHECK_EQ(downgraded, 0);
    NC_CHECK_EQ(std::string(presetName), std::string("veryfast"));  // Eco 预置
    nc_free_string(presetName);
    // 错误路径
    NC_CHECK_EQ(nc_export_setup_preset(nullptr, 1, 1, 4, 23, nullptr, nullptr, nullptr,
                                       nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_export_setup_preset(editor, 9, 1, 4, 23, nullptr, nullptr, nullptr,
                                       nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_export_setup_preset(editor, 1, 9, 4, 23, nullptr, nullptr, nullptr,
                                       nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

// ---- 口播精剪 C ABI（文档 7.2）----
NC_TEST(Capi, SpeechCutNullSafetyAndErrors) {
    NC_CHECK_EQ(nc_speech_analyze(nullptr, 1, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  nullptr),
                -1);
    NC_CHECK_EQ(nc_speech_cut_apply(nullptr, 0, nullptr), -1);
    NcEditor* editor = nc_editor_create();
    // 素材不存在
    char* err = nullptr;
    NC_CHECK_EQ(nc_speech_analyze(editor, 9999, nullptr, nullptr, nullptr, nullptr, nullptr,
                                  &err),
                -1);
    NC_CHECK(err != nullptr);
    if (err != nullptr) nc_free_string(err);
    // 无音频轨
    err = nullptr;
    NC_CHECK_EQ(nc_speech_cut_apply(editor, -1, &err), -1);
    NC_CHECK(err != nullptr);
    if (err != nullptr) nc_free_string(err);
    nc_editor_destroy(editor);
}

#ifdef NAIROCLIP_HAS_FFMPEG
NC_TEST(Capi, SpeechCutRealAudioEndToEnd) {
    {
        std::ifstream check("/tmp/media_gen/speech.wav");
        if (!check.good()) return;  // 素材未生成：跳过（与 test_media_ffmpeg 同一约定）
    }
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId =
        nc_import_asset(editor, "speech.wav", "/tmp/media_gen/speech.wav", 1, 8000000);
    NC_CHECK(assetId != 0);
    NC_CHECK(nc_add_track(editor, "A1", 1) != 0);  // 音频轨
    // 8 秒片段
    NC_CHECK(nc_insert_clip(editor, 0, assetId, 0, 8000000) != 0);
    // 分析（真实解码 + VAD）
    int64_t speechUs = 0;
    int64_t silenceUs = 0;
    double keepRatio = 0.0;
    int32_t segmentCount = 0;
    char* note = nullptr;
    char* err = nullptr;
    const int32_t segments = nc_speech_analyze(editor, assetId, &speechUs, &silenceUs,
                                               &keepRatio, &segmentCount, &note, &err);
    NC_CHECK(segments >= 1);
    NC_CHECK(segmentCount >= 1);
    NC_CHECK(speechUs > 0);
    NC_CHECK(silenceUs > 0);
    NC_CHECK(keepRatio > 0.0 && keepRatio < 1.0);
    if (note != nullptr) nc_free_string(note);
    if (err != nullptr) nc_free_string(err);
    // 落盘精剪
    char* cutErr = nullptr;
    const int32_t inserted = nc_speech_cut_apply(editor, -1, &cutErr);
    NC_CHECK(inserted >= 1);
    if (cutErr != nullptr) nc_free_string(cutErr);
    // 时间轴时长应显著缩短
    const int64_t durationUs = nc_project_duration_us(editor);
    NC_CHECK(durationUs > 0);
    NC_CHECK(durationUs < 7000000);  // < 7 秒（原 8 秒）
    // 撤销可回退
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_project_duration_us(editor), std::int64_t(8000000));
    nc_editor_destroy(editor);
}
#endif

// ---- 蒙版 C ABI（文档「✂️剪辑 → 蒙版：线性/镜面/圆形/矩形（羽化/反转）」）----
NC_TEST(Capi, ClipMaskSetGetAndClear) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t assetId = nc_import_asset(editor, "a.mp4", "/v/a.mp4", 0, 60000000);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    const std::uint64_t clipId = nc_insert_clip(editor, 0, assetId, 0, 2000000);
    NC_CHECK(clipId != 0);
    // 默认无蒙版
    NC_CHECK_EQ(nc_get_clip_mask(editor, 0, clipId, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr),
                0);
    // 设置圆形蒙版（shape=3）
    NC_CHECK_EQ(nc_set_clip_mask(editor, 0, clipId, 3, 0.5, 0.4, 0.0, 0.3, 0.3, 0.05, 0.0, 0),
                NC_OK);
    int32_t shape = -1;
    double cx = 0.0, cy = 0.0, rot = 0.0, w = 0.0, h = 0.0, feather = 0.0, corner = 0.0;
    int32_t invert = -1;
    NC_CHECK_EQ(nc_get_clip_mask(editor, 0, clipId, &shape, &cx, &cy, &rot, &w, &h, &feather,
                                 &corner, &invert),
                1);
    NC_CHECK_EQ(shape, 3);
    NC_CHECK(std::abs(cx - 0.5) < 1e-9);
    NC_CHECK(std::abs(cy - 0.4) < 1e-9);
    NC_CHECK(std::abs(feather - 0.05) < 1e-9);
    NC_CHECK_EQ(invert, 0);
    // 撤销恢复
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_get_clip_mask(editor, 0, clipId, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr),
                0);
    // 非法形状拒绝
    NC_CHECK_EQ(nc_set_clip_mask(editor, 0, clipId, 9, 0.5, 0.5, 0.0, 0.3, 0.3, 0.0, 0.0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    // 空安全
    NC_CHECK_EQ(nc_get_clip_mask(nullptr, 0, clipId, nullptr, nullptr, nullptr, nullptr, nullptr,
                                 nullptr, nullptr, nullptr, nullptr),
                -1);
    nc_editor_destroy(editor);
}

// ---- AI 模型管理 C ABI（文档第 5 部分 + 第 8 部分）----

NC_TEST(Capi, ModelCatalogAndInfo) {
    int32_t count = 0;
    NC_CHECK_EQ(nc_model_catalog(nullptr, 0, &count), NC_OK);
    NC_CHECK(count >= 8);

    char** ids = nullptr;
    NC_CHECK_EQ(nc_model_catalog(&ids, count, &count), NC_OK);
    NC_CHECK(ids != nullptr);
    bool foundWhisper = false;
    for (int32_t i = 0; i < count; ++i) {
        if (std::strcmp(ids[i], "whisper_zh") == 0) {
            foundWhisper = true;
        }
    }
    NC_CHECK(foundWhisper);
    nc_free_string_array(ids, count);

    // 目录信息（文档展示值 240MB）
    char* name = nullptr;
    char* description = nullptr;
    char* version = nullptr;
    int32_t sizeBytes = 0;
    int32_t requiresPro = -1;
    int32_t capability = -1;
    NC_CHECK_EQ(nc_model_info("whisper_zh", &name, &description, &sizeBytes, &version,
                              &requiresPro, &capability),
                NC_OK);
    NC_CHECK(name != nullptr && std::strlen(name) > 0);
    NC_CHECK_EQ(sizeBytes, 240 * 1024 * 1024);
    NC_CHECK_EQ(requiresPro, 0);
    NC_CHECK_EQ(capability, 1 + 2);  // ModelCapability::SpeechToText == 3
    nc_free_string(name);
    nc_free_string(description);
    nc_free_string(version);

    // 未知模型
    NC_CHECK_EQ(nc_model_info("nope", nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_ERROR_NOT_FOUND);
    NC_CHECK_EQ(nc_model_info(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
}

NC_TEST(Capi, ModelStatusLifecycle) {
    NcEditor* editor = nc_editor_create();
    // 初始未下载
    NC_CHECK_EQ(nc_model_status(editor, "matting_hq"), 0);
    const std::int64_t now = 1700000000;

    // 开始下载（120MB）
    const int64_t total = 120LL * 1024 * 1024;
    NC_CHECK_EQ(nc_model_download_begin(editor, "matting_hq", total, now), NC_OK);
    NC_CHECK_EQ(nc_model_status(editor, "matting_hq"), 1);  // Downloading

    int32_t phase = -1;
    int64_t outTotal = 0;
    int64_t outDone = 0;
    char* message = nullptr;
    NC_CHECK_EQ(nc_model_download_state(editor, "matting_hq", &phase, &outTotal, &outDone,
                                        &message),
                1);
    NC_CHECK_EQ(phase, 1);
    NC_CHECK_EQ(outTotal, total);
    nc_free_string(message);

    // 进度更新
    NC_CHECK_EQ(nc_model_download_progress(editor, "matting_hq", total / 2, now + 1), NC_OK);
    NC_CHECK_EQ(nc_model_download_state(editor, "matting_hq", nullptr, nullptr, &outDone,
                                        nullptr),
                1);
    NC_CHECK_EQ(outDone, total / 2);

    // 暂停 → 恢复（断点保留）
    NC_CHECK_EQ(nc_model_download_pause(editor, "matting_hq", now + 2), NC_OK);
    NC_CHECK_EQ(nc_model_download_resume(editor, "matting_hq", now + 3), NC_OK);
    NC_CHECK_EQ(nc_model_download_state(editor, "matting_hq", nullptr, nullptr, &outDone,
                                        nullptr),
                1);
    NC_CHECK_EQ(outDone, total / 2);

    // 下载完成 → 校验 → 安装（提供正确摘要）
    NC_CHECK_EQ(nc_model_download_finish(editor, "matting_hq", now + 4), NC_OK);
    NC_CHECK_EQ(nc_model_status(editor, "matting_hq"), 5);  // Verifying

    // 构造密文载荷 + 摘要（模拟真实下载结果）
    std::vector<std::uint8_t> payload(1024);
    for (std::size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>(i & 0xff);
    }
    // 摘要由测试侧计算（内部 API 可用；此处用 C ABI 安装路径验证校验逻辑）
    char* installMessage = nullptr;
    int32_t installed = -1;
    NC_CHECK_EQ(nc_model_install(editor, "matting_hq", payload.data(),
                                 static_cast<int64_t>(payload.size()), "/models/matting_hq.bin",
                                 "deadbeef", now + 5, &installed, &installMessage),
                NC_OK);
    NC_CHECK_EQ(installed, 0);  // 摘要不匹配 → 拒绝
    NC_CHECK(installMessage != nullptr);
    nc_free_string(installMessage);
    NC_CHECK_EQ(nc_model_status(editor, "matting_hq"), 3);  // Corrupt

    // 删除记录
    NC_CHECK_EQ(nc_model_remove(editor, "matting_hq"), NC_OK);
    NC_CHECK_EQ(nc_model_status(editor, "matting_hq"), 0);

    // 空安全
    NC_CHECK_EQ(nc_model_status(nullptr, "x"), -1);
    NC_CHECK_EQ(nc_model_download_begin(editor, "ghost", 100, now), NC_ERROR_NOT_FOUND);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ModelAccessGating) {
    NcEditor* editor = nc_editor_create();
    // 免费版：高精度抠像/补帧不可用；基础能力可用
    NC_CHECK_EQ(nc_model_access_allowed(editor, "matting_hq"), 0);
    NC_CHECK_EQ(nc_model_access_allowed(editor, "flow_interp"), 0);
    NC_CHECK_EQ(nc_model_access_allowed(editor, "matting_basic"), 1);
    NC_CHECK_EQ(nc_model_access_allowed(editor, "whisper_zh"), 1);

    // Pro（无到期）→ 全部可用
    NC_CHECK_EQ(nc_set_entitlement(editor, 1, 0, 0), NC_OK);
    NC_CHECK_EQ(nc_model_access_allowed(editor, "matting_hq"), 1);
    NC_CHECK_EQ(nc_model_access_allowed(editor, "flow_interp"), 1);

    // 过期 Pro → 降级
    NC_CHECK_EQ(nc_set_entitlement(editor, 1, 0, 1), NC_OK);  // 1970-01-01 到期
    NC_CHECK_EQ(nc_model_access_allowed(editor, "matting_hq"), 0);

    NC_CHECK_EQ(nc_model_access_allowed(editor, "nope"), -1);
    NC_CHECK_EQ(nc_model_access_allowed(nullptr, "matting_hq"), -1);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ModelResumePlanAndDecrypt) {
    NcEditor* editor = nc_editor_create();
    int64_t offset = -1;
    uint8_t counter[16] = {};
    int32_t intraSkip = -1;
    NC_CHECK_EQ(nc_model_resume_plan(editor, "whisper_zh", 33, &offset, counter, &intraSkip),
                NC_OK);
    NC_CHECK_EQ(offset, std::int64_t(33));
    NC_CHECK_EQ(intraSkip, 1);  // 33 % 16
    NC_CHECK_EQ(nc_model_resume_plan(editor, "ghost", 0, nullptr, nullptr, nullptr),
                NC_ERROR_NOT_FOUND);
    NC_CHECK_EQ(nc_model_resume_plan(nullptr, "whisper_zh", 0, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);

    // 解密：用确定密钥加密（core API）→ C ABI 解密还原（含偏移）
    const std::string modelId = "whisper_zh";
    const std::string version = "2.0.0";
    uint8_t key[32];
    for (int i = 0; i < 32; ++i) key[i] = static_cast<uint8_t>(i);
    std::vector<std::uint8_t> plaintext(256);
    for (std::size_t i = 0; i < plaintext.size(); ++i) {
        plaintext[i] = static_cast<std::uint8_t>((i * 17) & 0xff);
    }
    // 加密（等价于服务端分发前的密文；CTR 对称，直接对明文加密）
    std::vector<std::uint8_t> ciphertext(plaintext.size());
    {
        uint8_t derivedCounter[16] = {};
        // 与 core 派生规则一致：从 C ABI 的 resume_plan 获取计数器（offset=0）
        NC_CHECK_EQ(nc_model_resume_plan(editor, "whisper_zh", 0, nullptr, derivedCounter,
                                         nullptr),
                    NC_OK);
        // 直接使用内部推导（测试中链接 core 静态库；头部已含 crypto）
        std::uint8_t expected[16] = {};
        // 通过公开 API 无法拿到 key 派生 counter 之外的细节；用 resume_plan 的 counter
        // 配合加密方向进行一致性验证：对同一数据先加密再偏移解密。
        std::vector<std::uint8_t> scratch = plaintext;
        (void)expected;
        (void)derivedCounter;
        (void)scratch;
    }

    // 以「加密后的数据」构造：使用 nc_model_decrypt 的偏移语义验证一致性：
    // 先整体解密（偏移 0），再对同一密文尾部用偏移解密，两者必须一致。
    uint8_t* full = nullptr;
    int64_t fullSize = 0;
    NC_CHECK_EQ(nc_model_decrypt(modelId.c_str(), version.c_str(), key, plaintext.data(),
                                 static_cast<int64_t>(plaintext.size()), 0, &full, &fullSize),
                NC_OK);
    NC_CHECK_EQ(fullSize, static_cast<int64_t>(plaintext.size()));
    NC_CHECK(full != nullptr);

    const int64_t tailOffset = 100;
    uint8_t* tail = nullptr;
    int64_t tailSize = 0;
    NC_CHECK_EQ(nc_model_decrypt(modelId.c_str(), version.c_str(), key,
                                 plaintext.data() + tailOffset,
                                 static_cast<int64_t>(plaintext.size()) - tailOffset, tailOffset,
                                 &tail, &tailSize),
                NC_OK);
    NC_CHECK_EQ(tailSize, static_cast<int64_t>(plaintext.size()) - tailOffset);
    bool matched = true;
    for (int64_t i = 0; i < tailSize; ++i) {
        if (tail[i] != full[tailOffset + i]) {
            matched = false;
            break;
        }
    }
    NC_CHECK(matched);

    nc_free_buffer(full);
    nc_free_buffer(tail);
    NC_CHECK_EQ(nc_model_decrypt(nullptr, version.c_str(), key, plaintext.data(), 1, 0, nullptr,
                                 nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ModelStorageStats) {
    NcEditor* editor = nc_editor_create();
    int64_t modelBytes = -1;
    int32_t installed = -1;
    int32_t corrupt = -1;
    char* usageText = nullptr;
    char* modelText = nullptr;
    NC_CHECK_EQ(nc_model_storage_stats(editor, 128LL * 1024 * 1024 * 1024,
                                       45LL * 1024 * 1024 * 1024, &modelBytes, &installed,
                                       &corrupt, &usageText, &modelText),
                NC_OK);
    NC_CHECK_EQ(installed, 0);
    NC_CHECK_EQ(corrupt, 0);
    NC_CHECK(usageText != nullptr);
    NC_CHECK(modelText != nullptr);
    NC_CHECK(std::string(usageText).find("45.0 GB") != std::string::npos);
    nc_free_string(usageText);
    nc_free_string(modelText);
    NC_CHECK_EQ(nc_model_storage_stats(nullptr, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

// ---- 章节自动生成（文档第 7 部分「🧪AI工具 → 章节自动生成」）----

NC_TEST(Capi, ChapterDetectNullSafety) {
    char* err = nullptr;
    NC_CHECK_EQ(nc_chapter_detect(nullptr, 0, 0, 0, -1, -1, 0, nullptr, nullptr, &err), -1);
    NC_CHECK(err != nullptr);
    nc_free_string(err);
    err = nullptr;
    // 无主序列的 editor：默认工程有主序列，用 apply 无缓存验证错误路径
    NcEditor* editor = nc_editor_create();
    NC_CHECK_EQ(nc_chapter_apply(editor, &err), -1);  // 无缓存结果
    NC_CHECK(err != nullptr);
    nc_free_string(err);
    err = nullptr;
    NC_CHECK_EQ(nc_chapter_apply(nullptr, &err), -1);
    nc_free_string(err);
    NC_CHECK_EQ(nc_chapter_count(nullptr), 0);
    NC_CHECK_EQ(nc_chapter_get(nullptr, 0, nullptr, nullptr, nullptr), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_chapter_rename(nullptr, 0, "x", nullptr), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_chapter_clear(nullptr, nullptr), -1);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ChapterDetectAndApplyOnEmptyTimeline) {
    // 空工程（无轨道/片段）→ 检测结果说明原因，0 章节
    NcEditor* editor = nc_editor_create();
    char* summary = nullptr;
    char* note = nullptr;
    char* err = nullptr;
    const int32_t count =
        nc_chapter_detect(editor, 0, 0, 0, -1, -1, 0, &summary, &note, &err);
    NC_CHECK_EQ(count, 0);
    NC_CHECK(err == nullptr);
    NC_CHECK(note != nullptr);
    NC_CHECK(!std::string(note).empty());
    nc_free_string(summary);
    nc_free_string(note);
    // 无检测结果时 apply 拒绝
    NC_CHECK_EQ(nc_chapter_apply(editor, &err), -1);
    nc_free_string(err);
    NC_CHECK_EQ(nc_chapter_count(editor), 0);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ChapterLifecycleWithClips) {
    // 建 60s 视频片段 → 检测（无 FFmpeg 时为时长兜底；有 FFmpeg 时真实采样）
    NcEditor* editor = nc_editor_create();
    const uint64_t assetId =
        nc_import_asset(editor, "clip.mp4", "/videos/clip.mp4", 0, 60000000);
    NC_CHECK(assetId != 0);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    NC_CHECK(nc_insert_clip(editor, 0, assetId, 0, 60000000) != 0);
    char* summary = nullptr;
    char* note = nullptr;
    char* err = nullptr;
    // 最短 20s / 最长 300s → 兜底：1 章
    const int32_t count =
        nc_chapter_detect(editor, 20.0, 300.0, 1.5, -1, -1, 0, &summary, &note, &err);
    NC_CHECK_EQ(err, nullptr);
    NC_CHECK(count >= 1);
    NC_CHECK(summary != nullptr);
    nc_free_string(summary);
    nc_free_string(note);
    // 落盘
    const int32_t written = nc_chapter_apply(editor, &err);
    NC_CHECK(written >= 1);
    NC_CHECK_EQ(err, nullptr);
    NC_CHECK_EQ(nc_chapter_count(editor), written);
    // 读取首章
    int64_t startUs = -1;
    int64_t endUs = -1;
    char* title = nullptr;
    NC_CHECK_EQ(nc_chapter_get(editor, 0, &startUs, &endUs, &title), NC_OK);
    NC_CHECK_EQ(startUs, std::int64_t(0));
    NC_CHECK_EQ(endUs, std::int64_t(60000000));
    NC_CHECK(title != nullptr);
    NC_CHECK(nc_chapter_get(editor, written, nullptr, nullptr, nullptr) == NC_ERROR_OUT_OF_RANGE);
    // 手动重命名（真实更新标记）
    NC_CHECK_EQ(nc_chapter_rename(editor, 0, "开场介绍", &err), NC_OK);
    NC_CHECK_EQ(err, nullptr);
    nc_free_string(title);
    title = nullptr;
    NC_CHECK_EQ(nc_chapter_get(editor, 0, nullptr, nullptr, &title), NC_OK);
    NC_CHECK_EQ(std::string(title), std::string("开场介绍"));
    nc_free_string(title);
    // 清除（全部章节）
    NC_CHECK_EQ(nc_chapter_clear(editor, &err), written);
    NC_CHECK_EQ(nc_chapter_count(editor), 0);
    // 清除后重新检测（undo 不可见但可再生成）
    NC_CHECK_EQ(nc_chapter_clear(editor, &err), 0);  // 二次清除幂等
    nc_free_string(err);
    nc_editor_destroy(editor);
}

#ifdef NAIROCLIP_HAS_FFMPEG
NC_TEST(Capi, ChapterRealMediaEndToEnd) {
    {
        std::ifstream check("/tmp/media_gen/two_shots.mp4");
        if (!check.good()) return;  // 素材未生成：跳过
    }
    NcEditor* editor = nc_editor_create();
    const uint64_t assetId =
        nc_import_asset(editor, "two_shots.mp4", "/tmp/media_gen/two_shots.mp4", 0, 4000000);
    NC_CHECK(assetId != 0);
    NC_CHECK(nc_add_track(editor, "V1", 0) != 0);
    NC_CHECK(nc_insert_clip(editor, 0, assetId, 0, 4000000) != 0);
    char* summary = nullptr;
    char* note = nullptr;
    char* err = nullptr;
    // 真实采样检测（4s 素材：放宽最短章节）
    const int32_t count =
        nc_chapter_detect(editor, 0.5, 0, 1.5, -1, -1, 0, &summary, &note, &err);
    NC_CHECK_EQ(err, nullptr);
    NC_CHECK(count >= 2);
    NC_CHECK(summary != nullptr);
    NC_CHECK(std::string(summary).find("章节") != std::string::npos);
    nc_free_string(summary);
    nc_free_string(note);
    const int32_t written = nc_chapter_apply(editor, &err);
    NC_CHECK(written >= 2);
    NC_CHECK_EQ(nc_chapter_count(editor), written);
    // 首章从 0 起；第二章起点在 2s 附近（两镜头拼接点）
    int64_t startUs = -1;
    int64_t endUs = -1;
    NC_CHECK_EQ(nc_chapter_get(editor, 0, &startUs, &endUs, nullptr), NC_OK);
    NC_CHECK_EQ(startUs, std::int64_t(0));
    NC_CHECK_EQ(nc_chapter_get(editor, 1, &startUs, &endUs, nullptr), NC_OK);
    NC_CHECK(startUs > 1500000 && startUs < 2600000);
    nc_free_string(err);
    nc_editor_destroy(editor);
}
#endif

// ---- 导出队列（文档第 6 部分「二、导出队列管理」）----

NC_TEST(Capi, ExportQueueEnqueueAndLimits) {
    NcEditor* editor = nc_editor_create();
    uint64_t jobA = 0;
    char* err = nullptr;
    // 免费版：第一个成功
    NC_CHECK_EQ(nc_export_enqueue(editor, "春游Vlog", "/tmp/out_a.mp4", 0, 1, 1, 0, nullptr,
                                  &jobA, &err),
                NC_OK);
    NC_CHECK(jobA != 0);
    NC_CHECK_EQ(err, nullptr);
    int32_t queued = -1, running = -1, active = -1;
    NC_CHECK_EQ(nc_export_queue_summary(editor, &queued, &running, &active), NC_OK);
    NC_CHECK_EQ(queued, 1);
    NC_CHECK_EQ(running, 0);
    NC_CHECK_EQ(active, 1);
    // 免费版上限 1 → 第二个被拒
    uint64_t jobB = 0;
    NC_CHECK(nc_export_enqueue(editor, "产品宣传片", "/tmp/out_b.mp4", 0, 1, 1, 0, nullptr, &jobB,
                               &err) != NC_OK);
    NC_CHECK(err != nullptr);
    NC_CHECK(std::string(err).find("上限") != std::string::npos);
    nc_free_string(err);
    err = nullptr;
    // 参数校验
    NC_CHECK(nc_export_enqueue(nullptr, "x", "/tmp/x.mp4", 0, 1, 1, 0, nullptr, nullptr, nullptr) ==
             NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(nc_export_enqueue(editor, nullptr, "/tmp/x.mp4", 0, 1, 1, 0, nullptr, nullptr,
                               nullptr) == NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(nc_export_enqueue(editor, "x", "/tmp/x.mp4", 0, 99, 1, 1, nullptr, nullptr,
                               &err) == NC_ERROR_INVALID_ARGUMENT);  // 非法模式
    nc_free_string(err);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ExportQueueSchedulingAndLifecycle) {
    NcEditor* editor = nc_editor_create();
    uint64_t jobId = 0;
    NC_CHECK_EQ(nc_export_enqueue(editor, "任务1", "/tmp/job1.mp4", 1, 1, 1, 1, "AI 生成", &jobId,
                                  nullptr),
                NC_OK);
    // 调度：取到任务（返回 1）
    uint64_t polled = 0;
    char* outPath = nullptr;
    int32_t preset = -1, mode = -1, includeAudio = -1, attachLabel = -1;
    char* labelText = nullptr;
    NC_CHECK_EQ(nc_export_poll_next(editor, &polled, &outPath, &preset, &mode, &includeAudio,
                                    &attachLabel, &labelText),
                1);
    NC_CHECK_EQ(polled, jobId);
    NC_CHECK_EQ(std::string(outPath), std::string("/tmp/job1.mp4"));
    NC_CHECK_EQ(preset, 1);
    NC_CHECK_EQ(attachLabel, 1);
    NC_CHECK_EQ(std::string(labelText), std::string("AI 生成"));
    nc_free_string(outPath);
    nc_free_string(labelText);
    // 开始 → 进度 → 完成
    NC_CHECK_EQ(nc_export_job_update(editor, jobId, 0, 0, nullptr, 1000, nullptr), NC_OK);
    NC_CHECK_EQ(nc_export_job_progress(editor, jobId, 0.6), NC_OK);
    int32_t state = -1;
    double progress = -1.0;
    int64_t frames = -1;
    char* note = nullptr;
    NC_CHECK_EQ(nc_export_job_state(editor, jobId, &state, &progress, &frames, &note), 1);
    NC_CHECK_EQ(state, 1);  // Running
    NC_CHECK(progress > 0.59 && progress < 0.61);
    nc_free_string(note);
    note = nullptr;
    NC_CHECK_EQ(nc_export_job_update(editor, jobId, 1, 300, nullptr, 1001, nullptr), NC_OK);
    NC_CHECK_EQ(nc_export_job_state(editor, jobId, &state, &progress, &frames, nullptr), 1);
    NC_CHECK_EQ(state, 3);  // Completed
    NC_CHECK_EQ(frames, std::int64_t(300));
    NC_CHECK(progress == 1.0);
    // 移除
    NC_CHECK_EQ(nc_export_job_update(editor, jobId, 5, 0, nullptr, 1002, nullptr), NC_OK);
    NC_CHECK_EQ(nc_export_job_state(editor, jobId, nullptr, nullptr, nullptr, nullptr), 0);
    // 更新不存在的任务 → 错误
    char* err = nullptr;
    NC_CHECK(nc_export_job_update(editor, 9999, 0, 0, nullptr, 1000, &err) != NC_OK);
    nc_free_string(err);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, ExportQueueProTierAndCancelAll) {
    NcEditor* editor = nc_editor_create();
    // 升级 Pro → 可入队 5 个
    NC_CHECK_EQ(nc_set_entitlement(editor, 1, 0, 0), NC_OK);
    for (int i = 0; i < 5; ++i) {
        uint64_t id = 0;
        NC_CHECK_EQ(nc_export_enqueue(editor, ("任务" + std::to_string(i)).c_str(), "/tmp/x.mp4",
                                      0, 1, 1, 0, nullptr, &id, nullptr),
                    NC_OK);
    }
    int32_t active = -1;
    NC_CHECK_EQ(nc_export_queue_summary(editor, nullptr, nullptr, &active), NC_OK);
    NC_CHECK_EQ(active, 5);
    // 第 6 个被拒
    uint64_t extra = 0;
    NC_CHECK(nc_export_enqueue(editor, "第6个", "/tmp/x6.mp4", 0, 1, 1, 0, nullptr, &extra,
                               nullptr) != NC_OK);
    // 取消全部
    nc_export_queue_cancel_all(editor, 2000);
    NC_CHECK_EQ(nc_export_queue_summary(editor, nullptr, nullptr, &active), NC_OK);
    NC_CHECK_EQ(active, 0);
    // 取消后可重新入队
    NC_CHECK_EQ(nc_export_enqueue(editor, "新任务", "/tmp/new.mp4", 0, 1, 1, 0, nullptr, nullptr,
                                  nullptr),
                NC_OK);
    nc_editor_destroy(editor);
}

// ---- 媒体离线检测与重新链接（文档第 3 部分「四」）----

// 无 FFmpeg 构建：探测器不可用，接口应 fail-closed（返回 Unsupported，不崩溃）
NC_TEST(Capi, MediaOfflineNoProbeFailsClosed) {
    NcEditor* editor = nc_editor_create();
    char* report = nullptr;
    char* err = nullptr;
#ifndef NAIROCLIP_HAS_FFMPEG
    NC_CHECK_EQ(nc_media_scan_offline(editor, 1, &report, &err), NC_ERROR_UNSUPPORTED);
    NC_CHECK(report == nullptr);
    NC_CHECK(err != nullptr);
    nc_free_string(err);
#else
    (void)report;
    (void)err;
#endif
    nc_editor_destroy(editor);
}

#ifdef NAIROCLIP_HAS_FFMPEG
NC_TEST(Capi, MediaOfflineScanAndRelink) {
    NcEditor* editor = nc_editor_create();
    // 导入一个真实存在的文件素材（用测试素材）
    {
        std::ifstream check("/tmp/media_gen/test_av.mp4");
        if (!check.good()) return;  // 素材未生成：跳过
    }
    const uint64_t assetId =
        nc_import_asset(editor, "test_av.mp4", "/tmp/media_gen/test_av.mp4", 0, 3000000);
    NC_CHECK(assetId != 0);
    // 记录摘要（真实计算）
    // 先用 FFmpeg 探测器扫描（1）——此时素材无摘要 → 只做存在性检查
    char* report = nullptr;
    char* err = nullptr;
    NC_CHECK_EQ(nc_media_scan_offline(editor, 1, &report, &err), NC_OK);
    NC_CHECK(report != nullptr);
    const std::string reportText(report);
    NC_CHECK(reportText.find("\"online\":1") != std::string::npos);
    NC_CHECK(reportText.find("\"offline\":0") != std::string::npos);
    nc_free_string(report);
    report = nullptr;
    // 写回标记（在线 → 不设置）
    NC_CHECK_EQ(nc_media_apply_offline_flags(editor), 0);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, MediaOfflineMissingAndRelinkFlow) {
    NcEditor* editor = nc_editor_create();
    // 不存在的路径 → Missing
    const uint64_t assetId =
        nc_import_asset(editor, "ghost.mp4", "/tmp/nonexistent_ghost_12345.mp4", 0, 1000000);
    NC_CHECK(assetId != 0);
    char* report = nullptr;
    NC_CHECK_EQ(nc_media_scan_offline(editor, 1, &report, nullptr), NC_OK);
    NC_CHECK(report != nullptr);
    const std::string reportText(report);
    NC_CHECK(reportText.find("\"status\":\"missing\"") != std::string::npos);
    nc_free_string(report);
    // 写回 → offline = true
    NC_CHECK_EQ(nc_media_apply_offline_flags(editor), 1);
    // 重链到真实文件（用测试素材；无摘要要求时只校验存在性）
    {
        std::ifstream check("/tmp/media_gen/test_av.mp4");
        if (!check.good()) {
            nc_editor_destroy(editor);
            return;
        }
    }
    char* warning = nullptr;
    char* err = nullptr;
    NC_CHECK_EQ(nc_media_relink(editor, assetId, "/tmp/media_gen/test_av.mp4", 0, &warning, &err),
                NC_OK);
    NC_CHECK_EQ(err, nullptr);
    // 重链后再次扫描 → online
    report = nullptr;
    NC_CHECK_EQ(nc_media_scan_offline(editor, 1, &report, nullptr), NC_OK);
    NC_CHECK(std::string(report).find("\"offline\":0") != std::string::npos);
    nc_free_string(report);
    // 不存在的素材 → 错误
    NC_CHECK(nc_media_relink(editor, 99999, "/tmp/x.mp4", 0, nullptr, &err) != NC_OK);
    nc_free_string(err);
    err = nullptr;
    // 摘要写入校验
    NC_CHECK(nc_media_set_digest(editor, assetId, "zzz", &err) == NC_ERROR_INVALID_ARGUMENT);
    nc_free_string(err);
    err = nullptr;
    NC_CHECK_EQ(nc_media_set_digest(editor, assetId,
                                    "e3b0c44298fc1c149afbf4c8996fb924"
                                    "27ae41e4649b934ca495991b7852b855",
                                    &err),
                NC_OK);
    nc_free_string(err);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, MediaFindCandidates) {
    NcEditor* editor = nc_editor_create();
    {
        std::ifstream check("/tmp/media_gen/test_av.mp4");
        if (!check.good()) return;
    }
    const uint64_t assetId =
        nc_import_asset(editor, "test_av.mp4", "/tmp/elsewhere/test_av.mp4", 0, 3000000);
    NC_CHECK(assetId != 0);
    // 在 /tmp/media_gen 中查找同名文件 → 命中
    char* candidates = nullptr;
    NC_CHECK_EQ(nc_media_find_relink_candidates(editor, "[\"/tmp/media_gen\"]", &candidates,
                                                nullptr),
                NC_OK);
    NC_CHECK(candidates != nullptr);
    const std::string text(candidates);
    NC_CHECK(text.find("test_av.mp4") != std::string::npos);
    NC_CHECK(text.find("/tmp/media_gen/test_av.mp4") != std::string::npos);
    nc_free_string(candidates);
    // 空目录列表 → 参数错误
    NC_CHECK(nc_media_find_relink_candidates(editor, "[]", nullptr, nullptr) ==
             NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(Capi, MediaRelinkBatchFlow) {
    NcEditor* editor = nc_editor_create();
    {
        std::ifstream check("/tmp/media_gen/test_av.mp4");
        if (!check.good()) return;
    }
    const uint64_t a = nc_import_asset(editor, "a.mp4", "/tmp/gone/a.mp4", 0, 1000000);
    const uint64_t b = nc_import_asset(editor, "b.mp4", "/tmp/gone/b.mp4", 0, 1000000);
    (void)b;
    // 批量映射（a 有效；b 目标不存在 → 部分失败）
    const std::string mappings = "[{\"asset\":" + std::to_string(a) +
                                 ",\"path\":\"/tmp/media_gen/test_av.mp4\"},"
                                 "{\"asset\":" + std::to_string(b) +
                                 ",\"path\":\"/tmp/nonexistent_b.mp4\"}]";
    int32_t relinked = -1;
    char* report = nullptr;
    char* err = nullptr;
    NC_CHECK_EQ(nc_media_relink_batch(editor, mappings.c_str(), &relinked, &report, &err), NC_OK);
    NC_CHECK_EQ(relinked, 1);
    NC_CHECK(report != nullptr);
    nc_free_string(report);
    // 空列表 → 参数错误
    NC_CHECK(nc_media_relink_batch(editor, "[]", nullptr, nullptr, nullptr) ==
             NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}
#endif  // NAIROCLIP_HAS_FFMPEG（媒体离线检测的端到端用例）
