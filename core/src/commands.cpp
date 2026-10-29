#include "nairoclip/core/commands.h"
#include "nairoclip/core/transition.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace nairoclip {
namespace core {

namespace {

/// 按 start 升序的插入位置。
std::vector<Clip>::iterator insertionPoint(std::vector<Clip>& clips, const Time& start) {
    return std::lower_bound(clips.begin(), clips.end(), start,
                            [](const Clip& c, const Time& s) { return c.start < s; });
}

}  // namespace

/// 轨道类型的中文名（面向用户的错误消息与 UI）。声明见 commands.h。
const char* trackTypeLabel(TrackType t) {
    switch (t) {
        case TrackType::Video: return "视频轨";
        case TrackType::Audio: return "音频轨";
        case TrackType::Subtitle: return "字幕轨";
        case TrackType::Adjustment: return "调整图层";
    }
    return "轨道";
}

/// 素材类型的中文名（面向用户的错误消息与 UI）。声明见 commands.h。
const char* mediaTypeLabel(MediaType t) {
    switch (t) {
        case MediaType::Video: return "视频";
        case MediaType::Audio: return "音频";
        case MediaType::Image: return "图片";
        case MediaType::Unknown: return "未知类型";
    }
    return "未知类型";
}


// 声明见 commands.h（B2-8：UI 需要在不构造 Clip 的前提下查询相容性）。
//
// 为什么视频**可以**进音频轨：带画面的 mp4 通常含音轨，「把视频拖到音频轨
// 取声音」是剪辑软件的标准操作（音画分离本质就是这个）。混音器只读它的音频
// 流，不读画面。因此音频轨接受 Audio 与 Video，拒绝 Image。
//
// 宽容原则：MediaType::Unknown（导入后尚未 probe）→ 放行。真实流程里大量
// 存在，硬拒会让用户「刚导入就拖不进时间轴」。
PlacementCompat trackAcceptsClipType(TrackType trackType, MediaType mediaType) {
    if (mediaType == MediaType::Unknown) {
        return PlacementCompat::AllowedBecauseUnknown;
    }
    switch (trackType) {
        case TrackType::Video:
            // 视频轨承载画面：视频 / 图片
            return (mediaType == MediaType::Video || mediaType == MediaType::Image)
                       ? PlacementCompat::Allowed
                       : PlacementCompat::Rejected;
        case TrackType::Audio:
            // 音频轨承载声音：音频素材 + 含音轨的视频（取声音）。图片无声可读。
            return (mediaType == MediaType::Audio || mediaType == MediaType::Video)
                       ? PlacementCompat::Allowed
                       : PlacementCompat::Rejected;
        case TrackType::Subtitle:
            // 字幕走 SubtitleEvent，不接受任何 Clip
            return PlacementCompat::Rejected;
        case TrackType::Adjustment:
            // 调整图层尚未实现渲染（B2-6），创建后放入的内容不会生效。
            // 故此处拒绝任何 Clip，避免用户以为「放了就会有效果」。
            return PlacementCompat::Rejected;
    }
    return PlacementCompat::Rejected;
}

Result validateClipPlacement(const Project& project, const Track& track, const Clip& clip,
                             ClipId ignoreClipId) {
    if (!clip.valid()) {
        return Result::fail(ErrorCode::InvalidArgument, "片段参数不合法");
    }
    if (track.locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    if (track.hasOverlap(clip.start, clip.end(), ignoreClipId)) {
        return Result::fail(ErrorCode::Overlap, "与轨道上的已有片段重叠");
    }
    // B0-7：轨道类型 ↔ 片段内容类型相容校验。
    // 放在重叠检查之后：位置冲突对用户更直观，先报「重叠」再报「放错轨」。
    if (clip.isCompound()) {
        // 复合片段是渲染结果（含画面），只能进视频轨
        if (track.type != TrackType::Video) {
            return Result::fail(ErrorCode::InvalidArgument,
                                std::string("复合片段只能放入视频轨（当前为") +
                                    trackTypeLabel(track.type) + "）");
        }
    } else if (clip.asset.isValid()) {
        const MediaAsset* asset = project.findAsset(clip.asset);
        // 素材不存在由下方既有分支报 NotFound，这里不重复判定
        if (asset != nullptr &&
            trackAcceptsClipType(track.type, asset->type) == PlacementCompat::Rejected) {
            return Result::fail(ErrorCode::InvalidArgument,
                                std::string("片段类型（") + mediaTypeLabel(asset->type) +
                                    "）不能放入" + trackTypeLabel(track.type));
        }
    } else if (track.type == TrackType::Subtitle || track.type == TrackType::Adjustment) {
        // 无素材引用的片段（文字/占位）：字幕轨与特效轨仍不接受
        return Result::fail(ErrorCode::InvalidArgument,
                            std::string(trackTypeLabel(track.type)) + "不接受片段");
    }
    // 复合片段（嵌套序列）：校验收件序列存在
    if (clip.isCompound()) {
        const Sequence* nested = project.findSequence(clip.nestedSequence);
        if (nested == nullptr) {
            return Result::fail(ErrorCode::NotFound, "复合片段引用的嵌套序列不存在");
        }
        return Result::ok();
    }
    if (clip.asset.isValid()) {
        const MediaAsset* asset = project.findAsset(clip.asset);
        if (asset == nullptr) {
            return Result::fail(ErrorCode::NotFound, "引用的素材不存在");
        }
        if (!asset->isImage() && asset->duration.isPositive()) {
            if (clip.sourceOut() > asset->duration) {
                return Result::fail(ErrorCode::OutOfRange, "超出素材可用长度");
            }
        }
    }
    return Result::ok();
}

// ---------------- AddTrackCommand ----------------

AddTrackCommand::AddTrackCommand(SequenceId sequenceId, TrackId trackId, TrackType type,
                                 std::string trackName)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      type_(type),
      trackName_(std::move(trackName)) {}

Result AddTrackCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    if (seq->findTrack(trackId_) != nullptr) {
        return Result::fail(ErrorCode::InvalidArgument, "轨道 ID 已存在");
    }
    Track track;
    track.id = trackId_;
    track.type = type_;
    track.name = trackName_;
    seq->tracks.push_back(std::move(track));
    return Result::ok();
}

Result AddTrackCommand::revert(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const auto it = std::find_if(seq->tracks.begin(), seq->tracks.end(),
                                 [this](const Track& t) { return t.id == trackId_; });
    if (it == seq->tracks.end()) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    seq->tracks.erase(it);
    return Result::ok();
}

// ---------------- RemoveTrackCommand ----------------

RemoveTrackCommand::RemoveTrackCommand(SequenceId sequenceId, TrackId trackId)
    : sequenceId_(sequenceId), trackId_(trackId) {}

Result RemoveTrackCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const auto it = std::find_if(seq->tracks.begin(), seq->tracks.end(),
                                 [this](const Track& t) { return t.id == trackId_; });
    if (it == seq->tracks.end()) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (it->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定，无法删除");
    }
    removedTrack_ = *it;
    removedIndex_ = static_cast<std::size_t>(std::distance(seq->tracks.begin(), it));
    seq->tracks.erase(it);
    return Result::ok();
}

Result RemoveTrackCommand::revert(Project& project) {
    if (!removedTrack_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少轨道快照");
    }
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const std::size_t index =
        std::min(removedIndex_.value_or(seq->tracks.size()), seq->tracks.size());
    seq->tracks.insert(seq->tracks.begin() + static_cast<std::ptrdiff_t>(index),
                       *removedTrack_);
    return Result::ok();
}

// ---------------- MoveTrackCommand（B2-5） ----------------

MoveTrackCommand::MoveTrackCommand(SequenceId sequenceId, TrackId trackId, int newIndex)
    : sequenceId_(sequenceId), trackId_(trackId), newIndex_(newIndex) {}

bool MoveTrackCommand::isNoOp(const Project& project) const {
    const Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) return false;  // 交给 apply 报「序列不存在」
    const int count = static_cast<int>(seq->tracks.size());
    if (newIndex_ < 0 || newIndex_ >= count) return false;  // 交给 apply 报越界

    int from = -1;
    for (int i = 0; i < count; ++i) {
        if (seq->tracks[static_cast<std::size_t>(i)].id == trackId_) {
            from = i;
            break;
        }
    }
    if (from < 0) return false;  // 轨道不存在：交给 apply 报错

    // 组的判定要在这里做：no-op 必须在 apply 之前判断，
    // 而「整组搬」是否真的改变顺序取决于组在序列中的位置。
    std::vector<int> indexes{from};
    const TrackGroup* group = seq->groupOf(trackId_);
    if (group != nullptr) {
        indexes.clear();
        for (int i = 0; i < count; ++i) {
            if (group->contains(seq->tracks[static_cast<std::size_t>(i)].id)) {
                indexes.push_back(i);
            }
        }
    }
    // 单轨：原地即 no-op
    if (indexes.size() == 1) return indexes[0] == newIndex_;

    // 组：只有「被拖动那条落到 newIndex_」不构成 no-op —— 因为整组会重排，
    // 组内其他成员的位置会变。真正的 no-op 是「整组搬完后顺序与原来一致」，
    // 这需要模拟一次，成本高且 rare；这里保守返回 false（宁可多一条历史，
    // 也不误吞用户的真实操作）。
    return false;
}

namespace {

/// 把 indexes 中的轨道按它们当前顺序收集出来。
std::vector<Track> collectTracks(const Sequence& seq, const std::vector<int>& indexes) {
    std::vector<Track> out;
    out.reserve(indexes.size());
    for (const int i : indexes) {
        out.push_back(seq.tracks[static_cast<std::size_t>(i)]);
    }
    return out;
}

}  // namespace

Result MoveTrackCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const int count = static_cast<int>(seq->tracks.size());
    if (newIndex_ < 0 || newIndex_ >= count) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "目标位置超出范围（有效范围 0~" + std::to_string(count - 1) + "）");
    }
    int from = -1;
    for (int i = 0; i < count; ++i) {
        if (seq->tracks[static_cast<std::size_t>(i)].id == trackId_) {
            from = i;
            break;
        }
    }
    if (from < 0) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (seq->tracks[static_cast<std::size_t>(from)].locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定，无法移动");
    }

    // 首次 apply：记录整份顺序快照（revert 直接还原它）
    if (!beforeOrder_.has_value()) {
        beforeOrder_.emplace();
        beforeOrder_->reserve(seq->tracks.size());
        for (const auto& t : seq->tracks) beforeOrder_->push_back(t.id);
    }

    // 找出要一起搬的轨道：无组 = 只有自己；有组 = 组内全部成员
    std::vector<int> indexes;
    const TrackGroup* group = seq->groupOf(trackId_);
    if (group == nullptr) {
        indexes.push_back(from);
    } else {
        for (int i = 0; i < count; ++i) {
            if (group->contains(seq->tracks[static_cast<std::size_t>(i)].id)) {
                indexes.push_back(i);
            }
        }
    }
    if (indexes.empty()) {
        return Result::fail(ErrorCode::NotFound, "待移动的轨道不存在");
    }
    if (!movingIds_.empty()) {
        // 二次 apply（redo）：沿用首次记录的成员集合，避免组在撤销期间被改过
        indexes.clear();
        for (std::size_t i = 0; i < seq->tracks.size(); ++i) {
            const auto& t = seq->tracks[i];
            if (std::find(movingIds_.begin(), movingIds_.end(), t.id) != movingIds_.end()) {
                indexes.push_back(static_cast<int>(i));
            }
        }
    } else {
        for (const int i : indexes) movingIds_.push_back(seq->tracks[static_cast<std::size_t>(i)].id);
    }

    // 单轨且位置没变：不改动（no-op 已在 push 层拦截，这里兜底）
    if (indexes.size() == 1 && indexes[0] == newIndex_) {
        return Result::ok();
    }

    // 取出整组，再整体插到 newIndex_ 处。
    // 组内相对顺序由 collectTracks 保序（按它们在序列中的当前顺序）。
    std::vector<Track> moving = collectTracks(*seq, indexes);
    // 从后往前删，避免下标失效
    for (auto it = indexes.rbegin(); it != indexes.rend(); ++it) {
        seq->tracks.erase(seq->tracks.begin() + *it);
    }

    // 插入位置：newIndex_ 是「移动后」的最终下标。
    // 但整组被移除后，原 newIndex_ 可能已越界，需要按剩余长度收敛。
    //
    // 锚点语义：被拖动的轨道（trackId_）最终应该落在 newIndex_。
    // 组内该轨道之前有 k 个成员会排在它前面，所以整组的插入起点是
    // newIndex_ - k（再按移除后的数组长度收敛到合法区间）。
    int anchorOffset = 0;
    for (const auto& t : moving) {
        if (t.id == trackId_) break;
        ++anchorOffset;
    }
    int insertAt = newIndex_ - anchorOffset;
    const int afterSize = static_cast<int>(seq->tracks.size());
    if (insertAt < 0) insertAt = 0;
    if (insertAt > afterSize) insertAt = afterSize;

    seq->tracks.insert(seq->tracks.begin() + insertAt,
                       std::make_move_iterator(moving.begin()),
                       std::make_move_iterator(moving.end()));
    return Result::ok();
}

Result MoveTrackCommand::revert(Project& project) {
    if (!beforeOrder_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少轨道顺序快照");
    }
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    // 按 id 还原顺序：先把现有轨道做成 id → Track 的索引，
    // 再按快照顺序重排。快照里已不存在的轨道跳过（期间被删过）。
    std::vector<Track> restored;
    restored.reserve(seq->tracks.size());
    for (const TrackId id : *beforeOrder_) {
        const auto it = std::find_if(seq->tracks.begin(), seq->tracks.end(),
                                     [id](const Track& t) { return t.id == id; });
        if (it != seq->tracks.end()) {
            restored.push_back(std::move(*it));
        }
    }
    // 快照之后新增的轨道：附在末尾，绝不丢失用户数据
    for (auto& t : seq->tracks) {
        const bool known = std::any_of(beforeOrder_->begin(), beforeOrder_->end(),
                                       [&t](TrackId id) { return id == t.id; });
        if (!known) restored.push_back(std::move(t));
    }
    seq->tracks = std::move(restored);
    return Result::ok();
}

bool MoveTrackCommand::canMergeWith(const Command& other) const {
    // 只合并「同一序列、同一条轨道」的连续移动。
    // 用 dynamic_cast 而非改基类接口：合并是可选能力，不是所有命令都需要。
    const auto* o = dynamic_cast<const MoveTrackCommand*>(&other);
    return o != nullptr && o->sequenceId_.value == sequenceId_.value &&
           o->trackId_.value == trackId_.value;
}

void MoveTrackCommand::mergeFrom(const Command& other) {
    const auto* o = dynamic_cast<const MoveTrackCommand*>(&other);
    if (o != nullptr) {
        // 保留最初的 beforeOrder_（撤销要回到起点），只更新目标位置
        newIndex_ = o->newIndex_;
    }
}

SetTrackHeightCommand::SetTrackHeightCommand(SequenceId sequenceId, TrackId trackId,
                                             int heightPx, bool collapsed)
    : sequenceId_(sequenceId), trackId_(trackId), heightPx_(heightPx), collapsed_(collapsed) {}

Result SetTrackHeightCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    Track* track = seq->findTrack(trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (heightPx_ < Track::kMinTrackHeight || heightPx_ > Track::kMaxTrackHeight) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "轨道高度超出范围（" + std::to_string(Track::kMinTrackHeight) +
                                "~" + std::to_string(Track::kMaxTrackHeight) + " 像素）");
    }
    if (!oldHeight_.has_value()) {
        oldHeight_ = track->heightPx;
        oldCollapsed_ = track->collapsed;
    }
    track->heightPx = heightPx_;
    track->collapsed = collapsed_;
    return Result::ok();
}

Result SetTrackHeightCommand::revert(Project& project) {
    if (!oldHeight_.has_value() || !oldCollapsed_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少轨道快照");
    }
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    Track* track = seq->findTrack(trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    track->heightPx = *oldHeight_;
    track->collapsed = *oldCollapsed_;
    return Result::ok();
}

// ---------------- 轨道组命令（B2-7） ----------------

CreateTrackGroupCommand::CreateTrackGroupCommand(SequenceId sequenceId, TrackGroup group)
    : sequenceId_(sequenceId), group_(std::move(group)) {}

Result CreateTrackGroupCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    if (group_.tracks.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "轨道组成员不能为空");
    }
    for (TrackId member : group_.tracks) {
        if (seq->findTrack(member) == nullptr) {
            return Result::fail(ErrorCode::NotFound, "轨道组成员不存在");
        }
        if (seq->groupOf(member) != nullptr) {
            // 一条轨道只能属于一个组：两个组同时管一条轨会让联动语义歧义
            return Result::fail(ErrorCode::InvalidArgument,
                                "轨道 " + std::to_string(member.value) +
                                    " 已属于其它轨道组，请先解组");
        }
    }
    // 组 ID 已存在（重放场景）时直接拒绝，避免出现两个同 ID 的组
    for (const auto& g : seq->trackGroups) {
        if (g.id == group_.id) {
            return Result::fail(ErrorCode::InvalidArgument, "轨道组 ID 已存在");
        }
    }
    seq->trackGroups.push_back(group_);
    return Result::ok();
}

Result CreateTrackGroupCommand::revert(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const auto it = std::find_if(seq->trackGroups.begin(), seq->trackGroups.end(),
                                 [this](const TrackGroup& g) { return g.id == group_.id; });
    if (it == seq->trackGroups.end()) {
        return Result::fail(ErrorCode::NotFound, "轨道组不存在");
    }
    seq->trackGroups.erase(it);
    return Result::ok();
}

RemoveTrackGroupCommand::RemoveTrackGroupCommand(SequenceId sequenceId, TrackGroupId groupId)
    : sequenceId_(sequenceId), groupId_(groupId) {}

Result RemoveTrackGroupCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const auto it = std::find_if(seq->trackGroups.begin(), seq->trackGroups.end(),
                                 [this](const TrackGroup& g) { return g.id == groupId_; });
    if (it == seq->trackGroups.end()) {
        return Result::fail(ErrorCode::NotFound, "轨道组不存在");
    }
    removed_ = *it;
    removedIndex_ = static_cast<std::size_t>(std::distance(seq->trackGroups.begin(), it));
    seq->trackGroups.erase(it);
    return Result::ok();
}

Result RemoveTrackGroupCommand::revert(Project& project) {
    if (!removed_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少轨道组快照");
    }
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const std::size_t index =
        std::min(removedIndex_.value_or(seq->trackGroups.size()), seq->trackGroups.size());
    seq->trackGroups.insert(seq->trackGroups.begin() + static_cast<std::ptrdiff_t>(index),
                            *removed_);
    return Result::ok();
}

SetTrackGroupFlagCommand::SetTrackGroupFlagCommand(SequenceId sequenceId, TrackGroupId groupId,
                                                   Flag flag, bool value)
    : sequenceId_(sequenceId), groupId_(groupId), flag_(flag), value_(value) {}

Result SetTrackGroupFlagCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    TrackGroup* group = nullptr;
    for (auto& g : seq->trackGroups) {
        if (g.id == groupId_) {
            group = &g;
            break;
        }
    }
    if (group == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道组不存在");
    }
    if (!oldValues_.has_value()) {
        std::vector<bool> olds;
        olds.reserve(group->tracks.size());
        for (TrackId member : group->tracks) {
            const Track* t = seq->findTrack(member);
            if (t == nullptr) {
                return Result::fail(ErrorCode::NotFound, "轨道组成员不存在");
            }
            switch (flag_) {
                case Flag::Muted: olds.push_back(t->muted); break;
                case Flag::Hidden: olds.push_back(t->hidden); break;
                case Flag::Locked: olds.push_back(t->locked); break;
            }
        }
        oldValues_ = std::move(olds);
    }
    for (TrackId member : group->tracks) {
        Track* t = seq->findTrack(member);
        if (t == nullptr) {
            return Result::fail(ErrorCode::NotFound, "轨道组成员不存在");
        }
        switch (flag_) {
            case Flag::Muted: t->muted = value_; break;
            case Flag::Hidden: t->hidden = value_; break;
            case Flag::Locked: t->locked = value_; break;
        }
    }
    return Result::ok();
}

Result SetTrackGroupFlagCommand::revert(Project& project) {
    if (!oldValues_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少组成员快照");
    }
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const TrackGroup* group = nullptr;
    for (const auto& g : seq->trackGroups) {
        if (g.id == groupId_) {
            group = &g;
            break;
        }
    }
    if (group == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道组不存在");
    }
    if (group->tracks.size() != oldValues_->size()) {
        // 组成员在命令执行后变了（不应发生）：拒绝静默错配
        return Result::fail(ErrorCode::InvalidArgument, "轨道组成员已变更，无法还原");
    }
    for (std::size_t i = 0; i < group->tracks.size(); ++i) {
        Track* t = seq->findTrack(group->tracks[i]);
        if (t == nullptr) {
            return Result::fail(ErrorCode::NotFound, "轨道组成员不存在");
        }
        const bool v = (*oldValues_)[i];
        switch (flag_) {
            case Flag::Muted: t->muted = v; break;
            case Flag::Hidden: t->hidden = v; break;
            case Flag::Locked: t->locked = v; break;
        }
    }
    return Result::ok();
}

std::string SetTrackGroupFlagCommand::nameImpl() const {
    switch (flag_) {
        case Flag::Muted: return value_ ? "组内静音" : "组内取消静音";
        case Flag::Hidden: return value_ ? "组内隐藏" : "组内显示";
        case Flag::Locked: return value_ ? "组内锁定" : "组内解锁";
    }
    return "轨道组联动";
}

// ---------------- InsertClipCommand ----------------

InsertClipCommand::InsertClipCommand(SequenceId sequenceId, TrackId trackId, Clip clip)
    : sequenceId_(sequenceId), trackId_(trackId), clip_(std::move(clip)) {}

Result InsertClipCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const Result check = validateClipPlacement(project, *track, clip_);
    if (!check.isOk()) {
        return check;
    }
    track->clips.insert(insertionPoint(track->clips, clip_.start), clip_);
    return Result::ok();
}

Result InsertClipCommand::revert(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const auto it = std::find_if(track->clips.begin(), track->clips.end(),
                                 [this](const Clip& c) { return c.id == clip_.id; });
    if (it == track->clips.end()) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    track->clips.erase(it);
    return Result::ok();
}

// ---------------- RemoveClipCommand ----------------

RemoveClipCommand::RemoveClipCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId) {}

Result RemoveClipCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    const auto it = std::find_if(track->clips.begin(), track->clips.end(),
                                 [this](const Clip& c) { return c.id == clipId_; });
    if (it == track->clips.end()) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    removedClip_ = *it;
    removedIndex_ = static_cast<std::size_t>(std::distance(track->clips.begin(), it));
    track->clips.erase(it);
    return Result::ok();
}

Result RemoveClipCommand::revert(Project& project) {
    if (!removedClip_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少片段快照");
    }
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const std::size_t index =
        std::min(removedIndex_.value_or(track->clips.size()), track->clips.size());
    track->clips.insert(track->clips.begin() + static_cast<std::ptrdiff_t>(index),
                        *removedClip_);
    return Result::ok();
}

// ---------------- SetClipCommand ----------------

SetClipCommand::SetClipCommand(SequenceId sequenceId, TrackId trackId, Clip after,
                               std::string actionName)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      after_(std::move(after)),
      actionName_(std::move(actionName)) {}

Result SetClipCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    Clip* current = track->findClip(after_.id);
    if (current == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    const Result check = validateClipPlacement(project, *track, after_, after_.id);
    if (!check.isOk()) {
        return check;
    }
    if (!before_.has_value()) {
        before_ = *current;
    }
    *current = after_;
    track->sortClips();
    return Result::ok();
}

bool SetClipCommand::canMergeWith(const Command& other) const {
    const auto* o = dynamic_cast<const SetClipCommand*>(&other);
    if (o == nullptr) {
        return false;
    }
    // 同一序列、同一轨道、同一片段、同一操作类型 → 可合并
    return o->sequenceId_ == sequenceId_ && o->trackId_ == trackId_ &&
           o->after_.id == after_.id && o->actionName_ == actionName_;
}

void SetClipCommand::mergeFrom(const Command& other) {
    const auto* o = dynamic_cast<const SetClipCommand*>(&other);
    if (o == nullptr) {
        return;
    }
    // 目标状态更新为最新值；before_ 保持不变（撤销回到手势起点）
    after_ = o->after_;
}

Result SetClipCommand::revert(Project& project) {
    if (!before_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少片段快照");
    }
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    Clip* current = track->findClip(before_->id);
    if (current == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    *current = *before_;
    track->sortClips();
    return Result::ok();
}

// ---------------- SplitClipCommand ----------------

SplitClipCommand::SplitClipCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                   const Time& at, ClipId rightClipId)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      clipId_(clipId),
      at_(at),
      rightClipId_(rightClipId) {}

Result SplitClipCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    Clip* clip = track->findClip(clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!original_.has_value()) {
        original_ = *clip;
    }
    const Clip& original = *original_;
    if (!(original.start < at_ && at_ < original.end())) {
        return Result::fail(ErrorCode::OutOfRange, "分割点必须位于片段内部");
    }
    if (track->findClip(rightClipId_) != nullptr) {
        return Result::fail(ErrorCode::InvalidArgument, "右侧片段 ID 已被占用");
    }

    Clip left = original;
    left.duration = at_ - original.start;

    Clip right = original;
    right.id = rightClipId_;
    right.start = at_;
    right.duration = original.end() - at_;
    right.sourceIn = original.sourceIn + (at_ - original.start) * original.speed;

    *clip = left;
    track->clips.insert(insertionPoint(track->clips, right.start), right);
    return Result::ok();
}

Result SplitClipCommand::revert(Project& project) {
    if (!original_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少片段快照");
    }
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    // 移除右半
    const auto it = std::find_if(track->clips.begin(), track->clips.end(),
                                 [this](const Clip& c) { return c.id == rightClipId_; });
    if (it != track->clips.end()) {
        track->clips.erase(it);
    }
    // 恢复左半
    Clip* clip = track->findClip(clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    *clip = *original_;
    return Result::ok();
}

// ---------------- UndoStack ----------------

Result UndoStack::push(Project& project, std::unique_ptr<Command> command) {
    if (!command) {
        return Result::fail(ErrorCode::InvalidArgument, "命令为空");
    }
    // no-op 不应产生历史记录：用户按撤销会「毫无反应」，观感是撤销坏了。
    // 判定必须在 apply 之前 —— apply 会写入 oldIndex_/moved_ 等快照。
    if (command->isNoOp(project)) {
        return Result::ok();
    }
    // 事务期间：命令只执行不入栈，暂存到事务缓冲
    if (transactionDepth_ > 0) {
        const Result applied = command->apply(project);
        if (!applied.isOk()) {
            return applied;
        }
        transactionCommands_.push_back(std::move(command));
        return Result::ok();
    }
    return pushInternal(project, std::move(command));
}

Result UndoStack::pushInternal(Project& project, std::unique_ptr<Command> command) {
    // 合并尝试：仅在栈顶可合并且未截断 redo 时
    if (mergeEnabled_ && cursor_ > 0 && cursor_ == commands_.size()) {
        Command& top = *commands_[cursor_ - 1];
        if (top.canMergeWith(*command)) {
            const Result applied = command->apply(project);
            if (!applied.isOk()) {
                return applied;
            }
            top.mergeFrom(*command);
            return Result::ok();
        }
    }
    // 同 push：no-op 不入栈（这条路径也覆盖到，因为 push 之外还可能被直接调用）
    if (command->isNoOp(project)) {
        return Result::ok();
    }
    const Result applied = command->apply(project);
    if (!applied.isOk()) {
        return applied;
    }
    if (cursor_ < commands_.size()) {
        commands_.resize(cursor_);  // 截断 redo 分支
    }
    commands_.push_back(std::move(command));
    cursor_ = commands_.size();
    trimToMaxDepth();
    return Result::ok();
}

void UndoStack::setMaxDepth(std::size_t maxSteps) {
    maxDepth_ = maxSteps;
    trimToMaxDepth();
}

void UndoStack::trimToMaxDepth() {
    if (maxDepth_ == 0 || commands_.size() <= maxDepth_) {
        return;
    }
    const std::size_t drop = commands_.size() - maxDepth_;
    commands_.erase(commands_.begin(), commands_.begin() + static_cast<std::ptrdiff_t>(drop));
    cursor_ = cursor_ > drop ? cursor_ - drop : 0;
}

void UndoStack::beginTransaction(const std::string& name) {
    if (transactionDepth_ == 0) {
        transactionName_ = name;
        transactionCommands_.clear();
    }
    ++transactionDepth_;
}

// 分组命令：把一个事务内的多个命令封装为单个可撤销单元。
namespace {
class GroupedCommand : public Command {
public:
    GroupedCommand(std::string name, std::vector<std::unique_ptr<Command>> commands)
        : name_(std::move(name)), commands_(std::move(commands)) {}

    Result apply(Project& project) override {
        // 正向重做：按顺序重新 apply
        std::size_t applied = 0;
        for (; applied < commands_.size(); ++applied) {
            const Result r = commands_[applied]->apply(project);
            if (!r.isOk()) {
                // 回滚已执行的部分
                for (std::size_t i = applied; i-- > 0;) {
                    commands_[i]->revert(project);
                }
                return r;
            }
        }
        return Result::ok();
    }

    Result revert(Project& project) override {
        // 逆序还原
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
            const Result r = (*it)->revert(project);
            if (!r.isOk()) {
                return r;
            }
        }
        return Result::ok();
    }

    std::string nameImpl() const override { return name_; }

private:
    std::string name_;
    std::vector<std::unique_ptr<Command>> commands_;
};
}  // namespace

bool UndoStack::endTransaction(Project& project) {
    (void)project;
    if (transactionDepth_ == 0) {
        return false;
    }
    --transactionDepth_;
    if (transactionDepth_ > 0) {
        return false;  // 内层结束不封装
    }
    if (transactionCommands_.empty()) {
        return false;
    }
    // 如果事务内只有一个命令，直接入栈（不包装）
    // 注意：单一命令仍应显示**事务名**（用户视角的操作名，如「批量应用转场」），
    // 因此这里用事务名为其重命名（避免历史面板显示底层命令名如「添加转场」，
    // 与用户操作语义不符）。
    if (transactionCommands_.size() == 1) {
        transactionCommands_.front()->setDisplayName(transactionName_);
        if (cursor_ < commands_.size()) {
            commands_.resize(cursor_);
        }
        commands_.push_back(std::move(transactionCommands_.front()));
        cursor_ = commands_.size();
        transactionCommands_.clear();
        return false;
    }
    // 多命令：封装为 GroupedCommand 入栈
    auto grouped = std::make_unique<GroupedCommand>(transactionName_,
                                                    std::move(transactionCommands_));
    transactionCommands_.clear();
    if (cursor_ < commands_.size()) {
        commands_.resize(cursor_);
    }
    commands_.push_back(std::move(grouped));
    cursor_ = commands_.size();
    return true;
}

Result UndoStack::dropLast(Project& project) {
    if (transactionDepth_ > 0) {
        if (transactionCommands_.empty()) {
            return Result::fail(ErrorCode::OutOfRange, "事务中没有可丢弃的命令");
        }
        Command& cmd = *transactionCommands_.back();
        const Result r = cmd.revert(project);
        if (!r.isOk()) {
            return r;
        }
        transactionCommands_.pop_back();
        return Result::ok();
    }
    if (!canUndo()) {
        return Result::fail(ErrorCode::OutOfRange, "没有可丢弃的命令");
    }
    Command& cmd = *commands_[cursor_ - 1];
    const Result r = cmd.revert(project);
    if (!r.isOk()) {
        return r;
    }
    commands_.pop_back();
    --cursor_;
    return Result::ok();
}

Result UndoStack::undo(Project& project) {
    if (!canUndo()) {
        return Result::fail(ErrorCode::OutOfRange, "没有可撤销的操作");
    }
    Command& cmd = *commands_[cursor_ - 1];
    const Result reverted = cmd.revert(project);
    if (!reverted.isOk()) {
        return reverted;
    }
    --cursor_;
    return Result::ok();
}

Result UndoStack::redo(Project& project) {
    if (!canRedo()) {
        return Result::fail(ErrorCode::OutOfRange, "没有可重做的操作");
    }
    Command& cmd = *commands_[cursor_];
    const Result applied = cmd.apply(project);
    if (!applied.isOk()) {
        return applied;
    }
    ++cursor_;
    return Result::ok();
}

std::vector<std::string> UndoStack::undoHistoryNames() const {
    std::vector<std::string> names;
    names.reserve(cursor_);
    for (std::size_t i = 0; i < cursor_; ++i) {
        names.push_back(commands_[i]->name());
    }
    return names;
}

void UndoStack::clear() {
    commands_.clear();
    cursor_ = 0;
}

// ---------------- MoveClipToTrackCommand ----------------

MoveClipToTrackCommand::MoveClipToTrackCommand(SequenceId sequenceId, TrackId fromTrack,
                                               TrackId toTrack, ClipId clipId,
                                               const Time& newStart)
    : sequenceId_(sequenceId),
      fromTrack_(fromTrack),
      toTrack_(toTrack),
      clipId_(clipId),
      newStart_(newStart) {}

Result MoveClipToTrackCommand::apply(Project& project) {
    Track* from = findTrack(project, sequenceId_, fromTrack_);
    Track* to = findTrack(project, sequenceId_, toTrack_);
    if (from == nullptr || to == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (from->locked || to->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    if (newStart_.isNegative()) {
        return Result::fail(ErrorCode::InvalidArgument, "起点不能为负");
    }
    if (fromTrack_ == toTrack_) {
        return Result::fail(ErrorCode::InvalidArgument, "源与目标轨道相同，请使用移动操作");
    }
    const Clip* source = from->findClip(clipId_);
    if (source == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!movedClip_.has_value()) {
        movedClip_ = *source;
    }
    Clip toInsert = *movedClip_;
    toInsert.start = newStart_;
    const Result check = validateClipPlacement(project, *to, toInsert);
    if (!check.isOk()) {
        return check;
    }
    removedIndex_ = static_cast<std::size_t>(from->indexOf(clipId_));
    const auto it = std::find_if(from->clips.begin(), from->clips.end(),
                                 [this](const Clip& c) { return c.id == clipId_; });
    from->clips.erase(it);
    to->clips.insert(insertionPoint(to->clips, toInsert.start), toInsert);
    return Result::ok();
}

Result MoveClipToTrackCommand::revert(Project& project) {
    if (!movedClip_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少片段快照");
    }
    Track* from = findTrack(project, sequenceId_, fromTrack_);
    Track* to = findTrack(project, sequenceId_, toTrack_);
    if (from == nullptr || to == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const auto it = std::find_if(to->clips.begin(), to->clips.end(),
                                 [this](const Clip& c) { return c.id == clipId_; });
    if (it == to->clips.end()) {
        return Result::fail(ErrorCode::NotFound, "目标轨道上找不到片段");
    }
    to->clips.erase(it);
    const std::size_t index =
        std::min(removedIndex_.value_or(from->clips.size()), from->clips.size());
    from->clips.insert(from->clips.begin() + static_cast<std::ptrdiff_t>(index), *movedClip_);
    return Result::ok();
}


// ---------------- 字幕命令 ----------------

UpsertSubtitleCommand::UpsertSubtitleCommand(SequenceId sequenceId, SubtitleCue cue)
    : sequenceId_(sequenceId), cue_(std::move(cue)) {}

Result UpsertSubtitleCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const SubtitleCue* existing = seq->subtitles.findCue(cue_.id);
    if (existing != nullptr) {
        isReplace_ = true;
        previous_ = *existing;
    }
    return seq->subtitles.insert(cue_);
}

Result UpsertSubtitleCommand::revert(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    if (isReplace_) {
        return seq->subtitles.insert(previous_);
    }
    seq->subtitles.remove(cue_.id);
    return Result::ok();
}

RemoveSubtitleCommand::RemoveSubtitleCommand(SequenceId sequenceId, SubtitleId subtitleId)
    : sequenceId_(sequenceId), subtitleId_(subtitleId) {}

Result RemoveSubtitleCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const SubtitleCue* existing = seq->subtitles.findCue(subtitleId_);
    if (existing == nullptr) {
        return Result::fail(ErrorCode::NotFound, "字幕不存在");
    }
    removed_ = *existing;
    seq->subtitles.remove(subtitleId_);
    return Result::ok();
}

Result RemoveSubtitleCommand::revert(Project& project) {
    if (!removed_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少字幕快照");
    }
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    return seq->subtitles.insert(*removed_);
}


// ---------------- 转场命令 ----------------

AddTransitionCommand::AddTransitionCommand(SequenceId sequenceId, TrackId trackId,
                                           Transition transition)
    : sequenceId_(sequenceId), trackId_(trackId), transition_(std::move(transition)) {}

Result AddTransitionCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    const Result validation = validateTransition(*track, transition_, track->transitions);
    if (!validation.isOk()) {
        return validation;
    }
    track->transitions.push_back(transition_);
    return Result::ok();
}

Result AddTransitionCommand::revert(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const auto it = std::find_if(track->transitions.begin(), track->transitions.end(),
                                 [this](const Transition& t) { return t.id == transition_.id; });
    if (it == track->transitions.end()) {
        return Result::fail(ErrorCode::NotFound, "转场不存在");
    }
    track->transitions.erase(it);
    return Result::ok();
}

RemoveTransitionCommand::RemoveTransitionCommand(SequenceId sequenceId, TrackId trackId,
                                                 TransitionId transitionId)
    : sequenceId_(sequenceId), trackId_(trackId), transitionId_(transitionId) {}

Result RemoveTransitionCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const auto it = std::find_if(track->transitions.begin(), track->transitions.end(),
                                 [this](const Transition& t) { return t.id == transitionId_; });
    if (it == track->transitions.end()) {
        return Result::fail(ErrorCode::NotFound, "转场不存在");
    }
    removed_ = *it;
    track->transitions.erase(it);
    return Result::ok();
}

Result RemoveTransitionCommand::revert(Project& project) {
    if (!removed_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少转场快照");
    }
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    track->transitions.push_back(*removed_);
    return Result::ok();
}


// ---------------- 关键帧命令 ----------------

InsertKeyframeCommand::InsertKeyframeCommand(SequenceId sequenceId, TrackId trackId,
                                             ClipId clipId, ClipProperty property,
                                             Keyframe keyframe)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      clipId_(clipId),
      property_(property),
      keyframe_(std::move(keyframe)) {}

Result InsertKeyframeCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    KeyframeTrack& track = clip->animation.curve(property_);
    for (const auto& k : track.keyframes()) {
        if (k.time == keyframe_.time) {
            isReplace_ = true;
            previous_ = k;
            break;
        }
    }
    return track.insert(keyframe_);
}

Result InsertKeyframeCommand::revert(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    KeyframeTrack& track = clip->animation.curve(property_);
    if (isReplace_) {
        return track.insert(previous_);
    }
    track.remove(keyframe_.time);
    return Result::ok();
}

RemoveKeyframeCommand::RemoveKeyframeCommand(SequenceId sequenceId, TrackId trackId,
                                             ClipId clipId, ClipProperty property, Time at)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      clipId_(clipId),
      property_(property),
      at_(at) {}

Result RemoveKeyframeCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    KeyframeTrack& track = clip->animation.curve(property_);
    for (const auto& k : track.keyframes()) {
        if (k.time == at_) {
            removed_ = k;
            break;
        }
    }
    if (!removed_.has_value()) {
        return Result::fail(ErrorCode::NotFound, "关键帧不存在");
    }
    track.remove(at_);
    return Result::ok();
}

Result RemoveKeyframeCommand::revert(Project& project) {
    if (!removed_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少关键帧快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    return clip->animation.curve(property_).insert(*removed_);
}


// ---------------- 波纹编辑命令 ----------------

RippleDeleteCommand::RippleDeleteCommand(SequenceId sequenceId, TrackId trackId,
                                         std::vector<ClipId> clipIds)
    : sequenceId_(sequenceId), trackId_(trackId), clipIds_(std::move(clipIds)) {}

Result RippleDeleteCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    if (clipIds_.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "未指定要删除的片段");
    }
    // 按 start 降序处理：从后往前删除+前移，避免前移影响后续索引
    std::vector<Clip> toRemove;
    for (const ClipId id : clipIds_) {
        const Clip* c = track->findClip(id);
        if (c == nullptr) {
            return Result::fail(ErrorCode::NotFound, "待删除片段不存在");
        }
        toRemove.push_back(*c);
    }
    std::sort(toRemove.begin(), toRemove.end(),
              [](const Clip& a, const Clip& b) { return a.start > b.start; });
    removed_.clear();
    for (const Clip& target : toRemove) {
        const std::size_t index = static_cast<std::size_t>(track->indexOf(target.id));
        RemovedClip rc;
        rc.clip = target;
        rc.originalIndex = index;
        removed_.push_back(rc);
        // 删除
        track->clips.erase(track->clips.begin() + static_cast<std::ptrdiff_t>(index));
        // 后续片段整体前移 amount
        const Time amount = target.duration;
        for (auto& c : track->clips) {
            if (c.start >= target.end()) {
                c.start = c.start - amount;
            }
        }
    }
    if (!track->invariantsHold()) {
        return Result::fail(ErrorCode::Internal, "波纹删除破坏轨道不变量");
    }
    return Result::ok();
}

Result RippleDeleteCommand::revert(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    // 逆序恢复：从最早删除的开始反向恢复（后移+插回）
    for (auto it = removed_.rbegin(); it != removed_.rend(); ++it) {
        const Clip& target = it->clip;
        // 后续片段整体后移
        const Time amount = target.duration;
        for (auto& c : track->clips) {
            if (c.start >= target.start) {
                c.start = c.start + amount;
            }
        }
        // 插回原位
        const std::size_t index =
            std::min(it->originalIndex, track->clips.size());
        track->clips.insert(track->clips.begin() + static_cast<std::ptrdiff_t>(index), target);
    }
    return Result::ok();
}

InsertEditCommand::InsertEditCommand(SequenceId sequenceId, TrackId trackId, Clip clip,
                                     const Time& at, ClipId rightHalfId)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      clip_(std::move(clip)),
      at_(at),
      rightHalfId_(rightHalfId) {}

Result InsertEditCommand::apply(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    if (at_.isNegative() || !clip_.valid()) {
        return Result::fail(ErrorCode::InvalidArgument, "插入参数非法");
    }
    // 保存快照
    beforeClips_ = track->clips;
    // 1) 分割跨越 at 的片段
    for (std::size_t i = 0; i < track->clips.size(); ++i) {
        Clip& c = track->clips[i];
        if (c.start < at_ && at_ < c.end()) {
            const Time leftDuration = at_ - c.start;
            const Time rightDuration = c.end() - at_;
            const Time rightSourceIn = c.sourceIn + Time{leftDuration * c.speed};
            Clip right = c;
            right.id = rightHalfId_;
            right.start = at_;
            right.duration = rightDuration;
            right.sourceIn = rightSourceIn;
            c.duration = leftDuration;
            track->clips.insert(track->clips.begin() + static_cast<std::ptrdiff_t>(i + 1), right);
            break;
        }
    }
    // 2) start >= at 的片段整体后移 clip_.duration
    const Time shift = clip_.duration;
    for (auto& c : track->clips) {
        if (c.start >= at_) {
            c.start = c.start + shift;
        }
    }
    // 3) 插入新片段（start = at）
    Clip inserted = clip_;
    inserted.start = at_;
    const auto insertPos = std::lower_bound(
        track->clips.begin(), track->clips.end(), at_,
        [](const Clip& c, const Time& t) { return c.start < t; });
    track->clips.insert(insertPos, inserted);
    if (!track->invariantsHold()) {
        track->clips = beforeClips_;  // 回滚
        return Result::fail(ErrorCode::Internal, "插入编辑破坏轨道不变量");
    }
    return Result::ok();
}

Result InsertEditCommand::revert(Project& project) {
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    track->clips = beforeClips_;
    return Result::ok();
}


// ---------------- 轨道属性命令 ----------------

SetTrackCommand::SetTrackCommand(SequenceId sequenceId, TrackId trackId, Field field,
                                 std::string nameValue, bool boolValue, std::string actionName)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      field_(field),
      nameValue_(std::move(nameValue)),
      boolValue_(boolValue),
      actionName_(std::move(actionName)) {}

SetTrackCommand::SetTrackCommand(SequenceId sequenceId, TrackId trackId,
                                 const AutoDuckSettings& duckSettings, std::string actionName)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      field_(Field::AutoDuck),
      actionName_(std::move(actionName)),
      duckSettings_(duckSettings) {}

SetTrackCommand::SetTrackCommand(SequenceId sequenceId, TrackId trackId, Field field,
                                 double gainValue, std::string actionName)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      field_(field),
      gainValue_(gainValue),
      actionName_(std::move(actionName)) {}

Result SetTrackCommand::apply(Project& project) {
    // 主输出增益作用于序列（非轨道）：单独处理（其余字段均需轨道存在）
    if (field_ == Field::SeqMasterGain) {
        Sequence* seq = project.findSequence(sequenceId_);
        if (seq == nullptr) {
            return Result::fail(ErrorCode::NotFound, "序列不存在");
        }
        if (!beforeGain_.has_value()) beforeGain_ = seq->masterGainDb;
        seq->masterGainDb = gainValue_;
        return Result::ok();
    }
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    switch (field_) {
        case Field::Name: {
            if (!beforeName_.has_value()) beforeName_ = track->name;
            track->name = nameValue_;
            break;
        }
        case Field::Locked:
            if (!beforeBool_.has_value()) beforeBool_ = track->locked;
            track->locked = boolValue_;
            break;
        case Field::Muted:
            if (!beforeBool_.has_value()) beforeBool_ = track->muted;
            track->muted = boolValue_;
            break;
        case Field::Hidden:
            if (!beforeBool_.has_value()) beforeBool_ = track->hidden;
            track->hidden = boolValue_;
            break;
        case Field::AutoDuck:
            if (!beforeDuck_.has_value()) beforeDuck_ = track->autoDuck;
            track->autoDuck = duckSettings_;
            break;
        case Field::Solo:
            if (!beforeBool_.has_value()) beforeBool_ = track->solo;
            track->solo = gainValue_ != 0.0;
            break;
        case Field::Gain:
            if (!beforeGain_.has_value()) beforeGain_ = track->gainDb;
            track->gainDb = gainValue_;
            break;
    }
    return Result::ok();
}

Result SetTrackCommand::revert(Project& project) {
    // 主输出增益作用于序列（非轨道）：单独处理，避免误判「轨道不存在」
    if (field_ == Field::SeqMasterGain) {
        Sequence* seq = project.findSequence(sequenceId_);
        if (seq == nullptr) {
            return Result::fail(ErrorCode::NotFound, "序列不存在");
        }
        if (!beforeGain_.has_value()) {
            return Result::fail(ErrorCode::InvalidArgument, "缺少主输出增益快照");
        }
        seq->masterGainDb = *beforeGain_;
        return Result::ok();
    }
    Track* track = findTrack(project, sequenceId_, trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    switch (field_) {
        case Field::Name:
            if (!beforeName_.has_value()) {
                return Result::fail(ErrorCode::InvalidArgument, "缺少名称快照");
            }
            track->name = *beforeName_;
            break;
        case Field::Locked:
        case Field::Muted:
        case Field::Hidden:
            if (!beforeBool_.has_value()) {
                return Result::fail(ErrorCode::InvalidArgument, "缺少布尔快照");
            }
            if (field_ == Field::Locked) track->locked = *beforeBool_;
            if (field_ == Field::Muted) track->muted = *beforeBool_;
            if (field_ == Field::Hidden) track->hidden = *beforeBool_;
            break;
        case Field::AutoDuck:
            if (!beforeDuck_.has_value()) {
                return Result::fail(ErrorCode::InvalidArgument, "缺少自动闪避快照");
            }
            track->autoDuck = *beforeDuck_;
            break;
        case Field::Solo:
            if (!beforeBool_.has_value()) {
                return Result::fail(ErrorCode::InvalidArgument, "缺少独奏快照");
            }
            track->solo = *beforeBool_;
            break;
        case Field::Gain:
            if (!beforeGain_.has_value()) {
                return Result::fail(ErrorCode::InvalidArgument, "缺少增益快照");
            }
            track->gainDb = *beforeGain_;
            break;
    }
    return Result::ok();
}


// ---------------- 音画分离命令 ----------------

DetachAudioCommand::DetachAudioCommand(SequenceId sequenceId, TrackId videoTrackId,
                                       ClipId videoClipId, TrackId audioTrackId,
                                       ClipId newAudioClipId)
    : sequenceId_(sequenceId),
      videoTrackId_(videoTrackId),
      videoClipId_(videoClipId),
      audioTrackId_(audioTrackId),
      newAudioClipId_(newAudioClipId) {}

Result DetachAudioCommand::apply(Project& project) {
    const Track* videoTrack = findTrack(project, sequenceId_, videoTrackId_);
    Track* audioTrack = findTrack(project, sequenceId_, audioTrackId_);
    if (videoTrack == nullptr || audioTrack == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (!created_.has_value()) {
        const Clip* videoClip = videoTrack->findClip(videoClipId_);
        if (videoClip == nullptr) {
            return Result::fail(ErrorCode::NotFound, "视频片段不存在");
        }
        // 生成音频片段：同素材、同时间，新 ID
        Clip audio = *videoClip;
        audio.id = newAudioClipId_;
        created_ = audio;
    }
    // 校验目标音频轨放置合法性（锁定/重叠）
    const Result check = validateClipPlacement(project, *audioTrack, *created_);
    if (!check.isOk()) {
        return check;
    }
    audioTrack->clips.insert(insertionPoint(audioTrack->clips, created_->start), *created_);
    return Result::ok();
}

Result DetachAudioCommand::revert(Project& project) {
    if (!created_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少音频片段快照");
    }
    Track* audioTrack = findTrack(project, sequenceId_, audioTrackId_);
    if (audioTrack == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    const auto it = std::find_if(audioTrack->clips.begin(), audioTrack->clips.end(),
                                 [this](const Clip& c) { return c.id == newAudioClipId_; });
    if (it == audioTrack->clips.end()) {
        return Result::fail(ErrorCode::NotFound, "音频片段不存在");
    }
    audioTrack->clips.erase(it);
    return Result::ok();
}


// ---------------- 标记命令 ----------------

UpsertMarkerCommand::UpsertMarkerCommand(SequenceId sequenceId, Marker marker)
    : sequenceId_(sequenceId), marker_(std::move(marker)) {}

Result UpsertMarkerCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    if (marker_.time.isNegative()) {
        return Result::fail(ErrorCode::InvalidArgument, "标记时间不能为负");
    }
    for (auto& m : seq->markers) {
        if (m.id == marker_.id) {
            if (!isReplace_) {
                previous_ = m;
            }
            isReplace_ = true;
            m = marker_;
            return Result::ok();
        }
    }
    seq->markers.push_back(marker_);
    std::sort(seq->markers.begin(), seq->markers.end(),
              [](const Marker& a, const Marker& b) { return a.time < b.time; });
    return Result::ok();
}

Result UpsertMarkerCommand::revert(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    if (isReplace_) {
        for (auto& m : seq->markers) {
            if (m.id == marker_.id) {
                m = previous_;
                return Result::ok();
            }
        }
        return Result::fail(ErrorCode::NotFound, "标记不存在");
    }
    const auto it = std::find_if(seq->markers.begin(), seq->markers.end(),
                                 [this](const Marker& m) { return m.id == marker_.id; });
    if (it == seq->markers.end()) {
        return Result::fail(ErrorCode::NotFound, "标记不存在");
    }
    seq->markers.erase(it);
    return Result::ok();
}

RemoveMarkerCommand::RemoveMarkerCommand(SequenceId sequenceId, MarkerId markerId)
    : sequenceId_(sequenceId), markerId_(markerId) {}

Result RemoveMarkerCommand::apply(Project& project) {
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    const auto it = std::find_if(seq->markers.begin(), seq->markers.end(),
                                 [this](const Marker& m) { return m.id == markerId_; });
    if (it == seq->markers.end()) {
        return Result::fail(ErrorCode::NotFound, "标记不存在");
    }
    removed_ = *it;
    seq->markers.erase(it);
    return Result::ok();
}

Result RemoveMarkerCommand::revert(Project& project) {
    if (!removed_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少标记快照");
    }
    Sequence* seq = project.findSequence(sequenceId_);
    if (seq == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    seq->markers.push_back(*removed_);
    std::sort(seq->markers.begin(), seq->markers.end(),
              [](const Marker& a, const Marker& b) { return a.time < b.time; });
    return Result::ok();
}


// ---------------- 音频属性命令 ----------------

SetClipAudioCommand::SetClipAudioCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                         double gainDb, const Time& fadeIn, const Time& fadeOut,
                                         std::string actionName)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      clipId_(clipId),
      gainDb_(gainDb),
      fadeIn_(fadeIn),
      fadeOut_(fadeOut),
      actionName_(std::move(actionName)) {}

Result SetClipAudioCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!before_.has_value()) {
        before_ = Snapshot{clip->audioGainDb, clip->audioFadeIn, clip->audioFadeOut};
    }
    clip->audioGainDb = gainDb_;
    clip->audioFadeIn = fadeIn_;
    clip->audioFadeOut = fadeOut_;
    if (!clip->audioValid()) {
        // 回滚
        clip->audioGainDb = before_->gainDb;
        clip->audioFadeIn = before_->fadeIn;
        clip->audioFadeOut = before_->fadeOut;
        return Result::fail(ErrorCode::InvalidArgument, "音频参数非法（增益范围或淡入淡出超出片段时长）");
    }
    return Result::ok();
}

Result SetClipAudioCommand::revert(Project& project) {
    if (!before_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少音频快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    clip->audioGainDb = before_->gainDb;
    clip->audioFadeIn = before_->fadeIn;
    clip->audioFadeOut = before_->fadeOut;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段降噪强度（文档「🎵音频 → 降噪 → 开关/强度(弱中强)」）
// ---------------------------------------------------------------------------

SetClipDenoiseCommand::SetClipDenoiseCommand(SequenceId sequenceId, TrackId trackId,
                                             ClipId clipId, DenoiseStrength strength)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), strength_(strength) {}

std::string SetClipDenoiseCommand::nameImpl() const {
    switch (strength_) {
        case DenoiseStrength::Off: return "关闭降噪";
        case DenoiseStrength::Weak: return "降噪（弱）";
        case DenoiseStrength::Medium: return "降噪（中）";
        case DenoiseStrength::Strong: return "降噪（强）";
    }
    return "降噪";
}

Result SetClipDenoiseCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!before_.has_value()) {
        before_ = clip->denoise;
    }
    clip->denoise = strength_;
    return Result::ok();
}

Result SetClipDenoiseCommand::revert(Project& project) {
    if (!before_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少降噪快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    clip->denoise = *before_;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段启用/禁用（文档「剪辑」片段开关）
// ---------------------------------------------------------------------------

SetClipEnabledCommand::SetClipEnabledCommand(SequenceId sequenceId, TrackId trackId,
                                             ClipId clipId, bool enabled)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), enabled_(enabled) {}

Result SetClipEnabledCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!before_.has_value()) {
        before_ = clip->enabled;
    }
    clip->enabled = enabled_;
    return Result::ok();
}

Result SetClipEnabledCommand::revert(Project& project) {
    if (!before_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少启用状态快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    clip->enabled = *before_;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段倒放（文档「✂️剪辑 → 倒放」）
// ---------------------------------------------------------------------------

SetClipReversedCommand::SetClipReversedCommand(SequenceId sequenceId, TrackId trackId,
                                               ClipId clipId, bool reversed)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), reversed_(reversed) {}

Result SetClipReversedCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!before_.has_value()) {
        before_ = clip->reversed;
    }
    clip->reversed = reversed_;
    // 倒放与速度曲线互斥（倒放映射不参与曲线积分；清空避免语义歧义）
    if (reversed_ && !clip->speedCurve.empty()) {
        clip->speedCurve.clear();
    }
    return Result::ok();
}

Result SetClipReversedCommand::revert(Project& project) {
    if (!before_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少倒放状态快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    clip->reversed = *before_;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段透明度（文档「✂️剪辑 → 不透明度」）
// ---------------------------------------------------------------------------

SetClipOpacityCommand::SetClipOpacityCommand(SequenceId sequenceId, TrackId trackId,
                                             ClipId clipId, double opacity)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), opacity_(opacity) {}

Result SetClipOpacityCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    const PropertySpec spec = propertySpec(ClipProperty::Opacity);
    if (opacity_ < spec.minValue || opacity_ > spec.maxValue) {
        return Result::fail(ErrorCode::InvalidArgument, "透明度需在 0..1 之间");
    }
    if (!beforeCurve_.has_value()) {
        const KeyframeTrack* curve = clip->animation.findCurve(ClipProperty::Opacity);
        if (curve != nullptr) {
            beforeCurve_ = *curve;
        } else {
            beforeCurve_ = KeyframeTrack{};  // 空轨道（无曲线）
        }
    }
    // 清除关键帧并写入单点静态值（时刻 0，等同整段恒定）；
    // 透明度 = 默认值 1.0 时移除曲线（回归初始态，序列化不写）。
    KeyframeTrack& track = clip->animation.curve(ClipProperty::Opacity);
    track.clear();
    if (opacity_ != spec.defaultValue) {
        Keyframe keyframe;
        keyframe.time = Time{};
        keyframe.value = opacity_;
        keyframe.easing = Easing::linear();
        track.insert(keyframe);
    }
    return Result::ok();
}

Result SetClipOpacityCommand::revert(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!beforeCurve_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少透明度快照");
    }
    clip->animation.removeCurve(ClipProperty::Opacity);
    if (!beforeCurve_->empty()) {
        clip->animation.curve(ClipProperty::Opacity) = *beforeCurve_;
    }
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段滤镜（文档 Part 4-B 第 7 节「滤镜」）
// ---------------------------------------------------------------------------

SetClipFilterCommand::SetClipFilterCommand(SequenceId sequenceId, TrackId trackId,
                                           ClipId clipId, FilterStyle style, double intensity)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      clipId_(clipId),
      style_(style),
      intensity_(intensity) {}

std::string SetClipFilterCommand::nameImpl() const {
    if (style_ == FilterStyle::None) {
        return "清除滤镜";
    }
    return std::string("滤镜：") + filterStyleDisplayName(style_);
}

Result SetClipFilterCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    // 强度范围校验（文档 Part 4-B 7.2：0~100）
    if (intensity_ < 0.0 || intensity_ > 100.0) {
        return Result::fail(ErrorCode::InvalidArgument, "滤镜强度需在 0..100 之间");
    }
    if (!before_.has_value()) {
        before_ = Snapshot{clip->filterStyle, clip->filterIntensity};
    }
    clip->filterStyle = style_;
    clip->filterIntensity = intensity_;
    return Result::ok();
}

Result SetClipFilterCommand::revert(Project& project) {
    if (!before_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少滤镜快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    clip->filterStyle = before_->style;
    clip->filterIntensity = before_->intensity;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段背景填充（文档 Part 4-C 第 11 节「背景」）
// ---------------------------------------------------------------------------

SetClipBackgroundCommand::SetClipBackgroundCommand(SequenceId sequenceId, TrackId trackId,
                                                   ClipId clipId, BackgroundMode mode,
                                                   std::uint32_t rgba, double intensity)
    : sequenceId_(sequenceId),
      trackId_(trackId),
      clipId_(clipId),
      mode_(mode),
      rgba_(rgba),
      intensity_(intensity) {}

std::string SetClipBackgroundCommand::nameImpl() const {
    switch (mode_) {
        case BackgroundMode::None: return "清除背景填充";
        case BackgroundMode::Color: return "背景：颜色";
        case BackgroundMode::Blur: return "背景：模糊";
        case BackgroundMode::Style: return "背景：画布样式";
    }
    return "背景填充";
}

Result SetClipBackgroundCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    // 参数校验（文档 11.1：模糊度 0..100；画布样式 0..15）
    if (mode_ == BackgroundMode::Blur && (intensity_ < 0.0 || intensity_ > 100.0)) {
        return Result::fail(ErrorCode::InvalidArgument, "模糊度需在 0..100 之间");
    }
    if (mode_ == BackgroundMode::Style) {
        CanvasStyle style = CanvasStyle::None;
        if (!canvasStyleFromInt(static_cast<int>(std::lround(intensity_)), style) ||
            style == CanvasStyle::None) {
            return Result::fail(ErrorCode::InvalidArgument, "画布样式编号非法（1..15）");
        }
    }
    if (!before_.has_value()) {
        before_ = Snapshot{clip->backgroundMode, clip->backgroundColorRgba,
                           clip->backgroundIntensity};
    }
    clip->backgroundMode = mode_;
    clip->backgroundColorRgba = rgba_;
    clip->backgroundIntensity = intensity_;
    return Result::ok();
}

Result SetClipBackgroundCommand::revert(Project& project) {
    if (!before_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少背景快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    clip->backgroundMode = before_->mode;
    clip->backgroundColorRgba = before_->rgba;
    clip->backgroundIntensity = before_->intensity;
    return Result::ok();
}


// ---------------- 特效命令 ----------------

AddEffectCommand::AddEffectCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                   Effect effect)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), effect_(std::move(effect)) {}

Result AddEffectCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    const Effect* existing = clip->effects.find(effect_.id);
    if (existing != nullptr) {
        if (!isReplace_) {
            previous_ = *existing;
        }
        isReplace_ = true;
    }
    return clip->effects.add(effect_);
}

Result AddEffectCommand::revert(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (isReplace_) {
        return clip->effects.add(previous_);
    }
    clip->effects.remove(effect_.id);
    return Result::ok();
}

RemoveEffectCommand::RemoveEffectCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                         EffectId effectId)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), effectId_(effectId) {}

Result RemoveEffectCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    const Effect* existing = clip->effects.find(effectId_);
    if (existing == nullptr) {
        return Result::fail(ErrorCode::NotFound, "特效不存在");
    }
    removed_ = *existing;
    // 记录原位置
    for (std::size_t i = 0; i < clip->effects.effects().size(); ++i) {
        if (clip->effects.effects()[i].id == effectId_) {
            removedIndex_ = i;
            break;
        }
    }
    clip->effects.remove(effectId_);
    return Result::ok();
}

Result RemoveEffectCommand::revert(Project& project) {
    if (!removed_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少特效快照");
    }
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    return clip->effects.add(*removed_);
}


// ---------------- 素材命令 ----------------

ImportAssetCommand::ImportAssetCommand(AssetId assetId, MediaAsset asset)
    : assetId_(assetId), asset_(std::move(asset)) {
    asset_.id = assetId_;
}

// ---------------- ReorderAssetsCommand（B2-9 素材拖动排序）----------------

ReorderAssetsCommand::ReorderAssetsCommand(int fromIndex, int toIndex)
    : fromIndex_(fromIndex), toIndex_(toIndex) {}

bool ReorderAssetsCommand::isNoOp(const Project& project) const {
    // 只有「索引合法 + 原地不动」才算 no-op。
    // 若直接 return fromIndex_ == toIndex_，空素材池时的 (0,0) 会被误判为
    // no-op —— push 会直接返回 ok，绕过 apply 的越界报错，用户拖了个空操作
    // 却收不到任何提示。
    const int count = static_cast<int>(project.assets.size());
    if (fromIndex_ < 0 || fromIndex_ >= count) return false;
    if (toIndex_ < 0 || toIndex_ >= count) return false;
    return fromIndex_ == toIndex_;
}

Result ReorderAssetsCommand::apply(Project& project) {
    const int count = static_cast<int>(project.assets.size());
    if (fromIndex_ < 0 || fromIndex_ >= count) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "源位置超出范围（有效范围 0~" + std::to_string(count - 1) + "）");
    }
    if (toIndex_ < 0 || toIndex_ >= count) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "目标位置超出范围（有效范围 0~" + std::to_string(count - 1) + "）");
    }
    if (fromIndex_ == toIndex_) {
        return Result::ok();  // 位置没变：不改动 assets（也就不需要快照）
    }
    // 首次 apply 记录被移动素材的 id：revert 时按 id 找回它当前所在位置再挪回去，
    // 比「对 toIndex/fromIndex 做逆运算」更不容易出错（中间可能被其它命令改动过）。
    if (!movedId_.has_value()) {
        movedId_ = project.assets[static_cast<std::size_t>(fromIndex_)].id;
    }
    MediaAsset moved = project.assets[static_cast<std::size_t>(fromIndex_)];
    project.assets.erase(project.assets.begin() + fromIndex_);
    // 取出后再插入：erase 之后 toIndex 的语义就是「插入到该下标之前」，
    // 与 UI 的「拖到第 N 格」一致（不需要再 adjust）。
    project.assets.insert(project.assets.begin() + toIndex_, std::move(moved));
    return Result::ok();
}

Result ReorderAssetsCommand::revert(Project& project) {
    if (!movedId_.has_value()) {
        return Result::ok();  // 从未生效过（no-op 或 apply 失败）
    }
    const auto it = std::find_if(project.assets.begin(), project.assets.end(),
                                 [this](const MediaAsset& a) { return a.id == *movedId_; });
    if (it == project.assets.end()) {
        return Result::fail(ErrorCode::NotFound, "撤销失败：被移动的素材已不存在");
    }
    const int current = static_cast<int>(std::distance(project.assets.begin(), it));
    if (current == fromIndex_) {
        return Result::ok();  // 已在原位
    }
    MediaAsset moved = *it;
    project.assets.erase(project.assets.begin() + current);
    project.assets.insert(project.assets.begin() + fromIndex_, std::move(moved));
    return Result::ok();
}

Result ImportAssetCommand::apply(Project& project) {
    if (project.findAsset(assetId_) != nullptr) {
        return Result::fail(ErrorCode::InvalidArgument, "素材 ID 已存在");
    }
    project.assets.push_back(asset_);
    return Result::ok();
}

Result ImportAssetCommand::revert(Project& project) {
    const auto it = std::find_if(project.assets.begin(), project.assets.end(),
                                 [this](const MediaAsset& a) { return a.id == assetId_; });
    if (it == project.assets.end()) {
        return Result::fail(ErrorCode::NotFound, "素材不存在");
    }
    project.assets.erase(it);
    return Result::ok();
}

RemoveAssetCommand::RemoveAssetCommand(AssetId assetId) : assetId_(assetId) {}

Result RemoveAssetCommand::apply(Project& project) {
    for (std::size_t i = 0; i < project.assets.size(); ++i) {
        if (project.assets[i].id == assetId_) {
            removed_ = project.assets[i];
            removedIndex_ = i;
            project.assets.erase(project.assets.begin() + static_cast<std::ptrdiff_t>(i));
            return Result::ok();
        }
    }
    return Result::fail(ErrorCode::NotFound, "素材不存在");
}

Result RemoveAssetCommand::revert(Project& project) {
    if (!removed_.has_value() || !removedIndex_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少素材快照");
    }
    const std::size_t index = std::min(*removedIndex_, project.assets.size());
    project.assets.insert(project.assets.begin() + static_cast<std::ptrdiff_t>(index),
                          *removed_);
    return Result::ok();
}

// =============================================================================
// 重新链接素材（文档第 3 部分「四、媒体离线检测与重新链接」）
// =============================================================================

RelinkAssetCommand::RelinkAssetCommand(AssetId assetId, std::string newPath,
                                       std::string newDigest)
    : assetId_(assetId), newPath_(std::move(newPath)), newDigest_(std::move(newDigest)) {}

Result RelinkAssetCommand::apply(Project& project) {
    if (newPath_.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "新路径不能为空");
    }
    MediaAsset* asset = project.findAsset(assetId_);
    if (asset == nullptr) {
        return Result::fail(ErrorCode::NotFound, "素材不存在");
    }
    // 快照（供 revert 完整还原）
    if (!previous_.has_value()) {
        previous_ = *asset;
    }
    asset->path = newPath_;
    asset->offline = false;  // 重链后清除离线标记
    if (!newDigest_.empty()) {
        asset->contentSha256 = newDigest_;
    }
    return Result::ok();
}

Result RelinkAssetCommand::revert(Project& project) {
    if (!previous_.has_value()) {
        return Result::fail(ErrorCode::InvalidArgument, "缺少素材快照");
    }
    MediaAsset* asset = project.findAsset(assetId_);
    if (asset == nullptr) {
        return Result::fail(ErrorCode::NotFound, "素材不存在");
    }
    *asset = *previous_;
    return Result::ok();
}


// =============================================================================
// 复合片段 / 嵌套序列（文档第 3 部分「嵌套序列/复合片段」）
// =============================================================================

CreateCompoundClipCommand::CreateCompoundClipCommand(SequenceId sourceSequenceId, TrackId trackId,
                                                     std::vector<ClipId> selectedClipIds,
                                                     SequenceId newSequenceId, TrackId newTrackId,
                                                     ClipId compoundClipId,
                                                     std::string compoundName)
    : sourceSequenceId_(sourceSequenceId),
      trackId_(trackId),
      selectedClipIds_(std::move(selectedClipIds)),
      newSequenceId_(newSequenceId),
      newTrackId_(newTrackId),
      compoundClipId_(compoundClipId),
      compoundName_(std::move(compoundName)) {}

Result CreateCompoundClipCommand::apply(Project& project) {
    // 1) 校验
    if (selectedClipIds_.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "未选择任何片段");
    }
    if (!newSequenceId_.isValid()) {
        return Result::fail(ErrorCode::InvalidArgument, "嵌套序列 ID 无效");
    }
    if (project.findSequence(newSequenceId_) != nullptr) {
        return Result::fail(ErrorCode::InvalidArgument, "嵌套序列 ID 已被占用");
    }
    Sequence* source = project.findSequence(sourceSequenceId_);
    if (source == nullptr) {
        return Result::fail(ErrorCode::NotFound, "源序列不存在");
    }
    Track* track = source->findTrack(trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "源轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "源轨道已锁定");
    }
    // 收集选中片段（保持时间顺序），并定位索引
    std::vector<std::size_t> indexes;
    for (const auto& clipId : selectedClipIds_) {
        const int index = track->indexOf(clipId);
        if (index < 0) {
            return Result::fail(ErrorCode::NotFound, "选中的片段不存在");
        }
        indexes.push_back(static_cast<std::size_t>(index));
    }
    std::sort(indexes.begin(), indexes.end());
    // 区间必须连续：首尾之间不能夹带未选中的片段
    for (std::size_t i = 1; i < indexes.size(); ++i) {
        if (indexes[i] != indexes[i - 1] + 1) {
            return Result::fail(ErrorCode::InvalidArgument,
                                "选中的片段区间必须连续（不能夹带未选片段）");
        }
    }
    const Time spanStart = track->clips[indexes.front()].start;
    const Time spanEnd = track->clips[indexes.back()].end();
    if (!(spanEnd > spanStart)) {
        return Result::fail(ErrorCode::InvalidArgument, "选中片段区间无效");
    }

    // 2) 构建嵌套序列（保留相对时间：以 spanStart 为原点）
    Sequence nested;
    nested.id = newSequenceId_;
    nested.name = compoundName_.empty() ? "复合片段" : compoundName_;
    nested.frameRate = source->frameRate;
    nested.width = source->width;
    nested.height = source->height;
    nested.frameFit = source->frameFit;
    nested.backgroundRgba = source->backgroundRgba;
    // 同类型轨道承载（视频片段放入视频轨、音频放入音频轨；特效/字幕轨按类型映射）
    for (std::size_t i = 0; i < indexes.size(); ++i) {
        Clip copy = track->clips[indexes[i]];
        // 相对时间（嵌套序列从 0 开始）
        copy.start = copy.start - spanStart;
        const TrackType trackType = track->type;
        Track* target = nullptr;
        for (auto& candidate : nested.tracks) {
            if (candidate.type == trackType) {
                target = &candidate;
                break;
            }
        }
        if (target == nullptr) {
            Track created;
            // 预分配 ID（保证 redo 确定性；与既有命令约定一致）
            created.id = newTrackId_;
            created.type = trackType;
            created.name = track->name;
            nested.tracks.push_back(created);
            target = &nested.tracks.back();
        }
        target->clips.push_back(std::move(copy));
    }
    // 嵌套序列内容时长 = span 长度
    createdSequence_ = nested;

    // 3) 修改工程：插入嵌套序列 + 摘除片段 + 插入复合片段
    project.sequences.push_back(nested);
    // 3a) 摘除原片段（降序删除，索引稳定）
    std::vector<std::size_t> descendingIndexes(indexes.rbegin(), indexes.rend());
    for (const std::size_t index : descendingIndexes) {
        if (index < track->clips.size()) {
            removedClips_.insert(removedClips_.begin(), track->clips[index]);
            track->clips.erase(track->clips.begin() + static_cast<std::ptrdiff_t>(index));
        }
    }
    // 索引记录（重做为升序插入位置）
    removedIndexes_ = indexes;
    // 3b) 插入复合片段（覆盖原区间）
    Clip compound;
    // 预分配 ID（保证 redo 确定性）
    compound.id = compoundClipId_;
    compound.asset = AssetId{};  // 复合片段不引用素材
    compound.nestedSequence = newSequenceId_;
    compound.compoundName = nested.name;
    compound.start = spanStart;
    compound.duration = spanEnd - spanStart;
    compoundClip_ = compound;
    track->clips.insert(insertionPoint(track->clips, compound.start), compound);
    return Result::ok();
}

Result CreateCompoundClipCommand::revert(Project& project) {
    if (!createdSequence_.has_value() || !compoundClip_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少创建快照");
    }
    Sequence* source = project.findSequence(sourceSequenceId_);
    if (source == nullptr) {
        return Result::fail(ErrorCode::NotFound, "源序列不存在");
    }
    Track* track = source->findTrack(trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "源轨道不存在");
    }
    // 移除复合片段
    const int compoundIndex = track->indexOf(compoundClip_->id);
    if (compoundIndex >= 0) {
        track->clips.erase(track->clips.begin() + compoundIndex);
    }
    // 还原原片段（按记录索引升序插回；插入位置用起点排序确保一致性）
    for (const auto& clip : removedClips_) {
        track->clips.insert(insertionPoint(track->clips, clip.start), clip);
    }
    // 移除嵌套序列
    for (std::size_t i = 0; i < project.sequences.size(); ++i) {
        if (project.sequences[i].id == newSequenceId_) {
            project.sequences.erase(project.sequences.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    return Result::ok();
}

UnwrapCompoundClipCommand::UnwrapCompoundClipCommand(SequenceId sequenceId, TrackId trackId,
                                                     ClipId compoundClipId)
    : sequenceId_(sequenceId), trackId_(trackId), compoundClipId_(compoundClipId) {}

Result UnwrapCompoundClipCommand::apply(Project& project) {
    Sequence* sequence = project.findSequence(sequenceId_);
    if (sequence == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    Track* track = sequence->findTrack(trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    if (track->locked) {
        return Result::fail(ErrorCode::Locked, "轨道已锁定");
    }
    const Clip* compound = track->findClip(compoundClipId_);
    if (compound == nullptr) {
        return Result::fail(ErrorCode::NotFound, "复合片段不存在");
    }
    if (!compound->isCompound()) {
        return Result::fail(ErrorCode::InvalidArgument, "该片段不是复合片段");
    }
    Sequence* nested = project.findSequence(compound->nestedSequence);
    if (nested == nullptr) {
        return Result::fail(ErrorCode::NotFound, "复合片段引用的嵌套序列不存在");
    }
    // 收集嵌套序列中的所有片段（按轨道顺序，保持相对时间）
    const Time offset = compound->start;
    std::vector<Clip> restored;
    for (const auto& nestedTrack : nested->tracks) {
        if (nestedTrack.type != track->type) {
            continue;  // 仅还原到同类型轨道（跨类型还原需要多轨，UI 侧按需扩展）
        }
        for (const auto& nestedClip : nestedTrack.clips) {
            Clip copy = nestedClip;
            copy.start = copy.start + offset;
            restored.push_back(copy);
        }
    }
    if (restored.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "复合片段内容为空（无同类型片段）");
    }
    removedCompound_ = *compound;
    removedSequence_ = *nested;
    restoredClips_ = restored;
    // 移除复合片段
    const int compoundIndex = track->indexOf(compoundClipId_);
    if (compoundIndex >= 0) {
        track->clips.erase(track->clips.begin() + compoundIndex);
    }
    // 插入还原片段
    for (const auto& clip : restored) {
        track->clips.insert(insertionPoint(track->clips, clip.start), clip);
    }
    // 移除嵌套序列
    for (std::size_t i = 0; i < project.sequences.size(); ++i) {
        if (project.sequences[i].id == nested->id) {
            project.sequences.erase(project.sequences.begin() + static_cast<std::ptrdiff_t>(i));
            break;
        }
    }
    return Result::ok();
}

Result UnwrapCompoundClipCommand::revert(Project& project) {
    if (!removedCompound_.has_value() || !removedSequence_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少解除快照");
    }
    Sequence* sequence = project.findSequence(sequenceId_);
    if (sequence == nullptr) {
        return Result::fail(ErrorCode::NotFound, "序列不存在");
    }
    Track* track = sequence->findTrack(trackId_);
    if (track == nullptr) {
        return Result::fail(ErrorCode::NotFound, "轨道不存在");
    }
    // 移除还原片段
    for (auto it = track->clips.begin(); it != track->clips.end();) {
        bool isRestored = false;
        for (const auto& restored : restoredClips_) {
            if (it->id == restored.id) {
                isRestored = true;
                break;
            }
        }
        if (isRestored) {
            it = track->clips.erase(it);
        } else {
            ++it;
        }
    }
    // 还原嵌套序列与复合片段
    project.sequences.push_back(*removedSequence_);
    track->clips.insert(insertionPoint(track->clips, removedCompound_->start), *removedCompound_);
    return Result::ok();
}


// ---------------------------------------------------------------------------
// 设置片段蒙版
// ---------------------------------------------------------------------------

SetClipMaskCommand::SetClipMaskCommand(SequenceId sequenceId, TrackId trackId, ClipId clipId,
                                       Mask mask)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), mask_(mask) {}

Result SetClipMaskCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!mask_.valid()) {
        return Result::fail(ErrorCode::InvalidArgument, "蒙版参数非法（羽化/圆角需在 0..0.5）");
    }
    if (!beforeHasMask_.has_value()) beforeHasMask_ = clip->hasMask;
    if (!beforeMask_.has_value()) beforeMask_ = clip->mask;
    clip->hasMask = mask_.shape != MaskShape::None;
    clip->mask = mask_;
    return Result::ok();
}

Result SetClipMaskCommand::revert(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!beforeHasMask_.has_value() || !beforeMask_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少蒙版快照");
    }
    clip->hasMask = *beforeHasMask_;
    clip->mask = *beforeMask_;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段色度抠像
// ---------------------------------------------------------------------------

SetClipChromaKeyCommand::SetClipChromaKeyCommand(SequenceId sequenceId, TrackId trackId,
                                                 ClipId clipId, ChromaKey chromaKey)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), chromaKey_(chromaKey) {}

Result SetClipChromaKeyCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!chromaKey_.valid()) {
        return Result::fail(ErrorCode::InvalidArgument,
                            "抠像参数非法（阈值需在 0..1 且 outer > inner）");
    }
    if (!beforeHasChromaKey_.has_value()) beforeHasChromaKey_ = clip->hasChromaKey;
    if (!beforeChromaKey_.has_value()) beforeChromaKey_ = clip->chromaKey;
    clip->hasChromaKey = true;
    clip->chromaKey = chromaKey_;
    return Result::ok();
}

Result SetClipChromaKeyCommand::revert(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!beforeHasChromaKey_.has_value() || !beforeChromaKey_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少抠像快照");
    }
    clip->hasChromaKey = *beforeHasChromaKey_;
    clip->chromaKey = *beforeChromaKey_;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 清除片段色度抠像
// ---------------------------------------------------------------------------

ClearClipChromaKeyCommand::ClearClipChromaKeyCommand(SequenceId sequenceId, TrackId trackId,
                                                     ClipId clipId)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId) {}

Result ClearClipChromaKeyCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!beforeHasChromaKey_.has_value()) beforeHasChromaKey_ = clip->hasChromaKey;
    clip->hasChromaKey = false;
    clip->chromaKey = ChromaKey{};
    return Result::ok();
}

Result ClearClipChromaKeyCommand::revert(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!beforeHasChromaKey_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少抠像快照");
    }
    clip->hasChromaKey = *beforeHasChromaKey_;
    return Result::ok();
}

// ---------------------------------------------------------------------------
// 设置片段画中画混合模式（文档第二段 5.9）
// ---------------------------------------------------------------------------

SetClipBlendModeCommand::SetClipBlendModeCommand(SequenceId sequenceId, TrackId trackId,
                                                 ClipId clipId, BlendMode mode)
    : sequenceId_(sequenceId), trackId_(trackId), clipId_(clipId), mode_(mode) {}

Result SetClipBlendModeCommand::apply(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!beforeMode_.has_value()) beforeMode_ = clip->blendMode;
    clip->blendMode = mode_;
    return Result::ok();
}

Result SetClipBlendModeCommand::revert(Project& project) {
    Clip* clip = findClip(project, sequenceId_, trackId_, clipId_);
    if (clip == nullptr) {
        return Result::fail(ErrorCode::NotFound, "片段不存在");
    }
    if (!beforeMode_.has_value()) {
        return Result::fail(ErrorCode::Internal, "缺少混合模式快照");
    }
    clip->blendMode = *beforeMode_;
    return Result::ok();
}

}  // namespace core
}  // namespace nairoclip
