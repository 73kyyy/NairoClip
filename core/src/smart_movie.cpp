#include "nairoclip/core/smart_movie.h"

#include "nairoclip/core/commands.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/track.h"
#include "nairoclip/core/transition_types.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

// =============================================================================
// 权益门控（文档 7.6 / 7.7）
// =============================================================================

ReferenceStyleAccess referenceStyleAccess(const Entitlement& entitlement, std::int64_t nowUnix) {
    ReferenceStyleAccess access;
    if (entitlement.proActiveAt(nowUnix)) {
        access.allowed = true;
        access.reason = "Pro 权益：参考风格提取可用";
        return access;
    }
    access.allowed = false;
    access.showPaywall = true;
    access.reason =
        "🔒 参考视频风格提取为 Pro 功能；升级 Pro 解锁（免费版可用智能成片/音乐卡点等）";
    return access;
}

VersionCountAccess versionCountAccess(const Entitlement& entitlement, std::int64_t nowUnix,
                                      int requestedCount) {
    VersionCountAccess access;
    const int requested = requestedCount > 0 ? requestedCount : 1;
    access.requestedCount = requested;
    if (entitlement.proActiveAt(nowUnix)) {
        access.allowed = true;
        access.allowedCount = std::min(5, std::max(1, requested));
        access.reason = "Pro 权益：可选 1-5 个生成版本";
        return access;
    }
    access.allowedCount = 1;
    if (requested > 1) {
        access.allowed = false;
        access.showPaywall = true;
        access.reason = " 🔒 免费版固定生成 1 个版本；升级 Pro 可选 1-5 个版本";
    } else {
        access.allowed = true;
        access.reason = "免费版：固定生成 1 个版本";
    }
    return access;
}

// =============================================================================
// 变体名称
// =============================================================================

const char* movieVariantName(MovieVariant variant) {
    switch (variant) {
        case MovieVariant::Reference: return "reference";
        case MovieVariant::Fast: return "fast";
        case MovieVariant::Relaxed: return "relaxed";
        case MovieVariant::Dynamic: return "dynamic";
        case MovieVariant::Calm: return "calm";
    }
    return "reference";
}

const char* movieVariantLabel(MovieVariant variant) {
    switch (variant) {
        case MovieVariant::Reference: return "忠实参考";
        case MovieVariant::Fast: return "快切版";
        case MovieVariant::Relaxed: return "舒缓版";
        case MovieVariant::Dynamic: return "动感版";
        case MovieVariant::Calm: return "沉静版";
    }
    return "忠实参考";
}

std::size_t SmartMoviePlan::okCount() const {
    std::size_t count = 0;
    for (const auto& version : versions) {
        if (version.ok) {
            ++count;
        }
    }
    return count;
}

// =============================================================================
// 计划（内部工具）
// =============================================================================

namespace {

/// 变体对应的镜头时长（基于参考画像分位数 + 节奏滑块 + 变体偏移）。
Time variantShotDuration(const RhythmProfile& rhythm, MovieVariant variant, double paceFactor) {
    Time base = rhythm.median;
    if (!base.isPositive()) {
        base = rhythm.averageShotDuration;
    }
    if (!base.isPositive()) {
        base = Rational::make(2);  // 兜底 2 秒
    }
    // 节奏滑块：0.5 = 忠实（median）；> 0.5 向 p25；< 0.5 向 p75
    Time paced = base;
    if (paceFactor > 0.5 && rhythm.p25.isPositive()) {
        const double t = (paceFactor - 0.5) * 2.0;  // 0..1
        paced = base + (rhythm.p25 - base) * Rational::make(
                                                  static_cast<std::int64_t>(t * 1000), 1000);
    } else if (paceFactor < 0.5 && rhythm.p75.isPositive()) {
        const double t = (0.5 - paceFactor) * 2.0;  // 0..1
        paced = base + (rhythm.p75 - base) * Rational::make(
                                                  static_cast<std::int64_t>(t * 1000), 1000);
    }
    // 变体偏移
    Rational factor = Rational::make(1);
    switch (variant) {
        case MovieVariant::Reference: factor = Rational::make(1); break;
        case MovieVariant::Fast: factor = Rational::make(7, 10); break;
        case MovieVariant::Relaxed: factor = Rational::make(14, 10); break;
        case MovieVariant::Dynamic: factor = Rational::make(8, 10); break;
        case MovieVariant::Calm: factor = Rational::make(12, 10); break;
    }
    Time result = paced * factor;
    if (!result.isPositive()) {
        result = Rational::make(1, 2);
    }
    return result;
}

/// 变体是否在指定片段对之间加转场（确定性密度；真实差异可测）。
bool variantUsesTransition(MovieVariant variant, std::size_t pairIndex) {
    switch (variant) {
        case MovieVariant::Reference: return pairIndex % 2 == 0;
        case MovieVariant::Fast: return pairIndex % 3 == 0;
        case MovieVariant::Relaxed: return pairIndex % 2 == 0;
        case MovieVariant::Dynamic: return true;
        case MovieVariant::Calm: return pairIndex % 4 == 0;
    }
    return false;
}

/// 变体对应的转场时长。
Time variantTransitionDuration(MovieVariant variant) {
    switch (variant) {
        case MovieVariant::Reference: return Rational::make(30, 100);
        case MovieVariant::Fast: return Rational::make(20, 100);
        case MovieVariant::Relaxed: return Rational::make(40, 100);
        case MovieVariant::Dynamic: return Rational::make(50, 100);
        case MovieVariant::Calm: return Rational::make(60, 100);
    }
    return Rational::make(30, 100);
}

/// 按固定镜头时长生成剪辑建议（素材循环取用，与 suggestCuts 同一策略）。
std::vector<CutSuggestion> cutsForShotDuration(const RhythmProfile& rhythm,
                                               const Time& shotDuration,
                                               const std::vector<MaterialSegment>& material,
                                               double targetSeconds) {
    std::vector<CutSuggestion> cuts;
    if (material.empty() || !shotDuration.isPositive()) {
        return cuts;
    }
    const double shotSeconds = shotDuration.toDouble();
    std::size_t shotCount = rhythm.shotCount > 0 ? rhythm.shotCount : 1;
    if (targetSeconds > 0.0 && shotSeconds > 0.0) {
        shotCount = static_cast<std::size_t>(
            std::max(1.0, std::ceil(targetSeconds / shotSeconds)));
    }
    std::vector<Time> cursors(material.size());
    std::size_t materialIndex = 0;
    for (std::size_t i = 0; i < shotCount; ++i) {
        bool placed = false;
        for (std::size_t attempt = 0; attempt < material.size(); ++attempt) {
            const std::size_t idx = (materialIndex + attempt) % material.size();
            const Time remaining = material[idx].usableDuration - cursors[idx];
            if (!remaining.isPositive()) {
                continue;
            }
            CutSuggestion suggestion;
            suggestion.asset = material[idx].asset;
            suggestion.targetIndex = i;
            suggestion.start = cursors[idx];
            const Time use = remaining < shotDuration ? remaining : shotDuration;
            suggestion.duration = use;
            cursors[idx] = cursors[idx] + use;
            cuts.push_back(suggestion);
            materialIndex = (idx + 1) % material.size();
            placed = true;
            break;
        }
        if (!placed) {
            break;  // 素材耗尽
        }
    }
    return cuts;
}

}  // namespace

SmartMoviePlan planSmartMovie(const StyleProfile& profile, const SmartMovieConfig& config) {
    SmartMoviePlan plan;
    if (!profile.valid()) {
        plan.warnings.push_back("参考风格画像无效（无有效镜头），无法生成成片");
        return plan;
    }
    if (!config.valid()) {
        plan.warnings.push_back("配置非法（版本数 1-5、节奏滑块 0-1、素材不能为空）");
        return plan;
    }
    plan.rhythm = profile.rhythm;

    static const MovieVariant kVariants[5] = {MovieVariant::Reference, MovieVariant::Fast,
                                              MovieVariant::Relaxed, MovieVariant::Dynamic,
                                              MovieVariant::Calm};
    std::size_t totalCuts = 0;
    for (int v = 0; v < config.versionCount; ++v) {
        MovieVersionPlan version;
        version.index = v + 1;
        version.variant = kVariants[v];
        version.label = "版本 " + std::to_string(v + 1) + " · " + movieVariantLabel(version.variant);

        version.shotDuration =
            variantShotDuration(profile.rhythm, version.variant, config.paceFactor);
        version.cuts = cutsForShotDuration(profile.rhythm, version.shotDuration, config.material,
                                           config.targetSeconds);
        if (version.cuts.empty()) {
            version.ok = false;
            version.note = "素材不足以生成该版本（可用时长不足）";
            plan.warnings.push_back("版本 " + std::to_string(v + 1) + "：素材不足，已跳过");
            plan.versions.push_back(std::move(version));
            continue;
        }
        Time total;
        for (const auto& cut : version.cuts) {
            total = total + cut.duration;
        }
        version.totalDuration = total;
        if (config.applyTransitions && version.cuts.size() >= 2) {
            std::size_t count = 0;
            for (std::size_t i = 0; i + 1 < version.cuts.size(); ++i) {
                if (count >= config.maxTransitionsPerVersion) {
                    break;
                }
                if (variantUsesTransition(version.variant, i)) {
                    ++count;
                }
            }
            version.transitionCount = count;
        }
        version.ok = true;
        totalCuts += version.cuts.size();
        plan.versions.push_back(std::move(version));
    }

    if (plan.okCount() == 0) {
        plan.warnings.push_back("所有版本均无法生成（检查素材可用时长）");
        return plan;
    }
    plan.summary = "生成 " + std::to_string(plan.okCount()) + " 个版本（共 " +
                   std::to_string(totalCuts) + " 个片段；参考节奏平均 " +
                   std::to_string(profile.rhythm.averageShotDuration.toDouble()) + " 秒/镜头）";
    return plan;
}

// =============================================================================
// 落盘
// =============================================================================

SmartMovieApplyResult applySmartMovieVersion(TimelineEditor& editor, Project& project,
                                             SequenceId sequenceId, TrackId trackId,
                                             const MovieVersionPlan& version,
                                             const SmartMovieConfig& config,
                                             const std::string& transactionName) {
    SmartMovieApplyResult result;
    Track* track = findTrack(project, sequenceId, trackId);
    if (track == nullptr) {
        result.result = Result::fail(ErrorCode::NotFound, "目标轨道不存在");
        return result;
    }
    if (!track->clips.empty()) {
        result.result =
            Result::fail(ErrorCode::InvalidArgument, "目标轨道非空，智能成片仅支持空轨道");
        return result;
    }
    if (track->locked) {
        result.result = Result::fail(ErrorCode::Locked, "目标轨道已锁定");
        return result;
    }
    if (!version.ok || version.cuts.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument,
                                     version.note.empty() ? "版本计划无效" : version.note);
        return result;
    }

    const Time transitionDuration = variantTransitionDuration(version.variant);
    const bool useTransitions = config.applyTransitions && version.transitionCount > 0;
    auto needsGap = [&](std::size_t pairIndex) {
        return useTransitions && variantUsesTransition(version.variant, pairIndex);
    };

    const std::string name =
        transactionName.empty() ? ("智能成片 · " + version.label) : transactionName;
    editor.beginTransaction(name);
    Time cursor;
    std::vector<ClipId> insertedIds;
    for (std::size_t i = 0; i < version.cuts.size(); ++i) {
        const auto& cut = version.cuts[i];
        if (i > 0 && needsGap(i - 1)) {
            cursor = cursor + transitionDuration;  // 预留转场间隙
        }
        Clip clip;
        clip.asset = cut.asset;
        clip.start = cursor;
        clip.duration = cut.duration;
        clip.sourceIn = cut.start;
        ClipId insertedId;
        const Result r = editor.insertClip(sequenceId, trackId, clip, &insertedId);
        if (!r.isOk()) {
            result.warnings.push_back("片段插入失败：" + r.message);
            break;
        }
        insertedIds.push_back(insertedId);
        ++result.clipsInserted;
        cursor = cursor + cut.duration;
    }

    // 转场（间隙已预留）
    if (useTransitions && insertedIds.size() >= 2) {
        std::size_t added = 0;
        for (std::size_t i = 0;
             i + 1 < insertedIds.size() && added < config.maxTransitionsPerVersion; ++i) {
            if (!needsGap(i)) {
                continue;
            }
            const Track* current = findTrack(project, sequenceId, trackId);
            if (current == nullptr) {
                break;
            }
            const Clip* a = current->findClip(insertedIds[i]);
            const Clip* b = current->findClip(insertedIds[i + 1]);
            if (a == nullptr || b == nullptr) {
                continue;
            }
            const Time gap = b->start - a->end();
            if (!gap.isPositive() || gap < transitionDuration) {
                continue;  // 防御：不应发生
            }
            Transition tr;
            tr.id = TransitionId{project.ids.next()};
            tr.type = TransitionType::CrossDissolve;
            tr.duration = transitionDuration;
            tr.fromClip = a->id;
            tr.toClip = b->id;
            if (editor.addTransition(sequenceId, trackId, tr).isOk()) {
                ++added;
                ++result.transitionsAdded;
            }
        }
        if (added < version.transitionCount) {
            result.warnings.push_back("部分转场未添加（间隙约束）：计划 " +
                                      std::to_string(version.transitionCount) + " 实际 " +
                                      std::to_string(added));
        }
    }

    // 参考色调特效（自动添加内容）
    if (config.applyColor && !insertedIds.empty()) {
        const SuggestedColorEffects fx = suggestColorEffects(config.color);
        if (!fx.neutral()) {
            for (const auto& clipId : insertedIds) {
                auto addEffect = [&](EffectType type, double value) {
                    Effect e;
                    e.id = EffectId{project.ids.next()};
                    e.type = type;
                    e.value = value;
                    if (editor.addEffect(sequenceId, trackId, clipId, e).isOk()) {
                        ++result.effectsAdded;
                    }
                };
                if (fx.saturation != 1.0) {
                    addEffect(EffectType::Saturation, fx.saturation);
                }
                if (fx.contrast != 1.0) {
                    addEffect(EffectType::Contrast, fx.contrast);
                }
                if (fx.temperature != 0.0) {
                    addEffect(EffectType::Temperature, fx.temperature);
                }
            }
        }
    }

    editor.endTransaction();
    if (result.clipsInserted == 0) {
        result.result = Result::fail(ErrorCode::Internal, "未能插入任何片段");
        return result;
    }
    result.result = Result::ok();
    return result;
}

SmartMovieBatchResult applySmartMovieVersions(TimelineEditor& editor, Project& project,
                                              SequenceId sequenceId,
                                              const std::vector<TrackId>& trackIds,
                                              const SmartMoviePlan& plan,
                                              const SmartMovieConfig& config) {
    SmartMovieBatchResult out;
    if (plan.versions.empty()) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "没有可落盘的版本计划");
        return out;
    }
    std::size_t trackCursor = 0;
    for (const auto& version : plan.versions) {
        if (!version.ok) {
            out.failedVersionIndexes.push_back(version.index);
            out.failures.push_back("版本 " + std::to_string(version.index) + "：" +
                                   (version.note.empty() ? "计划无效" : version.note));
            continue;
        }
        if (trackCursor >= trackIds.size()) {
            out.failedVersionIndexes.push_back(version.index);
            out.failures.push_back("版本 " + std::to_string(version.index) +
                                   "：没有可用的空轨道（需为每个版本准备一条轨道）");
            continue;
        }
        const TrackId trackId = trackIds[trackCursor++];
        const SmartMovieApplyResult applied =
            applySmartMovieVersion(editor, project, sequenceId, trackId, version, config);
        if (applied.result.isOk()) {
            ++out.versionsApplied;
        } else {
            out.failedVersionIndexes.push_back(version.index);
            out.failures.push_back("版本 " + std::to_string(version.index) + "：" +
                                   applied.result.message);
        }
        out.perVersion.push_back(applied);
    }
    if (out.versionsApplied == 0) {
        out.result = Result::fail(ErrorCode::Internal, "所有版本均落盘失败");
        return out;
    }
    out.result = Result::ok();
    return out;
}

// =============================================================================
// 重新生成影响分析（文档 4.4）
// =============================================================================

bool RegenerationImpactReport::hasLosses() const { return lossCount() > 0; }

std::size_t RegenerationImpactReport::lossCount() const {
    std::size_t count = 0;
    for (const auto& item : willLose) {
        count += item.count;
    }
    return count;
}

std::vector<std::string> RegenerationImpactReport::detailLines() const {
    std::vector<std::string> lines;
    if (hasLosses()) {
        for (const auto& item : willLose) {
            if (item.count > 0) {
                lines.push_back("将会丢失：• " + item.label + "（" +
                                std::to_string(item.count) + " 处）");
            }
        }
    } else {
        lines.push_back("将会丢失：• 无（当前无手动编辑内容，重新生成不会丢失数据）");
    }
    for (const auto& item : willKeep) {
        if (item.count > 0) {
            lines.push_back("将会保留：• " + item.label + "（" + std::to_string(item.count) +
                            " 项）");
        }
    }
    if (lines.size() <= 1 && willKeep.empty()) {
        lines.push_back("将会保留：• 无受保护内容");
    }
    return lines;
}

namespace {

/// 把累计的数量写入清单（0 时不写入）。
void pushItem(std::vector<ImpactItem>& items, const std::string& code, const std::string& label,
              std::size_t count) {
    if (count == 0) {
        return;
    }
    ImpactItem item;
    item.code = code;
    item.label = label;
    item.count = count;
    items.push_back(std::move(item));
}

}  // namespace

RegenerationImpactReport analyzeRegenerationImpact(const Project& project, SequenceId sequenceId,
                                                   RegenerationScope scope) {
    RegenerationImpactReport report;
    report.scope = scope;
    const Sequence* seq = project.findSequence(sequenceId);
    if (seq == nullptr) {
        report.summary = "序列不存在";
        return report;
    }

    std::size_t transitions = 0;
    std::size_t effects = 0;
    std::size_t keyframes = 0;
    std::size_t masks = 0;
    std::size_t chromaKeys = 0;
    std::size_t audioAdjust = 0;
    std::size_t speedCurves = 0;
    std::size_t compounds = 0;
    std::size_t markers = 0;
    std::size_t subtitles = 0;

    for (const auto& track : seq->tracks) {
        if (track.locked) {
            ++report.protectedTracks;
            continue;
        }
        ++report.affectedTracks;
        transitions += track.transitions.size();
        for (const auto& clip : track.clips) {
            effects += clip.effects.effects().size();
            if (!clip.animation.empty()) {
                ++keyframes;
            }
            if (clip.hasMask) {
                ++masks;
            }
            if (clip.hasChromaKey) {
                ++chromaKeys;
            }
            if (clip.audioGainDb != 0.0 || clip.audioFadeIn.isPositive() ||
                clip.audioFadeOut.isPositive()) {
                ++audioAdjust;
            }
            if (!clip.speedCurve.empty()) {
                ++speedCurves;
            }
            if (clip.isCompound()) {
                ++compounds;
            }
        }
    }
    markers = seq->markers.size();
    if (scope == RegenerationScope::WholeProject) {
        subtitles = seq->subtitles.size();
    }

    pushItem(report.willLose, "transitions", "手动调整的转场", transitions);
    pushItem(report.willLose, "effects", "特效", effects);
    pushItem(report.willLose, "keyframes", "关键帧动画", keyframes);
    pushItem(report.willLose, "masks", "蒙版", masks);
    pushItem(report.willLose, "chroma-key", "智能抠像（色度/绿幕）", chromaKeys);
    pushItem(report.willLose, "audio-adjust", "音频调整（增益/淡入淡出）", audioAdjust);
    pushItem(report.willLose, "speed", "速度曲线", speedCurves);
    pushItem(report.willLose, "compound", "复合片段（将解除为独立片段）", compounds);
    pushItem(report.willLose, "markers", "时间轴标记/章节", markers);
    pushItem(report.willLose, "subtitles", "字幕", subtitles);

    if (report.protectedTracks > 0) {
        pushItem(report.willKeep, "locked-tracks", "已锁定的轨道", report.protectedTracks);
    }
    if (scope == RegenerationScope::Sequence && project.sequences.size() > 1) {
        pushItem(report.willKeep, "other-sequences", "其它序列",
                 project.sequences.size() - 1);
    }

    const std::size_t totalTracks = seq->tracks.size();
    report.summary = "范围：" +
                     std::string(scope == RegenerationScope::WholeProject ? "整个项目"
                                                                          : "当前序列") +
                     "；受影响轨道 " + std::to_string(report.affectedTracks) + "/" +
                     std::to_string(totalTracks) + "（锁定保护 " +
                     std::to_string(report.protectedTracks) + "）";
    return report;
}

// =============================================================================
// 重新生成清理
// =============================================================================

RegenerationResult clearForRegeneration(TimelineEditor& editor, Project& project,
                                        SequenceId sequenceId,
                                        const RegenerationImpactReport& report,
                                        const std::string& transactionName) {
    RegenerationResult result;
    Sequence* seq = project.findSequence(sequenceId);
    if (seq == nullptr) {
        result.result = Result::fail(ErrorCode::NotFound, "序列不存在");
        return result;
    }
    // 前置校验：锁定轨道不参与清理（失败不产生副作用）
    for (const auto& track : seq->tracks) {
        if (track.locked) {
            ++result.tracksProtected;
        }
    }

    editor.beginTransaction(transactionName);
    // 1) 清空片段（逐轨道；锁定轨道跳过）
    //
    // 注意：**不能**在循环条件里用 `seq->tracks.size()`。
    // `editor.removeClip(...)` 会走 commit() → UndoStack 保存/恢复
    // Project 快照，`seq`（以及它内部的 tracks vector）可能被重建，
    // 之后每次读 `seq->tracks` 都是悬垂内存。
    //
    // 症状：CI（x64 glibc）上表现为 `malloc(): invalid size (unsorted)`
    // 直接 Abort，位置在 Capi.RegenerationImpactAndClear；本地 aarch64
    // 恰好没崩，所以只在 CI 暴露。
    //
    // 处置：先把 TrackId 列表拷出来，之后每轮都用 findTrack 重新定位。
    std::vector<TrackId> trackIds;
    {
        const Sequence* current = project.findSequence(sequenceId);
        if (current == nullptr) {
            editor.endTransaction();
            result.result = Result::fail(ErrorCode::NotFound, "序列不存在");
            return result;
        }
        trackIds.reserve(current->tracks.size());
        for (const auto& track : current->tracks) {
            trackIds.push_back(track.id);
        }
    }
    for (const TrackId trackId : trackIds) {
        seq = project.findSequence(sequenceId);  // 每轮重新取，避免悬垂
        if (seq == nullptr) {
            break;
        }
        const Track* track = seq->findTrack(trackId);
        if (track == nullptr || track->locked) {
            continue;
        }
        // 收集片段 ID（避免边遍历边删）
        std::vector<ClipId> clipIds;
        clipIds.reserve(track->clips.size());
        for (const auto& clip : track->clips) {
            clipIds.push_back(clip.id);
        }
        bool cleared = false;
        for (const auto& clipId : clipIds) {
            if (editor.removeClip(sequenceId, trackId, clipId).isOk()) {
                cleared = true;
            }
        }
        // 2) 清空转场（removeClip 不自动删转场；转场引用片段已消失）
        {
            // 同样必须先重新取 seq：上面 removeClip 已可能重建 project。
            const Sequence* seqNow = project.findSequence(sequenceId);
            const Track* current = (seqNow != nullptr) ? seqNow->findTrack(trackId) : nullptr;
            if (current != nullptr) {
                std::vector<TransitionId> transitionIds;
                transitionIds.reserve(current->transitions.size());
                for (const auto& tr : current->transitions) {
                    transitionIds.push_back(tr.id);
                }
                for (const auto& trId : transitionIds) {
                    editor.removeTransition(sequenceId, trackId, trId);
                }
            }
        }
        if (cleared || !clipIds.empty()) {
            ++result.tracksCleared;
        }
    }
    // 3) 清空标记（含章节标记）
    {
        std::vector<MarkerId> markerIds;
        const Sequence* current = project.findSequence(sequenceId);
        if (current != nullptr) {
            markerIds.reserve(current->markers.size());
            for (const auto& marker : current->markers) {
                markerIds.push_back(marker.id);
            }
        }
        for (const auto& markerId : markerIds) {
            if (editor.removeMarker(sequenceId, markerId).isOk()) {
                ++result.markersCleared;
            }
        }
    }
    // 4) 清空字幕（scope=WholeProject）
    if (report.scope == RegenerationScope::WholeProject) {
        std::vector<SubtitleId> subtitleIds;
        const Sequence* current = project.findSequence(sequenceId);
        if (current != nullptr) {
            for (const auto& cue : current->subtitles.cues()) {
                subtitleIds.push_back(cue.id);
            }
        }
        for (const auto& subtitleId : subtitleIds) {
            if (editor.removeSubtitle(sequenceId, subtitleId).isOk()) {
                ++result.subtitlesCleared;
            }
        }
    }
    editor.endTransaction();

    result.summary = "已清空 " + std::to_string(result.tracksCleared) + " 条轨道（保留 " +
                     std::to_string(result.tracksProtected) + " 条锁定轨道），清理标记 " +
                     std::to_string(result.markersCleared) + " 个";
    if (report.scope == RegenerationScope::WholeProject) {
        result.summary += "、字幕 " + std::to_string(result.subtitlesCleared) + " 条";
    }
    result.result = Result::ok();
    return result;
}

}  // namespace core
}  // namespace nairoclip
