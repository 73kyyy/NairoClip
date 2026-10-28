#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cstring>
#include <string>
#include <vector>

// =============================================================================
// B2-4 模板 C ABI
// =============================================================================
// 验证的是「平台层能不能真的用起来」：列表能拿、套用能成、撤销能回、
// 失败有原因、内存所有权对。

namespace {

bool contains(const char* h, const char* n) {
    return h != nullptr && std::strstr(h, n) != nullptr;
}

/// 建 editor + 3 个 10 秒素材，返回素材 ID。
std::vector<uint64_t> makeFixture(NcEditor*& out) {
    out = nc_editor_create();
    if (out == nullptr) return {};
    nc_add_track(out, "T0", 0);  // 触发主序列存在
    std::vector<uint64_t> ids;
    for (int i = 0; i < 3; ++i) {
        const std::string name = "m" + std::to_string(i);
        const std::string path = "/tmp/m" + std::to_string(i) + ".mp4";
        // 10 秒 = 10,000,000 微秒
        const uint64_t id = nc_import_asset(out, name.c_str(), path.c_str(), 0, 10000000);
        if (id != 0) ids.push_back(id);
    }
    return ids;
}

void freeIfNotNull(char* p) {
    if (p != nullptr) nc_free_string(p);
}

}  // namespace

NC_TEST(CapiTemplate, ListReturnsJsonWithAllTemplates) {
    char* json = nullptr;
    char* err = nullptr;
    NC_CHECK_EQ(nc_template_list(&json, &err), NC_OK);
    NC_CHECK(json != nullptr);
    if (json != nullptr) {
        NC_CHECK(contains(json, "\"id\""));
        NC_CHECK(contains(json, "keyframe_shake"));
        NC_CHECK(contains(json, "slotCount"));
        NC_CHECK(contains(json, "totalDurationUs"));
        nc_free_string(json);
    }
    freeIfNotNull(err);
}

NC_TEST(CapiTemplate, SlotCountMatchesTemplate) {
    // 关键帧摇晃模板是 3 个槽位
    NC_CHECK_EQ(nc_template_slot_count("keyframe_shake"), 3);
    NC_CHECK_EQ(nc_template_slot_count("no_such"), -1);
    NC_CHECK_EQ(nc_template_slot_count(nullptr), -1);
}

NC_TEST(CapiTemplate, ApplyFillsAndReturnsSummary) {
    NcEditor* e = nullptr;
    const std::vector<uint64_t> ids = makeFixture(e);
    NC_CHECK(e != nullptr);
    NC_CHECK_EQ(ids.size(), static_cast<std::size_t>(3));

    char* summary = nullptr;
    char* err = nullptr;
    const int32_t rc = nc_template_apply(e, "keyframe_shake", ids.data(),
                                         static_cast<int32_t>(ids.size()), &summary, &err);
    NC_CHECK_EQ(rc, NC_OK);
    NC_CHECK(summary != nullptr);
    if (summary != nullptr) {
        NC_CHECK(contains(summary, "插入"));
        NC_CHECK(contains(summary, "轨道"));
        nc_free_string(summary);
    }
    freeIfNotNull(err);
    nc_editor_destroy(e);
}

NC_TEST(CapiTemplate, ApplyIsUndoableInOneStep) {
    NcEditor* e = nullptr;
    const std::vector<uint64_t> ids = makeFixture(e);
    NC_CHECK(e != nullptr);

    const std::string before = [&] {
        char* saved = nc_project_save(e);
        std::string s = (saved != nullptr) ? saved : "";
        freeIfNotNull(saved);
        return s;
    }();

    char* summary = nullptr;
    NC_CHECK_EQ(nc_template_apply(e, "keyframe_shake", ids.data(),
                                  static_cast<int32_t>(ids.size()), &summary, nullptr),
                NC_OK);
    freeIfNotNull(summary);

    const std::string afterApply = [&] {
        char* saved = nc_project_save(e);
        std::string s = (saved != nullptr) ? saved : "";
        freeIfNotNull(saved);
        return s;
    }();
    NC_CHECK(afterApply != before);

    NC_CHECK_EQ(nc_undo(e), NC_OK);

    const std::string afterUndo = [&] {
        char* saved = nc_project_save(e);
        std::string s = (saved != nullptr) ? saved : "";
        freeIfNotNull(saved);
        return s;
    }();
    NC_CHECK_EQ(afterUndo, before);
    nc_editor_destroy(e);
}

NC_TEST(CapiTemplate, UnknownTemplateGivesReason) {
    NcEditor* e = nullptr;
    const std::vector<uint64_t> ids = makeFixture(e);
    NC_CHECK(e != nullptr);

    char* err = nullptr;
    const int32_t rc = nc_template_apply(e, "nope", ids.data(),
                                         static_cast<int32_t>(ids.size()), nullptr, &err);
    NC_CHECK_EQ(rc, NC_ERROR_NOT_FOUND);
    NC_CHECK(err != nullptr);
    if (err != nullptr) {
        NC_CHECK(contains(err, "模板不存在"));
        nc_free_string(err);
    }
    nc_editor_destroy(e);
}

NC_TEST(CapiTemplate, NullArgsGivesReason) {
    char* err = nullptr;
    NC_CHECK_EQ(nc_template_apply(nullptr, "keyframe_shake", nullptr, 0, nullptr, &err),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(err != nullptr);
    if (err != nullptr) {
        NC_CHECK(contains(err, "editor"));
        nc_free_string(err);
        err = nullptr;
    }

    NcEditor* e = nullptr;
    const std::vector<uint64_t> ids = makeFixture(e);
    NC_CHECK_EQ(nc_template_apply(e, nullptr, ids.data(), 1, nullptr, &err),
                NC_ERROR_INVALID_ARGUMENT);
    freeIfNotNull(err);
    nc_editor_destroy(e);
}

NC_TEST(CapiTemplate, UnknownMaterialIdsAreRejected) {
    NcEditor* e = nullptr;
    makeFixture(e);
    NC_CHECK(e != nullptr);

    const uint64_t ghost[2] = {99999, 88888};
    char* err = nullptr;
    const int32_t rc = nc_template_apply(e, "keyframe_shake", ghost, 2, nullptr, &err);
    NC_CHECK(rc != NC_OK);
    NC_CHECK(err != nullptr);
    freeIfNotNull(err);
    nc_editor_destroy(e);
}

NC_TEST(CapiTemplate, ApplySlotsWithExplicitMapping) {
    NcEditor* e = nullptr;
    const std::vector<uint64_t> ids = makeFixture(e);
    NC_CHECK(e != nullptr);
    NC_CHECK_EQ(ids.size(), static_cast<std::size_t>(3));

    // 只给第 0 和第 2 个槽位指定素材
    const int32_t slots[2] = {0, 2};
    const uint64_t mats[2] = {ids[0], ids[1]};
    char* summary = nullptr;
    char* err = nullptr;
    const int32_t rc = nc_template_apply_slots(e, "keyframe_shake", slots, mats, 2, &summary, &err);
    NC_CHECK_EQ(rc, NC_OK);
    if (summary != nullptr) {
        // 第 1 个槽位未指定 → 应有提示
        NC_CHECK(contains(summary, "插入"));
        nc_free_string(summary);
    }
    freeIfNotNull(err);
    nc_editor_destroy(e);
}

NC_TEST(CapiTemplate, ApplySlotsRejectsNegativeIndex) {
    NcEditor* e = nullptr;
    const std::vector<uint64_t> ids = makeFixture(e);
    NC_CHECK(e != nullptr);

    const int32_t slots[1] = {-1};
    const uint64_t mats[1] = {ids[0]};
    char* err = nullptr;
    const int32_t rc = nc_template_apply_slots(e, "keyframe_shake", slots, mats, 1, nullptr, &err);
    NC_CHECK_EQ(rc, NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(err != nullptr);
    if (err != nullptr) {
        NC_CHECK(contains(err, "slot_index"));
        nc_free_string(err);
    }
    nc_editor_destroy(e);
}

NC_TEST(CapiTemplate, OutputStringsAreHeapAllocated) {
    // 所有权标注说「需 nc_free_string」→ 必须能安全释放
    char* json = nullptr;
    char* err = nullptr;
    NC_CHECK_EQ(nc_template_list(&json, &err), NC_OK);
    if (json != nullptr) {
        NC_CHECK(std::strlen(json) > 10);
        nc_free_string(json);
    }
    freeIfNotNull(err);
}

NC_TEST(CapiTemplate, NullOutParamsAccepted) {
    // 出参可 NULL 时不能崩
    NC_CHECK_EQ(nc_template_list(nullptr, nullptr), NC_OK);
}

NC_TEST(CapiTemplate, LastErrorSetOnTemplateFailure) {
    // B0-3 的 last_error 也要覆盖新增接口
    NcEditor* e = nullptr;
    const std::vector<uint64_t> ids = makeFixture(e);
    NC_CHECK(e != nullptr);

    char* err = nullptr;
    nc_template_apply(e, "nope", ids.data(), static_cast<int32_t>(ids.size()), nullptr, &err);
    freeIfNotNull(err);

    const char* last = nc_last_error();
    NC_CHECK(last != nullptr);
    NC_CHECK(contains(last, "模板"));
    nc_editor_destroy(e);
}
