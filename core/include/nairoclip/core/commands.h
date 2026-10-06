#pragma once

#include "nairoclip/core/command.h"

#include <cstddef>
#include <optional>
#include <string>

namespace nairoclip {
namespace core {

/// 通用校验：clip 放入 track 是否合法（重叠、锁定、素材范围、参数）。
/// ignoreClipId 用于「修改自身」场景（忽略与自己的重叠检查）。
Result validateClipPlacement(const Project& project, const Track& track, const Clip& clip,
                             ClipId ignoreClipId = ClipId{});

/// 添加轨道（trackId 由调用方预分配，保证 redo 确定性）。
class AddTrackCommand : public Command {
public:
    AddTrackCommand(SequenceId sequenceId, TrackId trackId, TrackType type, std::string trackName);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "添加轨道"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    TrackType type_;
    std::string trackName_;
};

/// 删除轨道（含其中片段；快照保存，可完整还原）。
class RemoveTrackCommand : public Command {
public:
    RemoveTrackCommand(SequenceId sequenceId, TrackId trackId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除轨道"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    std::optional<Track> removedTrack_;
    std::optional<std::size_t> removedIndex_;
};

/// 插入片段。
class InsertClipCommand : public Command {
public:
    InsertClipCommand(SequenceId sequenceId, TrackId trackId, Clip clip);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "添加片段"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    Clip clip_;
};

/// 删除片段（快照保存，可完整还原）。
class RemoveClipCommand : public Command {
public:
    RemoveClipCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除片段"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    ClipId clipId_;
    std::optional<Clip> removedClip_;
    std::optional<std::size_t> removedIndex_;
};

/// 修改单个片段（移动 / 裁剪 / 变速等统一走此命令）。
class SetClipCommand : public Command {
public:
    SetClipCommand(SequenceId sequenceId, TrackId trackId, Clip after, std::string actionName);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return actionName_; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    Clip after_;
    std::string actionName_;
    std::optional<Clip> before_;
};

/// 在指定时刻分割片段：左半保留原 ID，右半使用预分配 ID。
class SplitClipCommand : public Command {
public:
    SplitClipCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId, const Time& at,
                     ClipId rightClipId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "分割片段"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    ClipId clipId_;
    Time at_;
    ClipId rightClipId_;
    std::optional<Clip> original_;
};
/// 跨轨移动片段：从 (fromTrack) 删除并插入到 (toTrack) 的指定起点。
/// 校验：目标轨道存在且未锁定、片段类型兼容（视频片段 → 视频/特效轨，
/// 音频片段 → 音频轨由调用方素材类型判断）、目标区间不重叠、
/// 源/目标轨道相同时退化为普通移动。
class MoveClipToTrackCommand : public Command {
public:
    MoveClipToTrackCommand(SequenceId sequenceId, TrackId fromTrack, TrackId toTrack,
                           ClipId clipId, const Time& newStart);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "跨轨移动"; }

private:
    SequenceId sequenceId_;
    TrackId fromTrack_;
    TrackId toTrack_;
    ClipId clipId_;
    Time newStart_;
    std::optional<Clip> movedClip_;  // apply 时的源快照（含原参数）
    std::optional<std::size_t> removedIndex_;
};

}  // namespace core
}  // namespace nairoclip
