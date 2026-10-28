#include "nairoclip/core/template_apply.h"

#include <cstdio>
#include <set>

namespace nairoclip {
namespace core {

namespace {

/// 把槽位下标 → 素材。重复的槽位下标视为非法（后写覆盖前写会让用户困惑）。
Result buildSlotMap(const std::vector<MaterialAssignment>& assignments,
                    std::vector<AssetId>& outMap, std::vector<bool>& outPresent) {
    std::set<std::size_t> seen;
    for (const auto& a : assignments) {
        if (!seen.insert(a.slotIndex).second) {
            return Result::fail(ErrorCode::InvalidArgument,
                                "槽位 " + std::to_string(a.slotIndex + 1) +
                                    " 被指定了多个素材");
        }
    }
    for (const auto& a : assignments) {
        if (a.slotIndex >= outMap.size()) {
            return Result::fail(ErrorCode::OutOfRange,
                                "素材指定引用了不存在的槽位 " +
                                    std::to_string(a.slotIndex + 1));
        }
        outMap[a.slotIndex] = a.asset;
        outPresent[a.slotIndex] = true;
    }
    return Result::ok();
}

}  // namespace

std::vector<MaterialAssignment> assignmentsFromFill(
    const FillResult& fill, const std::vector<FillCandidate>& candidates) {
    std::vector<MaterialAssignment> out;
    for (const auto& a : fill.assignments) {
        if (a.candidateIndex < 0) continue;
        const std::size_t idx = static_cast<std::size_t>(a.candidateIndex);
        if (idx >= candidates.size()) continue;  // 防御：填充结果与候选集不匹配
        MaterialAssignment m;
        m.slotIndex = a.slotIndex;
        m.asset = candidates[idx].asset.id;
        out.push_back(m);
    }
    return out;
}

Result applyTemplate(TimelineEditor& editor, SequenceId sequenceId, const EditTemplate& tmpl,
                     const std::vector<MaterialAssignment>& assignments,
                     TemplateApplyResult& out) {
    out = TemplateApplyResult{};

    // 模板本身必须合法，否则后面的排布全是垃圾
    const Result tmplCheck = validateEditTemplate(tmpl);
    if (!tmplCheck.isOk()) {
        return tmplCheck;
    }

    std::vector<AssetId> slotAsset(tmpl.slots.size());
    std::vector<bool> slotPresent(tmpl.slots.size(), false);
    const Result mapResult = buildSlotMap(assignments, slotAsset, slotPresent);
    if (!mapResult.isOk()) {
        return mapResult;
    }

    // 素材必须真实存在（提前查，避免插入到一半才发现）
    for (std::size_t i = 0; i < tmpl.slots.size(); ++i) {
        if (!slotPresent[i]) continue;
        if (editor.project().findAsset(slotAsset[i]) == nullptr) {
            return Result::fail(ErrorCode::NotFound,
                                "槽位 " + std::to_string(i + 1) + " 指定的素材不存在");
        }
    }

    // 事务：所有改动合成一条撤销记录
    editor.beginTransaction("套用模板：" + tmpl.name);

    // 主轴轨道 + 按需创建的前景轨道
    TrackId mainTrack;
    Result r = editor.addTrack(sequenceId, TrackType::Video, "模板·主轴", &mainTrack);
    if (!r.isOk()) {
        editor.endTransaction();
        return r;
    }
    out.tracksUsed = 1;

    TrackId fg2Track;
    TrackId fg3Track;
    bool hasFg2 = false;
    bool hasFg3 = false;
    for (const auto& s : tmpl.slots) {
        if (s.role == LayerRole::Foreground2) hasFg2 = true;
        if (s.role == LayerRole::Foreground3) hasFg3 = true;
    }
    if (hasFg2) {
        r = editor.addTrack(sequenceId, TrackType::Video, "模板·前景2", &fg2Track);
        if (!r.isOk()) {
            editor.endTransaction();
            return r;
        }
        ++out.tracksUsed;
    }
    if (hasFg3) {
        r = editor.addTrack(sequenceId, TrackType::Video, "模板·前景3", &fg3Track);
        if (!r.isOk()) {
            editor.endTransaction();
            return r;
        }
        ++out.tracksUsed;
    }

    // 逐槽位排布：首尾相接
    Time cursor = Rational::make(0);
    // 上一段的 (轨道, ClipId)，用于建转场
    TrackId prevTrack;
    ClipId prevClip;
    bool hasPrev = false;

    for (std::size_t i = 0; i < tmpl.slots.size(); ++i) {
        const TemplateSlot& slot = tmpl.slots[i];
        const Time slotStart = cursor;
        cursor = cursor + slot.duration;

        if (!slotPresent[i]) {
            out.warnings.push_back("槽位 " + std::to_string(i + 1) + " 未指定素材，已跳过");
            hasPrev = false;
            continue;
        }

        TrackId target = mainTrack;
        if (slot.role == LayerRole::Foreground2) target = fg2Track;
        if (slot.role == LayerRole::Foreground3) target = fg3Track;

        Clip clip;
        clip.asset = slotAsset[i];
        clip.start = slotStart;
        clip.duration = slot.duration;
        clip.sourceIn = Rational::make(0);

        // 先插入片段（特效要挂在已存在的片段上，且必须走 editor.addEffect
        // 才会进入撤销事务；直接改 clip.effects 会绕过撤销）
        ClipId inserted;
        r = editor.insertClip(sequenceId, target, clip, &inserted);
        if (!r.isOk()) {
            editor.endTransaction();  // 回滚
            return r;
        }
        ++out.clipsInserted;

        // 特效：逐个通过 editor.addEffect 添加（可撤销）
        for (const auto& fx : slot.effects) {
            Effect e;
            e.id = EffectId{editor.project().ids.next()};
            e.type = fx.type;
            e.value = fx.value;
            e.enabled = true;
            const Result er = editor.addEffect(sequenceId, target, inserted, e);
            if (er.isOk()) {
                ++out.effectsAdded;
            } else {
                out.warnings.push_back("槽位 " + std::to_string(i + 1) +
                                       " 的特效添加失败：" + er.message);
            }
        }

        // 摇晃：写入片段字段（Clip::shake，B1-5 的 ShakeParams）。
        // 渲染侧 export_executor 会读它并调 applyShakeToRgba。
        if (slot.hasShake()) {
            Clip* placed = findClip(editor.project(), sequenceId, target, inserted);
            if (placed != nullptr) {
                placed->shake.trajectory = ShakeTrajectory::Horizontal;
                placed->shake.amplitude = slot.shakeAmplitude;
                placed->shake.frequency = slot.shakeFrequency;
                // 种子按槽位序号固定，保证同一模板多次套用结果一致
                placed->shake.seed = static_cast<std::uint32_t>(i + 1);
                ++out.shakesApplied;
            } else {
                out.warnings.push_back("槽位 " + std::to_string(i + 1) +
                                       " 的摇晃写入失败（找不到刚插入的片段）");
            }
        }

        // 转场：只对同一轨道上的相邻片段建立
        if (slot.inTransition != TransitionType::Cut && hasPrev && prevTrack == target) {
            Transition tr;
            tr.type = slot.inTransition;
            tr.duration = slot.transitionDuration;
            tr.fromClip = prevClip;
            tr.toClip = inserted;
            const Result trResult = editor.addTransition(sequenceId, target, tr);
            if (trResult.isOk()) {
                ++out.transitionsAdded;
            } else {
                // 转场失败不应让整个模板失败（例如片段相邻判定不满足），
                // 但必须如实记录，不能静默丢掉。
                out.warnings.push_back("槽位 " + std::to_string(i + 1) +
                                       " 的转场添加失败：" + trResult.message);
            }
        }

        prevTrack = target;
        prevClip = inserted;
        hasPrev = true;
    }

    editor.endTransaction();
    return Result::ok();
}

std::string describeApplyResult(const TemplateApplyResult& result) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "插入 %d 段，添加 %d 个转场、%d 个特效（使用 %d 条轨道）",
                  result.clipsInserted, result.transitionsAdded, result.effectsAdded,
                  result.tracksUsed);
    std::string out(buf);
    if (!result.warnings.empty()) {
        out += "；" + std::to_string(result.warnings.size()) + " 条提示";
    }
    return out;
}

}  // namespace core
}  // namespace nairoclip
