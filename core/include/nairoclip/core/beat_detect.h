#pragma once

#include "nairoclip/core/media_io.h"
#include "nairoclip/core/reference_style.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 节拍检测（Beat / Tempo Detection）—— 「音乐卡点」的算法层
// =============================================================================
// 用途（对应产品需求）：
//   - 音频 → 节拍 → AI 自动打点（本地算法，不上传音频）；
//   - 「音乐卡点」自动剪辑：把镜头切换对齐到音乐节拍；
//   - 时间轴节拍标记展示与手动微调基准。
//
// 算法（业界标准流程的精简实现，全部本地、可解释）：
//   1. 分帧能量包络（帧长/跳距可配，默认 1024/512 @ 实际采样率）；
//   2. 多频带（低/中/高）能量差分 → 半波整流 → onset 强度包络；
//   3. 自适应阈值 + 局部峰值检测 → onset（起音点）时间序列；
//   4. onset 强度包络自相关 → 主导周期 → BPM（限制在 [minBpm, maxBpm]）；
//   5. 以最强 onset 为相位基准生成节拍网格，并把每个节拍吸附到最近的 onset 峰值；
//   6. 输出置信度（自相关峰值 / 包络能量），供 UI 提示「检测质量」。
//
// 合规：全部计算在设备本地完成，不采集、不上传音频内容（见隐私政策 3.5）。

/// 检测配置。
struct BeatDetectConfig {
    /// 帧长（采样点数）。
    int frameSize = 1024;
    /// 跳距（采样点数；越大越快但时间分辨率越低）。
    int hopSize = 512;
    /// BPM 搜索下限。
    double minBpm = 60.0;
    /// BPM 搜索上限。
    double maxBpm = 200.0;
    /// 峰值检测灵敏度（>1 更保守，<1 更敏感；典型 1.0-1.5）。
    double sensitivity = 1.0;
    /// 峰值检测的自适应窗口秒数（局部均值范围）。
    double adaptiveWindowSeconds = 0.35;
    /// 节拍吸附范围（秒；每个节拍在 ±该范围内找最强 onset 对齐；0 = 不吸附）。
    double snapRadiusSeconds = 0.07;
    /// 最多输出的节拍数（防止长音频产生超大列表；0 = 不限制）。
    int maxBeats = 2000;
};

/// onset（起音点）。
struct Onset {
    Time time;          // 时间轴时间
    double strength = 0.0;  // 强度（0..1 归一化）
};

/// 节拍检测结果。
struct BeatAnalysis {
    /// 估计的每分钟节拍数（未检测出时 <= 0）。
    double bpm = 0.0;
    /// 节拍时间点（按时间升序；含吸附后的位置）。
    std::vector<Onset> beats;
    /// 全部 onset（诊断/手动打点参考）。
    std::vector<Onset> onsets;
    /// 置信度 [0,1]（自相关主峰显著性；< 0.2 视为不可靠）。
    double confidence = 0.0;
    /// 分析覆盖的时长。
    Time analyzedDuration;
    /// 使用的时间偏移（首块 start；节拍时间为绝对时间）。
    Time origin;
    /// 诊断信息（如「音频过短」「无有效音频」）。
    std::string note;

    /// 是否可用（有 BPM 且有节拍）。
    bool valid() const { return bpm > 0.0 && !beats.empty(); }
};

/// 从音频块序列检测节拍。
/// - blocks：按时间升序的音频块（交错 float；采样率/声道以首块为准）；
/// - 空输入/音频过短时返回 invalid 结果（note 说明原因），不抛异常。
BeatAnalysis analyzeBeats(const std::vector<AudioBlock>& blocks,
                          const BeatDetectConfig& config = {});

/// 生成节拍网格（不依赖 onset 检测：用于「固定 BPM 手动打点」路径）。
/// - startTime：网格起点；duration：总时长；bpm：节拍速度；
/// - 返回节拍时间点（不含末尾越界点）。
std::vector<Time> buildBeatGrid(const Time& startTime, const Time& duration, double bpm);

/// 把镜头切换点吸附到节拍（音乐卡点：镜头对齐到节拍）。
/// - cutPoints：候选切换时间（升序）；
/// - beats：节拍时间（升序）；
/// - radius：最大吸附距离（秒）。
/// 返回吸附后的切换时间（保持升序；每个切点吸附到最近的节拍，超出半径保持原值）。
std::vector<Time> snapCutsToBeats(const std::vector<Time>& cutPoints,
                                  const std::vector<Time>& beats, double radiusSeconds);

/// ---------------------------------------------------------------------------
/// 音乐卡点（Beat Sync Edit）：把镜头切点对齐到音乐节拍
/// ---------------------------------------------------------------------------
/// 产品对应：「AI成片 → 音乐卡点」与「🎵音频 → 节拍 → AI自动打点」。
/// 输入：用户导入的本地音乐（节拍分析结果）+ 用户素材；
/// 输出：按节拍切分的剪辑建议（可直接交给 applyAutoEdit / 手动落盘）。
/// 合规：只分析用户本机已导入的音乐（不内置曲库、不上传音频）。

/// 音乐卡点配置。
struct BeatSyncOptions {
    /// 是否优先对齐到强拍（能量更高的节拍），false 时所有节拍等价。
    bool preferStrongBeats = true;
    /// 强拍阈值（相对最大强度；0..1）。
    double strongBeatThreshold = 0.35;
    /// 卡片段最小长度（秒）；过短的节拍间隔会被跳过以避免碎片化。
    double minShotSeconds = 0.25;
    /// 卡片段最大长度（秒）；超过则按素材自然长度切分（0 = 不限制）。
    double maxShotSeconds = 8.0;
    /// 是否在节拍点插入转场（由调用方决定是否应用）。
    bool markTransitions = true;
};

/// 音乐卡点结果：按节拍切好的片段建议。
struct BeatSyncPlan {
    /// 片段建议（时间轴顺序；每段的 duration 对应一个节拍区间）。
    std::vector<CutSuggestion> cuts;
    /// 实际使用的节拍数（可能因 minShotSeconds 过滤而少于 beats）。
    std::size_t beatsUsed = 0;
    /// 使用的 BPM（来自分析结果）。
    double bpm = 0.0;
    std::vector<std::string> warnings;
    Result result;
};

/// 依音乐节拍为用户素材生成剪辑建议（「音乐卡点」核心）。
/// - analysis：analyzeBeats 的结果（需 valid()，否则返回失败）；
/// - material：用户素材（按顺序循环取用直至覆盖全部节拍区间）；
/// - 每个节拍区间生成一个片段建议；素材不足以覆盖时用剩余长度收尾并给出警告。
BeatSyncPlan planBeatSync(const BeatAnalysis& analysis,
                          const std::vector<MaterialSegment>& material,
                          const BeatSyncOptions& options = {});

}  // namespace core
}  // namespace nairoclip
