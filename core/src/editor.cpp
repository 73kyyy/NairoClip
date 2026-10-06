#include "nairoclip/core/editor.h"

#include "nairoclip/core/commands.h"

#include <utility>

namespace nairoclip {
namespace core {

TimelineEditor::TimelineEditor(Project& project) : project_(project) {}

AssetId TimelineEditor::importAsset(MediaAsset asset) {
    if (!asset.id.isValid()) {
        asset.id = AssetId{project_.ids.next()};
    }
    project_.assets.push_back(std::move(asset));
    return project_.assets.back().id;
}

Result TimelineEditor::addTrack(SequenceId sequenceId, TrackType type, const std::string& name,
                                TrackId* outId) {
    const TrackId id{project_.ids.next()};
    auto command = std::make_unique<AddTrackCommand>(sequenceId, id, type, name);
    const Result r = commit(std::move(command));
    if (r.isOk() && outId != nullptr) {
        *outId = id;
    }
    return r;
}

Result TimelineEditor::removeTrack(SequenceId sequenceId, TrackId trackId) {
    return commit(std::make_unique<RemoveTrackCommand>(sequenceId, trackId));
}

Result TimelineEditor::insertClip(SequenceId sequenceId, TrackId trackId, Clip clip,
                                  ClipId* outId) {
    if (!clip.id.isValid()) {
        clip.id = ClipId{project_.ids.next()};
    }
    const ClipId id = clip.id;
    auto command = std::make_unique<InsertClipCommand>(sequenceId, trackId, std::move(clip));
    const Result r = commit(std::move(command));
    if (r.isOk() && outId != nullptr) {
        *outId = id;
    }
    return r;
}

Result TimelineEditor::removeClip(SequenceId sequenceId, TrackId trackId, ClipId clipId) {
    return commit(std::make_unique<RemoveClipCommand>(sequenceId, trackId, clipId));
}

Result TimelineEditor::splitClip(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                 const Time& at) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const Clip* clip = track->findClip(clipId);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!(clip->start < at && at < clip->end())) {
        return Result::fail(ErrorCode::OutOfRange, "分割点必须位于片段内部");
    }
    const ClipId rightId{project_.ids.next()};
    auto command = std::make_unique<SplitClipCommand>(sequenceId, trackId, clipId, at, rightId);
    return commit(std::move(command));
}

Result TimelineEditor::moveClip(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                const Time& newStart) {
    if (newStart.isNegative()) {
        return Result::fail(ErrorCode::InvalidArgument, "起点不能为负");
    }
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const Clip* clip = track->findClip(clipId);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    Clip after = *clip;
    after.start = newStart;
    return commit(
        std::make_unique<SetClipCommand>(sequenceId, trackId, std::move(after), "移动片段"));
}

Result TimelineEditor::trimClipStart(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                     const Time& newStart) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const Clip* clip = track->findClip(clipId);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    const Clip& c = *clip;
    if (newStart.isNegative() || !(newStart < c.end())) {
        return Result::fail(ErrorCode::OutOfRange, "新的起点必须位于片段终点之前");
    }
    const Time delta = newStart - c.start;
    const Time newSourceIn = c.sourceIn + delta * c.speed;
    if (newSourceIn.isNegative()) {
        return Result::fail(ErrorCode::OutOfRange, "已到达素材开头");
    }
    Clip after = c;
    after.start = newStart;
    after.duration = c.end() - newStart;
    after.sourceIn = newSourceIn;
    const Result check = validateClipPlacement(project_, *track, after, clipId);
    if (!check.isOk()) {
        return check;
    }
    return commit(std::make_unique<SetClipCommand>(sequenceId, trackId, std::move(after),
                                                   "裁剪起点"));
}

Result TimelineEditor::trimClipEnd(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                   const Time& newEnd) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const Clip* clip = track->findClip(clipId);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    const Clip& c = *clip;
    if (!(c.start < newEnd)) {
        return Result::fail(ErrorCode::OutOfRange, "新的终点必须位于片段起点之后");
    }
    Clip after = c;
    after.duration = newEnd - c.start;
    const Result check = validateClipPlacement(project_, *track, after, clipId);
    if (!check.isOk()) {
        return check;
    }
    return commit(std::make_unique<SetClipCommand>(sequenceId, trackId, std::move(after),
                                                   "裁剪终点"));
}

Result TimelineEditor::setClipSpeed(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                    const Rational& newSpeed) {
    const Rational minSpeed = Rational::make(1, 10);
    const Rational maxSpeed = Rational::make(10);
    if (!(newSpeed >= minSpeed) || !(newSpeed <= maxSpeed)) {
        return Result::fail(ErrorCode::InvalidArgument, "速度需在 0.1x 到 10x 之间");
    }
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const Clip* clip = track->findClip(clipId);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    const Clip& c = *clip;
    const Time sourceRange = c.duration * c.speed;
    Clip after = c;
    after.speed = newSpeed;
    after.duration = sourceRange / newSpeed;
    if (!after.duration.isPositive()) {
        return Result::fail(ErrorCode::OutOfRange, "变速后时长无效");
    }
    const Result check = validateClipPlacement(project_, *track, after, clipId);
    if (!check.isOk()) {
        return check;
    }
    return commit(
        std::make_unique<SetClipCommand>(sequenceId, trackId, std::move(after), "变速"));
}

Result TimelineEditor::moveClipToTrack(SequenceId sequenceId, TrackId fromTrack,
                                    TrackId toTrack, ClipId clipId, const Time& newStart) {
    return commit(std::make_unique<MoveClipToTrackCommand>(sequenceId, fromTrack, toTrack,
                                                           clipId, newStart));
}

Result TimelineEditor::undo() {
    return undoStack_.undo(project_);
}

Result TimelineEditor::redo() {
    return undoStack_.redo(project_);
}

Result TimelineEditor::commit(std::unique_ptr<Command> command) {
    return undoStack_.push(project_, std::move(command));
}

}  // namespace core
}  // namespace nairoclip