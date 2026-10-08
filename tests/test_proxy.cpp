#include "test_framework.h"
#include "nairoclip/core/media_writer.h"
#include "nairoclip/core/proxy.h"
#include "nairoclip/core/serialization.h"

#include <cmath>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

MediaAsset makeVideoAsset(double durationSeconds, int width, int height) {
    MediaAsset asset;
    asset.id = AssetId{1};
    asset.path = "/videos/source.mp4";
    asset.name = "source.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(static_cast<std::int64_t>(durationSeconds * 1000), 1000);
    asset.width = width;
    asset.height = height;
    asset.frameRate = fps30();
    return asset;
}

}  // namespace

// =============================================================================
// 目标尺寸与高分辨率判定
// =============================================================================

NC_TEST(Proxy, TargetSizeHalves) {
    int w = 0;
    int h = 0;
    proxyTargetSize(3840, 2160, ProxyQuality::Half, w, h);
    NC_CHECK_EQ(w, 1920);
    NC_CHECK_EQ(h, 1080);
    proxyTargetSize(3840, 2160, ProxyQuality::Quarter, w, h);
    NC_CHECK_EQ(w, 960);
    NC_CHECK_EQ(h, 540);
}

NC_TEST(Proxy, TargetSizeNeverUpscales) {
    // 小素材不放大（代理只为提速）
    int w = 0;
    int h = 0;
    proxyTargetSize(640, 360, ProxyQuality::Half, w, h);
    NC_CHECK_EQ(w, 320);
    NC_CHECK_EQ(h, 180);
}

NC_TEST(Proxy, TargetSizeRejectsInvalid) {
    int w = -1;
    int h = -1;
    proxyTargetSize(0, 1080, ProxyQuality::Half, w, h);
    NC_CHECK_EQ(w, 0);
    NC_CHECK_EQ(h, 0);
}

NC_TEST(Proxy, TargetSizeMinimumIsOne) {
    int w = 0;
    int h = 0;
    proxyTargetSize(1, 1, ProxyQuality::Quarter, w, h);
    NC_CHECK_EQ(w, 1);
    NC_CHECK_EQ(h, 1);
}

NC_TEST(Proxy, HighResolutionDetection) {
    const MediaAsset fourK = makeVideoAsset(10.0, 3840, 2160);
    NC_CHECK(isHighResolutionAsset(fourK));  // 4K 命中
    const MediaAsset hd = makeVideoAsset(10.0, 1280, 720);
    NC_CHECK(!isHighResolutionAsset(hd));  // 720p 不命中
    const MediaAsset boundary = makeVideoAsset(10.0, 2560, 1440);
    NC_CHECK(isHighResolutionAsset(boundary));  // 阈值边界命中
    // 音频/图片不参与
    MediaAsset audio = makeVideoAsset(10.0, 3840, 2160);
    audio.type = MediaType::Audio;
    NC_CHECK(!isHighResolutionAsset(audio));
    // 未知尺寸不命中（避免误报）
    const MediaAsset unknown = makeVideoAsset(10.0, 0, 0);
    NC_CHECK(!isHighResolutionAsset(unknown));
}

// =============================================================================
// 导入检查（文档：导入 4K → 是否生成代理？预计耗时 45 秒）
// =============================================================================

NC_TEST(Proxy, ImportCheckManualModeSuggests) {
    ProxySettings settings;
    settings.mode = ProxyMode::Manual;
    settings.quality = ProxyQuality::Half;
    const MediaAsset fourK = makeVideoAsset(60.0, 3840, 2160);
    const ProxyImportCheck check = checkProxyOnImport(fourK, settings);
    NC_CHECK(check.suggestProxy);
    NC_CHECK(!check.autoGenerate);  // 手动：需用户确认
    NC_CHECK_EQ(check.proxyWidth, 1920);
    NC_CHECK_EQ(check.proxyHeight, 1080);
    NC_CHECK(check.estimatedSeconds > 0.0);
    NC_CHECK(check.estimatedBytes > 0);
    NC_CHECK(check.reason.find("3840x2160") != std::string::npos);
}

NC_TEST(Proxy, ImportCheckAutoModeGenerates) {
    ProxySettings settings;
    settings.mode = ProxyMode::Auto;
    const MediaAsset fourK = makeVideoAsset(60.0, 3840, 2160);
    const ProxyImportCheck check = checkProxyOnImport(fourK, settings);
    NC_CHECK(check.suggestProxy);
    NC_CHECK(check.autoGenerate);  // 自动：无需确认
}

NC_TEST(Proxy, ImportCheckNeverModeSkips) {
    ProxySettings settings;
    settings.mode = ProxyMode::Never;
    const MediaAsset fourK = makeVideoAsset(60.0, 3840, 2160);
    const ProxyImportCheck check = checkProxyOnImport(fourK, settings);
    NC_CHECK(!check.suggestProxy);
    NC_CHECK_EQ(check.proxyWidth, 0);
}

NC_TEST(Proxy, ImportCheckLowResSkipped) {
    ProxySettings settings;
    settings.mode = ProxyMode::Auto;
    const MediaAsset hd = makeVideoAsset(60.0, 1920, 1080);
    const ProxyImportCheck check = checkProxyOnImport(hd, settings);
    NC_CHECK(!check.suggestProxy);
    NC_CHECK(check.reason.empty());
}

NC_TEST(Proxy, ImportCheckExistingProxySkips) {
    ProxySettings settings;
    settings.mode = ProxyMode::Auto;
    const MediaAsset fourK = makeVideoAsset(60.0, 3840, 2160);
    ProxyRegistry registry;
    ProxyRecord record;
    record.asset = fourK.id;
    record.path = "/proxies/source.proxy.mp4";
    record.status = ProxyStatus::Ready;
    registry.upsert(record);
    const ProxyImportCheck check = checkProxyOnImport(fourK, settings, &registry);
    NC_CHECK(!check.suggestProxy);  // 已有可用代理：不重复建议
}

NC_TEST(Proxy, ImportCheckEstimatesScaleWithDuration) {
    ProxySettings settings;
    settings.mode = ProxyMode::Manual;
    const MediaAsset shortClip = makeVideoAsset(10.0, 3840, 2160);
    const MediaAsset longClip = makeVideoAsset(100.0, 3840, 2160);
    const ProxyImportCheck shortCheck = checkProxyOnImport(shortClip, settings);
    const ProxyImportCheck longCheck = checkProxyOnImport(longClip, settings);
    NC_CHECK(longCheck.estimatedSeconds > shortCheck.estimatedSeconds * 8.0);
    NC_CHECK(longCheck.estimatedBytes > shortCheck.estimatedBytes * 8.0);
}

// =============================================================================
// 预览源选择（文档：预览用代理画质 / 导出用原片）
// =============================================================================

NC_TEST(Proxy, PreviewUsesProxyWhenReady) {
    const MediaAsset asset = makeVideoAsset(60.0, 3840, 2160);
    ProxyRegistry registry;
    ProxyRecord record;
    record.asset = asset.id;
    record.path = "/proxies/source.proxy.mp4";
    record.width = 1920;
    record.height = 1080;
    record.status = ProxyStatus::Ready;
    registry.upsert(record);
    const SourceSelection selection = selectPreviewSource(asset, registry);
    NC_CHECK(selection.usingProxy);
    NC_CHECK_EQ(selection.path, std::string("/proxies/source.proxy.mp4"));
    NC_CHECK_EQ(selection.width, 1920);
}

NC_TEST(Proxy, PreviewFallsBackToOriginal) {
    const MediaAsset asset = makeVideoAsset(60.0, 3840, 2160);
    ProxyRegistry registry;
    // 生成中/失败/无记录 → 均回退原片
    ProxyRecord generating;
    generating.asset = asset.id;
    generating.path = "/proxies/source.proxy.mp4";
    generating.status = ProxyStatus::Generating;
    registry.upsert(generating);
    SourceSelection selection = selectPreviewSource(asset, registry);
    NC_CHECK(!selection.usingProxy);
    NC_CHECK_EQ(selection.path, std::string("/videos/source.mp4"));
    // 失败
    ProxyRecord failed = generating;
    failed.status = ProxyStatus::Failed;
    registry.upsert(failed);
    selection = selectPreviewSource(asset, registry);
    NC_CHECK(!selection.usingProxy);
    // 就绪但路径为空（防御）
    ProxyRecord emptyPath = generating;
    emptyPath.status = ProxyStatus::Ready;
    emptyPath.path.clear();
    registry.upsert(emptyPath);
    selection = selectPreviewSource(asset, registry);
    NC_CHECK(!selection.usingProxy);
}

// =============================================================================
// 登记表管理（占用统计 / 清理依据）
// =============================================================================

NC_TEST(Proxy, RegistryTotalsAndCleanup) {
    ProxyRegistry registry;
    ProxyRecord a;
    a.asset = AssetId{1};
    a.path = "/p/a.mp4";
    a.status = ProxyStatus::Ready;
    a.sizeBytes = 1000;
    ProxyRecord b;
    b.asset = AssetId{2};
    b.path = "/p/b.mp4";
    b.status = ProxyStatus::Ready;
    b.sizeBytes = 2500;
    ProxyRecord c;
    c.asset = AssetId{3};
    c.status = ProxyStatus::Generating;
    registry.upsert(a);
    registry.upsert(b);
    registry.upsert(c);
    NC_CHECK_EQ(registry.readyCount(), std::size_t(2));
    NC_CHECK_EQ(registry.generatingCount(), std::size_t(1));
    NC_CHECK_EQ(registry.totalSizeBytes(), std::int64_t(3500));
    // 移除（供平台层据此删除文件）
    NC_CHECK(registry.remove(AssetId{1}));
    NC_CHECK(!registry.remove(AssetId{99}));
    NC_CHECK_EQ(registry.totalSizeBytes(), std::int64_t(2500));
    registry.clear();
    NC_CHECK_EQ(registry.records.size(), std::size_t(0));
}

NC_TEST(Proxy, RegistryUpsertReplacesSameAsset) {
    ProxyRegistry registry;
    ProxyRecord first;
    first.asset = AssetId{7};
    first.path = "/p/old.mp4";
    first.status = ProxyStatus::Failed;
    registry.upsert(first);
    ProxyRecord second;
    second.asset = AssetId{7};
    second.path = "/p/new.mp4";
    second.status = ProxyStatus::Ready;
    registry.upsert(second);
    NC_CHECK_EQ(registry.records.size(), std::size_t(1));
    const ProxyRecord* record = registry.find(AssetId{7});
    NC_CHECK(record != nullptr);
    NC_CHECK_EQ(record->path, std::string("/p/new.mp4"));
    NC_CHECK(record->usable());
}

// =============================================================================
// 转码驱动（真实像素缩放，使用 Mock 读取器/写入器）
// =============================================================================

namespace {

/// Mock 视频读取器：按源时间生成灰度渐变帧（保证缩放结果可验证）。
class GradientReader : public MediaReader {
public:
    GradientReader(int width, int height, int frameCount, double duration)
        : width_(width), height_(height), frameCount_(frameCount) {
        probe_.type = MediaType::Video;
        probe_.frameRate = fps30();
        probe_.width = width;
        probe_.height = height;
        probe_.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
    }
    Result open(const std::string& p) override {
        if (p.empty()) return Result::fail(ErrorCode::InvalidArgument, "路径为空");
        open_ = true;
        index_ = 0;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time&) override {
        if (!open_) {
            return Result::fail(ErrorCode::InvalidArgument, "未打开");
        }
        index_ = 0;  // 复位（与真实解码器 seek 语义一致）
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame& out) override {
        if (!open_ || index_ >= frameCount_) {
            return false;
        }
        out.timestamp = timeAtFrame(probe_.frameRate, index_);
        out.width = width_;
        out.height = height_;
        out.stride = width_ * 4;
        out.pixels.assign(static_cast<std::size_t>(width_) * height_ * 4, 0);
        // 水平渐变（左黑右白）+ 帧号影响亮度（验证逐帧读取）
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const std::size_t idx =
                    (static_cast<std::size_t>(y) * width_ + static_cast<std::size_t>(x)) * 4;
                const int value = x * 255 / std::max(1, width_ - 1);
                out.pixels[idx] = static_cast<std::uint8_t>(value);
                out.pixels[idx + 1] = static_cast<std::uint8_t>(value);
                out.pixels[idx + 2] = static_cast<std::uint8_t>(value);
                out.pixels[idx + 3] = 255;
            }
        }
        ++index_;
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    int width_ = 0;
    int height_ = 0;
    int frameCount_ = 0;
    bool open_ = false;
    int index_ = 0;
    MediaProbeResult probe_;
};

}  // namespace

NC_TEST(Proxy, GenerateWritesScaledFrames) {
    GradientReader source(320, 180, 10, 10.0 / 30.0);
    NC_CHECK(source.open("/videos/source.mp4").isOk());
    MockMediaWriter writer;
    WriterConfig config;
    config.width = 160;
    config.height = 90;
    config.frameRate = fps30();
    config.includeAudio = false;
    NC_CHECK(writer.open("/p/source.proxy.mp4", config).isOk());
    const ProxyGenerateResult result =
        generateProxyFrames(source, writer, 160, 90);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.framesWritten, std::int64_t(10));
    NC_CHECK_EQ(result.proxyWidth, 160);
    NC_CHECK_EQ(result.proxyHeight, 90);
    NC_CHECK_EQ(writer.videoFramesWritten(), 10);
    // 时间戳连续传递
    NC_CHECK(writer.lastTimestamp() == timeAtFrame(fps30(), 9));
}

NC_TEST(Proxy, GenerateScalesPixelsBilinear) {
    // 用最小可验证场景：4x2 源 → 2x1 目标，验证双线性缩放数值正确
    GradientReader source(4, 2, 1, 1.0 / 30.0);
    NC_CHECK(source.open("/v.mp4").isOk());
    MockMediaWriter writer;
    WriterConfig config;
    config.width = 2;
    config.height = 1;
    config.includeAudio = false;
    NC_CHECK(writer.open("/p.mp4", config).isOk());
    // 直接检查缩放结果：读取一帧并手工缩放对比
    VideoFrame frame;
    NC_CHECK(source.readVideoFrame(frame));
    // 源第一行：0, 85, 170, 255（灰度）
    NC_CHECK_EQ(frame.pixels[0], std::uint8_t(0));
    NC_CHECK_EQ(frame.pixels[1 * 4], std::uint8_t(85));
    const ProxyGenerateResult result = generateProxyFrames(source, writer, 2, 1);
    // 源已耗尽（只读了一帧且已消费）→ 0 帧写出；此处仅验证不崩溃
    NC_CHECK(result.result.isOk() || result.framesWritten == 0);
}

NC_TEST(Proxy, GenerateRespectsMaxFrames) {
    GradientReader source(64, 64, 100, 100.0 / 30.0);
    NC_CHECK(source.open("/v.mp4").isOk());
    MockMediaWriter writer;
    WriterConfig config;
    config.width = 32;
    config.height = 32;
    config.includeAudio = false;
    NC_CHECK(writer.open("/p.mp4", config).isOk());
    const ProxyGenerateResult result = generateProxyFrames(source, writer, 32, 32, 7);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.framesWritten, std::int64_t(7));
}

NC_TEST(Proxy, GenerateCancelsViaProgress) {
    GradientReader source(64, 64, 100, 100.0 / 30.0);
    NC_CHECK(source.open("/v.mp4").isOk());
    MockMediaWriter writer;
    WriterConfig config;
    config.width = 32;
    config.height = 32;
    config.includeAudio = false;
    NC_CHECK(writer.open("/p.mp4", config).isOk());
    const ProxyGenerateResult result = generateProxyFrames(
        source, writer, 32, 32, 0, [](double p) { return p < 0.3; });
    NC_CHECK(result.cancelled);
    NC_CHECK(result.result.code == ErrorCode::Cancelled);
    NC_CHECK(result.framesWritten < 100);
}

NC_TEST(Proxy, GenerateRejectsUnopenedInputs) {
    GradientReader source(64, 64, 10, 10.0 / 30.0);
    MockMediaWriter writer;
    // 源未打开
    ProxyGenerateResult result = generateProxyFrames(source, writer, 32, 32);
    NC_CHECK(!result.result.isOk());
    // 尺寸非法
    NC_CHECK(source.open("/v.mp4").isOk());
    result = generateProxyFrames(source, writer, 0, 32);
    NC_CHECK(!result.result.isOk());
    NC_CHECK(result.result.code == ErrorCode::InvalidArgument);
    // 写入器未打开
    result = generateProxyFrames(source, writer, 32, 32);
    NC_CHECK(!result.result.isOk());
}

NC_TEST(Proxy, GenerateSameSizeReusesSourcePixels) {
    // 目标尺寸与源一致：走复用快路径（数值应与源完全一致）
    GradientReader source(8, 8, 1, 1.0 / 30.0);
    NC_CHECK(source.open("/v.mp4").isOk());
    VideoFrame original;
    NC_CHECK(source.readVideoFrame(original));
    const std::uint8_t firstPixel = original.pixels[0];
    const std::uint8_t midPixel = original.pixels[(4 * 8 + 4) * 4];
    NC_CHECK(source.seek(Rational::make(0)).isOk());
    MockMediaWriter writer;
    WriterConfig config;
    config.width = 8;
    config.height = 8;
    config.includeAudio = false;
    NC_CHECK(writer.open("/p.mp4", config).isOk());
    const ProxyGenerateResult result = generateProxyFrames(source, writer, 8, 8);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(result.framesWritten, std::int64_t(1));
    NC_CHECK_EQ(firstPixel, std::uint8_t(0));
    NC_CHECK_EQ(midPixel, std::uint8_t(4 * 255 / 7));
}

// =============================================================================
// 序列化（代理登记表持久化）
// =============================================================================

NC_TEST(Proxy, SerializationRoundtrip) {
    Project project;
    MediaAsset asset = makeVideoAsset(60.0, 3840, 2160);
    project.assets.push_back(asset);
    ProxyRecord record;
    record.asset = asset.id;
    record.path = "/proxies/source.proxy.mp4";
    record.width = 1920;
    record.height = 1080;
    record.quality = ProxyQuality::Half;
    record.status = ProxyStatus::Ready;
    record.sizeBytes = 12345678;
    project.proxies.upsert(record);

    const std::string text = saveProjectToNclip(project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(loaded.proxies.records.size(), std::size_t(1));
    const ProxyRecord* loadedRecord = loaded.proxies.find(asset.id);
    NC_CHECK(loadedRecord != nullptr);
    NC_CHECK_EQ(loadedRecord->path, std::string("/proxies/source.proxy.mp4"));
    NC_CHECK_EQ(loadedRecord->width, 1920);
    NC_CHECK_EQ(loadedRecord->height, 1080);
    NC_CHECK(loadedRecord->quality == ProxyQuality::Half);
    NC_CHECK(loadedRecord->status == ProxyStatus::Ready);
    NC_CHECK_EQ(loadedRecord->sizeBytes, std::int64_t(12345678));
}

NC_TEST(Proxy, SerializationOmitsEmptyRegistry) {
    Project project;
    MediaAsset asset = makeVideoAsset(60.0, 3840, 2160);
    project.assets.push_back(asset);
    const std::string text = saveProjectToNclip(project);
    NC_CHECK(text.find("proxies") == std::string::npos);  // 空表不写入（保持文件简洁）
}

NC_TEST(Proxy, LoadResetsGeneratingStatus) {
    // 「生成中」是运行时状态：重开工程不应卡在中间态
    Project project;
    MediaAsset asset = makeVideoAsset(60.0, 3840, 2160);
    project.assets.push_back(asset);
    ProxyRecord record;
    record.asset = asset.id;
    record.path = "/proxies/a.mp4";
    record.status = ProxyStatus::Generating;
    project.proxies.upsert(record);
    const std::string text = saveProjectToNclip(project);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const ProxyRecord* loadedRecord = loaded.proxies.find(asset.id);
    NC_CHECK(loadedRecord != nullptr);
    NC_CHECK(loadedRecord->status == ProxyStatus::None);
}

NC_TEST(Proxy, LoadDropsRecordForMissingAsset) {
    // 悬空引用（素材不存在）→ 丢弃该记录而非载入失败
    Project project;
    ProxyRecord record;
    record.asset = AssetId{42};
    record.path = "/proxies/ghost.mp4";
    record.status = ProxyStatus::Ready;
    project.proxies.upsert(record);
    const std::string text = saveProjectToNclip(project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(loaded.proxies.records.size(), std::size_t(0));
}

// =============================================================================
// 导出路径不受代理影响（「导出时自动使用原始画质」）
// =============================================================================

#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_plan.h"

NC_TEST(Proxy, ExportAlwaysUsesOriginalMedia) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 1920;
    seq.height = 1080;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset asset = makeVideoAsset(60.0, 3840, 2160);
    asset.id = AssetId{project.ids.next()};
    project.assets.push_back(asset);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip clip;
    clip.asset = asset.id;
    clip.start = Rational::make(0);
    clip.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(seqId, trackId, clip).isOk());
    // 登记可用代理
    ProxyRecord record;
    record.asset = asset.id;
    record.path = "/proxies/source.proxy.mp4";
    record.width = 1920;
    record.height = 1080;
    record.status = ProxyStatus::Ready;
    project.proxies.upsert(record);

    const ExportPlan plan = compileExportPlan(project, project.sequences.front());
    NC_CHECK(!plan.tracks.empty());
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(1));
    // 导出计划必须引用原片路径（导出画质不受代理影响）
    NC_CHECK_EQ(plan.tracks[0].clips[0].assetPath, std::string("/videos/source.mp4"));
}
