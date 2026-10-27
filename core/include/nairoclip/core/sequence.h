#pragma once

#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/reframe.h"
#include "nairoclip/core/subtitle.h"
#include "nairoclip/core/track.h"

#include <algorithm>

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
/// 轨道组（B2-7）：把跨类型的多条轨道绑在一起联动操作。
///
/// 产品场景：主画面 = 视频轨 + 其分离出的音频轨 + 字幕轨。用户希望
/// 「一起隐藏」「一起静音」「一起上下挪」，而不必逐条点。
///
/// 约束：一条轨道**只能属于一个组**。允许同一条轨出现在两个组里会让
/// 「静音联动」产生歧义（A 组要静音、B 组要取消），且没有自然的解法。
struct TrackGroup {
    TrackGroupId id;
    std::string name;
    /// 成员轨道（允许跨类型；顺序仅用于显示，不改变 tracks 的实际顺序）。
    std::vector<TrackId> tracks;
    /// UI 折叠状态（B2-7 与 B2-10 的折叠语义不同：这里指「组在面板里收起」）。
    bool collapsed = false;

    bool contains(TrackId t) const {
        return std::find(tracks.begin(), tracks.end(), t) != tracks.end();
    }
    bool valid() const { return id.isValid() && !tracks.empty(); }
};

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
    /// 智能重构图轨迹（非空时导出/预览优先按主体跟踪裁剪；
    /// 对应文档「AI工具 → 智能重构图」）。空 = 使用 frameFit。
    ReframeTrack reframe;
    /// 主输出增益（dB；混音台「主输出」推子；合法范围 [-60, +12]）。
    double masterGainDb = 0.0;
    std::vector<Track> tracks;
    std::vector<Marker> markers;
    /// 序列级字幕轨道（独立于普通轨道，字幕事件集合）。
    SubtitleTrack subtitles;
    /// 轨道组（B2-7）。成员必须是本序列中存在的轨道。
    std::vector<TrackGroup> trackGroups;

    /// 查找包含指定轨道的组（无则返回 nullptr）。
    const TrackGroup* groupOf(TrackId trackId) const {
        for (const auto& g : trackGroups) {
            if (g.contains(trackId)) {
                return &g;
            }
        }
        return nullptr;
    }
    TrackGroup* groupOf(TrackId trackId) {
        for (auto& g : trackGroups) {
            if (g.contains(trackId)) {
                return &g;
            }
        }
        return nullptr;
    }

    Track* findTrack(TrackId trackId);
    const Track* findTrack(TrackId trackId) const;

    /// 序列内容时长 = 所有轨道最后一个片段的终点最大值。
    Time duration() const;

    bool invariantsHold() const;
};

}  // namespace core
}  // namespace nairoclip