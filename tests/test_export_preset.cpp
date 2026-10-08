#include "test_framework.h"
#include "nairoclip/core/export_preset.h"

#include <cmath>
#include <string>

using namespace nairoclip::core;

namespace {

Entitlement freeEntitlement() { return Entitlement{}; }

Entitlement proEntitlement() {
    Entitlement e;
    e.tier = MembershipTier::Pro;
    return e;
}

}  // namespace

// =============================================================================
// 性能模式（文档第 4 部分：极速/平衡/省电）
// =============================================================================

NC_TEST(ExportPreset, PerformanceTuningModes) {
    // 极速：ultrafast + 全核 + 暂停编辑
    const PerformanceTuning turbo = performanceTuning(PerformanceMode::Turbo, 8);
    NC_CHECK_EQ(turbo.encoderPreset, std::string("ultrafast"));
    NC_CHECK_EQ(turbo.threads, 8);
    NC_CHECK(turbo.pauseEditing);
    NC_CHECK(!turbo.description.empty());

    // 平衡：veryfast + 保留 1 核给编辑 + 不暂停
    const PerformanceTuning balanced = performanceTuning(PerformanceMode::Balanced, 8);
    NC_CHECK_EQ(balanced.encoderPreset, std::string("veryfast"));
    NC_CHECK_EQ(balanced.threads, 7);
    NC_CHECK(!balanced.pauseEditing);

    // 省电：单线程 + 不暂停
    const PerformanceTuning eco = performanceTuning(PerformanceMode::Eco, 8);
    NC_CHECK_EQ(eco.threads, 1);
    NC_CHECK(!eco.pauseEditing);
}

NC_TEST(ExportPreset, PerformanceTuningUnknownCores) {
    // 未知核心数（0）按 4 核保守处理
    const PerformanceTuning balanced = performanceTuning(PerformanceMode::Balanced, 0);
    NC_CHECK_EQ(balanced.threads, 3);
    const PerformanceTuning turbo = performanceTuning(PerformanceMode::Turbo, 0);
    NC_CHECK_EQ(turbo.threads, 4);
}

NC_TEST(ExportPreset, PerformanceModeNames) {
    NC_CHECK_EQ(std::string(performanceModeName(PerformanceMode::Turbo)), std::string("turbo"));
    NC_CHECK_EQ(std::string(performanceModeName(PerformanceMode::Balanced)),
                std::string("balanced"));
    NC_CHECK_EQ(std::string(performanceModeName(PerformanceMode::Eco)), std::string("eco"));
    PerformanceMode mode;
    NC_CHECK(performanceModeFromInt(0, mode));
    NC_CHECK(mode == PerformanceMode::Turbo);
    NC_CHECK(performanceModeFromInt(2, mode));
    NC_CHECK(mode == PerformanceMode::Eco);
    NC_CHECK(!performanceModeFromInt(9, mode));
}

// =============================================================================
// 预设列表（文档：📱抖音9:16 📷Reels9:16 ▶️YouTube16:9 4K 🎵Shorts9:16 💬视频号9:16）
// =============================================================================

NC_TEST(ExportPreset, BuiltinListCoversDocument) {
    const auto& presets = builtinExportPresets();
    // 文档 6 个预设：自定义 + 5 个平台预设
    NC_CHECK_EQ(presets.size(), std::size_t(6));
    NC_CHECK(presets.front().id == ExportPresetId::Custom);
    // 竖屏平台预设：1080x1920
    for (const auto id : {ExportPresetId::Douyin, ExportPresetId::Reels,
                          ExportPresetId::Shorts, ExportPresetId::WechatChannels}) {
        const ExportPreset& preset = exportPreset(id);
        NC_CHECK_EQ(preset.width, 1080);
        NC_CHECK_EQ(preset.height, 1920);
        NC_CHECK_EQ(preset.aspect, std::string("9:16"));
        NC_CHECK(!preset.requiresPro);
        NC_CHECK(preset.recommendedFit == FrameFit::Fill);  // 竖屏铺满无黑边
    }
    // YouTube 4K：3840x2160 + 需 Pro + Fit（完整保留画面）
    const ExportPreset& youtube = exportPreset(ExportPresetId::YouTube4K);
    NC_CHECK_EQ(youtube.width, 3840);
    NC_CHECK_EQ(youtube.height, 2160);
    NC_CHECK(youtube.requiresPro);
    NC_CHECK(youtube.recommendedFit == FrameFit::Fit);
}

NC_TEST(ExportPreset, PresetNamesAndParsing) {
    NC_CHECK_EQ(std::string(exportPresetName(ExportPresetId::Douyin)), std::string("douyin"));
    NC_CHECK_EQ(std::string(exportPresetName(ExportPresetId::YouTube4K)),
                std::string("youtube4k"));
    ExportPresetId id;
    NC_CHECK(exportPresetFromString("douyin", id));
    NC_CHECK(id == ExportPresetId::Douyin);
    NC_CHECK(exportPresetFromString("YOUTUBE4K", id));  // 大小写不敏感
    NC_CHECK(id == ExportPresetId::YouTube4K);
    NC_CHECK(!exportPresetFromString("tiktok", id));
    NC_CHECK(!exportPresetFromString(nullptr, id));
    // 非法 ID 回退 Custom
    NC_CHECK(exportPreset(static_cast<ExportPresetId>(99)).id == ExportPresetId::Custom);
}

// =============================================================================
// 资源预估（文档：📊 生成文件大小: 约245MB / 本地渲染耗时: 约1分15秒）
// =============================================================================

NC_TEST(ExportEstimate, ComputesSizeAndTime) {
    // 1080p30 60 秒 8Mbps → 约 60MB（视频）+ 1.4MB（音频）
    const ExportEstimate estimate = estimateExport(1920, 1080, 30, 60.0, 8.0, true);
    NC_CHECK_EQ(estimate.width, 1920);
    NC_CHECK(estimate.fileSizeBytes > 0);
    // (8 + 0.192) Mbps × 60s / 8 = 61.44 MB
    const double expected = (8.0 + 0.192) * 1000000.0 / 8.0 * 60.0;
    NC_CHECK(std::fabs(static_cast<double>(estimate.fileSizeBytes) - expected) < 100000.0);
    NC_CHECK(estimate.renderSeconds > 0.0);
    NC_CHECK(!estimate.basis.empty());
    NC_CHECK(!estimate.fileSizeText().empty());
    NC_CHECK(!estimate.renderTimeText().empty());
}

NC_TEST(ExportEstimate, DerivesBitrateWhenNotProvided) {
    // 未提供码率：按 0.12 bit/像素/帧 推导
    const ExportEstimate estimate = estimateExport(3840, 2160, 30, 10.0, 0.0);
    const double expectedMbps = 3840.0 * 2160.0 * 30.0 * 0.12 / 1000000.0;
    NC_CHECK(std::fabs(estimate.videoBitrateMbps - expectedMbps) < 0.01);
}

NC_TEST(ExportEstimate, AudioOptional) {
    const ExportEstimate withAudio = estimateExport(1920, 1080, 30, 60.0, 8.0, true);
    const ExportEstimate noAudio = estimateExport(1920, 1080, 30, 60.0, 8.0, false);
    NC_CHECK(withAudio.fileSizeBytes > noAudio.fileSizeBytes);
    const std::int64_t audioBytes = withAudio.fileSizeBytes - noAudio.fileSizeBytes;
    // 音频 = 0.192 Mbps × 60s / 8 ≈ 1.44 MB
    NC_CHECK(std::fabs(static_cast<double>(audioBytes) - 1440000.0) < 50000.0);
}

NC_TEST(ExportEstimate, RejectsInvalidInput) {
    NC_CHECK_EQ(estimateExport(0, 1080, 30, 60.0).fileSizeBytes, std::int64_t(0));
    NC_CHECK_EQ(estimateExport(1920, 0, 30, 60.0).fileSizeBytes, std::int64_t(0));
    NC_CHECK_EQ(estimateExport(1920, 1080, 0, 60.0).fileSizeBytes, std::int64_t(0));
    NC_CHECK_EQ(estimateExport(1920, 1080, 30, 0.0).fileSizeBytes, std::int64_t(0));
    NC_CHECK_EQ(estimateExport(1920, 1080, 30, -5.0).fileSizeBytes, std::int64_t(0));
    // 空估算文案为空
    const ExportEstimate empty = estimateExport(0, 0, 0, 0.0);
    NC_CHECK(empty.fileSizeText().empty());
    NC_CHECK(empty.renderTimeText().empty());
}

NC_TEST(ExportEstimate, FriendlyTextFormats) {
    // 大文件 → GB
    const ExportEstimate big = estimateExport(3840, 2160, 60, 600.0, 45.0);
    NC_CHECK(big.fileSizeText().find("GB") != std::string::npos);
    // 长耗时 → 分钟
    const ExportEstimate longRender = estimateExport(1920, 1080, 30, 600.0, 8.0);
    NC_CHECK(longRender.renderTimeText().find("分") != std::string::npos);
    // 短耗时 → 秒
    const ExportEstimate shortRender = estimateExport(320, 240, 30, 2.0, 1.0);
    NC_CHECK(shortRender.renderTimeText().find("秒") != std::string::npos);
}

NC_TEST(ExportEstimate, CustomRenderThroughput) {
    // 更快的渲染吞吐 → 更短耗时（供设备基准二次校正）
    const ExportEstimate slow = estimateExport(1920, 1080, 30, 60.0, 8.0, true, 10.0);
    const ExportEstimate fast = estimateExport(1920, 1080, 30, 60.0, 8.0, true, 50.0);
    NC_CHECK(std::fabs(slow.renderSeconds / fast.renderSeconds - 5.0) < 0.01);
}

// =============================================================================
// 预设 + 权益组装（4K 门控）
// =============================================================================

NC_TEST(ExportPresetSetup, FreeDowngrades4K) {
    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::YouTube4K, freeEntitlement(), 0, PerformanceMode::Balanced);
    NC_CHECK(setup.ok);
    NC_CHECK(setup.requiresPro);
    NC_CHECK(setup.downgradedForFree);
    // 4K → 1080p（短边限制）
    NC_CHECK_EQ(setup.width, 1920);
    NC_CHECK_EQ(setup.height, 1080);
    NC_CHECK(!setup.warnings.empty());
    NC_CHECK(setup.warnings[0].find("Pro") != std::string::npos);
}

NC_TEST(ExportPresetSetup, ProKeeps4K) {
    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::YouTube4K, proEntitlement(), 0, PerformanceMode::Balanced);
    NC_CHECK(setup.ok);
    NC_CHECK(!setup.downgradedForFree);
    NC_CHECK_EQ(setup.width, 3840);
    NC_CHECK_EQ(setup.height, 2160);
    NC_CHECK(setup.warnings.empty());
}

NC_TEST(ExportPresetSetup, VerticalPresetsUnaffectedByGate) {
    // 竖屏预设（1080x1920）短边 1080，免费版无降级
    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::Douyin, freeEntitlement(), 0, PerformanceMode::Balanced);
    NC_CHECK(!setup.downgradedForFree);
    NC_CHECK_EQ(setup.width, 1080);
    NC_CHECK_EQ(setup.height, 1920);
    NC_CHECK(setup.frameFit == FrameFit::Fill);
}

NC_TEST(ExportPresetSetup, WriterConfigPopulated) {
    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::Douyin, freeEntitlement(), 0, PerformanceMode::Turbo, 8, 20);
    NC_CHECK_EQ(setup.writer.width, 1080);
    NC_CHECK_EQ(setup.writer.height, 1920);
    NC_CHECK_EQ(setup.writer.crf, 20);
    NC_CHECK_EQ(setup.writer.preset, std::string("ultrafast"));  // Turbo
    NC_CHECK_EQ(setup.writer.threads, 8);
    NC_CHECK(setup.writer.frameRate == FrameRate::of(30));
}

NC_TEST(ExportPresetSetup, EcoModeWriterConfig) {
    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::Reels, freeEntitlement(), 0, PerformanceMode::Eco, 8);
    NC_CHECK_EQ(setup.writer.threads, 1);  // 省电单线程
    NC_CHECK_EQ(setup.writer.preset, std::string("veryfast"));
}

NC_TEST(ExportPresetSetup, ExpiredProDowngrades) {
    // 到期后（expiresAtUnix 已过）→ 按 Free 处理（fail-closed）
    Entitlement expired;
    expired.tier = MembershipTier::Pro;
    expired.expiresAtUnix = 1000;  // 已过期
    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::YouTube4K, expired, 2000, PerformanceMode::Balanced);
    NC_CHECK(setup.downgradedForFree);
    NC_CHECK_EQ(setup.width, 1920);
}

// =============================================================================
// 预设应用到导出计划
// =============================================================================

NC_TEST(ExportPresetSetup, ApplyPresetToPlan) {
    ExportPlan plan;
    plan.width = 1080;
    plan.height = 1920;
    plan.frameRate = fps30();
    plan.frameFit = FrameFit::Fit;

    // 竖屏预设 → 覆盖尺寸/帧率/画幅适配
    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::Douyin, freeEntitlement(), 0, PerformanceMode::Balanced);
    const ExportPlan applied = applyPresetToPlan(plan, setup);
    NC_CHECK_EQ(applied.width, 1080);
    NC_CHECK_EQ(applied.height, 1920);
    NC_CHECK(applied.frameFit == FrameFit::Fill);
}

NC_TEST(ExportPresetSetup, ApplyCustomKeepsPlanSettings) {
    ExportPlan plan;
    plan.width = 2560;
    plan.height = 1440;
    plan.frameRate = fps60();
    plan.frameFit = FrameFit::Fit;
    plan.reframe.srcWidth = 2560;  // 存在重构图轨迹（非空即有效需 keyframes）

    const PresetExportSetup setup = setupPresetExport(
        ExportPresetId::Custom, freeEntitlement(), 0, PerformanceMode::Balanced);
    const ExportPlan applied = applyPresetToPlan(plan, setup);
    // Custom：尺寸/帧率/画幅适配保持原值
    NC_CHECK_EQ(applied.width, 2560);
    NC_CHECK_EQ(applied.height, 1440);
    NC_CHECK(applied.frameRate == fps60());
    NC_CHECK(applied.frameFit == FrameFit::Fit);
}
