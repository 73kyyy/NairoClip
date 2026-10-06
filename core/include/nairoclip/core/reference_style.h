#pragma once

#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 参考风格分析（Reference Style Analysis）
// =============================================================================
// 产品定位说明（合规相关）：
//  本模块从参考素材中提取「风格特征」（节奏/转场偏好/色调/字幕版式），
//  用于对用户自己的素材进行「参考风格生成剪辑」。
//  ★ 对外表述红线：只能宣传「参考风格生成」，不可宣传「复刻同款视频」。
//  ★ 本模块输出的是统计特征（时长分布、转场类型频次、主色调），
//    不复制参考素材的任何画面、音频或文字内容。

/// 镜头边界（由场景检测器输出；本层只消费结果）。
struct ShotBoundary {
    Time start;
    Time end;
    double motionScore = 0.0;   // 镜头运动强度 0..1（可选）
    double brightness = 0.5;    // 平均亮度 0..1（可选）
};

/// 节奏画像：从镜头序列统计而来。
struct RhythmProfile {
    std::size_t shotCount = 0;
    Time totalDuration;
    Time averageShotDuration;   // 平均镜头时长
    Time shortestShot;          // 最短镜头
    Time longestShot;           // 最长镜头
    /// 快节奏镜头占比（时长 < 1s）。
    double fastCutRatio = 0.0;
    /// 镜头时长的分位数（25/50/75）。
    Time p25;
    Time median;
    Time p75;
    /// 每 10 秒平均镜头数（剪辑密度）。
    double cutsPer10s = 0.0;

    bool valid() const { return shotCount > 0 && totalDuration.isPositive(); }
};

/// 转场偏好画像。
struct TransitionProfile {
    /// 各类型使用频次（下标 = TransitionType 枚举值）。
    std::vector<std::uint32_t> typeCounts;
    /// 平均转场时长。
    Time averageDuration;
    /// 硬切占比（无转场镜头对 / 总镜头对）。
    double hardCutRatio = 1.0;
};

/// 色调画像（简化：主色直方图统计）。
struct ColorProfile {
    /// 平均色（0..1 归一化）。
    double avgR = 0.5;
    double avgG = 0.5;
    double avgB = 0.5;
    /// 平均饱和度与对比度（0..1）。
    double avgSaturation = 0.5;
    double avgContrast = 0.5;
    /// 暖冷倾向：-1 冷 .. +1 暖。
    double warmth = 0.0;
};

/// 字幕版式画像。
struct SubtitleStyleProfile {
    /// 是否出现字幕。
    bool present = false;
    /// 平均每屏字数（估算）。
    double averageCharsPerCue = 0.0;
    /// 字幕出现频率：每 10 秒条数。
    double cuesPer10s = 0.0;
    /// 文字相对画布高度（估算，0..1）。
    double relativeFontSize = 0.06;
};

/// 参考风格总画像。
struct StyleProfile {
    RhythmProfile rhythm;
    TransitionProfile transitions;
    ColorProfile color;
    SubtitleStyleProfile subtitles;
    /// 分析来源备注（如文件名），用于 UI 展示与审计。
    std::string sourceNote;

    bool valid() const { return rhythm.valid(); }
};

/// 从镜头边界序列统计节奏画像。
/// 边界需按 start 升序；不合法（重叠/负值）的边界被跳过。
RhythmProfile analyzeRhythm(const std::vector<ShotBoundary>& shots);

/// 从镜头序列与转场使用记录统计转场画像。
/// typeCounts：每种类型的次数（下标 = 枚举值）。
/// hardCuts：无转场镜头对数量；totalPairs：总镜头对数。
TransitionProfile analyzeTransitions(const std::vector<std::uint32_t>& typeCounts,
                                     const Time& averageDuration, std::size_t hardCuts,
                                     std::size_t totalPairs);

/// 从平均色/饱和度/对比度样本统计色调画像。
/// RGB 输入为 0..1。
ColorProfile analyzeColor(double avgR, double avgG, double avgB, double avgSaturation,
                          double avgContrast);

/// 从字幕文本与时长统计字幕版式画像。
/// charCounts：每条字幕字数；cueCount：字幕条数；duration：素材总时长。
SubtitleStyleProfile analyzeSubtitles(const std::vector<std::size_t>& charCounts,
                                      std::size_t cueCount, const Time& duration);

/// ---------------------------------------------------------------------------
/// 风格应用：把目标节奏对齐到用户素材。
/// ---------------------------------------------------------------------------

/// 一段用户素材（可用区间）。
struct MaterialSegment {
    AssetId asset;
    Time usableDuration;  // 可用时长
};

/// 剪辑建议：按目标节奏把用户素材切成 N 个片段。
struct CutSuggestion {
    AssetId asset;
    Time start;      // 素材内起点
    Time duration;   // 建议时长
    std::size_t targetIndex = 0;  // 对应目标节奏的第几刀
};

/// 依据风格画像的节奏，为用户素材生成剪辑建议。
/// 策略：
///  - 目标节奏：shots 的时长序列（取 median 附近的典型时长）；
///  - 用户素材循环取用，直到覆盖目标总时长（或素材耗尽）；
///  - 素材不足以填满一个镜头时长时，用剩余时长收尾。
/// 输出片段总数 = 目标节奏的镜头数（素材充足时）。
std::vector<CutSuggestion> suggestCuts(const RhythmProfile& target,
                                       const std::vector<MaterialSegment>& material);

/// ---------------------------------------------------------------------------
/// 风格 → 特效映射（把画像翻译为具体的调色特效参数）。
/// ---------------------------------------------------------------------------

/// 由色调画像生成的建议特效参数。
struct SuggestedColorEffects {
    /// 亮度 [-1,1]（0 = 不变）。
    double brightness = 0.0;
    /// 对比度 [0,3]（1 = 不变）。
    double contrast = 1.0;
    /// 饱和度 [0,3]（1 = 不变）。
    double saturation = 1.0;
    /// 色温 [-1,1]（0 = 不变）。
    double temperature = 0.0;

    /// 是否全部为中性值（无调整）。
    bool neutral() const {
        return brightness == 0.0 && contrast == 1.0 && saturation == 1.0 &&
               temperature == 0.0;
    }
};

/// 把色调画像映射为建议特效参数。
/// 映射策略（简化、可解释）：
///  - 饱和度：向目标平均饱和度靠拢（差异 × 0.5 增益）；
///  - 对比度：向目标平均对比度靠拢（差异 × 0.5 增益）；
///  - 色温：按 warmth 的 60% 施加；
///  - 亮度：中性（避免过曝，交由用户手动微调）。
SuggestedColorEffects suggestColorEffects(const ColorProfile& profile);

/// 由节奏画像生成建议的剪辑密度描述（供 UI 文案展示）。
/// 例：「快节奏（平均 0.8 秒/镜头，每 10 秒 12 刀）」。
std::string describeRhythm(const RhythmProfile& profile);

}  // namespace core
}  // namespace nairoclip
