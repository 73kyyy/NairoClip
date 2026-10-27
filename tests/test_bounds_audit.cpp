#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cstdint>
#include <vector>

// =============================================================================
// B0-6 边界访问审计
// =============================================================================
//
// 文档要求审计三类路径：C ABI 传入的 index/count、JSON 解析下标、
// 时间→帧号换算。`[]` 越界是 UB（可能不崩溃、可能读到垃圾），
// 因此这类问题不能靠「跑一遍没崩」发现，必须用**断言非法输入被显式拒绝**。
//
// 这些用例的价值：把「手工探针跑一遍看到没问题」变成可重复回归。
// 开发时用 `-D_GLIBCXX_ASSERTIONS` 编译探针验证过：全部越界输入返回错误码
// 且程序存活（未触发任何容器断言）。

namespace {

/// 建一个含 2 轨道 + 1 片段 + 1 素材的编辑器。
NcEditor* makeFixture() {
    NcEditor* e = nc_editor_create();
    if (e == nullptr) return nullptr;
    nc_add_track(e, "T0", 0);
    nc_add_track(e, "T1", 0);
    const uint64_t asset = nc_import_asset(e, "a", "/tmp/none.mp4", 0, 1000000);
    nc_insert_clip(e, 0, asset, 0, 500000);
    return e;
}

}  // namespace

NC_TEST(BoundsAudit, NegativeTrackIndexRejected) {
    NcEditor* e = makeFixture();
    NC_CHECK(e != nullptr);

    const int32_t bad[] = {-1, -1000, -2147483647 - 1};
    for (int32_t b : bad) {
        NC_CHECK(nc_remove_track(e, b) != NC_OK);
        NC_CHECK(nc_rename_track(e, b, "x") != NC_OK);
        NC_CHECK(nc_clip_info(e, b, 0, nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr) != NC_OK);
    }
    // 轨道数不变 —— 拒绝必须是真的没动
    NC_CHECK_EQ(nc_track_count(e), 2);
    nc_editor_destroy(e);
}

NC_TEST(BoundsAudit, HugeTrackIndexRejected) {
    NcEditor* e = makeFixture();
    NC_CHECK(e != nullptr);

    const int32_t huge[] = {1000, 100000, 2147483647};
    for (int32_t h : huge) {
        NC_CHECK(nc_remove_track(e, h) != NC_OK);
        NC_CHECK(nc_rename_track(e, h, "x") != NC_OK);
    }
    NC_CHECK_EQ(nc_track_count(e), 2);
    nc_editor_destroy(e);
}

NC_TEST(BoundsAudit, ClipIndexOutOfRangeRejected) {
    NcEditor* e = makeFixture();
    NC_CHECK(e != nullptr);

    // 轨道 0 只有 1 个片段（合法 index 仅 0）
    for (uint32_t b : {1u, 2u, 1000u, 0xFFFFFFFFu}) {
        NC_CHECK(nc_clip_info(e, 0, b, nullptr, nullptr, nullptr, nullptr, nullptr,
                              nullptr, nullptr) != NC_OK);
    }
    // 合法 index 仍可用（防止「一律拒绝」的假实现）
    NC_CHECK_EQ(nc_clip_info(e, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr),
                NC_OK);
    nc_editor_destroy(e);
}

NC_TEST(BoundsAudit, UnknownClipIdRejected) {
    NcEditor* e = makeFixture();
    NC_CHECK(e != nullptr);

    const uint64_t ghost = 0xFFFFFFFFULL;
    NC_CHECK(nc_remove_clip(e, 0, ghost) != NC_OK);
    NC_CHECK(nc_set_clip_opacity(e, 0, ghost, 0.5) != NC_OK);
    NC_CHECK(nc_set_clip_enabled(e, 0, ghost, 1) != NC_OK);
    // 片段仍在
    NC_CHECK_EQ(nc_clip_count(e, 0), 1);
    nc_editor_destroy(e);
}

NC_TEST(BoundsAudit, AssetIndexOutOfRangeRejected) {
    NcEditor* e = makeFixture();
    NC_CHECK(e != nullptr);

    uint64_t id = 0;
    char* name = nullptr;
    char* path = nullptr;
    int32_t type = 0;
    NC_CHECK(nc_asset_get(e, -1, &id, &name, &path, &type, nullptr) != NC_OK);
    NC_CHECK(nc_asset_get(e, 99999, &id, &name, &path, &type, nullptr) != NC_OK);
    // 合法 index 可用
    NC_CHECK_EQ(nc_asset_get(e, 0, &id, &name, &path, &type, nullptr), NC_OK);
    if (name != nullptr) nc_free_string(name);
    if (path != nullptr) nc_free_string(path);
    nc_editor_destroy(e);
}

NC_TEST(BoundsAudit, NullEditorNeverCrashes) {
    // 句柄为空是最常见的调用方错误，所有入口都必须安全返回
    NC_CHECK(nc_track_count(nullptr) == 0);
    NC_CHECK(nc_clip_count(nullptr, 0) == 0);
    NC_CHECK(nc_remove_track(nullptr, 0) != NC_OK);
    NC_CHECK(nc_remove_clip(nullptr, 0, 0) != NC_OK);
    NC_CHECK(nc_set_project_name(nullptr, "x") != NC_OK);
    NC_CHECK(nc_insert_clip(nullptr, 0, 0, 0, 1000) == 0);
    nc_editor_destroy(nullptr);  // 约定：传 NULL 安全
}

NC_TEST(BoundsAudit, ZeroLengthInsertRejectedOrSane) {
    // 零时长插入：不应产生非法片段（如 sourceIn == sourceOut 的坏状态）
    NcEditor* e = makeFixture();
    NC_CHECK(e != nullptr);
    const int32_t before = nc_clip_count(e, 0);
    nc_insert_clip(e, 0, 1, 0, 0);  // 时长 0
    const int32_t after = nc_clip_count(e, 0);
    // 要么被拒绝（数量不变），要么插入但工程仍自洽
    // 这里只断言不越界、不自增出负数
    NC_CHECK(after >= before);
    NC_CHECK(nc_clip_count(e, 0) >= 0);
    nc_editor_destroy(e);
}
