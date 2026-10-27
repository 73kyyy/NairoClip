#include "nairoclip/core/sequence.h"

#include <algorithm>

namespace nairoclip {
namespace core {

Track* Sequence::findTrack(TrackId trackId) {
    for (auto& t : tracks) {
        if (t.id == trackId) {
            return &t;
        }
    }
    return nullptr;
}

const Track* Sequence::findTrack(TrackId trackId) const {
    for (const auto& t : tracks) {
        if (t.id == trackId) {
            return &t;
        }
    }
    return nullptr;
}

Time Sequence::duration() const {
    Time result;
    for (const auto& t : tracks) {
        for (const auto& c : t.clips) {
            if (c.end() > result) {
                result = c.end();
            }
        }
    }
    return result;
}

bool Sequence::invariantsHold() const {
    for (const auto& t : tracks) {
        if (!t.invariantsHold()) {
            return false;
        }
    }
    // B2-7：轨道组必须自洽——组有成员、成员都真实存在、且一条轨道只属于一个组。
    // 最后一条尤其重要：重复归组会让「静音联动」出现两个组同时改一条轨的歧义。
    std::vector<TrackId> seen;
    for (const auto& g : trackGroups) {
        if (!g.valid()) {
            return false;
        }
        for (TrackId member : g.tracks) {
            if (findTrack(member) == nullptr) {
                return false;  // 组里引用了不存在的轨道
            }
            if (std::find(seen.begin(), seen.end(), member) != seen.end()) {
                return false;  // 一条轨道出现在多个组
            }
            seen.push_back(member);
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip