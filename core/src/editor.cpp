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
    const AssetId id = asset.id;
    const Result r = commit(std::make_unique<ImportAssetCommand>(id, std::move(asset)));
    if (!r.isOk()) {
        return AssetId{};
    }
    return id;
}

Result TimelineEditor::removeAsset(AssetId assetId) {
    if (isAssetReferenced(assetId)) {
        return Result::fail(ErrorCode::InvalidArgument, "素材仍被时间轴引用，无法删除");
    }
    return commit(std::make_unique<RemoveAssetCommand>(assetId));
}

Result TimelineEditor::relinkAsset(AssetId assetId, const std::string& newPath,
                                   const std::string& newDigest) {
    return commit(std::make_unique<RelinkAssetCommand>(assetId, newPath, newDigest));
}

bool TimelineEditor::isAssetReferenced(AssetId assetId) const {
    for (const auto& seq : project_.sequences) {
        for (const auto& track : seq.tracks) {
            for (const auto& clip : track.clips) {
                if (clip.asset == assetId) {
                    return true;
                }
            }
        }
    }
    return false;
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

Result TimelineEditor::renameTrack(SequenceId sequenceId, TrackId trackId,
                                    const std::string& newName) {
    return commit(std::make_unique<SetTrackCommand>(sequenceId, trackId, SetTrackCommand::Field::Name,
                                                    newName, false, "重命名轨道"));
}

Result TimelineEditor::setTrackLocked(SequenceId sequenceId, TrackId trackId, bool locked) {
    return commit(std::make_unique<SetTrackCommand>(sequenceId, trackId, SetTrackCommand::Field::Locked,
                                                    std::string(), locked, "锁定轨道"));
}

Result TimelineEditor::setTrackMuted(SequenceId sequenceId, TrackId trackId, bool muted) {
    return commit(std::make_unique<SetTrackCommand>(sequenceId, trackId, SetTrackCommand::Field::Muted,
                                                    std::string(), muted, "静音轨道"));
}

Result TimelineEditor::setTrackHidden(SequenceId sequenceId, TrackId trackId, bool hidden) {
    return commit(std::make_unique<SetTrackCommand>(sequenceId, trackId, SetTrackCommand::Field::Hidden,
                                                    std::string(), hidden, "隐藏轨道"));
}

Result TimelineEditor::setTrackAutoDuck(SequenceId sequenceId, TrackId trackId,
                                        const AutoDuckSettings& settings) {
    if (!settings.valid()) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "自动闪避参数非法（灵敏度 0..1、幅度 0..24dB、时间 0..3s）");
    }
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->type != TrackType::Audio) {
        return Result::fail(ErrorCode::InvalidArgument, "自动闪避仅支持音频轨");
    }
    return commit(std::make_unique<SetTrackCommand>(sequenceId, trackId, settings, "自动闪避"));
}

Result TimelineEditor::createCompoundClip(SequenceId sequenceId, TrackId trackId,
                                          std::vector<ClipId> clipIds, const std::string& name,
                                          SequenceId* outSequenceId, ClipId* outClipId) {
    if (clipIds.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "未选择任何片段");
    }
    // 预分配所有实体 ID（保证 redo 确定性；与既有命令约定一致）
    const SequenceId newSequenceId{project_.ids.next()};
    const TrackId newTrackId{project_.ids.next()};
    const ClipId newClipId{project_.ids.next()};
    auto command = std::make_unique<CreateCompoundClipCommand>(
        sequenceId, trackId, std::move(clipIds), newSequenceId, newTrackId, newClipId, name);
    const Result r = commit(std::move(command));
    if (r.isOk()) {
        if (outSequenceId != nullptr) {
            *outSequenceId = newSequenceId;
        }
        if (outClipId != nullptr) {
            *outClipId = newClipId;
        }
    }
    return r;
}

Result TimelineEditor::unwrapCompoundClip(SequenceId sequenceId, TrackId trackId,
                                          ClipId compoundClipId) {
    return commit(std::make_unique<UnwrapCompoundClipCommand>(sequenceId, trackId, compoundClipId));
}

Result TimelineEditor::setClipMask(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                    Mask mask) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    if (track->findClip(clipId) == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!mask.valid()) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "蒙版参数非法（羽化/圆角需在 0..0.5，尺寸需为正）");
    }
    return commit(std::make_unique<SetClipMaskCommand>(sequenceId, trackId, clipId, mask));
}

Result TimelineEditor::setClipChromaKey(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                        ChromaKey key, bool enabled) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    if (track->findClip(clipId) == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!key.valid()) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "抠像参数非法（阈值需在 0..1 且 outer > inner）");
    }
    if (!enabled) {
        // 清除抠像：包装为「设置但立即以禁用快照提交」的等价命令——
        // 复用设置命令的 apply/revert 语义（hasChromaKey 由命令置 true），
        // 因此这里用一个专用清除命令避免语义歧义。
        return commit(std::make_unique<ClearClipChromaKeyCommand>(sequenceId, trackId, clipId));
    }
    return commit(std::make_unique<SetClipChromaKeyCommand>(sequenceId, trackId, clipId, key));
}

Result TimelineEditor::setClipBlendMode(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                        BlendMode mode) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    if (track->findClip(clipId) == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    return commit(std::make_unique<SetClipBlendModeCommand>(sequenceId, trackId, clipId, mode));
}

Result TimelineEditor::setTrackSolo(SequenceId sequenceId, TrackId trackId, bool solo) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    return commit(std::make_unique<SetTrackCommand>(sequenceId, trackId,
                                                    SetTrackCommand::Field::Solo,
                                                    solo ? 1.0 : 0.0, solo ? "独奏" : "取消独奏"));
}

Result TimelineEditor::setTrackGain(SequenceId sequenceId, TrackId trackId, double gainDb) {
    if (!(gainDb >= -60.0) || !(gainDb <= 12.0)) {
        return Result::fail(ErrorCode::InvalidArgument, "轨道增益需在 -60dB 到 +12dB 之间");
    }
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    return commit(std::make_unique<SetTrackCommand>(sequenceId, trackId,
                                                    SetTrackCommand::Field::Gain, gainDb,
                                                    "调整轨道音量"));
}

Result TimelineEditor::setMasterGain(SequenceId sequenceId, double gainDb) {
    if (!(gainDb >= -60.0) || !(gainDb <= 12.0)) {
        return Result::fail(ErrorCode::InvalidArgument, "主输出增益需在 -60dB 到 +12dB 之间");
    }
    const Sequence* seq = project_.findSequence(sequenceId);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    return commit(std::make_unique<SetTrackCommand>(
        sequenceId, TrackId{}, SetTrackCommand::Field::SeqMasterGain, gainDb, "调整主输出音量"));
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

Result TimelineEditor::setClipSpeedCurve(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                         SpeedCurve curve) {
    const Track* track = findTrack(project_, sequenceId, trackId);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    const Clip* clip = track->findClip(clipId);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!curve.invariantsHold()) {
        return Result::fail(ErrorCode::InvalidArgument, "速度曲线不满足不变量");
    }
    // 时长语义：非空曲线时时间轴时长 = 曲线积分（源区间 [sourceIn, sourceOut)）
    Clip after = *clip;
    after.speedCurve = std::move(curve);
    if (!after.speedCurve.empty()) {
        const Time sourceOut = clip->sourceIn + clip->duration * clip->speed;
        const Time mapped = after.speedCurve.timelineDuration(clip->sourceIn, sourceOut,
                                                              clip->speed);
        if (mapped.isPositive()) {
            after.duration = mapped;
        }
    }
    // 倒放与曲线互斥（倒放映射不参与曲线积分）
    if (after.reversed && !after.speedCurve.empty()) {
        after.reversed = false;
    }
    const Result check = validateClipPlacement(project_, *track, after, clipId);
    if (!check.isOk()) {
        return check;
    }
    return commit(
        std::make_unique<SetClipCommand>(sequenceId, trackId, std::move(after), "变速曲线"));
}

Result TimelineEditor::upsertSubtitle(SequenceId sequenceId, SubtitleCue cue) {
    return commit(std::make_unique<UpsertSubtitleCommand>(sequenceId, std::move(cue)));
}

Result TimelineEditor::removeSubtitle(SequenceId sequenceId, SubtitleId subtitleId) {
    return commit(std::make_unique<RemoveSubtitleCommand>(sequenceId, subtitleId));
}

Result TimelineEditor::insertKeyframe(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                     ClipProperty property, Keyframe keyframe) {
    return commit(std::make_unique<InsertKeyframeCommand>(sequenceId, trackId, clipId, property,
                                                          std::move(keyframe)));
}

Result TimelineEditor::removeKeyframe(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                      ClipProperty property, const Time& at) {
    return commit(
        std::make_unique<RemoveKeyframeCommand>(sequenceId, trackId, clipId, property, at));
}

Result TimelineEditor::addEffect(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                  Effect effect) {
    return commit(std::make_unique<AddEffectCommand>(sequenceId, trackId, clipId,
                                                     std::move(effect)));
}

Result TimelineEditor::removeEffect(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                    EffectId effectId) {
    return commit(
        std::make_unique<RemoveEffectCommand>(sequenceId, trackId, clipId, effectId));
}

Result TimelineEditor::setClipAudio(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                     double gainDb, const Time& fadeIn, const Time& fadeOut) {
    return commit(std::make_unique<SetClipAudioCommand>(sequenceId, trackId, clipId, gainDb,
                                                        fadeIn, fadeOut, "调整音频"));
}

Result TimelineEditor::setClipDenoise(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                      DenoiseStrength strength) {
    return commit(
        std::make_unique<SetClipDenoiseCommand>(sequenceId, trackId, clipId, strength));
}

Result TimelineEditor::setClipEnabled(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                      bool enabled) {
    return commit(std::make_unique<SetClipEnabledCommand>(sequenceId, trackId, clipId, enabled));
}

Result TimelineEditor::setClipReversed(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                       bool reversed) {
    return commit(std::make_unique<SetClipReversedCommand>(sequenceId, trackId, clipId, reversed));
}

Result TimelineEditor::setClipOpacity(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                      double opacity) {
    return commit(std::make_unique<SetClipOpacityCommand>(sequenceId, trackId, clipId, opacity));
}

Result TimelineEditor::setClipFilter(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                     FilterStyle style, double intensity) {
    return commit(
        std::make_unique<SetClipFilterCommand>(sequenceId, trackId, clipId, style, intensity));
}

Result TimelineEditor::setClipBackground(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                         BackgroundMode mode, std::uint32_t rgba,
                                         double intensity) {
    return commit(std::make_unique<SetClipBackgroundCommand>(sequenceId, trackId, clipId, mode,
                                                             rgba, intensity));
}

Result TimelineEditor::upsertMarker(SequenceId sequenceId, Marker marker) {
    return commit(std::make_unique<UpsertMarkerCommand>(sequenceId, std::move(marker)));
}

Result TimelineEditor::removeMarker(SequenceId sequenceId, MarkerId markerId) {
    return commit(std::make_unique<RemoveMarkerCommand>(sequenceId, markerId));
}

Result TimelineEditor::detachAudio(SequenceId sequenceId, TrackId videoTrackId,
                                    ClipId videoClipId, TrackId audioTrackId) {
    return commit(std::make_unique<DetachAudioCommand>(sequenceId, videoTrackId, videoClipId,
                                                       audioTrackId,
                                                       ClipId{project_.ids.next()}));
}

Result TimelineEditor::addTransition(SequenceId sequenceId, TrackId trackId,
                                     Transition transition) {
    return commit(std::make_unique<AddTransitionCommand>(sequenceId, trackId, std::move(transition)));
}

Result TimelineEditor::removeTransition(SequenceId sequenceId, TrackId trackId,
                                        TransitionId transitionId) {
    return commit(std::make_unique<RemoveTransitionCommand>(sequenceId, trackId, transitionId));
}

Result TimelineEditor::rippleDelete(SequenceId sequenceId, TrackId trackId,
                                     std::vector<ClipId> clipIds) {
    return commit(std::make_unique<RippleDeleteCommand>(sequenceId, trackId, std::move(clipIds)));
}

Result TimelineEditor::insertEdit(SequenceId sequenceId, TrackId trackId, Clip clip,
                                  const Time& at) {
    return commit(std::make_unique<InsertEditCommand>(sequenceId, trackId, std::move(clip), at,
                                                      ClipId{project_.ids.next()}));
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