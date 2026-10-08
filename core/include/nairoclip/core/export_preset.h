#pragma once

#include "nairoclip/core/entitlement.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/media_writer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 导出预设 / 资源预估 / 性能模式（文档第 6 部分 6.1、第 4 部分「后台导出」）
// =============================================================================
// 产品对应：
//   · 快速预设：📱抖音9:16 📷Reels9:16 ▶️YouTube16:9 4K 🎵Shorts9:16 💬视频号9:16 ⚙️自定义；
//   · 资源预估：📊「生成文件大小: 约245MB / 本地渲染耗时: 约1分15秒」；
//   · 性能模式：○极速(暂停编辑，最快) ●平衡(可继续编辑) ○省电(降速，后台静默)。
//
// 本模块定位（纯计算 + 配置组装，零文件系统/线程依赖）：
//   · builtinExportPresets：预设的单一事实来源（UI 列表与导出执行共用）；
//   · performanceTuning：性能模式 → 编码器参数（真实作用于 FFmpeg 写入器）；
//   · estimateExport：文件大小/渲染耗时估算（可被设备基准覆盖）；
//   · setupPresetExport：预设 + 权益 → 完整写入配置（4K 需 Pro，非 Pro 自动降级）。

// ---------------------------------------------------------------------------
// 性能模式
// ---------------------------------------------------------------------------

/// 导出性能模式（文档第 4 部分「导出时性能模式」）。
enum class PerformanceMode {
    Turbo = 0,     ///< 极速模式（暂停编辑，最快）
    Balanced = 1,  ///< 平衡模式（可继续编辑其他项目；默认）
    Eco = 2,       ///< 省电模式（降速，后台静默）
};

/// 性能模式对应的编码调优（真实作用于写入器）。
struct PerformanceTuning {
    /// 编码器预置名（x264/x265 通用：ultrafast..placebo）。
    std::string encoderPreset = "veryfast";
    /// 编码线程数（0 = 编码器自动；>0 为显式上限）。
    int threads = 0;
    /// 平台层是否应暂停编辑交互（Turbo = true；对应「暂停编辑，最快」）。
    bool pauseEditing = false;
    /// 说明文案（供 UI 直接展示）。
    std::string description;
};

/// 性能模式调优：由模式 + 设备核心数推导编码参数。
/// - cpuCores：设备 CPU 核心数（0 = 未知，按 4 核保守处理）；
/// - Turbo：ultrafast + 全核 + 暂停编辑；Balanced：veryfast + 保留 1 核；
///   Eco：veryfast + 单核（降速静默，减少发热与电量消耗）。
PerformanceTuning performanceTuning(PerformanceMode mode, int cpuCores = 0);

/// 模式名称（"turbo" / "balanced" / "eco"；序列化与 UI 用）。
const char* performanceModeName(PerformanceMode mode);
/// 从整数解析（0/1/2；非法返回 false）。
bool performanceModeFromInt(int value, PerformanceMode& out);

// ---------------------------------------------------------------------------
// 导出预设
// ---------------------------------------------------------------------------

/// 导出预设 ID（与 UI 快速预设一一对应；保持稳定枚举值）。
enum class ExportPresetId {
    Custom = 0,          ///< ⚙️ 自定义（沿用当前序列设置）
    Douyin = 1,          ///< 📱 抖音 9:16
    Reels = 2,           ///< 📷 Instagram Reels 9:16
    Shorts = 3,          ///< 🎵 YouTube Shorts 9:16
    WechatChannels = 4,  ///< 💬 视频号 9:16
    YouTube4K = 5,       ///< ▶️ YouTube 16:9 4K（Pro 权益）
};

/// 单个预设。
struct ExportPreset {
    ExportPresetId id = ExportPresetId::Custom;
    /// 展示名（如「抖音 9:16」）。
    std::string label;
    /// 平台名（如「抖音」；自定义为空）。
    std::string platform;
    /// 画幅描述（"9:16" / "16:9" / "自定义"）。
    std::string aspect;
    int width = 0;   ///< 目标宽度（Custom 为 0 = 沿用序列）
    int height = 0;  ///< 目标高度
    int fps = 0;     ///< 目标帧率（0 = 沿用序列）
    /// 推荐视频码率（Mbps；用于体积预估与写入器提示；0 = 按分辨率推导）。
    double videoBitrateMbps = 0.0;
    /// 是否需要 Pro 权益（4K/8K）。
    bool requiresPro = false;
    /// 推荐画幅适配（竖屏平台用 Fill 避免黑边；YouTube 用 Fit 完整保留画面）。
    FrameFit recommendedFit = FrameFit::Fill;
    /// 说明（供 UI 副标题）。
    std::string description;
};

/// 内置预设列表（顺序即 UI 展示顺序）。
const std::vector<ExportPreset>& builtinExportPresets();

/// 取指定预设（越界/非法返回 Custom）。
const ExportPreset& exportPreset(ExportPresetId id);

/// 名称（"custom" / "douyin" / "reels" / "shorts" / "wechat" / "youtube4k"）。
const char* exportPresetName(ExportPresetId id);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool exportPresetFromString(const char* text, ExportPresetId& out);

// ---------------------------------------------------------------------------
// 资源预估（文档「📊 资源预估」）
// ---------------------------------------------------------------------------

/// 导出资源预估结果。
struct ExportEstimate {
    int width = 0;
    int height = 0;
    int fps = 0;
    double durationSeconds = 0.0;
    /// 视频码率（Mbps；用于展示与推导）。
    double videoBitrateMbps = 0.0;
    /// 预估文件大小（字节；含音频轨）。
    std::int64_t fileSizeBytes = 0;
    /// 预估渲染耗时（秒；纯软件编码 + 叠加渲染的经验值）。
    double renderSeconds = 0.0;
    /// 估算依据说明（可直接展示，提醒用户为估算值）。
    std::string basis;

    /// 便于 UI 展示的友好文案（如「约 245 MB」「约 1分15秒」）。
    std::string fileSizeText() const;
    std::string renderTimeText() const;
};

/// 预估导出资源。
/// - width/height/fps：输出规格（非正返回空估算）；
/// - durationSeconds：成片时长（非正返回空估算）；
/// - videoBitrateMbps：视频码率（<= 0 时按分辨率经验值推导：0.12 bit/像素/帧）；
/// - includeAudio：是否含音频（音频按 192 kbps 计入）；
/// - renderSpeedMPps：渲染吞吐（输出像素/秒；0 = 默认 25 MP/s 保守经验值）。
ExportEstimate estimateExport(int width, int height, int fps, double durationSeconds,
                              double videoBitrateMbps = 0.0, bool includeAudio = true,
                              double renderSpeedMPps = 0.0);

// ---------------------------------------------------------------------------
// 预设 + 权益 → 导出配置组装
// ---------------------------------------------------------------------------

/// 预设导出设置（可直接用于 runExport：writer 已填好，尺寸已含门控结果）。
struct PresetExportSetup {
    bool ok = false;
    ExportPresetId preset = ExportPresetId::Custom;
    /// 输出尺寸（应用预设与门控后的最终值；Custom 时为 0 = 沿用序列）。
    int width = 0;
    int height = 0;
    int fps = 0;
    /// 画幅适配（预设推荐值；导出计划应使用该值）。
    FrameFit frameFit = FrameFit::Fill;
    /// 写入器配置（尺寸/帧率/编码预置/线程/CRF 已填好）。
    WriterConfig writer;
    /// 是否需要 Pro（预设要求）。
    bool requiresPro = false;
    /// 是否因当前权益不足而被降级（如 4K → 1080p）。
    bool downgradedForFree = false;
    /// 提示信息（降级说明等；供 UI 展示）。
    std::vector<std::string> warnings;
};

/// 组装预设导出设置（含权益门控与性能模式）。
/// - preset：预设 ID；
/// - entitlement / nowUnix：用于 4K 门控（非 Pro 自动降级 1080p 并写入 warnings）；
/// - mode / cpuCores：性能模式 → 编码预置与线程数；
/// - crf：H.264 质量（默认 23）。
PresetExportSetup setupPresetExport(ExportPresetId preset, const Entitlement& entitlement,
                                    std::int64_t nowUnix, PerformanceMode mode,
                                    int cpuCores = 0, int crf = 23);

/// 把预设设置应用到导出计划（返回覆盖后的副本；尺寸为 0 时保持原值）。
/// 注意：仅覆盖 width/height/frameFit；时长/轨道内容不变。
ExportPlan applyPresetToPlan(const ExportPlan& plan, const PresetExportSetup& setup);

}  // namespace core
}  // namespace nairoclip
