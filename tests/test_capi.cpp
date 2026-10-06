#include "test_framework.h"
#include "nairoclip/core/capi.h"
#include <cstdint>
#include <cstring>
#include <string>
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
