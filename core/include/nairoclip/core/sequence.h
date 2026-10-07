#pragma once

#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/subtitle.h"
#include "nairoclip/core/track.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 时间轴标记（批注）。
struct Marker {
    MarkerId id;
    Time time;
    std::string name;
    std::string color;  // 如 "#FF8A5B"
};

/// 序列（一条独立时间线）。
struct Sequence {
    SequenceId id;
    std::string name = "主序列";
    FrameRate frameRate = FrameRate::of(30);
    int width = 1080;
    int height = 1920;
    /// 画幅适配模式：源素材比例与序列画布不一致时的处理策略
    /// （如 9:16 竖屏画布放入 16:9 横屏素材、智能重构图横转竖）。
    FrameFit frameFit = FrameFit::Fit;
    /// 背景填充色（0xRRGGBBAA；Fit 模式的信箱/邮筒边颜色）。
    std::uint32_t backgroundRgba = 0x000000FF;
    std::vector<Track> tracks;
    std::vector<Marker> markers;
    /// 序列级字幕轨道（独立于普通轨道，字幕事件集合）。
    SubtitleTrack subtitles;

    Track* findTrack(TrackId trackId);
    const Track* findTrack(TrackId trackId) const;

    /// 序列内容时长 = 所有轨道最后一个片段的终点最大值。
    Time duration() const;

    bool invariantsHold() const;
};

}  // namespace core
}  // namespace nairoclip