#include "nairoclip/core/export_plan.h"

#include <algorithm>
#include <set>

namespace nairoclip {
namespace core {

namespace {

RenderClip compileClip(const Clip& clip, const Project& project) {
    RenderClip rc;
    rc.clipId = clip.id;
    rc.asset = clip.asset;
    if (const MediaAsset* asset = project.findAsset(clip.asset)) {
        rc.assetPath = asset->path;
    }
    rc.timelineStart = clip.start;
    rc.timelineEnd = clip.end();
    rc.sourceIn = clip.sourceIn;
    rc.sourceOut = clip.sourceOut();
    rc.speed = clip.speed;
    rc.reversed = clip.reversed;
    rc.hasAnimation = !clip.animation.empty();
    rc.effects = clip.effects;
    rc.hasMask = clip.hasMask;
    rc.mask = clip.mask;
    rc.hasChromaKey = clip.hasChromaKey;
    rc.chromaKey = clip.chromaKey;
    rc.blendMode = clip.blendMode;
    rc.animation = clip.animation;
    rc.speedCurve = clip.speedCurve;
    (void)project;
    return rc;
}

/// 展开复合片段：把嵌套序列的内层视频片段平移到外层时间轴区间。
/// - 递归展开（嵌套复合片段也支持），depth 限制防止循环引用导致栈溢出；
/// - visited 记录已进入的序列，阻断 A→B→A 的循环（防御性）；
/// - 内层片段的本地时间 [0, duration) 映射到外层 [compound.start, +duration)。
void flattenCompoundClip(const Clip& compound, const Project& project, std::vector<RenderClip>& out,
                         std::set<std::uint64_t>& visited, int depth, std::vector<std::string>& warnings) {
    if (depth > 8) {
        warnings.push_back("复合片段嵌套层级过深（>8），已截断展开");
        return;
    }
    if (visited.count(compound.nestedSequence.value) > 0) {
        warnings.push_back("复合片段存在循环引用（序列 ID " +
                           std::to_string(compound.nestedSequence.value) + "），已跳过");
        return;
    }
    const Sequence* nested = project.findSequence(compound.nestedSequence);
    if (nested == nullptr) {
        warnings.push_back("复合片段引用的嵌套序列不存在（片段 ID " +
                           std::to_string(compound.id.value) + "）");
        return;
    }
    visited.insert(compound.nestedSequence.value);
    for (const auto& nestedTrack : nested->tracks) {
        // 仅展开视频类轨道内容（音频/特效轨由音频混音路径与效果栈处理）
        if (nestedTrack.hidden || nestedTrack.type != TrackType::Video) {
            continue;
        }
        for (const auto& nestedClip : nestedTrack.clips) {
            if (!nestedClip.enabled) {
                continue;
            }
            if (nestedClip.isCompound()) {
                // 嵌套的复合片段：先平移到外层坐标，再递归展开
                Clip shifted = nestedClip;
                shifted.start = compound.start + nestedClip.start;
                flattenCompoundClip(shifted, project, out, visited, depth + 1, warnings);
                continue;
            }
            if (!nestedClip.asset.isValid()) {
                continue;  // 无素材片段（如文字片段）暂不参与像素渲染
            }
            RenderClip expanded = compileClip(nestedClip, project);
            // 时间映射：内层 [0, x) → 外层 [compound.start, compound.start + x)
            const Time offset = compound.start;
            expanded.timelineStart = expanded.timelineStart + offset;
            expanded.timelineEnd = expanded.timelineEnd + offset;
            expanded.fromCompound = compound.id;
            out.push_back(std::move(expanded));
        }
    }
    visited.erase(compound.nestedSequence.value);
}

}  // namespace

ExportPlan compileExportPlan(const Project& project, const Sequence& sequence) {
    ExportPlan plan;
    plan.sequenceId = sequence.id;
    plan.frameRate = sequence.frameRate;
    plan.width = sequence.width;
    plan.height = sequence.height;
    plan.duration = sequence.duration();
    plan.frameFit = sequence.frameFit;
    plan.backgroundRgba = sequence.backgroundRgba;
    // 智能重构图轨道（序列级设置；空 = 未启用）
    plan.reframe = sequence.reframe;

    // 循环引用阻断用（跨轨道共享；每次访问退出后移除，允许同一嵌套序列被多处引用）
    std::set<std::uint64_t> visitedSequences;

    for (const auto& track : sequence.tracks) {
        RenderTrack rt;
        rt.trackId = track.id;
        rt.type = track.type;
        rt.muted = track.muted;
        rt.hidden = track.hidden;
        // 隐藏视频轨 / 静音音频轨：仍保留轨道（渲染器决定处理策略），
        // 但记录警告便于 UI 提示。
        if (track.hidden && track.type == TrackType::Video) {
            plan.warnings.push_back("视频轨「" + track.name + "」处于隐藏状态，导出时将被跳过");
        }
        if (track.muted && track.type == TrackType::Audio) {
            plan.warnings.push_back("音频轨「" + track.name + "」处于静音状态，导出时将被跳过");
        }
        for (const auto& clip : track.clips) {
            if (!clip.enabled) {
                continue;  // 已禁用片段不进入导出
            }
            // 复合片段：展开嵌套序列内层内容（真实渲染内部剪辑）
            if (clip.isCompound()) {
                flattenCompoundClip(clip, project, rt.clips, visitedSequences, 0, plan.warnings);
                continue;
            }
            if (clip.asset.isValid()) {
                const MediaAsset* asset = project.findAsset(clip.asset);
                if (asset == nullptr) {
                    plan.warnings.push_back("片段引用的素材不存在（片段 ID " +
                                            std::to_string(clip.id.value) + "）");
                    continue;
                }
                if (asset->offline) {
                    plan.warnings.push_back("素材「" + asset->name + "」已离线");
                }
            }
            rt.clips.push_back(compileClip(clip, project));
        }
        // 展开后按时间排序（复合片段内容与相邻片段可能交错）
        std::sort(rt.clips.begin(), rt.clips.end(), [](const RenderClip& a, const RenderClip& b) {
            if (a.timelineStart == b.timelineStart) {
                return a.clipId.value < b.clipId.value;  // 稳定顺序（确定性导出）
            }
            return a.timelineStart < b.timelineStart;
        });
        // 编译转场（仅视频轨）：start = fromClip.end(), end = start + duration
        if (track.type == TrackType::Video) {
            for (const auto& tr : track.transitions) {
                const Clip* from = track.findClip(tr.fromClip);
                if (from == nullptr) {
                    continue;  // 悬空引用：跳过并警告
                }
                RenderTransition compiled;
                compiled.transitionId = tr.id;
                compiled.type = tr.type;
                compiled.start = from->end();
                compiled.end = compiled.start + tr.duration;
                compiled.fromClip = tr.fromClip;
                compiled.toClip = tr.toClip;
                rt.transitions.push_back(std::move(compiled));
            }
            std::sort(rt.transitions.begin(), rt.transitions.end(),
                      [](const RenderTransition& a, const RenderTransition& b) {
                          return a.start < b.start;
                      });
        }
        plan.tracks.push_back(std::move(rt));
    }
    return plan;
}

bool exportPlanValid(const ExportPlan& plan) {
    if (plan.width <= 0 || plan.height <= 0) {
        return false;
    }
    if (!plan.frameRate.value.isPositive()) {
        return false;
    }
    if (plan.duration.isNegative()) {
        return false;
    }
    for (const auto& track : plan.tracks) {
        Time previousEnd;
        bool first = true;
        for (const auto& clip : track.clips) {
            if (clip.timelineStart.isNegative() || clip.sourceIn.isNegative()) {
                return false;
            }
            if (!(clip.timelineEnd > clip.timelineStart)) {
                return false;
            }
            if (!(clip.sourceOut > clip.sourceIn)) {
                return false;
            }
            if (!(clip.speed.isPositive())) {
                return false;
            }
            if (!first && clip.timelineStart < previousEnd) {
                return false;  // 重叠
            }
            previousEnd = clip.timelineEnd;
            first = false;
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
