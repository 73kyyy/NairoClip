#pragma once

#include "nairoclip/core/command.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 时间轴编辑器：对外统一 API 门面。
/// 所有编辑操作自动进入撤销栈；查询操作不改变状态。
class TimelineEditor {
public:
    explicit TimelineEditor(Project& project);

    Project& project() { return project_; }
    const Project& project() const { return project_; }

    // ---- 素材 ----
    AssetId importAsset(MediaAsset asset);

    // ---- 轨道 ----
    Result addTrack(SequenceId sequenceId, TrackType type, const std::string& name,
                    TrackId* outId = nullptr);
    Result removeTrack(SequenceId sequenceId, TrackId trackId);

    // ---- 片段 ----
    Result insertClip(SequenceId sequenceId, TrackId trackId, Clip clip, ClipId* outId = nullptr);
    Result removeClip(SequenceId sequenceId, TrackId trackId, ClipId clipId);
    Result splitClip(SequenceId sequenceId, TrackId trackId, ClipId clipId, const Time& at);
    Result moveClip(SequenceId sequenceId, TrackId trackId, ClipId clipId, const Time& newStart);
    Result trimClipStart(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                         const Time& newStart);
    Result trimClipEnd(SequenceId sequenceId, TrackId trackId, ClipId clipId, const Time& newEnd);
    Result setClipSpeed(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                        const Rational& newSpeed);

    // ---- 撤销 / 重做 ----
    Result undo();
    Result redo();
    bool canUndo() const { return undoStack_.canUndo(); }
    bool canRedo() const { return undoStack_.canRedo(); }
    std::size_t undoCount() const { return undoStack_.undoCount(); }
    std::size_t redoCount() const { return undoStack_.redoCount(); }
    std::vector<std::string> undoHistoryNames() const { return undoStack_.undoHistoryNames(); }

private:
    Result commit(std::unique_ptr<Command> command);

    Project& project_;
    UndoStack undoStack_;
};

}  // namespace core
}  // namespace nairoclip