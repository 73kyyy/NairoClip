#include "nairoclip/core/project_doctor.h"

#include <algorithm>

namespace nairoclip {
namespace core {

std::size_t HealthReport::errorCount() const {
    std::size_t count = 0;
    for (const auto& issue : issues) {
        if (issue.level == HealthLevel::Error) {
            ++count;
        }
    }
    return count;
}

std::size_t HealthReport::warningCount() const {
    std::size_t count = 0;
    for (const auto& issue : issues) {
        if (issue.level == HealthLevel::Warning) {
            ++count;
        }
    }
    return count;
}

namespace {

void checkTrack(const Project& project, const Sequence& seq, const Track& track,
                HealthReport& report) {
    // 悬空素材引用
    for (const auto& clip : track.clips) {
        if (clip.asset.isValid() && project.findAsset(clip.asset) == nullptr) {
            HealthIssue issue;
            issue.level = HealthLevel::Error;
            issue.code = "dangling-asset";
            issue.message = "片段引用的素材不存在（轨道「" + track.name + "」）";
            issue.sequenceId = seq.id.value;
            issue.trackId = track.id.value;
            issue.clipId = clip.id.value;
            report.issues.push_back(std::move(issue));
        }
        if (clip.start.isNegative()) {
            HealthIssue issue;
            issue.level = HealthLevel::Warning;
            issue.code = "negative-start";
            issue.message = "片段起点为负（轨道「" + track.name + "」）";
            issue.trackId = track.id.value;
            issue.clipId = clip.id.value;
            report.issues.push_back(std::move(issue));
        }
        if (!clip.duration.isPositive()) {
            HealthIssue issue;
            issue.level = HealthLevel::Error;
            issue.code = "non-positive-duration";
            issue.message = "片段时长非正（轨道「" + track.name + "」）";
            issue.trackId = track.id.value;
            issue.clipId = clip.id.value;
            report.issues.push_back(std::move(issue));
        }
    }
    // 重叠检查
    for (std::size_t i = 0; i + 1 < track.clips.size(); ++i) {
        const Clip& a = track.clips[i];
        const Clip& b = track.clips[i + 1];
        if (a.start < b.start && b.start < a.end()) {
            HealthIssue issue;
            issue.level = HealthLevel::Error;
            issue.code = "overlapping-clips";
            issue.message = "片段重叠（轨道「" + track.name + "」）";
            issue.trackId = track.id.value;
            issue.clipId = b.id.value;
            report.issues.push_back(std::move(issue));
        }
    }
    // 转场引用完整性
    for (const auto& tr : track.transitions) {
        if (track.findClip(tr.fromClip) == nullptr || track.findClip(tr.toClip) == nullptr) {
            HealthIssue issue;
            issue.level = HealthLevel::Error;
            issue.code = "dangling-transition";
            issue.message = "转场引用的片段缺失（轨道「" + track.name + "」）";
            issue.trackId = track.id.value;
            report.issues.push_back(std::move(issue));
        }
        if (!tr.duration.isPositive()) {
            HealthIssue issue;
            issue.level = HealthLevel::Error;
            issue.code = "invalid-transition-duration";
            issue.message = "转场时长非正（轨道「" + track.name + "」）";
            issue.trackId = track.id.value;
            report.issues.push_back(std::move(issue));
        }
    }
}

}  // namespace

HealthReport checkProjectHealth(const Project& project) {
    HealthReport report;
    if (project.sequences.empty()) {
        HealthIssue issue;
        issue.level = HealthLevel::Warning;
        issue.code = "no-sequences";
        issue.message = "工程没有任何序列";
        report.issues.push_back(std::move(issue));
    }
    for (const auto& seq : project.sequences) {
        for (const auto& track : seq.tracks) {
            checkTrack(project, seq, track, report);
        }
        if (!seq.subtitles.invariantsHold()) {
            HealthIssue issue;
            issue.level = HealthLevel::Error;
            issue.code = "subtitle-invariant";
            issue.message = "字幕轨不变量被破坏（序列「" + seq.name + "」）";
            issue.sequenceId = seq.id.value;
            report.issues.push_back(std::move(issue));
        }
    }
    return report;
}

RepairReport repairProject(Project& project) {
    RepairReport report;

    // 1) 悬空素材引用 → 禁用片段
    std::size_t disabled = 0;
    for (auto& seq : project.sequences) {
        for (auto& track : seq.tracks) {
            for (auto& clip : track.clips) {
                if (clip.asset.isValid() && project.findAsset(clip.asset) == nullptr &&
                    clip.enabled) {
                    clip.enabled = false;
                    ++disabled;
                }
            }
        }
    }
    if (disabled > 0) {
        report.actions.push_back(
            {"disable-dangling", "禁用引用缺失素材的片段（保留数据，可重新链接）", disabled});
    }

    // 2) 逐轨修复：负起点钳制、非正时长删除、重叠截断、悬空转场删除
    std::size_t clampedStart = 0;
    std::size_t removedClips = 0;
    std::size_t truncated = 0;
    std::size_t removedTransitions = 0;
    for (auto& seq : project.sequences) {
        for (auto& track : seq.tracks) {
            // 非正时长：删除
            const auto before = track.clips.size();
            track.clips.erase(
                std::remove_if(track.clips.begin(), track.clips.end(),
                               [&](const Clip& c) {
                                   if (!c.duration.isPositive()) {
                                       return true;
                                   }
                                   return false;
                               }),
                track.clips.end());
            removedClips += before - track.clips.size();
            // 负起点：钳制
            for (auto& clip : track.clips) {
                if (clip.start.isNegative()) {
                    clip.start = Rational::make(0);
                    ++clampedStart;
                }
            }
            // 排序，然后截断重叠
            track.sortClips();
            for (std::size_t i = 0; i + 1 < track.clips.size(); ++i) {
                Clip& a = track.clips[i];
                Clip& b = track.clips[i + 1];
                if (b.start < a.end()) {
                    const Time newDuration = b.start - b.start;  // = 0 → 删除 b 前检查
                    (void)newDuration;
                    // b 与 a 重叠：把 b 的起点移到 a.end()（保持时长）或截断 a？
                    // 保守策略：将 b 整体后移，保持所有内容
                    b.start = a.end();
                    ++truncated;
                }
            }
            // 悬空转场：删除
            const auto trBefore = track.transitions.size();
            track.transitions.erase(
                std::remove_if(track.transitions.begin(), track.transitions.end(),
                               [&](const Transition& tr) {
                                   return track.findClip(tr.fromClip) == nullptr ||
                                          track.findClip(tr.toClip) == nullptr ||
                                          !tr.duration.isPositive();
                               }),
                track.transitions.end());
            removedTransitions += trBefore - track.transitions.size();
        }
    }
    if (clampedStart > 0) {
        report.actions.push_back({"clamp-start", "将负起点片段钳制到 0", clampedStart});
    }
    if (removedClips > 0) {
        report.actions.push_back({"remove-invalid", "删除时长非法的片段", removedClips});
    }
    if (truncated > 0) {
        report.actions.push_back({"shift-overlap", "将重叠片段后移至不重叠", truncated});
    }
    if (removedTransitions > 0) {
        report.actions.push_back({"remove-dangling-transition", "删除悬空转场", removedTransitions});
    }

    // 3) ID 分配器推进
    std::uint64_t maxId = 0;
    for (const auto& asset : project.assets) {
        maxId = std::max(maxId, asset.id.value);
    }
    for (const auto& seq : project.sequences) {
        maxId = std::max(maxId, seq.id.value);
        for (const auto& track : seq.tracks) {
            maxId = std::max(maxId, track.id.value);
            for (const auto& clip : track.clips) {
                maxId = std::max(maxId, clip.id.value);
            }
            for (const auto& tr : track.transitions) {
                maxId = std::max(maxId, tr.id.value);
            }
        }
        for (const auto& marker : seq.markers) {
            maxId = std::max(maxId, marker.id.value);
        }
        for (const auto& cue : seq.subtitles.cues()) {
            maxId = std::max(maxId, cue.id.value);
        }
    }
    if (maxId > project.ids.peek()) {
        project.ids.restore(maxId);
        report.actions.push_back({"advance-id-allocator", "推进 ID 分配器避免冲突", 1});
    }

    report.after = checkProjectHealth(project);
    return report;
}

}  // namespace core
}  // namespace nairoclip
