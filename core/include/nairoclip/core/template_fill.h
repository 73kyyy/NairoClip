#pragma once
// =============================================================================
// 智能填充算法（B2-2）
// =============================================================================
// 给一个模板 + 一批候选素材，把素材分配到槽位。**不是随机塞**，而是打分匹配。
//
// ## 关于「画质特征」的能力边界（重要，先读这段）
// 文档列了四个打分维度，但它们需要的数据**本库目前提供不了**：
//
// | 维度 | 需要的数据 | 现状 |
// |---|---|---|
// | 时长够不够槽位 | `MediaAsset::duration` | ✅ 已有，硬性过滤 |
// | 色调与相邻槽位协调 | 素材平均色 | ⚠️ 需调用方先分析（`AssetFeatures::color`）|
// | 主体位置适配 | 主体在画面的 x/y | ❌ 无任何分析能力 |
// | 运动方向承接 | 运镜方向 | ❌ 无任何分析能力 |
//
// 因此本模块的接口设计是：**特征由调用方提供**（`AssetFeatures`），
// 不假装能自己算出来。若调用方不填（默认值），对应维度按"中性"处理
// （不加分也不减分），而不是伪造一个看似合理的分数。
//
// 为什么不用色温直接猜：`MediaAsset` 里没有像素数据，导入时不分析。
// 硬要在填充时解码视频会引入 FFmpeg 依赖，且对 100 个素材要解码 100 次，
// 在 UI 交互路径上不可接受。正确做法是在**导入时**分析一次并缓存
// （`AssetFeatures` 就是那个缓存载体）。
//
// ## 打分公式
//   总分 = 时长余量分 × w1
//        + 色调协调分 × w2（与前一个已选素材比）
//        + 主体位置适配分 × w3（与槽位角色比）
//        + 运动方向承接分 × w4（与前一个已选素材比）
//        - 复用惩罚 × w5（同素材用过几次）
//
// 权重可配（`FillWeights`），默认值见 `defaultFillWeights()`。
// 所有分项归一化到 0..1，总分为加权和再减惩罚，因此**可比较、可解释**。

#include "nairoclip/core/edit_template.h"
#include "nairoclip/core/media.h"
#include "nairoclip/core/reference_style.h"

#include <cstddef>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 素材的画质特征（由调用方在导入/分析阶段填充）。
///
/// 全部字段都有"中性默认值"：不填时该维度不加分不减分。
struct AssetFeatures {
    /// 平均色画像；`hasColor == false` 时忽略。
    ColorProfile color;
    bool hasColor = false;

    /// 主体位置：-1（画面左侧）.. +1（右侧）；0 = 居中。
    /// `hasSubject == false` 时忽略。
    double subjectX = 0.0;
    bool hasSubject = false;

    /// 运动方向（弧度，0 = 向右，π/2 = 向下）；`hasMotion == false` 时忽略。
    double motionAngleRadians = 0.0;
    bool hasMotion = false;

    bool any() const { return hasColor || hasSubject || hasMotion; }
};

/// 素材 + 其可选特征。
struct FillCandidate {
    MediaAsset asset;
    AssetFeatures features;
};

/// 打分权重。全部为 0 时退化为"只按素材原顺序填"。
struct FillWeights {
    /// 时长余量：素材越长于槽位需求，分越高（"够用且有余地"）。
    double durationSlack = 1.0;
    /// 色调协调（与前一个已选素材的色温差越小越好）。
    double colorHarmony = 2.0;
    /// 主体位置与槽位角色匹配。
    double subjectFit = 1.5;
    /// 运动方向承接（与前一个已选素材方向夹角越小越好）。
    double motionContinuity = 1.5;
    /// 复用惩罚：同一素材每被用过一次扣多少。
    double reusePenalty = 0.8;
};

/// 默认权重。
FillWeights defaultFillWeights();

/// 单个槽位的填充结果。
struct SlotAssignment {
    std::size_t slotIndex = 0;
    /// 选中的候选下标；`-1` = 没有可用素材。
    int candidateIndex = -1;
    /// 该选择的总分（用于调试/UI 解释）。
    double score = 0.0;
    /// 若为 -1，说明为什么填不上。
    std::string reason;
};

/// 填充结果。
struct FillResult {
    std::vector<SlotAssignment> assignments;
    /// 是否所有槽位都填上了。
    bool complete = false;
    /// 未填上的槽位数量。
    std::size_t unfilledCount = 0;
};

/// 执行填充。
///
/// 规则：
/// 1. **硬性过滤**：素材时长 < 槽位时长 → 直接排除（图片视为可无限拉长，
///    不受此限）；
/// 2. 逐个槽位独立评分选最优（贪心）。不做全局最优——槽位之间通过
///    "与前一个已选素材比"耦合，贪心已能保证相邻协调，且 O(S×C) 而非
///    指数级，UI 交互可接受；
/// 3. 素材可以复用（短素材集也能填满），但每次复用都扣分，因此
///    "够用时不会重复"、"不够时才复用"是自然结果，不需要额外开关。
FillResult fillTemplateSlots(const EditTemplate& tmpl, const std::vector<FillCandidate>& candidates,
                             const FillWeights& weights = defaultFillWeights());

/// 便捷重载：不带特征（只按时长 + 原顺序 + 复用惩罚）。
FillResult fillTemplateSlots(const EditTemplate& tmpl,
                             const std::vector<MediaAsset>& assets,
                             const FillWeights& weights = defaultFillWeights());

/// 说明文案（供 UI 展示"为什么选这个素材"）。
std::string describeAssignment(const FillResult& result, std::size_t slotIndex);

}  // namespace core
}  // namespace nairoclip
