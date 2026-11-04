/* =============================================================================
   预览质量：720p 下限 + 暂停回全质量（§8.3.1）
   -----------------------------------------------------------------------------
   验收依据（用户 2026-11-04 定案）：
     · 渲染画面短边不低于 720px
     · 暂停/单帧刷新必须回到 1.0
     · 导出永远是 1.0，不受预览设置影响
   ============================================================================= */

#include "test_framework.h"

#include "nairoclip/core/capi.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/project.h"

#include <cmath>

using namespace nairoclip::core;

namespace {

/// 建一个指定画布尺寸的工程。
NcEditor* makeEditor(int w, int h) {
    NcEditor* ed = nc_editor_create();
    nc_set_project_canvas(ed, w, h);
    return ed;
}

}  // namespace

NC_TEST(PreviewQuality, MinScaleFor1080pIsAbout067) {
    NcEditor* ed = makeEditor(1080, 1920);
    NC_CHECK(ed != nullptr);
    const double s = nc_preview_min_scale(ed);
    /* 短边 1080 → 720/1080 = 0.667 */
    NC_CHECK(std::abs(s - 720.0 / 1080.0) < 0.01);
    nc_editor_destroy(ed);
}

NC_TEST(PreviewQuality, MinScaleFor4KIsAbout033) {
    NcEditor* ed = makeEditor(2160, 3840);
    NC_CHECK(ed != nullptr);
    const double s = nc_preview_min_scale(ed);
    /* 短边 2160 → 720/2160 = 0.333 */
    NC_CHECK(std::abs(s - 720.0 / 2160.0) < 0.01);
    nc_editor_destroy(ed);
}

NC_TEST(PreviewQuality, MinScaleIsOneForSmallCanvas) {
    /* 画布本身不足 720p —— 不能再降，否则就是糊。 */
    NcEditor* ed = makeEditor(540, 960);
    NC_CHECK(ed != nullptr);
    NC_CHECK(std::abs(nc_preview_min_scale(ed) - 1.0) < 0.001);
    nc_editor_destroy(ed);
}

NC_TEST(PreviewQuality, MinScaleFor720pIsOne) {
    NcEditor* ed = makeEditor(720, 1280);
    NC_CHECK(ed != nullptr);
    NC_CHECK(std::abs(nc_preview_min_scale(ed) - 1.0) < 0.001);
    nc_editor_destroy(ed);
}

NC_TEST(PreviewQuality, DefaultIsNotDegraded) {
    NcEditor* ed = makeEditor(1080, 1920);
    NC_CHECK(ed != nullptr);
    NC_CHECK_EQ(nc_preview_is_degraded(ed), 0);
    nc_editor_destroy(ed);
}

NC_TEST(PreviewQuality, ResetWhenAlreadyFullIsNoOp) {
    NcEditor* ed = makeEditor(1080, 1920);
    NC_CHECK(ed != nullptr);
    /* 本来就是全质量 → 返回 0（不改变），不清缓存 */
    NC_CHECK_EQ(nc_preview_reset_full_quality(ed), 0);
    nc_editor_destroy(ed);
}

NC_TEST(PreviewQuality, NullEditorIsSafe) {
    NC_CHECK_EQ(nc_preview_reset_full_quality(nullptr), 0);
    NC_CHECK_EQ(nc_preview_is_degraded(nullptr), 0);
    NC_CHECK(std::abs(nc_preview_min_scale(nullptr)) < 1e-9);
}

NC_TEST(PreviewQuality, ExportQualityIsAlwaysFull) {
    /* 导出路径的 RenderQuality 必须 scale=1、不允许丢帧。
       这是「预览降级不影响成片」的硬保证。 */
    const RenderQuality q = RenderQuality::exportQuality();
    NC_CHECK(std::abs(q.scale - 1.0) < 1e-9);
    NC_CHECK(std::abs(q.sampleBudget - 1.0) < 1e-9);
    NC_CHECK(!q.allowFrameDrop);
    NC_CHECK(q.valid());
}

NC_TEST(PreviewQuality, DegradedQualityWithDropIsValid) {
    const RenderQuality q = RenderQuality::previewQuality(0.5, 0.5);
    NC_CHECK(q.valid());
    NC_CHECK(q.allowFrameDrop);
}

NC_TEST(PreviewQuality, ExportPathRejectsDegradedScale) {
    /* 误把预览质量传给导出路径 → valid() 必须拒绝，
       否则会「导出成片也是降分辨率」。 */
    RenderQuality q = RenderQuality::exportQuality();
    q.scale = 0.5;
    NC_CHECK(!q.valid());
}
