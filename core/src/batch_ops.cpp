#include "nairoclip/core/batch_ops.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace nairoclip {
namespace core {

namespace {

/// 逐项说明辅助。
void addMessage(BatchResult& result, const std::string& message) {
    result.messages.push_back(message);
}

/// 校验批量项（轨道/片段存在性）并分组。
/// - out：按轨道分组（保持输入顺序）；
/// - 缺失项直接计入 failed 并说明；返回是否至少有一项通过。
bool groupValidItems(const Project& project, SequenceId sequenceId,
                     const std::vector<BatchItem>& items,
                     std::map<std::uint64_t, std::vector<ClipId>>& out, BatchResult& result) {
    for (const auto& item : items) {
        const Track* track = findTrack(project, sequenceId, item.track);
        if (track == nullptr) {
            ++result.failed;
            addMessage(result, "轨道不存在（片段 " + std::to_string(item.clip.value) + "）");
            continue;
        }
        if (!item.clip.isValid()) {
            ++result.failed;
            addMessage(result, "片段 ID 非法");
            continue;
        }
        if (track->findClip(item.clip) == nullptr) {
            ++result.failed;
            addMessage(result, "片段不存在（ID " + std::to_string(item.clip.value) + "）");
            continue;
        }
        out[item.track.value].push_back(item.clip);
    }
    return !out.empty();
}

}  // namespace

// =============================================================================
// 批量删除
// =============================================================================

BatchResult batchDelete(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                        const std::vector<BatchItem>& items, bool ripple,
                        const std::string& transactionName) {
    BatchResult result;
    if (items.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "未选择任何片段");
        return result;
    }
    std::map<std::uint64_t, std::vector<ClipId>> byTrack;
    if (!groupValidItems(project, sequenceId, items, byTrack, result)) {
        result.result = Result::fail(ErrorCode::NotFound, "没有可处理的片段");
        return result;
    }
    // 事务执行（逐项独立；单项失败不阻断）
    editor.beginTransaction(transactionName);
    for (const auto& entry : byTrack) {
        const TrackId trackId{entry.first};
        if (ripple) {
            // 波纹删除：同轨道批量一次（内部按时间降序处理）
            const Result r = editor.rippleDelete(sequenceId, trackId, entry.second);
            if (r.isOk()) {
                result.succeeded += entry.second.size();
            } else {
                result.failed += entry.second.size();
                addMessage(result, "波纹删除失败：" + r.message);
            }
            continue;
        }
        for (const auto& clipId : entry.second) {
            const Result r = editor.removeClip(sequenceId, trackId, clipId);
            if (r.isOk()) {
                ++result.succeeded;
            } else {
                ++result.failed;
                addMessage(result, "删除失败（片段 " + std::to_string(clipId.value) +
                                       "）：" + r.message);
            }
        }
    }
    editor.endTransaction();
    if (result.succeeded == 0) {
        result.result = Result::fail(ErrorCode::Internal, "批量删除未成功任何一项");
        return result;
    }
    result.result = Result::ok();
    return result;
}

// =============================================================================
// 批量应用转场
// =============================================================================

BatchResult batchApplyTransitions(TimelineEditor& editor, Project& project,
                                  SequenceId sequenceId, TrackId trackId,
                                  const std::vector<ClipId>& clipIds,
                                  const BatchTransitionOptions& options,
                                  const std::string& transactionName) {
    BatchResult result;
    if (clipIds.size() < 2) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "至少需要两个片段才能应用转场");
        return result;
    }
    if (!options.duration.isPositive()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "转场时长必须大于 0");
        return result;
    }
    const Track* track = findTrack(project, sequenceId, trackId);
    if (track == nullptr) {
        result.result = Result::fail(ErrorCode::NotFound, "轨道不存在");
        return result;
    }
    if (track->locked) {
        result.result = Result::fail(ErrorCode::Locked, "轨道已锁定");
        return result;
    }
    // 按时间顺序取选中片段 ID（过滤掉轨道上不存在的；只保存 ID，
    // 避免多次移动后指针失效）
    std::vector<ClipId> selected;
    for (const auto& clipId : clipIds) {
        if (track->findClip(clipId) != nullptr) {
            selected.push_back(clipId);
        } else {
            ++result.failed;
            addMessage(result, "片段不存在（ID " + std::to_string(clipId.value) + "）");
        }
    }
    std::sort(selected.begin(), selected.end(), [&](const ClipId& a, const ClipId& b) {
        const Clip* ca = track->findClip(a);
        const Clip* cb = track->findClip(b);
        if (ca == nullptr || cb == nullptr) {
            return a.value < b.value;
        }
        return ca->start < cb->start;
    });
    if (selected.size() < 2) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "轨道内可用片段不足两个");
        return result;
    }

    editor.beginTransaction(transactionName);
    for (std::size_t i = 0; i + 1 < selected.size(); ++i) {
        if (options.maxCount > 0 && result.succeeded >= options.maxCount) {
            addMessage(result, "已达数量上限，剩余片段对跳过");
            break;
        }
        // 每轮重新读取（前一轮的移动会影响起点）
        const Track* current = findTrack(project, sequenceId, trackId);
        if (current == nullptr) {
            break;
        }
        const Clip* from = current->findClip(selected[i]);
        const Clip* to = current->findClip(selected[i + 1]);
        if (from == nullptr || to == nullptr) {
            ++result.failed;
            addMessage(result, "片段已不存在，跳过");
            continue;
        }
        // 已存在转场判断：仅当「同一对片段」已有转场，或该片段已在
        // **同一侧**被占用时才跳过。
        // 说明：专业剪辑允许同一片段同时作为「前一个转场的终点」与
        // 「后一个转场的起点」（如 A→B→C 链），因此不能简单按片段判占用。
        bool occupied = false;
        {
            const Track* currentTrack = findTrack(project, sequenceId, trackId);
            if (currentTrack != nullptr) {
                for (const auto& existing : currentTrack->transitions) {
                    // 同一对（顺序或逆序）已存在 → 跳过
                    if ((existing.fromClip == from->id && existing.toClip == to->id) ||
                        (existing.fromClip == to->id && existing.toClip == from->id)) {
                        occupied = true;
                        break;
                    }
                    // from 已作为某转场的 from（同一侧重复占用）→ 跳过
                    if (existing.fromClip == from->id) {
                        occupied = true;
                        break;
                    }
                }
            }
        }
        if (occupied) {
            ++result.failed;
            addMessage(result, "片段已被其他转场占用，跳过（片段 " +
                                   std::to_string(from->id.value) + " → " +
                                   std::to_string(to->id.value) + "）");
            continue;
        }
        Time gap = to->start - from->end();
        if (gap.isNegative()) {
            ++result.failed;
            addMessage(result, "两端片段重叠，跳过（片段 " + std::to_string(from->id.value) +
                                   " → " + std::to_string(to->id.value) + "）");
            continue;
        }
        Time duration = options.duration;
        if (duration > gap) {
            if (options.shiftToMakeRoom) {
                // 后移 to 片段创造足够间隙（时间轴总时长随之变化，由调用方按需提示）
                const Time needed = duration - gap;
                const Time targetStart = to->start + needed;
                const Result moved = editor.moveClip(sequenceId, trackId, to->id, targetStart);
                if (!moved.isOk()) {
                    ++result.failed;
                    addMessage(result, "腾出间隙失败（片段 " + std::to_string(to->id.value) +
                                           "）：" + moved.message);
                    continue;
                }
                addMessage(result, "已后移片段 " + std::to_string(to->id.value) + " 腾出 " +
                                       std::to_string(static_cast<int>(needed.toDouble() * 1000)) +
                                       "ms 间隙（时间轴总时长增加）");
                gap = gap + needed;
            } else if (options.allowShrink) {
                duration = gap;
                if (!duration.isPositive()) {
                    ++result.failed;
                    addMessage(result, "间隙为零，无法插入转场（可开启「自动腾出间隙」）");
                    continue;
                }
                addMessage(result, "转场时长已缩短至 " +
                                       std::to_string(static_cast<int>(duration.toDouble() * 1000)) +
                                       "ms 以适配间隙");
            } else {
                ++result.failed;
                addMessage(result, "转场时长超出间隙，跳过（片段 " +
                                       std::to_string(from->id.value) + " → " +
                                       std::to_string(to->id.value) + "）");
                continue;
            }
        }
        Transition tr;
        tr.id = TransitionId{project.ids.next()};
        tr.type = options.type;
        tr.duration = duration;
        tr.fromClip = from->id;
        tr.toClip = to->id;
        const Result added = editor.addTransition(sequenceId, trackId, tr);
        if (added.isOk()) {
            ++result.succeeded;
        } else {
            ++result.failed;
            addMessage(result, "添加转场失败：" + added.message);
        }
    }
    editor.endTransaction();
    if (result.succeeded == 0) {
        result.result = Result::fail(ErrorCode::Internal, "批量应用转场未成功任何一项");
        return result;
    }
    result.result = Result::ok();
    return result;
}

// =============================================================================
// 批量调整音量
// =============================================================================

BatchResult batchSetAudio(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                          const std::vector<BatchItem>& items, const BatchAudioOptions& options,
                          const std::string& transactionName) {
    BatchResult result;
    if (items.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "未选择任何片段");
        return result;
    }
    if (!options.setGain && !options.setFades) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "未指定要修改的音频属性");
        return result;
    }
    if (options.setGain && (options.gainDb < -60.0 || options.gainDb > 12.0) &&
        !options.clampToRange) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "增益需在 -60dB 到 +12dB 之间");
        return result;
    }
    std::map<std::uint64_t, std::vector<ClipId>> byTrack;
    if (!groupValidItems(project, sequenceId, items, byTrack, result)) {
        result.result = Result::fail(ErrorCode::NotFound, "没有可处理的片段");
        return result;
    }

    editor.beginTransaction(transactionName);
    for (const auto& entry : byTrack) {
        const TrackId trackId{entry.first};
        for (const auto& clipId : entry.second) {
            const Track* track = findTrack(project, sequenceId, trackId);
            const Clip* clip = track != nullptr ? track->findClip(clipId) : nullptr;
            if (clip == nullptr) {
                ++result.failed;
                addMessage(result, "片段已不存在：" + std::to_string(clipId.value));
                continue;
            }
            double gainDb = clip->audioGainDb;
            Time fadeIn = clip->audioFadeIn;
            Time fadeOut = clip->audioFadeOut;
            if (options.setGain) {
                gainDb = options.gainDb;
                if (gainDb < -60.0 || gainDb > 12.0) {
                    gainDb = std::max(-60.0, std::min(12.0, gainDb));
                }
            }
            if (options.setFades) {
                fadeIn = options.fadeIn;
                fadeOut = options.fadeOut;
                // 淡入淡出超出片段时长 → 按比例缩放（避免整体失败）
                const Time total = fadeIn + fadeOut;
                if (total > clip->duration) {
                    const double scale = clip->duration.toDouble() /
                                         std::max(1e-9, total.toDouble());
                    fadeIn = fadeIn * Rational::make(
                                          static_cast<std::int64_t>(scale * 1000000.0), 1000000);
                    fadeOut = fadeOut * Rational::make(
                                            static_cast<std::int64_t>(scale * 1000000.0),
                                            1000000);
                    addMessage(result, "淡入淡出已按比例缩放以适配片段时长（片段 " +
                                           std::to_string(clipId.value) + "）");
                }
            }
            const Result r =
                editor.setClipAudio(sequenceId, trackId, clipId, gainDb, fadeIn, fadeOut);
            if (r.isOk()) {
                ++result.succeeded;
            } else {
                ++result.failed;
                addMessage(result, "调整失败（片段 " + std::to_string(clipId.value) +
                                       "）：" + r.message);
            }
        }
    }
    editor.endTransaction();
    if (result.succeeded == 0) {
        result.result = Result::fail(ErrorCode::Internal, "批量调整音量未成功任何一项");
        return result;
    }
    result.result = Result::ok();
    return result;
}

// =============================================================================
// 批量变速
// =============================================================================

BatchResult batchSetSpeed(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                          const std::vector<BatchItem>& items, const Rational& speed,
                          const std::string& transactionName) {
    BatchResult result;
    if (items.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "未选择任何片段");
        return result;
    }
    const Rational minSpeed = Rational::make(1, 10);
    const Rational maxSpeed = Rational::make(10);
    if (!(speed >= minSpeed) || !(speed <= maxSpeed)) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "速度需在 0.1x 到 10x 之间");
        return result;
    }
    std::map<std::uint64_t, std::vector<ClipId>> byTrack;
    if (!groupValidItems(project, sequenceId, items, byTrack, result)) {
        result.result = Result::fail(ErrorCode::NotFound, "没有可处理的片段");
        return result;
    }
    // 同轨道内按时间降序处理（缩短时长时避免影响后续片段位置判断）
    editor.beginTransaction(transactionName);
    for (const auto& entry : byTrack) {
        const TrackId trackId{entry.first};
        std::vector<ClipId> ordered = entry.second;
        // 按时间降序（先处理靠后的，减少重叠冲突）
        std::sort(ordered.begin(), ordered.end(), [&](const ClipId& a, const ClipId& b) {
            const Track* track = findTrack(project, sequenceId, trackId);
            const Clip* ca = track != nullptr ? track->findClip(a) : nullptr;
            const Clip* cb = track != nullptr ? track->findClip(b) : nullptr;
            if (ca == nullptr || cb == nullptr) {
                return a.value > b.value;
            }
            return ca->start > cb->start;
        });
        for (const auto& clipId : ordered) {
            const Result r = editor.setClipSpeed(sequenceId, trackId, clipId, speed);
            if (r.isOk()) {
                ++result.succeeded;
            } else {
                ++result.failed;
                addMessage(result, "变速失败（片段 " + std::to_string(clipId.value) +
                                       "）：" + r.message);
            }
        }
    }
    editor.endTransaction();
    if (result.succeeded == 0) {
        result.result = Result::fail(ErrorCode::Internal, "批量变速未成功任何一项");
        return result;
    }
    result.result = Result::ok();
    return result;
}

// =============================================================================
// 对齐到同一起点
// =============================================================================

BatchResult batchAlignStart(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                            const std::vector<BatchItem>& items,
                            const BatchAlignOptions& options,
                            const std::string& transactionName) {
    BatchResult result;
    if (items.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "未选择任何片段");
        return result;
    }
    // 目标起点：显式值或选中项最早起点
    Time target = options.targetStart;
    if (!options.useExplicitTarget) {
        bool found = false;
        for (const auto& item : items) {
            const Track* track = findTrack(project, sequenceId, item.track);
            const Clip* clip = track != nullptr ? track->findClip(item.clip) : nullptr;
            if (clip == nullptr) {
                continue;
            }
            if (!found || clip->start < target) {
                target = clip->start;
                found = true;
            }
        }
        if (!found) {
            result.result = Result::fail(ErrorCode::NotFound, "没有可对齐的片段");
            return result;
        }
    }
    if (target.isNegative()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "目标起点不能为负");
        return result;
    }

    // 逐项移动：先移目标点之后的项？简化——按轨道逐个移动，重叠自动失败并说明
    std::map<std::uint64_t, std::vector<ClipId>> byTrack;
    if (!groupValidItems(project, sequenceId, items, byTrack, result)) {
        result.result = Result::fail(ErrorCode::NotFound, "没有可处理的片段");
        return result;
    }
    editor.beginTransaction(transactionName);
    for (const auto& entry : byTrack) {
        const TrackId trackId{entry.first};
        for (const auto& clipId : entry.second) {
            // 已对齐的跳过（幂等）
            const Track* track = findTrack(project, sequenceId, trackId);
            const Clip* clip = track != nullptr ? track->findClip(clipId) : nullptr;
            if (clip == nullptr) {
                ++result.failed;
                addMessage(result, "片段已不存在：" + std::to_string(clipId.value));
                continue;
            }
            if (clip->start == target) {
                ++result.succeeded;  // 已对齐，视为成功（无副作用）
                continue;
            }
            // 为避免「先移动的片段挡住后移动的片段」，先把所有选中项临时移出？
            // 简化策略：单轨道内若存在多个选中片段，先移动最靠近目标的（可能互相重叠 → 失败并说明）
            const Result r = editor.moveClip(sequenceId, trackId, clipId, target);
            if (r.isOk()) {
                ++result.succeeded;
            } else {
                ++result.failed;
                addMessage(result, "对齐失败（片段 " + std::to_string(clipId.value) +
                                       "）：" + r.message +
                                       "（同轨道多片段对齐需先保证目标位置空闲）");
            }
        }
    }
    editor.endTransaction();
    if (result.succeeded == 0) {
        result.result = Result::fail(ErrorCode::Internal, "对齐未成功任何一项");
        return result;
    }
    result.result = Result::ok();
    return result;
}

}  // namespace core
}  // namespace nairoclip
