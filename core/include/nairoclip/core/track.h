#pragma once

#include "nairoclip/core/clip.h"
#include "nairoclip/core/transition_types.h"

#include <string>
#include <vector>

namespace nairoclip {
namespace core {

enum class TrackType {
    Video,
    Audio,
    Subtitle,
    Effect,
};

/// 单条轨道。不变量：clips 按 start 升序且互不重叠。
struct Track {
    TrackId id;
    TrackType type = TrackType::Video;
    std::string name;
    bool locked = false;
    bool muted = false;
    bool hidden = false;
    std::vector<Clip> clips;
    /// 转场列表（作用于 clips 中相邻的片段对）。
    std::vector<Transition> transitions;

    const Clip* findClip(ClipId clipId) const;
    Clip* findClip(ClipId clipId);
    const Clip* clipAt(const Time& t) const;
    int indexOf(ClipId clipId) const;

    /// 校验不变量：排序 + 无重叠 + 各片段合法。
    bool invariantsHold() const;

    /// 目标区间是否与现有片段重叠（ignore 用于忽略自身）。
    bool hasOverlap(const Time& start, const Time& end, ClipId ignore = ClipId{}) const;

    void sortClips();
};

}  // namespace core
}  // namespace nairoclip