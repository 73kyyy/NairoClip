#include "nairoclip/core/template_fill.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;

double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

/// 时长余量分：素材刚好够槽位 → 0.5；素材是槽位的 2 倍以上 → 接近 1。
///
/// 为什么"刚好够"不是满分：刚好够意味着没有裁剪余地，用户想微调时长就会
/// 触发重填。留余量是剪辑里的实际需求。
double durationSlackScore(const Time& assetDuration, const Time& slotDuration, bool isImage) {
    if (isImage) {
        return 1.0;  // 图片可无限拉长，视为余量充足
    }
    if (!slotDuration.isPositive()) {
        return 0.0;
    }
    const double need = toMicroseconds(slotDuration);
    const double have = toMicroseconds(assetDuration);
    if (have <= 0.0) return 0.0;
    const double ratio = have / need;  // >= 1（硬性过滤已保证）
    if (ratio >= 2.0) return 1.0;
    // 1.0 → 0.5，2.0 → 1.0（线性）
    return clamp01(0.5 + (ratio - 1.0) * 0.5);
}

/// 色调协调分：两个 ColorProfile 的平均色欧氏距离越小越好。
///
/// 只比较**平均色**，不比较冷暖/饱和度：后者在相邻镜头间跳变是常见的
/// 创作手法（如冷调室内切暖调夕阳），用平均色已经能抓住"明显不搭"。
double colorHarmonyScore(const ColorProfile& a, const ColorProfile& b) {
    const double dr = a.avgR - b.avgR;
    const double dg = a.avgG - b.avgG;
    const double db = a.avgB - b.avgB;
    // 归一化距离上限：黑白两点距离 = sqrt(3) ≈ 1.732
    const double dist = std::sqrt(dr * dr + dg * dg + db * db) / 1.7320508;
    return clamp01(1.0 - dist);
}

/// 主体位置适配分：槽位角色决定期望位置。
///
/// 主轴（Main）期望主体居中；前景 2/3 常为画中画小窗，主体在其中也是居中，
/// 因此两者期望相同。这个维度目前区分度有限——但接口留着，因为一旦接入
/// 人脸/目标检测（B5-3），"左摇镜头接左摇"这类判断才有意义。
double subjectFitScore(double assetSubjectX, LayerRole role) {
    (void)role;  // 当前所有角色都期望居中；保留参数以便将来按角色区分
    // subjectX ∈ [-1, 1]，居中 = 0 → 分最高
    return clamp01(1.0 - std::fabs(assetSubjectX));
}

/// 运动方向承接分：夹角越小越好（左摇接左摇）。
double motionContinuityScore(double aRad, double bRad) {
    double diff = std::fabs(aRad - bRad);
    // 归一化到 [0, π]
    while (diff > 2.0 * kPi) diff -= 2.0 * kPi;
    if (diff > kPi) diff = 2.0 * kPi - diff;
    return clamp01(1.0 - diff / kPi);
}

}  // namespace

FillWeights defaultFillWeights() { return FillWeights{}; }

FillResult fillTemplateSlots(const EditTemplate& tmpl,
                             const std::vector<FillCandidate>& candidates,
                             const FillWeights& weights) {
    FillResult result;
    result.assignments.reserve(tmpl.slots.size());

    // 每个候选被使用的次数（用于复用惩罚）
    std::vector<int> useCount(candidates.size(), 0);

    for (std::size_t si = 0; si < tmpl.slots.size(); ++si) {
        const TemplateSlot& slot = tmpl.slots[si];
        SlotAssignment best;
        best.slotIndex = si;
        best.score = -std::numeric_limits<double>::infinity();

        // 前一个已选素材（用于色调/运动承接）
        const FillCandidate* prev = nullptr;
        if (si > 0 && result.assignments[si - 1].candidateIndex >= 0) {
            prev = &candidates[static_cast<std::size_t>(result.assignments[si - 1].candidateIndex)];
        }

        bool anyDurationOk = false;
        for (std::size_t ci = 0; ci < candidates.size(); ++ci) {
            const FillCandidate& cand = candidates[ci];
            const MediaAsset& asset = cand.asset;

            // ---- 硬性过滤：时长必须够 ----
            if (!asset.isImage()) {
                if (!slot.duration.isPositive()) continue;
                if (asset.duration < slot.duration) continue;
            }
            anyDurationOk = true;

            // ---- 逐维度打分 ----
            double score = 0.0;
            score += weights.durationSlack *
                     durationSlackScore(asset.duration, slot.duration, asset.isImage());

            if (prev != nullptr && cand.features.hasColor && prev->features.hasColor) {
                score += weights.colorHarmony *
                         colorHarmonyScore(cand.features.color, prev->features.color);
            } else if (weights.colorHarmony > 0.0) {
                // 缺少数据 → 中性分 0.5，不奖不罚。
                // 关键是**所有候选都用同一个中性值**，因此不会扭曲相对排序。
                score += weights.colorHarmony * 0.5;
            }

            if (cand.features.hasSubject) {
                score += weights.subjectFit *
                         subjectFitScore(cand.features.subjectX, slot.role);
            } else if (weights.subjectFit > 0.0) {
                score += weights.subjectFit * 0.5;
            }

            if (prev != nullptr && cand.features.hasMotion && prev->features.hasMotion) {
                score += weights.motionContinuity *
                         motionContinuityScore(cand.features.motionAngleRadians,
                                              prev->features.motionAngleRadians);
            } else if (weights.motionContinuity > 0.0) {
                score += weights.motionContinuity * 0.5;
            }

            // 复用惩罚
            score -= weights.reusePenalty * static_cast<double>(useCount[ci]);

            if (score > best.score) {
                best.score = score;
                best.candidateIndex = static_cast<int>(ci);
                best.reason.clear();
            }
        }

        if (best.candidateIndex < 0) {
            best.score = 0.0;
            if (candidates.empty()) {
                best.reason = "没有候选素材";
            } else if (!anyDurationOk) {
                best.reason = "没有素材时长足够覆盖本槽位（" +
                              std::to_string(toMicroseconds(slot.duration) / 1000) + " 毫秒）";
            } else {
                best.reason = "没有可用素材";
            }
            ++result.unfilledCount;
        } else {
            ++useCount[static_cast<std::size_t>(best.candidateIndex)];
        }
        result.assignments.push_back(best);
    }

    result.complete = (result.unfilledCount == 0);
    return result;
}

FillResult fillTemplateSlots(const EditTemplate& tmpl, const std::vector<MediaAsset>& assets,
                             const FillWeights& weights) {
    std::vector<FillCandidate> candidates;
    candidates.reserve(assets.size());
    for (const auto& a : assets) {
        FillCandidate c;
        c.asset = a;
        candidates.push_back(std::move(c));
    }
    return fillTemplateSlots(tmpl, candidates, weights);
}

std::string describeAssignment(const FillResult& result, std::size_t slotIndex) {
    if (slotIndex >= result.assignments.size()) {
        return "槽位下标越界";
    }
    const SlotAssignment& a = result.assignments[slotIndex];
    if (a.candidateIndex < 0) {
        return "第 " + std::to_string(slotIndex + 1) + " 个槽位未填充：" + a.reason;
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf), "第 %zu 个槽位 → 候选 #%d（得分 %.3f）", slotIndex + 1,
                  a.candidateIndex, a.score);
    return std::string(buf);
}

}  // namespace core
}  // namespace nairoclip
