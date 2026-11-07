// AI 剪辑落盘（nc_ref_apply）：整段一个撤销事务 + 只往空轨道生成
#include "test_framework.h"
#include "nairoclip/core/capi.h"
#include <cstdint>

namespace {
struct Fx {
    NcEditor* ed = nullptr;
    uint64_t a1 = 0;
    uint64_t a2 = 0;
    Fx() {
        ed = nc_editor_create();
        if (ed == nullptr) return;
        a1 = nc_import_asset(ed, "a1.mp4", "/tmp/media_gen/detail_src.mp4", 0, 2000000);
        a2 = nc_import_asset(ed, "a2.mp4", "/tmp/media_gen/two_shots.mp4", 0, 2000000);
        nc_add_track(ed, "V1", 0);
    }
    ~Fx() { if (ed != nullptr) nc_editor_destroy(ed); }
    bool ok() const { return ed != nullptr && a1 != 0 && a2 != 0; }
};
}  // namespace

NC_TEST(RefApply, AppliesAsSingleUndoStep) {
    Fx f;
    NC_CHECK(f.ok());
    if (!f.ok()) return;

    const uint64_t ids[3] = {f.a1, f.a2, f.a1};
    const double starts[3] = {0.0, 0.0, 0.0};
    const double durs[3] = {1.0, 1.2, 0.8};
    const int32_t kinds[2] = {2, 2};
    const int32_t before = nc_undo_count(f.ed);

    char* err = nullptr;
    const int32_t n = nc_ref_apply(f.ed, 0, ids, starts, durs, 3, kinds, 1.05, 1.10, 0.02, 3.0,
                                   &err);
    NC_CHECK(n == 3);
    if (n < 0) {
        if (err) nc_free_string(err);
        return;
    }
    if (err) nc_free_string(err);

    NC_CHECK(nc_clip_count(f.ed, 0) == 3);
    NC_CHECK(nc_undo_count(f.ed) == before + 1);
    NC_CHECK(nc_undo(f.ed) == NC_OK);
    NC_CHECK(nc_clip_count(f.ed, 0) == 0);
}

NC_TEST(RefApply, RejectsNonEmptyTrack) {
    Fx f;
    NC_CHECK(f.ok());
    if (!f.ok()) return;
    NC_CHECK(nc_insert_clip(f.ed, 0, f.a1, 0, 1000000) != 0);

    const uint64_t ids[1] = {f.a1};
    const double starts[1] = {0.0};
    const double durs[1] = {1.0};
    char* err = nullptr;
    const int32_t n = nc_ref_apply(f.ed, 0, ids, starts, durs, 1, nullptr, 1.0, 1.0, 0.0, 0.0,
                                   &err);
    NC_CHECK(n < 0);
    if (err) nc_free_string(err);
    NC_CHECK(nc_clip_count(f.ed, 0) == 1);
}
