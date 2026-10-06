#include "nairoclip/core/auto_edit.h"

#include "nairoclip/core/effects.h"

#include <algorithm>

namespace nairoclip {
namespace core {

AutoEditResult applyAutoEdit(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                             TrackId trackId, const StyleProfile& profile,
                             const std::vector<MaterialSegment>& material,
                             const AutoEditOptions& options) {
    AutoEditResult result;

    // 1) 前置校验：轨道存在且为空
    Track* track = findTrack(project, sequenceId, trackId);
    if (track == nullptr) {
        result.result = Result::fail(ErrorCode::NotFound, "目标轨道不存在");
        return result;
    }
    if (!track->clips.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument,
                                     "目标轨道非空，自动剪辑仅支持空轨道");
        return result;
    }
    if (track->locked) {
        result.result = Result::fail(ErrorCode::Locked, "目标轨道已锁定");
        return result;
    }
    if (!profile.valid()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "风格画像无效（无有效镜头）");
        return result;
    }
    if (material.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "用户素材为空");
        return result;
    }

    // 2) 计算剪辑建议
    const std::vector<CutSuggestion> cuts = suggestCuts(profile.rhythm, material);
    if (cuts.empty()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "素材不足以生成片段");
        return result;
    }

    // 3) 开事务执行
    editor.beginTransaction(options.transactionName);
    const SuggestedColorEffects colorEffects = suggestColorEffects(profile.color);

    Time cursor;  // 时间轴位置
    std::vector<ClipId> insertedIds;
    std::size_t effectSerial = 0;
    for (const auto& cut : cuts) {
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

        // 3a) 色调特效（每个片段各加一套，ID 递增）
        if (options.applyColor && !colorEffects.neutral()) {
            auto addEffect = [&](EffectType type, double value) {
                Effect e;
                e.id = EffectId{project.ids.next()};
                e.type = type;
                e.value = value;
                const Result er = editor.addEffect(sequenceId, trackId, insertedId, e);
                if (er.isOk()) {
                    ++result.effectsAdded;
                }
                ++effectSerial;
            };
            if (colorEffects.saturation != 1.0) {
                addEffect(EffectType::Saturation, colorEffects.saturation);
            }
            if (colorEffects.contrast != 1.0) {
                addEffect(EffectType::Contrast, colorEffects.contrast);
            }
            if (colorEffects.temperature != 0.0) {
                addEffect(EffectType::Temperature, colorEffects.temperature);
            }
        }
        cursor = cursor + cut.duration;
    }

    // 3b) 转场（可选）：相邻片段之间（需要间隙；无间隙时跳过并警告）
    if (options.applyTransitions && insertedIds.size() >= 2) {
        std::size_t added = 0;
        for (std::size_t i = 0; i + 1 < insertedIds.size() && added < options.maxTransitions;
             ++i) {
            const Clip* a = track->findClip(insertedIds[i]);
            const Clip* b = track->findClip(insertedIds[i + 1]);
            if (a == nullptr || b == nullptr || !(a->end() < b->start)) {
                continue;  // 无间隙：无法放转场
            }
            Transition tr;
            tr.id = TransitionId{project.ids.next()};
            tr.type = TransitionType::CrossDissolve;
            tr.duration = Rational::make(3, 10);  // 0.3s
            tr.fromClip = a->id;
            tr.toClip = b->id;
            const Result trResult = editor.addTransition(sequenceId, trackId, tr);
            if (trResult.isOk()) {
                ++added;
                ++result.transitionsAdded;
            }
        }
        if (added == 0) {
            result.warnings.push_back("相邻片段无间隙，未添加转场（当前自动剪辑为无缝拼接）");
        }
    }

    const bool grouped = editor.endTransaction();
    (void)grouped;
    result.result = Result::ok();
    return result;
}

}  // namespace core
}  // namespace nairoclip
