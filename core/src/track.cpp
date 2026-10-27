#include "nairoclip/core/track.h"

#include <algorithm>
#include <cstddef>

namespace nairoclip {
namespace core {

const Clip* Track::findClip(ClipId clipId) const {
    for (const auto& c : clips) {
        if (c.id == clipId) {
            return &c;
        }
    }
    return nullptr;
}

Clip* Track::findClip(ClipId clipId) {
    for (auto& c : clips) {
        if (c.id == clipId) {
            return &c;
        }
    }
    return nullptr;
}

const Clip* Track::clipAt(const Time& t) const {
    for (const auto& c : clips) {
        if (c.contains(t)) {
            return &c;
        }
    }
    return nullptr;
}

int Track::indexOf(ClipId clipId) const {
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (clips[i].id == clipId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool Track::invariantsHold() const {
    // B2-10：显示高度越界会让时间轴布局错乱，属于不变量的一部分。
    if (!heightValid()) {
        return false;
    }
    for (std::size_t i = 0; i < clips.size(); ++i) {
        if (!clips[i].valid()) {
            return false;
        }
        if (i > 0 && clips[i].start < clips[i - 1].end()) {
            return false;
        }
    }
    return true;
}

bool Track::hasOverlap(const Time& start, const Time& end, ClipId ignore) const {
    for (const auto& c : clips) {
        if (c.id == ignore) {
            continue;
        }
        if (start < c.end() && c.start < end) {
            return true;
        }
    }
    return false;
}

void Track::sortClips() {
    std::sort(clips.begin(), clips.end(),
              [](const Clip& a, const Clip& b) { return a.start < b.start; });
}

}  // namespace core
}  // namespace nairoclip