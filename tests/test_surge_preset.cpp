// 超燃预设（UI_BACKLOG P-08 / B3-3）
//
// 三条关键性质，各对应一个真实会踩的坑：
//   ① 一次操作 = 一条撤销记录（循环 addEffect 会留 N 条）
//   ② 强度缩放对「乘性参数」以 1.0 为中心（直接乘会让画面变灰）
//   ③ 应用是**追加**，不覆盖用户已有特效

#include "nairoclip/core/capi.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "test_framework.h"

namespace {

struct SurgeFixture {
    NcEditor* editor = nullptr;
    uint64_t clipId = 0;

    SurgeFixture() {
        editor = nc_editor_create();
        if (editor == nullptr) return;
        const std::uint64_t assetId = nc_import_asset(editor, "surge_src.mp4",
                                                     "/tmp/media_gen/detail_src.mp4", 0, 2000000);
        if (assetId == 0) return;
        nc_add_track(editor, "V1", 0);
        clipId = nc_insert_clip(editor, 0, assetId, 0, 1000000);
    }
    ~SurgeFixture() {
        if (editor != nullptr) nc_editor_destroy(editor);
    }
    bool ok() const { return editor != nullptr && clipId != 0; }
};

}  // namespace

NC_TEST(SurgePreset, CountAndNamesAreValid) {
    const int32_t n = nc_surge_preset_count();
    NC_CHECK(n == 3);

    std::string seen;
    for (int32_t i = 0; i < n; ++i) {
        const char* nm = nc_surge_preset_name(i);
        NC_CHECK(nm != nullptr && nm[0] != '\0');
        // 名字不能重复（重复的话用户分不清点的是哪个）
        const std::string s(nm ? nm : "");
        NC_CHECK(seen.find("|" + s + "|") == std::string::npos);
        seen += "|" + s + "|";
    }
    // 越界返回空串，不是崩溃也不是野指针
    NC_CHECK(std::strlen(nc_surge_preset_name(-1)) == 0);
    NC_CHECK(std::strlen(nc_surge_preset_name(n)) == 0);
}

NC_TEST(SurgePreset, DescribeListsWhatWillBeApplied) {
    /* 「透明不黑箱」是文档的硬要求：用户在点应用之前，
       必须能看到具体会加哪些特效。 */
    SurgeFixture f;
    NC_CHECK(f.ok());

    for (int32_t i = 0; i < nc_surge_preset_count(); ++i) {
        char* summary = nullptr;
        char* err = nullptr;
        const int32_t rc = nc_surge_preset_describe(i, &summary, &err);
        NC_CHECK(rc == NC_OK);
        NC_CHECK(summary != nullptr);
        if (summary != nullptr) {
            const std::string s(summary);
            NC_CHECK(!s.empty());
            NC_CHECK(s.find("包含") != std::string::npos);
            nc_free_string(summary);
        }
        if (err != nullptr) nc_free_string(err);
    }
}

NC_TEST(SurgePreset, ApplyAddsEffectsAndIsOneUndoStep) {
    /* 核心测试。一键组合必须是**一次**可撤销的操作。
       如果实现成循环 addEffect，撤销栈会多出 5~6 条记录，
       用户按一次撤销只退掉最后一条，体验上等于「坏了」。 */
    SurgeFixture f;
    NC_CHECK(f.ok());

    const int32_t beforeEffects = nc_effect_count(f.editor, 0, f.clipId);
    const int32_t beforeUndo = nc_undo_count(f.editor);

    char* summary = nullptr;
    char* err = nullptr;
    const int32_t rc = nc_surge_preset_apply(f.editor, 0, f.clipId, 0, 0.6,
                                             &summary, &err);
    NC_CHECK(rc == NC_OK);
    if (summary != nullptr) nc_free_string(summary);
    if (err != nullptr) nc_free_string(err);

    const int32_t afterEffects = nc_effect_count(f.editor, 0, f.clipId);
    NC_CHECK(afterEffects > beforeEffects);

    // 只多了一条撤销记录
    NC_CHECK(nc_undo_count(f.editor) == beforeUndo + 1);

    // 一次撤销回到原样
    NC_CHECK(nc_undo(f.editor) == NC_OK);
    NC_CHECK(nc_effect_count(f.editor, 0, f.clipId) == beforeEffects);
}

NC_TEST(SurgePreset, IntensityScalesMultiplicativeAroundOne) {
    /* 对比度/饱和度的中性值是 1.0。
       强度 0.5 时正确结果是 1 + (1.4-1)*0.5 = 1.2（对比度略增）；
       若按加性参数那样直接乘，会得到 1.4*0.5 = 0.7 —— 画面变灰。

       这条测试专门锁住「乘性参数以 1.0 为中心缩放」这个行为：
       只要结果是 0.7 而不是 1.2，就会挂在 `value > 1.0` 上。 */
    SurgeFixture f;
    NC_CHECK(f.ok());

    char* err = nullptr;
    const int32_t rc = nc_surge_preset_apply(f.editor, 0, f.clipId,
                                             /*动漫超燃*/ 0, /*强度*/ 0.5,
                                             nullptr, &err);
    NC_CHECK(rc == NC_OK);
    if (err != nullptr) nc_free_string(err);

    /* 动漫超燃的顺序：Flash / RgbSplit / ZoomPunch / Contrast / Saturation / Vignette
       → 下标 3 是 Contrast */
    uint64_t eid = 0;
    int32_t type = -1;
    double value = 0.0;
    int32_t enabled = 0;
    const int32_t got = nc_effect_get(f.editor, 0, f.clipId, 3, &eid, &type, &value, &enabled);
    NC_CHECK(got == NC_OK);
    NC_CHECK(value > 1.0);      // 关键：不是 0.7
    NC_CHECK(value < 1.45);     // 也没超过满强度值
    NC_CHECK(std::fabs(value - 1.2) < 0.001);
    NC_CHECK(enabled == 1);
}

NC_TEST(SurgePreset, ApplyAppendsInsteadOfReplacing) {
    /* 用户可能已经手调过几个特效，再用预设 —— 不能把他的工作清掉。 */
    SurgeFixture f;
    NC_CHECK(f.ok());

    char* err = nullptr;
    NC_CHECK(nc_surge_preset_apply(f.editor, 0, f.clipId, 1, 0.6, nullptr, &err) == NC_OK);
    const int32_t afterFirst = nc_effect_count(f.editor, 0, f.clipId);
    NC_CHECK(afterFirst > 0);

    NC_CHECK(nc_surge_preset_apply(f.editor, 0, f.clipId, 2, 0.6, nullptr, &err) == NC_OK);
    const int32_t afterSecond = nc_effect_count(f.editor, 0, f.clipId);
    NC_CHECK(afterSecond > afterFirst);   // 叠加，不是替换
    if (err != nullptr) nc_free_string(err);
}

NC_TEST(SurgePreset, RejectsInvalidArguments) {
    SurgeFixture f;
    NC_CHECK(f.ok());

    char* err = nullptr;
    // 预设下标越界
    NC_CHECK(nc_surge_preset_apply(f.editor, 0, f.clipId, -1, 0.5, nullptr, &err) != NC_OK);
    if (err != nullptr) { nc_free_string(err); err = nullptr; }
    NC_CHECK(nc_surge_preset_apply(f.editor, 0, f.clipId, 99, 0.5, nullptr, &err) != NC_OK);
    if (err != nullptr) { nc_free_string(err); err = nullptr; }

    // 强度必须 ∈ (0,1]：0 会让所有特效归零（等于没应用），负数无意义
    NC_CHECK(nc_surge_preset_apply(f.editor, 0, f.clipId, 0, 0.0, nullptr, &err) != NC_OK);
    if (err != nullptr) { nc_free_string(err); err = nullptr; }
    NC_CHECK(nc_surge_preset_apply(f.editor, 0, f.clipId, 0, 1.5, nullptr, &err) != NC_OK);
    if (err != nullptr) { nc_free_string(err); err = nullptr; }

    // 片段不存在
    NC_CHECK(nc_surge_preset_apply(f.editor, 0, 999999, 0, 0.5, nullptr, &err) != NC_OK);
    if (err != nullptr) nc_free_string(err);

    // 失败的一律不落库、不留撤销记录
    NC_CHECK(nc_effect_count(f.editor, 0, f.clipId) == 0);
}
