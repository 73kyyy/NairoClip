#include "nairoclip/core/export_plan.h"

#include <algorithm>

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
    rc.hasAnimation = !clip.animation.empty();
    rc.effects = clip.effects;
    rc.animation = clip.animation;
    rc.speedCurve = clip.speedCurve;
    (void)project;
    return rc;
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
