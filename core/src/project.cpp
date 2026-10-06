#include "nairoclip/core/project.h"

namespace nairoclip {
namespace core {

MediaAsset* Project::findAsset(AssetId assetId) {
    for (auto& a : assets) {
        if (a.id == assetId) {
            return &a;
        }
    }
    return nullptr;
}

const MediaAsset* Project::findAsset(AssetId assetId) const {
    for (const auto& a : assets) {
        if (a.id == assetId) {
            return &a;
        }
    }
    return nullptr;
}

Sequence* Project::findSequence(SequenceId sequenceId) {
    for (auto& s : sequences) {
        if (s.id == sequenceId) {
            return &s;
        }
    }
    return nullptr;
}

const Sequence* Project::findSequence(SequenceId sequenceId) const {
    for (const auto& s : sequences) {
        if (s.id == sequenceId) {
            return &s;
        }
    }
    return nullptr;
}

Sequence* Project::mainSequence() {
    return sequences.empty() ? nullptr : &sequences.front();
}

const Sequence* Project::mainSequence() const {
    return sequences.empty() ? nullptr : &sequences.front();
}

Track* findTrack(Project& project, SequenceId sequenceId, TrackId trackId) {
    Sequence* seq = project.findSequence(sequenceId);
    return seq == nullptr ? nullptr : seq->findTrack(trackId);
}

const Track* findTrack(const Project& project, SequenceId sequenceId, TrackId trackId) {
    const Sequence* seq = project.findSequence(sequenceId);
    return seq == nullptr ? nullptr : seq->findTrack(trackId);
}

Clip* findClip(Project& project, SequenceId sequenceId, TrackId trackId, ClipId clipId) {
    Track* track = findTrack(project, sequenceId, trackId);
    return track == nullptr ? nullptr : track->findClip(clipId);
}

const Clip* findClip(const Project& project, SequenceId sequenceId, TrackId trackId,
                     ClipId clipId) {
    const Track* track = findTrack(project, sequenceId, trackId);
    return track == nullptr ? nullptr : track->findClip(clipId);
}

}  // namespace core
}  // namespace nairoclip