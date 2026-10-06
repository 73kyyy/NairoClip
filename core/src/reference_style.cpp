#include "nairoclip/core/reference_style.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

/// 计算升序序列的分位数（线性插值）。
Time percentile(const std::vector<Time>& sorted, double p) {
    if (sorted.empty()) return Time{};
    if (sorted.size() == 1) return sorted[0];
    const double index = p * static_cast<double>(sorted.size() - 1);
    const std::size_t lower = static_cast<std::size_t>(std::floor(index));
    const std::size_t upper = static_cast<std::size_t>(std::ceil(index));
    if (lower == upper) return sorted[lower];
    const double frac = index - static_cast<double>(lower);
    // 千分位精度的有理插值（足以表达分位数场景）
    return sorted[lower] +
           Time{(sorted[upper] - sorted[lower]) *
                Rational::make(static_cast<std::int64_t>(frac * 1000.0 + 0.5), 1000)};
}

}  // namespace

RhythmProfile analyzeRhythm(const std::vector<ShotBoundary>& shots) {
    RhythmProfile profile;
    std::vector<Time> durations;
    for (const auto& shot : shots) {
        if (!shot.start.isNegative() && shot.end > shot.start) {
            durations.push_back(shot.end - shot.start);
            profile.totalDuration = profile.totalDuration + (shot.end - shot.start);
        }
    }
    if (durations.empty()) {
        return profile;
    }
    profile.shotCount = durations.size();
    std::sort(durations.begin(), durations.end());
    profile.shortestShot = durations.front();
    profile.longestShot = durations.back();

    // 平均
    Time sum;
    for (const auto& d : durations) {
        sum = sum + d;
    }
    profile.averageShotDuration =
        Time{sum * Rational::make(1, static_cast<std::int64_t>(durations.size()))};

    // 分位数
    profile.p25 = percentile(durations, 0.25);
    profile.median = percentile(durations, 0.5);
    profile.p75 = percentile(durations, 0.75);

    // 快切占比（< 1s）
    std::size_t fast = 0;
    for (const auto& d : durations) {
        if (d < Rational::make(1)) {
            ++fast;
        }
    }
    profile.fastCutRatio = static_cast<double>(fast) / static_cast<double>(durations.size());

    // 剪辑密度
    const double totalSec = profile.totalDuration.toDouble();
    profile.cutsPer10s =
        totalSec > 0.0 ? static_cast<double>(durations.size()) * 10.0 / totalSec : 0.0;
    return profile;
}

TransitionProfile analyzeTransitions(const std::vector<std::uint32_t>& typeCounts,
                                     const Time& averageDuration, std::size_t hardCuts,
                                     std::size_t totalPairs) {
    TransitionProfile profile;
    profile.typeCounts = typeCounts;
    profile.averageDuration = averageDuration;
    if (totalPairs > 0) {
        profile.hardCutRatio = static_cast<double>(hardCuts) / static_cast<double>(totalPairs);
    }
    return profile;
}

ColorProfile analyzeColor(double avgR, double avgG, double avgB, double avgSaturation,
                          double avgContrast) {
    ColorProfile profile;
    profile.avgR = std::max(0.0, std::min(1.0, avgR));
    profile.avgG = std::max(0.0, std::min(1.0, avgG));
    profile.avgB = std::max(0.0, std::min(1.0, avgB));
    profile.avgSaturation = std::max(0.0, std::min(1.0, avgSaturation));
    profile.avgContrast = std::max(0.0, std::min(1.0, avgContrast));
    // 暖冷：R - B 的差值（归一化到 [-1,1]）
    profile.warmth = std::max(-1.0, std::min(1.0, profile.avgR - profile.avgB));
    return profile;
}

SubtitleStyleProfile analyzeSubtitles(const std::vector<std::size_t>& charCounts,
                                      std::size_t cueCount, const Time& duration) {
    SubtitleStyleProfile profile;
    profile.present = cueCount > 0;
    if (charCounts.empty()) {
        return profile;
    }
    std::size_t total = 0;
    for (const auto count : charCounts) {
        total += count;
    }
    profile.averageCharsPerCue =
        static_cast<double>(total) / static_cast<double>(charCounts.size());
    const double durationSec = duration.toDouble();
    if (durationSec > 0.0) {
        profile.cuesPer10s = static_cast<double>(cueCount) * 10.0 / durationSec;
    }
    return profile;
}

std::vector<CutSuggestion> suggestCuts(const RhythmProfile& target,
                                       const std::vector<MaterialSegment>& material) {
    std::vector<CutSuggestion> result;
    if (!target.valid() || material.empty()) {
        return result;
    }
    // 每个目标镜头分配一段素材：素材按顺序循环使用，
    // 每段从素材内依次切割（记录消耗游标）。
    std::vector<Time> cursors(material.size());
    std::size_t materialIndex = 0;
    // 目标镜头时长：用中位数作为典型时长（快切比例高时向 p25 靠拢）。
    Time typical = target.median;
    if (target.fastCutRatio > 0.5) {
        typical = Time{(target.median + target.p25) * Rational::make(1, 2)};
    }
    if (!typical.isPositive()) {
        typical = Rational::make(1);
    }
    for (std::size_t i = 0; i < target.shotCount; ++i) {
        // 找下一个有剩余容量的素材
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
            // 剩余不足一个典型时长：用剩余收尾
            const Time use = remaining < typical ? remaining : typical;
            suggestion.duration = use;
            cursors[idx] = cursors[idx] + use;
            result.push_back(suggestion);
            materialIndex = (idx + 1) % material.size();
            placed = true;
            break;
        }
        if (!placed) {
            break;  // 素材耗尽
        }
    }
    return result;
}

}  // namespace core
}  // namespace nairoclip
