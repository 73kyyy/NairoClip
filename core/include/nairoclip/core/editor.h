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
    /// 导入素材（进入撤销栈；返回素材 ID）。
    AssetId importAsset(MediaAsset asset);
    /// 删除素材（进入撤销栈）。
    Result removeAsset(AssetId assetId);
    /// 检查素材是否被任何序列引用（删除前校验用）。
    bool isAssetReferenced(AssetId assetId) const;
    /// 重新链接素材（写入新路径 + 清除离线标记；进入撤销栈）。
    /// 文档第 3 部分「四、媒体离线检测与重新链接」——[重新链接] 的落盘动作。
    /// - newDigest：空 = 保留原摘要（不做摘要覆盖）。
    Result relinkAsset(AssetId assetId, const std::string& newPath,
                       const std::string& newDigest = "");

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
    /// 设置音频自动闪避（开关/灵敏度/压低幅度；文档「🎵音频 → 自动闪避」）。
    Result setTrackAutoDuck(SequenceId sequenceId, TrackId trackId,
                            const AutoDuckSettings& settings);
    // ---- 复合片段 / 嵌套序列（文档第 3 部分「嵌套序列/复合片段」）----
    /// 创建复合片段：把轨道上连续的片段打包为嵌套序列，并在原位置留下引用片段
    /// （时间轴显示「📦复合片段」；双击进入内部编辑由 UI 层用嵌套序列实现）。
    /// - clipIds：待打包片段（必须连续、同一轨道）；
    /// - outSequenceId：出参，嵌套序列 ID；outClipId：出参，复合片段 ID。
    Result createCompoundClip(SequenceId sequenceId, TrackId trackId,
                              std::vector<ClipId> clipIds, const std::string& name,
                              SequenceId* outSequenceId = nullptr,
                              ClipId* outClipId = nullptr);
    /// 解除复合片段：把嵌套序列内容还原到复合片段所在位置并删除嵌套序列。
    Result unwrapCompoundClip(SequenceId sequenceId, TrackId trackId, ClipId compoundClipId);
    // ---- 混音台（文档第 3 部分「专业混音台视图」）----
    /// 设置轨道独奏（任一轨道 solo 时只有 solo 的音频轨参与混音）。
    Result setTrackSolo(SequenceId sequenceId, TrackId trackId, bool solo);
    /// 设置轨道增益（dB；-60..+12）。
    Result setTrackGain(SequenceId sequenceId, TrackId trackId, double gainDb);
    /// 设置序列主输出增益（dB；-60..+12）。
    Result setMasterGain(SequenceId sequenceId, double gainDb);
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
    /// 设置片段速度曲线（文档「剪辑 → 变速 → 曲线」；空曲线 = 恒速）。
    Result setClipSpeedCurve(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                             SpeedCurve curve);
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
    // ---- 蒙版（文档「✂️剪辑 → 蒙版：线性/镜面/圆形/矩形（羽化/反转）」）----
    /// 设置片段蒙版（shape=None 等价于清除蒙版）；参数非法/片段不存在时失败且不入撤销栈。
    Result setClipMask(SequenceId sequenceId, TrackId trackId, ClipId clipId, Mask mask);
    // ---- 色度抠像（文档「🤖AI实验室 → 智能抠像 → 色度抠像(绿幕)」）----
    /// 设置片段色度抠像（enabled=false 等价于清除）；参数非法/片段不存在时失败且不入撤销栈。
    /// 说明：Pro 权益门控由平台层执行（mattingAccessFor）；core 只保证算法与数据正确。
    Result setClipChromaKey(SequenceId sequenceId, TrackId trackId, ClipId clipId, ChromaKey key,
                            bool enabled);
    // ---- 画中画混合模式（文档第二段 5.9）----
    /// 设置片段画中画混合模式（多层合成时与下层的混合公式）。
    /// 说明：normal 为默认（与既有 alpha 混合行为一致）；其余四种为行业标准公式。
    Result setClipBlendMode(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                            BlendMode mode);
    // ---- 音频属性 ----
    /// 设置片段音频属性（增益 dB / 淡入 / 淡出）。
    Result setClipAudio(SequenceId sequenceId, TrackId trackId, ClipId clipId, double gainDb,
                        const Time& fadeIn, const Time& fadeOut);
    /// 设置片段降噪强度（文档「🎵音频 → 降噪 → 开关/强度(弱中强)」）。
    /// off = 关闭（清除）；其余三档启用真实谱减降噪。
    Result setClipDenoise(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                          DenoiseStrength strength);
    /// 设置片段启用/禁用（文档「剪辑」片段开关）。
    Result setClipEnabled(SequenceId sequenceId, TrackId trackId, ClipId clipId, bool enabled);
    /// 设置片段倒放（文档「✂️剪辑 → 倒放」）。
    Result setClipReversed(SequenceId sequenceId, TrackId trackId, ClipId clipId, bool reversed);
    /// 设置片段透明度（文档「✂️剪辑 → 不透明度」；0..1）。
    Result setClipOpacity(SequenceId sequenceId, TrackId trackId, ClipId clipId, double opacity);
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
    /// 清空撤销历史（文档 Part 5(续) 第32节「栈清空时机」：
    /// 用户退出剪辑页重新进入同一项目时清空，不跨会话保留）。
    void clearUndoHistory() { undoStack_.clear(); }
    /// 设置撤销栈最大深度（文档第 32 节「最大可撤销步数 20 步」）。
    void setUndoDepthLimit(std::size_t maxSteps) { undoStack_.setMaxDepth(maxSteps); }
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