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
