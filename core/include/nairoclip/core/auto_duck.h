#pragma once

#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/track.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 音频自动闪避（Auto Ducking）—— 本地真实信号分析
// =============================================================================
// 产品对应（文档第 2/3 部分）：
//   「🎵音频 → 自动闪避 → 开关/灵敏度/压低幅度」；
//   时间轴音乐轨显示「自动压低区间」、混音台高亮联动关系。
//
// 工作原理（全部本地，无网络）：
//   1. 解码「触发轨」（旁白/人声，通常是主视频轨或专用人声轨）的音频；
//   2. 逐帧计算 RMS 能量，与灵敏度映射的阈值比较 → 语音活动检测（VAD）；
//   3. 区间合并（hold 保留短间隙）→ 得到语音活动区间；
//   4. 对每个语音区间加 attack/release 过渡 → 生成被压低轨的增益包络；
//   5. 输出 DuckRegion 列表（供时间轴绘制、混音台高亮、导出混音应用）。
//
// 与混音器的关系：混音器在 mixInto 中查询本模块的包络（按块采样插值），
// 无需为被压低轨单独解码两遍（触发轨的 VAD 结果预先算好）。

/// 自动闪避分析配置。
struct AutoDuckConfig {
    /// VAD 帧长（采样点数；影响时间分辨率）。
    int frameSize = 1024;
    /// VAD 跳距（采样点数）。
    int hopSize = 512;
    /// 被压低轨的线性增益（由 duckAmountDb 换算；<=0 视为完全静音）。
    double targetGain = 0.25;
    /// 启动时间（秒）。
    double attackSeconds = 0.08;
    /// 恢复时间（秒）。
    double releaseSeconds = 0.35;
    /// 保留短间隙（秒）。
    double holdSeconds = 0.25;
    /// 触发阈值（RMS，由灵敏度映射；见 AutoDuckSettings::voiceThreshold）。
    double voiceThreshold = 0.11;
    /// 是否在输出区间时应用 attack/release 渐变（false = 硬切，调试/预览用）。
    bool smoothTransitions = true;
};

/// 自动闪避分析结果。
struct AutoDuckResult {
    /// 压低区间（按时间升序；已合并短间隙、应用过渡）。
    std::vector<DuckRegion> regions;
    /// 语音活动 RMS 采样（诊断/UI 波形对照）。
    std::vector<double> voiceRms;
    /// 每帧时间间隔（秒；voiceRms 用）。
    double frameSeconds = 0.0;
    /// 分析覆盖时长。
    Time analyzedDuration;
    /// 语音活动总时长（秒）。
    double voiceSeconds = 0.0;
    /// 诊断信息（如「触发轨无音频」）。
    std::string note;

    /// 是否检测到需要压低的区间。
    bool valid() const { return !regions.empty(); }
};

/// 从触发轨音频块分析语音活动并生成压低区间。
/// - blocks：触发轨的音频块（按时间升序，交错 float）；
/// - 空输入/无有效语音时返回空区间结果（note 说明原因），不抛异常。
AutoDuckResult analyzeAutoDuck(const std::vector<AudioBlock>& blocks,
                               const AutoDuckConfig& config = {});

/// 查询时刻 t 的闪避增益（0..1；1 = 不压低）。
/// - regions 为空或 t 不在任何区间 → 1.0；
/// - 区间边界内保持区间增益（区间本身已含过渡段）。
double duckGainAt(const std::vector<DuckRegion>& regions, const Time& t);

/// 由设置换算目标线性增益（duckAmountDb → 线性）。
double duckTargetGain(const AutoDuckSettings& settings);

}  // namespace core
}  // namespace nairoclip
