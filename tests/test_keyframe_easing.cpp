// 关键帧曲线（自定义缓动 / 贝塞尔）
//
// 针对一个真实的数据损坏缺陷：
//   `easingIndexOf` 把 EasingType::Bezier 落到 default 返回 0（线性）。
//   结果：用户设了贝塞尔曲线 → 面板读回来显示「线性」→ 再保存一次
//   就把这条曲线真正写成了线性，曲线被悄悄抹平。
//
// 所以必须有「写进去 → 读回来 → 控制点还在」的往返测试。

#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cmath>
#include <cstdint>

namespace {

struct EasingFixture {
    NcEditor* editor = nullptr;
    uint64_t clipId = 0;

    EasingFixture() {
        editor = nc_editor_create();
        if (editor == nullptr) return;
        const std::uint64_t assetId = nc_import_asset(editor, "easing_src.mp4",
                                                     "/tmp/media_gen/detail_src.mp4", 0, 2000000);
        if (assetId == 0) return;
        nc_add_track(editor, "V1", 0);
        clipId = nc_insert_clip(editor, 0, assetId, 0, 1000000);
    }
    ~EasingFixture() {
        if (editor != nullptr) nc_editor_destroy(editor);
    }
    bool ok() const { return editor != nullptr && clipId != 0; }
};

constexpr int32_t kOpacity = 0;   // ClipProperty::Opacity

}  // namespace

NC_TEST(KeyframeEasing, BezierSurvivesRoundTrip) {
    /* 核心：贝塞尔写进去，读回来必须还是贝塞尔，控制点一致。
       修复前挂在 `easing == 5` 上（读回来是 0）。 */
    EasingFixture f;
    NC_CHECK(f.ok());

    NC_CHECK(nc_insert_keyframe(f.editor, 0, f.clipId, kOpacity, 0, 0.0) == NC_OK);
    NC_CHECK(nc_insert_keyframe(f.editor, 0, f.clipId, kOpacity, 500000, 1.0) == NC_OK);

    // 带回弹过冲的曲线：y 超出 [0,1] 是刻意允许的
    const double c1x = 0.42, c1y = 0.0, c2x = 0.58, c2y = 1.35;
    NC_CHECK(nc_set_keyframe_easing(f.editor, 0, f.clipId, kOpacity, 0, 5, c1x, c1y, c2x, c2y) ==
             NC_OK);

    int64_t t = -1;
    double v = -1.0;
    int32_t easing = -1;
    double g1x = 0, g1y = 0, g2x = 0, g2y = 0;
    NC_CHECK(nc_keyframe_get_ex(f.editor, 0, f.clipId, kOpacity, 0, &t, &v, &easing, &g1x, &g1y,
                                &g2x, &g2y) == NC_OK);

    NC_CHECK(easing == 5);            // 关键：不是 0
    NC_CHECK(t == 0);
    NC_CHECK(std::fabs(v - 0.0) < 1e-9);
    NC_CHECK(std::fabs(g1x - c1x) < 1e-9);
    NC_CHECK(std::fabs(g1y - c1y) < 1e-9);
    NC_CHECK(std::fabs(g2x - c2x) < 1e-9);
    NC_CHECK(std::fabs(g2y - c2y) < 1e-9);   // 过冲值不能被钳掉
}

NC_TEST(KeyframeEasing, PresetEasingsMapCorrectly) {
    /* 0..4 是文档里的五种预设缓动，必须一一对上。 */
    EasingFixture f;
    NC_CHECK(f.ok());
    NC_CHECK(nc_insert_keyframe(f.editor, 0, f.clipId, kOpacity, 0, 0.0) == NC_OK);
    NC_CHECK(nc_insert_keyframe(f.editor, 0, f.clipId, kOpacity, 400000, 1.0) == NC_OK);

    for (int32_t want = 0; want <= 4; ++want) {
        NC_CHECK(nc_set_keyframe_easing(f.editor, 0, f.clipId, kOpacity, 0, want, 0, 0, 1, 1) ==
                 NC_OK);
        int32_t got = -1;
        NC_CHECK(nc_keyframe_get_ex(f.editor, 0, f.clipId, kOpacity, 0, nullptr, nullptr, &got,
                                    nullptr, nullptr, nullptr, nullptr) == NC_OK);
        NC_CHECK(got == want);
    }
}

NC_TEST(KeyframeEasing, SetEasingKeepsValue) {
    /* 改曲线不能改数值 —— 实现是「同值重插」，这条锁住语义。 */
    EasingFixture f;
    NC_CHECK(f.ok());
    NC_CHECK(nc_insert_keyframe(f.editor, 0, f.clipId, kOpacity, 123456, 0.375) == NC_OK);
    NC_CHECK(nc_set_keyframe_easing(f.editor, 0, f.clipId, kOpacity, 123456, 3, 0, 0, 1, 1) ==
             NC_OK);

    double v = -1.0;
    int64_t t = -1;
    NC_CHECK(nc_keyframe_get_ex(f.editor, 0, f.clipId, kOpacity, 0, &t, &v, nullptr, nullptr,
                                nullptr, nullptr, nullptr) == NC_OK);
    NC_CHECK(t == 123456);
    NC_CHECK(std::fabs(v - 0.375) < 1e-9);
}

NC_TEST(KeyframeEasing, RejectsInvalidInput) {
    /* 非法输入必须报错而不是静默新建关键帧 ——
       「打曲线打到不存在的帧上」若静默新建，用户会莫名多出一帧。 */
    EasingFixture f;
    NC_CHECK(f.ok());

    NC_CHECK(nc_set_keyframe_easing(f.editor, 0, f.clipId, kOpacity, 777777, 5, 0.1, 0.2, 0.3,
                                    0.4) != NC_OK);
    NC_CHECK(nc_insert_keyframe(f.editor, 0, f.clipId, kOpacity, 0, 0.5) == NC_OK);
    NC_CHECK(nc_set_keyframe_easing(f.editor, 0, f.clipId, kOpacity, 0, 6, 0, 0, 1, 1) != NC_OK);
    NC_CHECK(nc_set_keyframe_easing(f.editor, 0, f.clipId, kOpacity, 0, -1, 0, 0, 1, 1) != NC_OK);
    NC_CHECK(nc_set_keyframe_easing(f.editor, 0, f.clipId, 99, 0, 0, 0, 0, 1, 1) != NC_OK);

    int32_t easing = -1;
    NC_CHECK(nc_keyframe_get_ex(f.editor, 0, f.clipId, kOpacity, 0, nullptr, nullptr, &easing,
                                nullptr, nullptr, nullptr, nullptr) == NC_OK);
    NC_CHECK(easing == 0);   // 失败不改数据
}
