#pragma once

#include "nairoclip/core/editor.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// AI 检查（AI Check）—— 成片质量与合规自检（文档第 7 部分 4.5）
// =============================================================================
// 产品对应：
//   ⚠️ 字幕重叠 (2处)   00:12 [跳转查看]   00:47 [跳转查看]
//   ⚠️ 音频峰值过高 (1处)  01:03 [跳转查看]
//   [全部自动修复]  [逐项手动处理]
//
// 设计：
//   · 检测（checkSequence）：纯只读分析，产出问题列表（每项带时间点供「跳转查看」）；
//   · 修复（fixIssue / fixAll）：经编辑器命令执行（撤销栈可回退，一次修复一条历史）；
//   · 全部本地算法：字幕重叠基于字幕轨时间区间；音频峰值基于真实解码样本。
//
// 合规：全部计算在设备本地完成，不联网、不上传（文档隐私政策 3.1/3.7）。

/// 问题类别（可扩展；保持稳定枚举值供 UI 映射文案与图标）。
enum class AiCheckIssueType {
    SubtitleOverlap = 0,   ///< 字幕重叠（同一时刻多条字幕可见）
    SubtitleTooShort = 1,  ///< 字幕时长过短（一闪而过，来不及阅读）
    SubtitleEmpty = 2,     ///< 空字幕（无文本却占用时间）
    AudioPeakClipping = 3, ///< 音频峰值过高（接近或达到满刻度，易爆音）
    AudioSilentClip = 4,   ///< 片段音频近乎静音（可能是素材问题）
    ClipGap = 5,           ///< 视频轨空隙（黑场跳变，多为误删）
};

/// 严重级别（供 UI 配色：色盲友好模式下同时有图标区分）。
enum class AiCheckSeverity {
    Info = 0,
    Warning = 1,
    Error = 2,
};

/// 单个问题。
struct AiCheckIssue {
    AiCheckIssueType type = AiCheckIssueType::SubtitleOverlap;
    AiCheckSeverity severity = AiCheckSeverity::Warning;
    /// 定位时间（可点击「跳转查看」）。
    Time time;
    /// 关联实体（按类型：
    ///  - 字幕类：SubtitleId；
    ///  - 音频/视频类：ClipId；无关联时为 0）。
    std::uint64_t entityId = 0;
    /// 所属轨道（音频/视频类；无关联时为 0）。
    TrackId trackId;
    /// 可读说明（可直接用于 UI 列表）。
    std::string message;
    /// 是否可自动修复（false 时 UI 显示「逐项手动处理」）。
    bool autoFixable = false;
    /// 实测数值（按类型：音频峰值 0..N 的绝对值；字幕重叠秒数；其余 0）。
    /// 供修复逻辑做精确计算（不使用估算值）。
    double measuredValue = 0.0;

    bool valid() const { return !message.empty(); }
};

/// 检查配置。
struct AiCheckConfig {
    /// 字幕重叠判定：允许的重叠容差（秒；小于该值不算问题，避免邻接误报）。
    double subtitleOverlapToleranceSeconds = 0.02;
    /// 字幕过短阈值（秒；短于该值提示）。
    double subtitleMinDurationSeconds = 0.5;
    /// 音频峰值阈值（0..1；样本绝对值超过即视为过高）。
    double audioPeakThreshold = 0.995;
    /// 音频峰值峰值的统计窗口（秒；窗口内峰值超阈值即报一处）。
    double audioPeakWindowSeconds = 1.0;
    /// 静音判定阈值（RMS；低于该值视为静音）。
    double silenceRmsThreshold = 0.02;
    /// 空隙判定阈值（秒；视频轨空隙超过该值提示）。
    double clipGapThresholdSeconds = 0.5;
    /// 是否检查字幕类问题。
    bool checkSubtitles = true;
    /// 是否检查音频类问题。
    bool checkAudio = true;
    /// 是否检查空隙。
    bool checkGaps = true;

    bool valid() const {
        return subtitleOverlapToleranceSeconds >= 0.0 && subtitleMinDurationSeconds >= 0.0 &&
               audioPeakThreshold > 0.0 && audioPeakThreshold <= 1.0 &&
               audioPeakWindowSeconds > 0.0 && silenceRmsThreshold >= 0.0 &&
               clipGapThresholdSeconds >= 0.0;
    }
};

/// 检查报告。
struct AiCheckReport {
    std::vector<AiCheckIssue> issues;
    /// 各类型计数（下标 = AiCheckIssueType 枚举值）。
    std::vector<std::size_t> countsByType;
    /// 扫描覆盖时长。
    Time scannedDuration;
    /// 诊断信息（如「未检测到字幕轨」）。
    std::vector<std::string> notes;

    /// 问题总数。
    std::size_t size() const { return issues.size(); }
    bool empty() const { return issues.empty(); }
    /// 某类型的问题数量（越界返回 0）。
    std::size_t countOf(AiCheckIssueType type) const;
    /// 是否有可自动修复的问题。
    bool hasAutoFixable() const;
    /// 可读汇总（如「字幕重叠 2 处；音频峰值过高 1 处」；无问题返回空串）。
    std::string summary() const;
};

/// 只读检查（不修改工程）：
/// - 字幕类：重叠 / 过短 / 空字幕；
/// - 音频类：峰值过高（真实解码样本分析；经 readerFactory 提供解码器）；
/// - 视频轨空隙；
/// readerFactory 为空或解码失败时跳过音频类检查并写入 notes（不视为失败）。
AiCheckReport checkSequence(const Project& project, SequenceId sequenceId,
                            MediaReaderFactory* readerFactory = nullptr,
                            const AiCheckConfig& config = {});

/// 修复单个问题（经编辑器命令；可撤销）。
/// - 字幕重叠：把后一条字幕起点调整到前一条结束处（若调整后导致时长非法则缩短为最短合法值）；
/// - 字幕过短/空字幕：无自动修复（返回 Unsupported，UI 走手动）；
/// - 音频峰值过高：对所属片段施加安全增益（降到阈值以下，带余量）；
/// - 片段静音 / 空隙：无自动修复。
/// 返回 Ok = 已修复；Unsupported = 不支持自动修复；其他 = 失败原因。
Result fixIssue(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                const AiCheckIssue& issue, const AiCheckConfig& config = {});

/// 自动修复全部可修复问题（逐条执行；返回成功修复数量）。
/// 失败项写入 failedIssues（保持原顺序）供 UI 展示。
struct AiCheckFixResult {
    std::size_t fixed = 0;
    std::size_t skipped = 0;  ///< 不支持自动修复
    std::size_t failed = 0;   ///< 修复失败
    std::vector<AiCheckIssue> failedIssues;
};

AiCheckFixResult fixAllIssues(TimelineEditor& editor, Project& project, SequenceId sequenceId,
                              const std::vector<AiCheckIssue>& issues,
                              const AiCheckConfig& config = {});

/// 类型名称（中文，供 UI 直接展示）。
const char* aiCheckIssueName(AiCheckIssueType type);

}  // namespace core
}  // namespace nairoclip
