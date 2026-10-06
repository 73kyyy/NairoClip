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

    // ---- 轨道属性 ----
    /// 重命名轨道。
    Result renameTrack(SequenceId sequenceId, TrackId trackId, const std::string& newName);
    /// 设置轨道锁定（锁定后所有编辑被拒绝）。
    Result setTrackLocked(SequenceId sequenceId, TrackId trackId, bool locked);
    /// 设置轨道静音。
    Result setTrackMuted(SequenceId sequenceId, TrackId trackId, bool muted);
    /// 设置轨道隐藏。
    Result setTrackHidden(SequenceId sequenceId, TrackId trackId, bool hidden);
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
    /// 跨轨移动片段到目标轨道指定起点（源=目标时拒绝，请用 moveClip）。
    Result moveClipToTrack(SequenceId sequenceId, TrackId fromTrack, TrackId toTrack,
                           ClipId clipId, const Time& newStart);

    // ---- 字幕 ----
    /// 添加或更新字幕（同 ID 替换）。
    Result upsertSubtitle(SequenceId sequenceId, SubtitleCue cue);
    /// 删除字幕。
    Result removeSubtitle(SequenceId sequenceId, SubtitleId subtitleId);
    // ---- 关键帧 ----
    /// 插入/更新片段关键帧（同时刻替换；失败不入撤销栈）。
    Result insertKeyframe(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                          ClipProperty property, Keyframe keyframe);
    /// 删除片段关键帧。
    Result removeKeyframe(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                          ClipProperty property, const Time& at);
    // ---- 特效 ----
    /// 添加/更新片段特效。
    Result addEffect(SequenceId sequenceId, TrackId trackId, ClipId clipId, Effect effect);
    /// 删除片段特效。
    Result removeEffect(SequenceId sequenceId, TrackId trackId, ClipId clipId, EffectId effectId);
    // ---- 音频属性 ----
    /// 设置片段音频属性（增益 dB / 淡入 / 淡出）。
    Result setClipAudio(SequenceId sequenceId, TrackId trackId, ClipId clipId, double gainDb,
                        const Time& fadeIn, const Time& fadeOut);
    // ---- 标记 ----
    /// 添加/更新标记（同 ID 替换）。
    Result upsertMarker(SequenceId sequenceId, Marker marker);
    /// 删除标记。
    Result removeMarker(SequenceId sequenceId, MarkerId markerId);
    // ---- 音画分离 ----
    /// 将视频片段分离出音频片段到目标音频轨（同素材同时间）。
    Result detachAudio(SequenceId sequenceId, TrackId videoTrackId, ClipId videoClipId,
                       TrackId audioTrackId);
    // ---- 转场 ----
    /// 添加转场（自动校验；失败不入撤销栈）。
    Result addTransition(SequenceId sequenceId, TrackId trackId, Transition transition);
    /// 删除转场。
    Result removeTransition(SequenceId sequenceId, TrackId trackId, TransitionId transitionId);
    // ---- 波纹编辑 ----
    /// 波纹删除：删除片段并前移后续片段（可批量）。
    Result rippleDelete(SequenceId sequenceId, TrackId trackId, std::vector<ClipId> clipIds);
    /// 插入编辑：在 at 处插入片段，后续片段整体后移。
    Result insertEdit(SequenceId sequenceId, TrackId trackId, Clip clip, const Time& at);
    // ---- 撤销 / 重做 ----
    Result undo();
    Result redo();
    bool canUndo() const { return undoStack_.canUndo(); }
    bool canRedo() const { return undoStack_.canRedo(); }
    std::size_t undoCount() const { return undoStack_.undoCount(); }
    std::size_t redoCount() const { return undoStack_.redoCount(); }
    std::vector<std::string> undoHistoryNames() const { return undoStack_.undoHistoryNames(); }
    // ---- 事务（手势合并成单条历史）----
    void beginTransaction(const std::string& name) { undoStack_.beginTransaction(name); }
    bool endTransaction() { return undoStack_.endTransaction(project_); }
    bool inTransaction() const { return undoStack_.inTransaction(); }
    void setMergeEnabled(bool enabled) { undoStack_.setMergeEnabled(enabled); }

private:
    Result commit(std::unique_ptr<Command> command);

    Project& project_;
    UndoStack undoStack_;
};

}  // namespace core
}  // namespace nairoclip