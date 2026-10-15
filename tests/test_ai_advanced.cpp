// =============================================================================
// 高级 AI 功能接口测试（第7批）：
//   镜头追踪（Part 4-A 3.3）/ 深度图效果（Part 4-C 13.2）/
//   自定义关键帧转场（附录二）
// =============================================================================
#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cmath>
#include <cstdint>
#include <string>

namespace {

std::string takeString(char* text) {
    if (text == nullptr) {
        return std::string();
    }
    const std::string result(text);
    nc_free_string(text);
    return result;
}

/// 带字幕的编辑器（追踪目标）。
NcEditor* makeEditorWithSubtitle(std::uint64_t* outSubtitle) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t subtitle = nc_upsert_subtitle(editor, 0, 5000000, "跟随文字");
    if (outSubtitle != nullptr) *outSubtitle = subtitle;
    return editor;
}

/// 带两段相邻片段 + 转场的编辑器（关键帧转场测试用）。
NcEditor* makeEditorWithTransition(std::uint64_t* outTransition, std::uint64_t* outFromClip) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "v.mp4", "/v/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip1 = nc_insert_clip(editor, 0, asset, 0, 2000000);
    const std::uint64_t clip2 = nc_insert_clip(editor, 0, asset, 2500000, 2000000);
    // 交叉溶解 0.5s（间隙 0.5s）
    const std::uint64_t transition = nc_add_transition(editor, 0, 0, 500000, clip1, clip2);
    if (outTransition != nullptr) *outTransition = transition;
    if (outFromClip != nullptr) *outFromClip = clip1;
    return editor;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 镜头追踪（文档 Part 4-A 3.3：追踪结果生成关键帧并绑定文字对象）
// ---------------------------------------------------------------------------

NC_TEST(AiAdvanced, CameraTrackingSetGet) {
    std::uint64_t subtitle = 0;
    NcEditor* editor = makeEditorWithSubtitle(&subtitle);
    NC_CHECK(subtitle != 0);
    // 初始无追踪
    NC_CHECK_EQ(nc_tracking_point_count(editor, subtitle), 0);
    // 设置 4 个追踪采样点（物体从左向右移动）
    const double times[4] = {0.0, 500000.0, 1000000.0, 1500000.0};
    const double xs[4] = {0.3, 0.4, 0.6, 0.7};
    const double ys[4] = {0.5, 0.5, 0.5, 0.55};
    NC_CHECK_EQ(nc_set_camera_tracking(editor, subtitle, times, xs, ys, 4, 0), NC_OK);
    NC_CHECK_EQ(nc_tracking_point_count(editor, subtitle), 4);
    // 回读（升序）
    double timeUs = 0.0;
    double x = 0.0;
    double y = 0.0;
    NC_CHECK_EQ(nc_tracking_point_get(editor, subtitle, 0, &timeUs, &x, &y), NC_OK);
    NC_CHECK(std::abs(timeUs - 0.0) < 1e-6);
    NC_CHECK(std::abs(x - 0.3) < 1e-9);
    NC_CHECK_EQ(nc_tracking_point_get(editor, subtitle, 3, &timeUs, &x, &y), NC_OK);
    NC_CHECK(std::abs(timeUs - 1500000.0) < 1e-6);
    NC_CHECK(std::abs(x - 0.7) < 1e-9);
    NC_CHECK(std::abs(y - 0.55) < 1e-9);
    // 越界
    NC_CHECK_EQ(nc_tracking_point_get(editor, subtitle, 9, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    // 追踪类型（文档 3.3：位置/位置+旋转/位置+缩放）
    NC_CHECK_EQ(nc_set_camera_tracking(editor, subtitle, times, xs, ys, 4, 1), NC_OK);
    // 清除
    NC_CHECK_EQ(nc_clear_camera_tracking(editor, subtitle), NC_OK);
    NC_CHECK_EQ(nc_tracking_point_count(editor, subtitle), 0);
    // 参数校验
    NC_CHECK_EQ(nc_set_camera_tracking(editor, subtitle, nullptr, xs, ys, 4, 0),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_camera_tracking(editor, 9999, times, xs, ys, 4, 0), NC_ERROR_NOT_FOUND);
    nc_editor_destroy(editor);
}

NC_TEST(AiAdvanced, CameraTrackingPersists) {
    std::uint64_t subtitle = 0;
    NcEditor* editor = makeEditorWithSubtitle(&subtitle);
    const double times[2] = {0.0, 1000000.0};
    const double xs[2] = {0.25, 0.75};
    const double ys[2] = {0.4, 0.6};
    NC_CHECK_EQ(nc_set_camera_tracking(editor, subtitle, times, xs, ys, 2, 0), NC_OK);
    // 序列化往返（追踪数据随工程持久化）
    char* text = nc_project_save(editor);
    NC_CHECK(text != nullptr);
    const std::string saved(text);
    nc_free_string(text);
    NC_CHECK(saved.find("\"tracking\"") != std::string::npos);
    NcEditor* reloaded = nc_editor_create();
    NC_CHECK_EQ(nc_project_load(reloaded, saved.c_str(), nullptr), NC_OK);
    NC_CHECK_EQ(nc_tracking_point_count(reloaded, subtitle), 2);
    double x = 0.0;
    NC_CHECK_EQ(nc_tracking_point_get(reloaded, subtitle, 1, nullptr, &x, nullptr), NC_OK);
    NC_CHECK(std::abs(x - 0.75) < 1e-9);
    nc_editor_destroy(reloaded);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 2. 深度图效果（文档 Part 4-C 13.2：景深/雾化/色彩分层/视差3D）
// ---------------------------------------------------------------------------

NC_TEST(AiAdvanced, DepthEffectSetGet) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t asset = nc_import_asset(editor, "v.mp4", "/v/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 5000000);
    // 默认未设置
    NC_CHECK_EQ(nc_get_depth_effect(editor, 0, clip, nullptr, nullptr, nullptr, nullptr, nullptr),
                0);
    // 景深虚化（文档 13.2 默认值：对焦 50%、光圈 2.8、强度 70）
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, NC_DEPTH_EFFECT_BOKEH, 0.5, 2.8, 70.0,
                                    0xFFFFFFFF),
                NC_OK);
    std::int32_t mode = -1;
    double focus = -1.0;
    double aperture = -1.0;
    double intensity = -1.0;
    NC_CHECK_EQ(nc_get_depth_effect(editor, 0, clip, &mode, &focus, &aperture, &intensity,
                                    nullptr),
                1);
    NC_CHECK_EQ(mode, NC_DEPTH_EFFECT_BOKEH);
    NC_CHECK(std::abs(focus - 0.5) < 1e-9);
    NC_CHECK(std::abs(aperture - 2.8) < 1e-9);
    NC_CHECK(std::abs(intensity - 70.0) < 1e-9);
    // 雾化（文档 13.2：雾浓度 55）
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, NC_DEPTH_EFFECT_FOG, 0.5, 2.8, 55.0,
                                    0xFFFFFFF0),
                NC_OK);
    // 色彩分层
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, NC_DEPTH_EFFECT_COLOR_BANDS, 0.5, 2.8,
                                    50.0, 0xFFCC88FF),
                NC_OK);
    // 视差3D（文档 13.2：视差强度 40）
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, NC_DEPTH_EFFECT_PARALLAX, 0.5, 2.8, 40.0,
                                    0xFFFFFFFF),
                NC_OK);
    // 参数边界（文档 13.2：光圈 1.0~22.0；对焦 0~100%）
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, NC_DEPTH_EFFECT_BOKEH, 1.5, 2.8, 70.0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, NC_DEPTH_EFFECT_BOKEH, 0.5, 30.0, 70.0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, NC_DEPTH_EFFECT_BOKEH, 0.5, 2.8, 150.0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_depth_effect(editor, 0, clip, 9, 0.5, 2.8, 70.0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    // NULL 安全
    NC_CHECK_EQ(nc_get_depth_effect(nullptr, 0, clip, nullptr, nullptr, nullptr, nullptr, nullptr),
                -1);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 3. 自定义关键帧转场（文档附录二）
// ---------------------------------------------------------------------------

NC_TEST(AiAdvanced, TransitionKeyframes) {
    std::uint64_t transition = 0;
    std::uint64_t fromClip = 0;
    NcEditor* editor = makeEditorWithTransition(&transition, &fromClip);
    NC_CHECK(transition != 0);
    // 转场时长查询（0.5s = 500000us）
    NC_CHECK_EQ(nc_transition_duration_us(editor, 0, transition), std::int64_t(500000));
    // 文档附录二：对「不透明度」(property 0) 打两个关键帧（0s=1.0, 0.3s=0.0）
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 0, 0, 1.0, 0), NC_OK);
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 0, 300000, 0.0, 3), NC_OK);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 0), 2);
    // 回读（偏移微秒 / 数值 / 缓动）
    double offsetUs = -1.0;
    double value = -1.0;
    std::int32_t easing = -1;
    NC_CHECK_EQ(nc_transition_keyframe_get(editor, 0, transition, 0, 1, &offsetUs, &value,
                                           &easing),
                NC_OK);
    NC_CHECK(std::abs(offsetUs - 300000.0) < 1e-6);
    NC_CHECK(std::abs(value - 0.0) < 1e-9);
    NC_CHECK_EQ(easing, 3);  // 缓入缓出
    // 位置X（property 2）：文档附录二「位置」属性
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 2, 0, 0.4, 1), NC_OK);
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 2, 400000, 0.6, 2), NC_OK);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 2), 2);
    // 超出转场区间 → 拒绝（文档附录二：仅区间内）
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 0, 900000, 1.0, 0),
                NC_ERROR_OUT_OF_RANGE);
    // 删除
    NC_CHECK_EQ(nc_remove_transition_keyframe(editor, 0, transition, 0, 300000), NC_OK);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 0), 1);
    // 撤销恢复
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 0), 2);
    // 参数校验
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, 9999, 0, 0, 1.0, 0), NC_ERROR_NOT_FOUND);
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 9, 0, 1.0, 0),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_transition_duration_us(editor, 0, 9999), std::int64_t(-1));
    nc_editor_destroy(editor);
}

NC_TEST(AiAdvanced, TransitionKeyframesIndependentPerProperty) {
    // 文档附录二：5 个属性分别独立打关键帧，互不影响
    std::uint64_t transition = 0;
    NcEditor* editor = makeEditorWithTransition(&transition, nullptr);
    // 位置Y(3)/缩放(1)/旋转(4) 各 1 个
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 3, 100000, 0.6, 0), NC_OK);
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 1, 100000, 1.2, 0), NC_OK);
    NC_CHECK_EQ(nc_add_transition_keyframe(editor, 0, transition, 4, 100000, 15.0, 0), NC_OK);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 3), 1);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 1), 1);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 4), 1);
    // 未使用的属性为 0
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 0), 0);
    NC_CHECK_EQ(nc_transition_keyframe_count(editor, 0, transition, 5), 0);
    // 值与缓动独立回读
    double value = 0.0;
    NC_CHECK_EQ(nc_transition_keyframe_get(editor, 0, transition, 1, 0, nullptr, &value,
                                           nullptr),
                NC_OK);
    NC_CHECK(std::abs(value - 1.2) < 1e-9);
    nc_editor_destroy(editor);
}
