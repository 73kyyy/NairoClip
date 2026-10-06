#include "nairoclip/core/sequence.h"

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
    return true;
}

}  // namespace core
}  // namespace nairoclip