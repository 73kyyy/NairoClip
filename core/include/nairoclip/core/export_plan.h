#pragma once

#include "nairoclip/core/effects.h"
#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/reframe.h"
#include "nairoclip/core/speed_curve.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/transition_types.h"
#include "nairoclip/core/sequence.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 导出计划中的一个片段渲染描述（由时间轴编译而来）。
/// 这是「时间轴 → 导出器/预览器」的中间表示（IR），
/// 渲染器（FFmpeg/GPU）只消费该结构，不直接读取工程数据。
struct RenderClip {
    ClipId clipId;
    AssetId asset;
    /// 素材文件路径（编译时从工程素材表复制，导出器用）。
    std::string assetPath;
    /// 复合片段展开来源：非空表示本片段来自复合片段的嵌套序列
    /// （值为外层复合片段的 ID）。用于导出诊断与 UI 提示。
    ClipId fromCompound;
    /// 时间轴区间 [timelineStart, timelineEnd)。
    Time timelineStart;
    Time timelineEnd;
    /// 源素材区间 [sourceIn, sourceOut)。
    Time sourceIn;
    Time sourceOut;
    Rational speed = Rational::make(1);
    /// 导出用：该片段的动画采样（关键属性，逐帧渲染时重采样）。
    /// 这里仅携带标记；具体采样由渲染器在每帧调用 animation 完成。
    bool hasAnimation = false;
    /// 特效栈快照（颜色变换在导出时逐帧应用）。
    EffectStack effects;
    /// 动画快照（透明度等逐帧采样）。
    ClipAnimation animation;
    /// 速度曲线快照（非空时变速映射按曲线积分）。
    SpeedCurve speedCurve;
};

/// 导出计划中的转场（含计算好的时间区间）。
struct RenderTransition {
    TransitionId transitionId;
    TransitionType type = TransitionType::CrossDissolve;
    Time start;      // 转场区间起点（= fromClip.end()）
    Time end;        // 转场区间终点（= start + duration）
    ClipId fromClip;
    ClipId toClip;
};

/// 单个轨道在导出计划中的快照。
struct RenderTrack {
    TrackId trackId;
    TrackType type = TrackType::Video;
    bool muted = false;    // 音频轨：静音则跳过
    bool hidden = false;   // 视频轨：隐藏则跳过
    std::vector<RenderClip> clips;
    /// 转场列表（视频轨有效；已按 start 升序）。
    std::vector<RenderTransition> transitions;
};

/// 完整导出计划。
struct ExportPlan {
    SequenceId sequenceId;
    FrameRate frameRate;
    int width = 0;
    int height = 0;
    Time duration;   // 全片时长
    std::vector<RenderTrack> tracks;
    /// 校验错误（为空 = 计划可用）。
    std::vector<std::string> warnings;
    /// 画幅适配模式：源素材比例与序列画布不一致时的处理策略
    /// （导出预设 9:16 / 16:9 / 1:1、智能重构图横转竖依赖此设置）。
    FrameFit frameFit = FrameFit::Fit;
    /// 背景填充色（0xRRGGBBAA；Fit 模式的信箱/邮筒边颜色）。
    std::uint32_t backgroundRgba = 0x000000FF;
    /// 智能重构图轨迹（非空时优先于画幅适配：按主体跟踪裁剪 + 缩放，
    /// 对应文档「AI工具 → 智能重构图：横转竖/竖转横/跟踪灵敏度」）。
    /// 由 planReframe 生成；为空则走 frameFit 路径。
    ReframeTrack reframe;
};

/// 将序列编译为导出计划。
/// - 跳过 muted/hidden 轨道
/// - 跳过 disabled 片段
/// - 按轨道顺序排列（视频在下、音频在上由平台层决定，这里保留原顺序）
/// - 收集警告（如素材离线）
ExportPlan compileExportPlan(const Project& project, const Sequence& sequence);

/// 导出计划的基本校验：时长一致、区间合法、无负数。
bool exportPlanValid(const ExportPlan& plan);

}  // namespace core
}  // namespace nairoclip
