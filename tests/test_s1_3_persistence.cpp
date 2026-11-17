// S1-3 工程持久化专项测试
//
// 覆盖：字段往返一致、ID 唯一性与分配器恢复、加载失败不污染、
//       版本与格式校验、畸形输入安全、多次保存/重开稳定、
//       自动保存环形槽位与"最近快照"语义。
#include "test_framework.h"

#include <cstdint>
#include <string>
#include <vector>

#include "nairoclip/core/capi.h"
#include "nairoclip/core/recovery.h"
#include "nairoclip/core/time.h"

namespace {

std::string saveText(NcEditor* editor) {
    char* text = nc_project_save(editor);
    if (text == nullptr) return std::string();
    std::string out(text);
    nc_free_string(text);
    return out;
}

NcEditor* loadText(const std::string& text, int32_t* rcOut = nullptr) {
    NcEditor* editor = nc_editor_create();
    char* detail = nullptr;
    const int32_t rc = nc_project_load(editor, text.c_str(), &detail);
    if (detail != nullptr) nc_free_string(detail);
    if (rcOut != nullptr) *rcOut = rc;
    return editor;
}

/// 构造一个尽可能覆盖各实体与字段的工程。
std::string buildRichProject(uint64_t* outClip) {
    NcEditor* e = nc_editor_create();
    nc_set_project_name(e, "S13");
    nc_set_project_canvas(e, 1920, 1080);
    nc_set_project_frame_rate(e, 30000, 1001);
    nc_set_background_color(e, 0x11223344u);
    const uint64_t asset = nc_import_asset(e, "m.mp4", "/v/m.mp4", 0, 60000000);
    nc_add_track(e, "V1", 0);
    const uint64_t clip = nc_insert_clip(e, 0, asset, 1000000, 5000000);
    nc_set_clip_opacity(e, 0, clip, 0.5);
    nc_set_clip_speed(e, 0, clip, 2, 1);
    nc_set_clip_blend_mode(e, 0, clip, 3);
    nc_set_clip_filter(e, 0, clip, 2, 70.0);
    nc_set_clip_lut(e, 0, clip, "/v/x.cube", 0.8);
    nc_set_clip_mask(e, 0, clip, 1, 0.5, 0.5, 0.0, 1.0, 1.0, 0.02, 0.0, 1);
    nc_set_clip_transform(e, 0, clip, 4, 0.1, 0.2, 15.0, 1.2, 1);
    nc_add_effect(e, 0, clip, 5, 0.6);
    nc_insert_keyframe(e, 0, clip, 0, 2000000, 0.9);
    nc_set_clip_audio(e, 0, clip, -3.0, 100000, 200000);
    nc_set_clip_denoise(e, 0, clip, 40);
    nc_add_marker(e, 1500000, "mk", "#FF0000");
    nc_upsert_subtitle(e, 1000000, 3000000, "hello");
    if (outClip != nullptr) *outClip = clip;
    const std::string text = saveText(e);
    nc_editor_destroy(e);
    return text;
}

}  // namespace

// ---------- 字段往返一致 ----------

NC_TEST(S1_3, RoundTripIsFieldIdentical) {
    uint64_t clip = 0;
    const std::string s1 = buildRichProject(&clip);
    NC_CHECK(!s1.empty());
    int32_t rc = -1;
    NcEditor* b = loadText(s1, &rc);
    NC_CHECK_EQ(rc, static_cast<int32_t>(NC_OK));
    const std::string s2 = saveText(b);
    nc_editor_destroy(b);
    // 逐字节一致 = 保存的每个字段都能被完整读回并再次写出
    NC_CHECK(s2 == s1);
}

NC_TEST(S1_3, RepeatedCyclesStayStable) {
    uint64_t clip = 0;
    std::string text = buildRichProject(&clip);
    NC_CHECK(!text.empty());
    for (int i = 0; i < 3; ++i) {
        int32_t rc = -1;
        NcEditor* e = loadText(text, &rc);
        NC_CHECK_EQ(rc, static_cast<int32_t>(NC_OK));
        const std::string again = saveText(e);
        nc_editor_destroy(e);
        NC_CHECK(again == text);   // 多轮后无漂移
        text = again;
    }
}

// ---------- ID 唯一性与分配器 ----------

NC_TEST(S1_3, DuplicateSequenceIdRejected) {
    // 两个同 id 序列：修复前可成功载入（rc=0），findSequence 会命中第一个
    const std::string bad =
        R"({"format":"nclip","version":1,"project":{"name":"X"},"assets":[],)"
        R"("sequences":[{"id":9,"tracks":[]},{"id":9,"tracks":[]}]})";
    int32_t rc = -1;
    NcEditor* e = loadText(bad, &rc);
    nc_editor_destroy(e);
    NC_CHECK(rc != NC_OK);
}

NC_TEST(S1_3, DuplicateAssetIdRejected) {
    const std::string bad =
        R"({"format":"nclip","version":1,"project":{"name":"X"},)"
        R"("assets":[{"id":5,"name":"a","path":"/a"},{"id":5,"name":"b","path":"/b"}],)"
        R"("sequences":[]})";
    int32_t rc = -1;
    NcEditor* e = loadText(bad, &rc);
    nc_editor_destroy(e);
    NC_CHECK(rc != NC_OK);
}

NC_TEST(S1_3, AllocatorRestoredAfterLoad) {
    // 载入后新建对象不得与已载入对象的 id 冲突
    uint64_t clip = 0;
    const std::string text = buildRichProject(&clip);
    int32_t rc = -1;
    NcEditor* e = loadText(text, &rc);
    NC_CHECK_EQ(rc, static_cast<int32_t>(NC_OK));
    const uint64_t newAsset = nc_import_asset(e, "n.mp4", "/v/n.mp4", 0, 1000000);
    const uint64_t newTrack = nc_add_track(e, "V2", 0);
    const uint64_t newClip = nc_insert_clip(e, 1, newAsset, 0, 1000000);
    NC_CHECK(newAsset != 0 && newTrack != 0 && newClip != 0);
    NC_CHECK(newAsset != clip);
    NC_CHECK(newTrack != clip);
    NC_CHECK(newClip != clip);
    nc_editor_destroy(e);
}

// ---------- 加载失败不污染 ----------

NC_TEST(S1_3, FailedLoadDoesNotPollute) {
    uint64_t clip = 0;
    const std::string good = buildRichProject(&clip);
    NcEditor* e = loadText(good);
    const std::string before = saveText(e);
    char* detail = nullptr;
    const int32_t rc = nc_project_load(e, "{ not json", &detail);
    if (detail != nullptr) nc_free_string(detail);
    NC_CHECK(rc != NC_OK);
    NC_CHECK(saveText(e) == before);   // 当前工程必须原样保留
    nc_editor_destroy(e);
}

// ---------- 版本 / 格式 / 畸形输入 ----------

NC_TEST(S1_3, VersionAndFormatValidated) {
    int32_t rc = -1;
    NcEditor* a = loadText(
        R"({"format":"nclip","version":999,"project":{"name":"X"},"assets":[],"sequences":[]})", &rc);
    nc_editor_destroy(a);
    NC_CHECK(rc != NC_OK);   // 未来版本必须拒绝（不得静默降级解析）

    NcEditor* b = loadText(
        R"({"format":"other","version":1,"project":{"name":"X"},"assets":[],"sequences":[]})", &rc);
    nc_editor_destroy(b);
    NC_CHECK(rc != NC_OK);

    NcEditor* c = loadText(
        R"({"format":"nclip","version":0,"project":{"name":"X"},"assets":[],"sequences":[]})", &rc);
    nc_editor_destroy(c);
    NC_CHECK(rc != NC_OK);
}

NC_TEST(S1_3, MalformedJsonRejectedSafely) {
    const char* cases[] = {
        "",
        "{",
        R"({"format":"nclip","version":1,"project":{"na)",
        R"({"format":"nclip","version":1,"project":{"name":"X"},"assets":[],"sequences":[{"id":9,"tracks":[{"id":5,"clips":[{"id":7,"asset":777,"start":0,"duration":1000}]}]}]})",
    };
    for (const char* bad : cases) {
        int32_t rc = -1;
        NcEditor* e = loadText(bad, &rc);
        NC_CHECK(e != nullptr);      // 句柄本身仍可用（不崩溃）
        NC_CHECK(rc != NC_OK);       // 但加载必须失败
        nc_editor_destroy(e);
    }
}

// ---------- 自动保存：最近快照语义（S1-3 修复点）----------

NC_TEST(S1_3, RecoverLatestReturnsNewestAfterRingWrap) {
    nairoclip::core::InMemoryRecoveryStore store;
    nairoclip::core::AutoSavePolicy policy;
    policy.maxSnapshots = 3;                                     // 环形回绕
    policy.interval = nairoclip::core::fromMicroseconds(1000000);
    policy.idleDelay = nairoclip::core::fromMicroseconds(1000000);
    nairoclip::core::AutoSaveManager manager(store, policy);

    std::string last;
    for (int i = 0; i < 5; ++i) {                                // 写 5 次 → 回绕
        const auto now = nairoclip::core::fromMicroseconds((i + 1) * 1000000);
        manager.markDirty(now);
        const bool wrote = manager.tick(
            nairoclip::core::fromMicroseconds((i + 1) * 1000000 + 2000000),
            [i] { return std::string("SNAP") + std::to_string(i); });
        NC_CHECK(wrote);
        last = std::string("SNAP") + std::to_string(i);
    }
    std::string out;
    const auto r = manager.recoverLatest(out);
    NC_CHECK(r.isOk());
    // 修复前返回 SNAP2（按 slot 编号倒序取到第 3 次写入），必须返回最新的 SNAP4
    NC_CHECK(out == last);
}

NC_TEST(S1_3, MarkCleanClearsSnapshots) {
    nairoclip::core::InMemoryRecoveryStore store;
    nairoclip::core::AutoSavePolicy policy;
    policy.maxSnapshots = 2;
    policy.interval = nairoclip::core::fromMicroseconds(1000000);
    policy.idleDelay = nairoclip::core::fromMicroseconds(1000000);
    nairoclip::core::AutoSaveManager manager(store, policy);
    manager.markDirty(nairoclip::core::fromMicroseconds(1000000));
    NC_CHECK(manager.tick(nairoclip::core::fromMicroseconds(3000000),
                          [] { return std::string("A"); }));
    NC_CHECK(manager.snapshotCount() == 1);
    manager.markClean();
    NC_CHECK_EQ(manager.snapshotCount(), static_cast<std::size_t>(0));
    NC_CHECK(!manager.hasRecoverable());
}

NC_TEST(S1_3, RecoverLatestEmptyIsNotFound) {
    nairoclip::core::InMemoryRecoveryStore store;
    nairoclip::core::AutoSaveManager manager(store);
    std::string out;
    NC_CHECK(!manager.recoverLatest(out).isOk());
    NC_CHECK(!manager.hasRecoverable());
}
