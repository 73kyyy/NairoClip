#include "nairoclip/core/export_preset.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nairoclip {
namespace core {

namespace {

bool equalsIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

std::string formatDouble(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return std::string(buffer);
}

}  // namespace

// =============================================================================
// 性能模式
// =============================================================================

PerformanceTuning performanceTuning(PerformanceMode mode, int cpuCores) {
    const int cores = cpuCores > 0 ? cpuCores : 4;  // 未知按 4 核保守处理
    PerformanceTuning tuning;
    switch (mode) {
        case PerformanceMode::Turbo:
            tuning.encoderPreset = "ultrafast";
            tuning.threads = cores;
            tuning.pauseEditing = true;
            tuning.description = "极速模式（暂停编辑，最快）";
            break;
        case PerformanceMode::Eco:
            // 省电：单线程 + 常规预置（降速静默，减少发热与电量消耗）
            tuning.encoderPreset = "veryfast";
            tuning.threads = 1;
            tuning.pauseEditing = false;
            tuning.description = "省电模式（降速，后台静默）";
            break;
        case PerformanceMode::Balanced:
        default:
            // 平衡：保留 1 核给编辑交互（对应「可继续编辑其他项目」）
            tuning.encoderPreset = "veryfast";
            tuning.threads = std::max(1, cores - 1);
            tuning.pauseEditing = false;
            tuning.description = "平衡模式（可继续编辑其他项目）";
            break;
    }
    return tuning;
}

const char* performanceModeName(PerformanceMode mode) {
    switch (mode) {
        case PerformanceMode::Turbo:
            return "turbo";
        case PerformanceMode::Balanced:
            return "balanced";
        case PerformanceMode::Eco:
            return "eco";
        default:
            return "balanced";
    }
}

bool performanceModeFromInt(int value, PerformanceMode& out) {
    switch (value) {
        case 0:
            out = PerformanceMode::Turbo;
            return true;
        case 1:
            out = PerformanceMode::Balanced;
            return true;
        case 2:
            out = PerformanceMode::Eco;
            return true;
        default:
            return false;
    }
}

// =============================================================================
// 导出预设
// =============================================================================

const std::vector<ExportPreset>& builtinExportPresets() {
    static const std::vector<ExportPreset> presets = {
        {ExportPresetId::Custom,
         "自定义",
         "",
         "自定义",
         0,
         0,
         0,
         0.0,
         false,
         FrameFit::Fit,
         "沿用当前序列设置（分辨率/帧率/画幅适配）"},
        {ExportPresetId::Douyin,
         "抖音 9:16",
         "抖音",
         "9:16",
         1080,
         1920,
         30,
         8.0,
         false,
         FrameFit::Fill,
         "竖屏短视频，铺满无黑边"},
        {ExportPresetId::Reels,
         "Reels 9:16",
         "Instagram",
         "9:16",
         1080,
         1920,
         30,
         8.0,
         false,
         FrameFit::Fill,
         "竖屏短视频，铺满无黑边"},
        {ExportPresetId::Shorts,
         "Shorts 9:16",
         "YouTube",
         "9:16",
         1080,
         1920,
         30,
         10.0,
         false,
         FrameFit::Fill,
         "竖屏短视频（YouTube 推荐高码率）"},
        {ExportPresetId::WechatChannels,
         "视频号 9:16",
         "微信",
         "9:16",
         1080,
         1920,
         30,
         8.0,
         false,
         FrameFit::Fill,
         "竖屏短视频，铺满无黑边"},
        {ExportPresetId::YouTube4K,
         "YouTube 16:9 4K",
         "YouTube",
         "16:9",
         3840,
         2160,
         30,
         45.0,
         true,  // 4K 为 Pro 权益
         FrameFit::Fit,
         "4K 横屏（完整保留画面，需 Pro）"},
    };
    return presets;
}

const ExportPreset& exportPreset(ExportPresetId id) {
    const auto& presets = builtinExportPresets();
    for (const auto& preset : presets) {
        if (preset.id == id) {
            return preset;
        }
    }
    return presets.front();  // Custom
}

const char* exportPresetName(ExportPresetId id) {
    switch (id) {
        case ExportPresetId::Custom:
            return "custom";
        case ExportPresetId::Douyin:
            return "douyin";
        case ExportPresetId::Reels:
            return "reels";
        case ExportPresetId::Shorts:
            return "shorts";
        case ExportPresetId::WechatChannels:
            return "wechat";
        case ExportPresetId::YouTube4K:
            return "youtube4k";
        default:
            return "custom";
    }
}

bool exportPresetFromString(const char* text, ExportPresetId& out) {
    if (text == nullptr) {
        return false;
    }
    const ExportPresetId ids[] = {
        ExportPresetId::Custom,       ExportPresetId::Douyin, ExportPresetId::Reels,
        ExportPresetId::Shorts,       ExportPresetId::WechatChannels,
        ExportPresetId::YouTube4K,
    };
    for (const auto id : ids) {
        if (equalsIgnoreCase(text, exportPresetName(id))) {
            out = id;
            return true;
        }
    }
    return false;
}

// =============================================================================
// 资源预估
// =============================================================================

std::string ExportEstimate::fileSizeText() const {
    if (fileSizeBytes <= 0) {
        return {};
    }
    const double mb = static_cast<double>(fileSizeBytes) / 1048576.0;
    if (mb >= 1024.0) {
        return "约 " + formatDouble(mb / 1024.0, 1) + " GB";
    }
    if (mb >= 1.0) {
        return "约 " + formatDouble(mb, 0) + " MB";
    }
    return "约 " + formatDouble(static_cast<double>(fileSizeBytes) / 1024.0, 0) + " KB";
}

std::string ExportEstimate::renderTimeText() const {
    if (renderSeconds <= 0.0) {
        return {};
    }
    const int total = static_cast<int>(renderSeconds + 0.5);
    if (total < 60) {
        return "约 " + std::to_string(total) + "秒";
    }
    const int minutes = total / 60;
    const int seconds = total % 60;
    if (minutes < 60) {
        if (seconds == 0) {
            return "约 " + std::to_string(minutes) + "分";
        }
        return "约 " + std::to_string(minutes) + "分" + std::to_string(seconds) + "秒";
    }
    const int hours = minutes / 60;
    const int remainMinutes = minutes % 60;
    return "约 " + std::to_string(hours) + "小时" + std::to_string(remainMinutes) + "分";
}

ExportEstimate estimateExport(int width, int height, int fps, double durationSeconds,
                              double videoBitrateMbps, bool includeAudio,
                              double renderSpeedMPps) {
    ExportEstimate estimate;
    if (width <= 0 || height <= 0 || fps <= 0 || !(durationSeconds > 0.0)) {
        return estimate;  // 空估算（字段保持 0，文案为空）
    }
    estimate.width = width;
    estimate.height = height;
    estimate.fps = fps;
    estimate.durationSeconds = durationSeconds;

    // 视频码率：显式提供优先；否则按 0.12 bit/像素/帧 推导（1080p30 ≈ 7.5 Mbps，
    // 与主流平台推荐值同量级，偏保守）。
    double videoMbps = videoBitrateMbps;
    if (!(videoMbps > 0.0)) {
        videoMbps = static_cast<double>(width) * static_cast<double>(height) *
                    static_cast<double>(fps) * 0.12 / 1000000.0;
    }
    estimate.videoBitrateMbps = videoMbps;

    // 文件大小 = (视频 + 音频) 码率 × 时长
    const double audioMbps = includeAudio ? 0.192 : 0.0;  // AAC 192 kbps
    estimate.fileSizeBytes = static_cast<std::int64_t>(
        (videoMbps + audioMbps) * 1000000.0 / 8.0 * durationSeconds);

    // 渲染耗时 = 输出总像素 / 吞吐（解码 + 缩放 + 编码 + 字幕/水印叠加的合成吞吐）
    const double speed = renderSpeedMPps > 0.0 ? renderSpeedMPps : 25.0;
    const double totalMegapixels =
        static_cast<double>(width) * static_cast<double>(height) / 1000000.0 *
        static_cast<double>(fps) * durationSeconds;
    estimate.renderSeconds = totalMegapixels / speed;

    estimate.basis = "估算值（" + formatDouble(speed, 0) + " MP/s 渲染吞吐 + " +
                     formatDouble(videoMbps, 1) + " Mbps 视频码率";
    if (includeAudio) {
        estimate.basis += " + 192 kbps 音频";
    }
    estimate.basis += "）";
    return estimate;
}

// =============================================================================
// 预设 + 权益 → 导出配置组装
// =============================================================================

PresetExportSetup setupPresetExport(ExportPresetId presetId, const Entitlement& entitlement,
                                    std::int64_t nowUnix, PerformanceMode mode, int cpuCores,
                                    int crf) {
    PresetExportSetup setup;
    const ExportPreset& preset = exportPreset(presetId);
    setup.ok = true;
    setup.preset = presetId;
    setup.requiresPro = preset.requiresPro;
    setup.frameFit = preset.recommendedFit;
    setup.width = preset.width;
    setup.height = preset.height;
    setup.fps = preset.fps;

    // 权益门控：4K/8K 预设对非 Pro 降级（导出时强制降级 1080p）。
    // 注意：此处用 proActiveAt（含到期判定，fail-closed），而不用
    // entitlement.maxExportShortSide() —— 后者基于 tier 字段（服务器维护的
    // 名义层级），在「已到期但服务器尚未下发更新」的窗口内会返回 Pro 的无限制值。
    const bool pro = entitlement.proActiveAt(nowUnix);
    if (preset.requiresPro && !pro) {
        const int limit = kFreeMaxExportShortSide;  // 免费版：短边 1080
        int outW = preset.width;
        int outH = preset.height;
        if (clampSizeToShortSide(preset.width, preset.height, limit, outW, outH)) {
            setup.width = outW;
            setup.height = outH;
            setup.downgradedForFree = true;
            setup.warnings.push_back("4K 导出为 Pro 权益，当前已降级为 " +
                                     std::to_string(outW) + "x" + std::to_string(outH) +
                                     "（升级 Pro 解锁 4K/8K）");
        }
    }

    // 性能模式 → 编码参数（真实作用于写入器）
    const PerformanceTuning tuning = performanceTuning(mode, cpuCores);
    setup.writer.preset = tuning.encoderPreset;
    setup.writer.threads = tuning.threads;
    setup.writer.crf = crf;
    if (setup.width > 0 && setup.height > 0) {
        setup.writer.width = setup.width;
        setup.writer.height = setup.height;
    }
    if (setup.fps > 0) {
        setup.writer.frameRate = FrameRate::of(setup.fps);
    }
    return setup;
}

ExportPlan applyPresetToPlan(const ExportPlan& plan, const PresetExportSetup& setup) {
    ExportPlan out = plan;
    if (setup.width > 0 && setup.height > 0) {
        out.width = setup.width;
        out.height = setup.height;
    }
    if (setup.fps > 0) {
        out.frameRate = FrameRate::of(setup.fps);
    }
    // Custom 预设：沿用计划自身的画幅适配设置（不覆盖）
    if (setup.preset != ExportPresetId::Custom) {
        out.frameFit = setup.frameFit;
    }
    return out;
}

}  // namespace core
}  // namespace nairoclip
