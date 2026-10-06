#pragma once

#include "nairoclip/core/project.h"
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
};

/// 单个轨道在导出计划中的快照。
struct RenderTrack {
    TrackId trackId;
    TrackType type = TrackType::Video;
    bool muted = false;    // 音频轨：静音则跳过
    bool hidden = false;   // 视频轨：隐藏则跳过
    std::vector<RenderClip> clips;
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
