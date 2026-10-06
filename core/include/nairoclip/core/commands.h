#pragma once

#include "nairoclip/core/command.h"
#include "nairoclip/core/animation.h"
#include "nairoclip/core/transition_types.h"

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

    /// 同片段 + 同操作名 + 当前值等于对方目标值 → 可合并（连续拖动/微调）。
    bool canMergeWith(const Command& other) const override;
    /// 吸收后续命令的目标状态（before_ 保持不变，撤销回到手势起点）。
    void mergeFrom(const Command& other) override;

    /// 供编辑器包装类访问（合并判定需要）。
    ClipId clipId() const { return after_.id; }
    const std::string& actionName() const { return actionName_; }

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


/// 添加/更新字幕（新增或替换；撤销时恢复旧值或删除）。
class UpsertSubtitleCommand : public Command {
public:
    UpsertSubtitleCommand(SequenceId sequenceId, SubtitleCue cue);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "添加字幕"; }

private:
    SequenceId sequenceId_;
    SubtitleCue cue_;
    bool isReplace_ = false;
    SubtitleCue previous_;  // 替换时的旧值快照
};

/// 删除字幕。
class RemoveSubtitleCommand : public Command {
public:
    RemoveSubtitleCommand(SequenceId sequenceId, SubtitleId subtitleId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除字幕"; }

private:
    SequenceId sequenceId_;
    SubtitleId subtitleId_;
    std::optional<SubtitleCue> removed_;  // apply 时快照
};


/// 新增转场（校验通过后追加到轨道转场列表）。
class AddTransitionCommand : public Command {
public:
    AddTransitionCommand(SequenceId sequenceId, TrackId trackId, Transition transition);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "添加转场"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    Transition transition_;
};

/// 删除转场。
class RemoveTransitionCommand : public Command {
public:
    RemoveTransitionCommand(SequenceId sequenceId, TrackId trackId, TransitionId transitionId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除转场"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    TransitionId transitionId_;
    std::optional<Transition> removed_;
};


/// 插入/更新片段动画关键帧。
class InsertKeyframeCommand : public Command {
public:
    InsertKeyframeCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                          ClipProperty property, Keyframe keyframe);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "添加关键帧"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    ClipId clipId_;
    ClipProperty property_;
    Keyframe keyframe_;
    bool isReplace_ = false;
    Keyframe previous_;  // 替换时的旧值快照
};

/// 删除片段动画关键帧。
class RemoveKeyframeCommand : public Command {
public:
    RemoveKeyframeCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                          ClipProperty property, Time at);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除关键帧"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    ClipId clipId_;
    ClipProperty property_;
    Time at_;
    std::optional<Keyframe> removed_;
};


/// 波纹删除：删除片段，并将轨道上后续片段整体前移（填补空缺）。
/// 支持批量（同一轨道多个片段：按 start 降序依次处理）。
class RippleDeleteCommand : public Command {
public:
    RippleDeleteCommand(SequenceId sequenceId, TrackId trackId, std::vector<ClipId> clipIds);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "波纹删除"; }

private:
    struct RemovedClip {
        Clip clip;
        std::size_t originalIndex = 0;
    };
    SequenceId sequenceId_;
    TrackId trackId_;
    std::vector<ClipId> clipIds_;  // apply 前按 start 降序排序
    std::vector<RemovedClip> removed_;  // 快照（供 revert）
};

/// 插入编辑：在指定时刻插入片段，并将该时刻之后的重叠片段整体后移。
/// 语义：所有 start >= at 的片段后移 insertedDuration；
/// 跨越 at 的片段先分割，右半部分与后续片段一起后移。
class InsertEditCommand : public Command {
public:
    InsertEditCommand(SequenceId sequenceId, TrackId trackId, Clip clip, const Time& at,
                      ClipId rightHalfId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "插入编辑"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    Clip clip_;
    Time at_;
    ClipId rightHalfId_;  // 跨时刻片段被分割后右半的新 ID
    // revert 快照
    std::vector<Clip> beforeClips_;
};


/// 修改轨道属性（重命名/锁定/静音/隐藏）。
class SetTrackCommand : public Command {
public:
    /// 属性字段选择（单字段修改，可组合扩展）。
    enum class Field { Name, Locked, Muted, Hidden };

    SetTrackCommand(SequenceId sequenceId, TrackId trackId, Field field, std::string nameValue,
                    bool boolValue, std::string actionName);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return actionName_; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    Field field_;
    std::string nameValue_;
    bool boolValue_ = false;
    std::string actionName_;
    // 快照
    std::optional<std::string> beforeName_;
    std::optional<bool> beforeBool_;
};


/// 音画分离：在音频轨创建与视频片段同时间的音频片段。
/// 音频片段引用同一素材（真实实现按音频流读取）。
class DetachAudioCommand : public Command {
public:
    DetachAudioCommand(SequenceId sequenceId, TrackId videoTrackId, ClipId videoClipId,
                       TrackId audioTrackId, ClipId newAudioClipId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "音画分离"; }

private:
    SequenceId sequenceId_;
    TrackId videoTrackId_;
    ClipId videoClipId_;
    TrackId audioTrackId_;
    ClipId newAudioClipId_;
    std::optional<Clip> created_;  // apply 时生成的音频片段快照
};


/// 添加/更新标记。
class UpsertMarkerCommand : public Command {
public:
    UpsertMarkerCommand(SequenceId sequenceId, Marker marker);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "添加标记"; }

private:
    SequenceId sequenceId_;
    Marker marker_;
    bool isReplace_ = false;
    Marker previous_;
};

/// 删除标记。
class RemoveMarkerCommand : public Command {
public:
    RemoveMarkerCommand(SequenceId sequenceId, MarkerId markerId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除标记"; }

private:
    SequenceId sequenceId_;
    MarkerId markerId_;
    std::optional<Marker> removed_;
};


/// 设置片段音频属性（增益 / 淡入 / 淡出）。
class SetClipAudioCommand : public Command {
public:
    SetClipAudioCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                        double gainDb, const Time& fadeIn, const Time& fadeOut,
                        std::string actionName);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return actionName_; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    ClipId clipId_;
    double gainDb_ = 0.0;
    Time fadeIn_;
    Time fadeOut_;
    std::string actionName_;
    struct Snapshot {
        double gainDb = 0.0;
        Time fadeIn;
        Time fadeOut;
    };
    std::optional<Snapshot> before_;
};


/// 添加/更新片段特效。
class AddEffectCommand : public Command {
public:
    AddEffectCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId, Effect effect);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "添加特效"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    ClipId clipId_;
    Effect effect_;
    bool isReplace_ = false;
    Effect previous_;
};

/// 删除片段特效。
class RemoveEffectCommand : public Command {
public:
    RemoveEffectCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId, EffectId effectId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除特效"; }

private:
    SequenceId sequenceId_;
    TrackId trackId_;
    ClipId clipId_;
    EffectId effectId_;
    std::optional<Effect> removed_;
    std::optional<std::size_t> removedIndex_;
};


/// 导入素材（进入撤销栈）。
class ImportAssetCommand : public Command {
public:
    ImportAssetCommand(AssetId assetId, MediaAsset asset);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "导入素材"; }

private:
    AssetId assetId_;
    MediaAsset asset_;
    bool hadSnapshot_ = false;
};

/// 删除素材（排除被引用？由编辑器层校验；本命令只负责数据结构）。
class RemoveAssetCommand : public Command {
public:
    RemoveAssetCommand(AssetId assetId);

    Result apply(Project& project) override;
    Result revert(Project& project) override;
    std::string name() const override { return "删除素材"; }

private:
    AssetId assetId_;
    std::optional<MediaAsset> removed_;
    std::optional<std::size_t> removedIndex_;
};

}  // namespace core
}  // namespace nairoclip
