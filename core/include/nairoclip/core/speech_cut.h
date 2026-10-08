#pragma once

#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/track.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 口播精剪（Speech Cut）—— 文档第 7 部分 7.2「AI成片 → 口播精剪」
// =============================================================================
// 产品定位：快速剪掉口播/播客/教学视频中的停顿、静音、空档，
// 一键把长讲稿压缩成紧凑成片（保留全部有效语音，去掉沉默）。
//
// 流程（全本地，不联网）：
//   1. 真实解码触发轨音频 → 逐帧 RMS 语音活动检测（VAD）；
//   2. 区间合并：保留句内短停顿（hold），剔除长静音段；
//   3. 生成「保留区间」列表（KeepSegment）与统计（原始/成片时长、压缩比）；
//   4. planSpeechCut：把保留区间映射为时间轴上的新片段序列（源区间连续）；
//   5. applySpeechCut：一条撤销事务落盘（删旧片段 + 插入保留段）。
//
// 与自动闪避的区别：闪避只降音量（保留时间轴）；口播精剪真实删时间轴时长。
// 与音乐卡点的区别：卡点按节拍切分（音乐驱动）；精剪按语音活动切分（内容驱动）。
//
// 合规：全部计算在设备本地完成，不采集、不上传音频（隐私政策 3.1/3.4）。

/// 语音活动检测配置。
struct SpeechDetectConfig {
    /// VAD 帧长（采样点数）。
    int frameSize = 1024;
    /// VAD 跳距（采样点数；决定时间分辨率）。
    int hopSize = 512;
    /// 静音阈值（RMS；低于该值视为静音/无语音）。
    double silenceRmsThreshold = 0.02;
    /// 最短静音时长（秒）——短于此值的停顿保留（呼吸/换气不剪）。
    double minSilenceSeconds = 0.35;
    /// 保留段落两端余量（秒）——静音切除边界外扩，避免语音首尾被切。
    double paddingSeconds = 0.08;
    /// 最短保留段时长（秒）——过短的保留段与相邻段合并。
    double minKeepSeconds = 0.10;
    /// 最大单段时长（秒；0 = 不限制）——超长连续语音按此切分。
    double maxKeepSeconds = 0.0;

    bool valid() const {
        return frameSize > 0 && hopSize > 0 && silenceRmsThreshold >= 0.0 &&
               minSilenceSeconds >= 0.0 && paddingSeconds >= 0.0 && minKeepSeconds >= 0.0 &&
               maxKeepSeconds >= 0.0;
    }
};

/// 语音活动区间统计结果（供 UI 展示「已剪掉 X 秒静音」）。
struct SpeechActivityResult {
    /// 保留区间（按时间升序；源时间轴坐标）。
    struct KeepSegment {
        Time start;
        Time end;
        /// 区间内平均 RMS（供强度展示）。
        double averageRms = 0.0;

        Time duration() const { return end - start; }
    };
    std::vector<KeepSegment> keepSegments;
    /// 分析覆盖时长（源素材）。
    Time sourceDuration;
    /// 有效语音总时长。
    Time speechDuration;
    /// 被剪掉的静音总时长。
    Time silenceDuration;
    /// 压缩后时长占比（0..1；越小剪得越多）。
    double keepRatio = 1.0;
    /// 帧级 RMS 序列（诊断/UI 波形对照；每 hopSize 一个）。
    std::vector<double> rmsByFrame;
    /// 帧间隔（秒）。
    double frameSeconds = 0.0;
    /// 诊断信息（如「素材无有效语音」）。
    std::string note;

    /// 是否检测到可用语音（有保留区间）。
    bool valid() const { return !keepSegments.empty(); }
};

/// 分析语音活动：真实解码音频 → VAD → 保留区间（含静音统计）。
/// - blocks：音频块（升序；交错 float）；
/// - 空输入/无语音时返回无效结果（note 说明原因），不抛异常。
SpeechActivityResult analyzeSpeechActivity(const std::vector<AudioBlock>& blocks,
                                           const SpeechDetectConfig& config = {});

/// 口播精剪计划（供 UI 预览与落盘共用）。
struct SpeechCutPlan {
    /// 新片段序列（时间轴连续排布；每段对应一个保留区间）。
    struct Cut {
        AssetId asset;
        Time sourceStart;   ///< 源素材入点
        Time duration;      ///< 时长
        Time timelineStart; ///< 落盘后的时间轴起点
    };
    std::vector<Cut> cuts;
    /// 原始片段总时长（时间轴上被处理的范围）。
    Time originalDuration;
    /// 精剪后总时长。
    Time resultDuration;
    /// 剪掉时长。
    Time removedDuration;
    /// 说明（可直接展示，如「剪掉 42.3 秒静音，成片缩短 35%」）。
    std::string summary;
    std::vector<std::string> warnings;
    Result result;

    bool valid() const { return result.isOk() && !cuts.empty(); }
};

/// 生成精剪计划（不修改工程）：
/// - targetClip：待处理片段（其源区间 [sourceIn, sourceOut) 被分析）；
/// - activity：analyzeSpeechActivity 结果（其坐标系应覆盖 targetClip 源区间）；
/// - keepGaps：保留原片段间的空隙（false = 紧凑排布，默认紧凑）。
/// 说明：保留区间会按 targetClip 的 sourceIn 偏移裁剪到片段范围内。
SpeechCutPlan planSpeechCut(const Clip& targetClip, const SpeechActivityResult& activity,
                            bool keepGaps = false);

/// 批量：对整条轨道生成精剪计划（按片段顺序处理，保留片段间原空隙）。
SpeechCutPlan planSpeechCutForTrack(const Track& track, const SpeechActivityResult& activity,
                                    bool keepGaps = true);

/// 精剪落盘结果。
struct SpeechCutApplyResult {
    Result result;
    std::size_t clipsRemoved = 0;
    std::size_t clipsInserted = 0;
};

/// 落盘精剪（真实修改时间轴；一条撤销事务可整体回退）。
/// - targetTrack：目标轨道；targetClipIds：待替换的旧片段（为空 = 轨道全部片段）；
/// - activity：语音分析结果（坐标系 = 源素材时间）；
/// - 前置条件：轨道存在、未锁定、旧片段存在；
/// - 失败不产生副作用（计划先在事务外算好）。
SpeechCutApplyResult applySpeechCut(TimelineEditor& editor, Project& project,
                                    SequenceId sequenceId, TrackId trackId,
                                    const std::vector<ClipId>& targetClipIds,
                                    const SpeechActivityResult& activity,
                                    const std::string& transactionName = "口播精剪");

/// 由素材解码并分析（便捷入口；readerFactory 为空返回 note 说明的结果）。
SpeechActivityResult analyzeAssetSpeech(const MediaAsset& asset,
                                        MediaReaderFactory* readerFactory,
                                        const SpeechDetectConfig& config = {});

}  // namespace core
}  // namespace nairoclip
