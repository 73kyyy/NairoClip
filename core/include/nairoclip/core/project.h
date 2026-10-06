#pragma once

#include "nairoclip/core/media.h"
#include "nairoclip/core/sequence.h"

#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 工程（顶层聚合根）。
struct Project {
    ProjectId id;
    std::string name = "未命名项目";
    std::vector<MediaAsset> assets;
    std::vector<Sequence> sequences;
    IdAllocator ids;

    MediaAsset* findAsset(AssetId assetId);
    const MediaAsset* findAsset(AssetId assetId) const;
    Sequence* findSequence(SequenceId sequenceId);
    const Sequence* findSequence(SequenceId sequenceId) const;
    Sequence* mainSequence();
    const Sequence* mainSequence() const;
};

/// 跨序列查找轨道。
Track* findTrack(Project& project, SequenceId sequenceId, TrackId trackId);
const Track* findTrack(const Project& project, SequenceId sequenceId, TrackId trackId);

/// 跨序列查找片段。
Clip* findClip(Project& project, SequenceId sequenceId, TrackId trackId, ClipId clipId);
const Clip* findClip(const Project& project, SequenceId sequenceId, TrackId trackId,
                     ClipId clipId);

}  // namespace core
}  // namespace nairoclip