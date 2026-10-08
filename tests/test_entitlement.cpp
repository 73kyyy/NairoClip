#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/entitlement.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/export_runner.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 记录配置的 Mock 写入器（用于检查门控后的输出尺寸）。
class CapturingWriter : public MediaWriter {
public:
    Result open(const std::string& path, const WriterConfig& config) override {
        path_ = path;
        config_ = config;
        open_ = true;
        framesWritten_ = 0;
        return Result::ok();
    }
    Result writeVideoFrame(const OutputVideoFrame&) override {
        if (!open_) return Result::fail(ErrorCode::InvalidArgument, "未打开");
        ++framesWritten_;
        return Result::ok();
    }
    Result writeAudioBlock(const OutputAudioBlock&) override { return Result::ok(); }
    Result finish() override {
        open_ = false;
        finished_ = true;
        return Result::ok();
    }
    void abort() override { open_ = false; }
    bool isOpen() const override { return open_; }

    const WriterConfig& config() const { return config_; }
    int framesWritten() const { return framesWritten_; }
    bool finished() const { return finished_; }

private:
    std::string path_;
    WriterConfig config_;
    bool open_ = false;
    bool finished_ = false;
    int framesWritten_ = 0;
};

class CapturingWriterFactory : public MediaWriterFactory {
public:
    std::unique_ptr<MediaWriter> create() override {
        auto writer = std::make_unique<CapturingWriter>();
        last_ = writer.get();
        return writer;
    }
    CapturingWriter* last_ = nullptr;
};

class FixedReader : public MediaReader {
public:
    FixedReader() {
        cfg_.probe.type = MediaType::Video;
        cfg_.probe.frameRate = fps30();
        cfg_.probe.width = 320;
        cfg_.probe.height = 240;
        cfg_.probe.duration = Rational::make(10);
        cfg_.videoFrameCount = 100000;
        reader_ = std::make_unique<MockMediaReader>(cfg_);
    }
    Result open(const std::string& p) override { return reader_->open(p); }
    void close() override { reader_->close(); }
    bool isOpen() const override { return reader_->isOpen(); }
    const MediaProbeResult& probe() const override { return reader_->probe(); }
    Result seek(const Time& t) override { return reader_->seek(t); }
    bool readVideoFrame(VideoFrame& o) override { return reader_->readVideoFrame(o); }
    bool readAudioBlock(AudioBlock& o) override { return reader_->readAudioBlock(o); }

private:
    MockMediaReader::Config cfg_;
    std::unique_ptr<MockMediaReader> reader_;
};

class FixedFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<FixedReader>(); }
};

/// 构造指定分辨率与单片段工程的导出计划。
struct PlanFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    PlanFixture(int width, int height) {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = width;
        seq.height = height;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/videos/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        TrackId trackId;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
        Clip c;
        c.asset = a.id;
        c.start = Rational::make(0);
        c.duration = Rational::make(1);  // 30 帧
        editor.insertClip(seqId, trackId, c);
    }
    ExportPlan plan() const { return compileExportPlan(project, *project.mainSequence()); }
};

}  // namespace

// =============================================================================
// 权益矩阵（文档 6.3.1 / 7.6）
// =============================================================================

NC_TEST(Entitlement, FreeTierLimits) {
    Entitlement free;
    NC_CHECK(free.tier == MembershipTier::Free);
    NC_CHECK_EQ(free.maxTracks(), 4);                    // 主轨 + 3 = 4
    NC_CHECK_EQ(free.maxExportQueue(), 1);
    NC_CHECK_EQ(free.maxExportShortSide(), 1080);
    NC_CHECK(free.watermarkRequired());
    NC_CHECK_EQ(free.subtitleMinutesPerMonth(), 30);
    NC_CHECK_EQ(free.aiGeneratePerMonth(), 5);
    NC_CHECK(!free.frameInterpolation());
    NC_CHECK(!free.highPrecisionMatting());
}

NC_TEST(Entitlement, ProTierLimits) {
    Entitlement pro;
    pro.tier = MembershipTier::Pro;
    NC_CHECK_EQ(pro.maxTracks(), 0);      // 0 = 无限
    NC_CHECK_EQ(pro.maxExportQueue(), 5);
    NC_CHECK_EQ(pro.maxExportShortSide(), 0);
    NC_CHECK(!pro.watermarkRequired());
    NC_CHECK_EQ(pro.subtitleMinutesPerMonth(), 0);
    NC_CHECK_EQ(pro.aiGeneratePerMonth(), 0);
    NC_CHECK(pro.frameInterpolation());
    NC_CHECK(pro.highPrecisionMatting());
}

NC_TEST(Entitlement, TrackGateTrigger) {
    Entitlement free;
    NC_CHECK(free.canAddTrack(0));
    NC_CHECK(free.canAddTrack(3));
    NC_CHECK(!free.canAddTrack(4));  // 第 5 条 → 付费墙（文档 3.3）
    NC_CHECK(!free.canAddTrack(-1)); // 非法计数拒绝
    Entitlement pro;
    pro.tier = MembershipTier::Pro;
    NC_CHECK(pro.canAddTrack(4));
    NC_CHECK(pro.canAddTrack(100));  // 无限轨道
}

NC_TEST(Entitlement, ProActiveAtRespectsExpiry) {
    Entitlement pro;
    pro.tier = MembershipTier::Pro;
    // 无到期信息：生效（以服务端为准）
    NC_CHECK(pro.proActiveAt(1000));
    // 有到期时间：到期后失效（文档 3.10：到期后导出降级）
    pro.expiresAtUnix = 5000;
    NC_CHECK(pro.proActiveAt(4999));
    NC_CHECK(!pro.proActiveAt(5000));
    // 永久买断：不受时间影响
    pro.lifetime = true;
    NC_CHECK(pro.proActiveAt(999999999));
    // 免费层级始终不生效
    Entitlement free;
    NC_CHECK(!free.proActiveAt(0));
}

// =============================================================================
// 分辨率降级（文档 3.10：导出时强制降级 1080p）
// =============================================================================

NC_TEST(Entitlement, ClampSizeToShortSide) {
    int w = 0;
    int h = 0;
    // 4K 横屏 → 1080p
    NC_CHECK(clampSizeToShortSide(3840, 2160, 1080, w, h));
    NC_CHECK_EQ(w, 1920);
    NC_CHECK_EQ(h, 1080);
    // 4K 竖屏 → 1080×1920
    NC_CHECK(clampSizeToShortSide(2160, 3840, 1080, w, h));
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1920);
    // 2560×1440 → 1920×1080（等比）
    NC_CHECK(clampSizeToShortSide(2560, 1440, 1080, w, h));
    NC_CHECK_EQ(w, 1920);
    NC_CHECK_EQ(h, 1080);
    // 超宽 3840×1600 → 短边 1600 > 1080 → 2592×1080
    NC_CHECK(clampSizeToShortSide(3840, 1600, 1080, w, h));
    NC_CHECK_EQ(w, 2592);
    NC_CHECK_EQ(h, 1080);
    // 720p 不放大（也返回 false）
    NC_CHECK(!clampSizeToShortSide(1280, 720, 1080, w, h));
    NC_CHECK_EQ(w, 1280);
    NC_CHECK_EQ(h, 720);
    // 1080p 本身不变
    NC_CHECK(!clampSizeToShortSide(1920, 1080, 1080, w, h));
    // 上限 0 = 不限制
    NC_CHECK(!clampSizeToShortSide(3840, 2160, 0, w, h));
    NC_CHECK_EQ(w, 3840);
}

NC_TEST(Entitlement, MakeExportPolicy) {
    // 免费：1080 上限 + 水印
    const ExportPolicy freePolicy = makeExportPolicy(Entitlement{}, 0);
    NC_CHECK_EQ(freePolicy.maxExportShortSide, 1080);
    NC_CHECK(freePolicy.watermark.enabled);
    // Pro 生效：不限制、无水印
    Entitlement pro;
    pro.tier = MembershipTier::Pro;
    const ExportPolicy proPolicy = makeExportPolicy(pro, 0);
    NC_CHECK_EQ(proPolicy.maxExportShortSide, 0);
    NC_CHECK(!proPolicy.watermark.enabled);
    // 过期 Pro：fail-closed → 降级 + 水印
    pro.expiresAtUnix = 100;
    const ExportPolicy expired = makeExportPolicy(pro, 200);
    NC_CHECK_EQ(expired.maxExportShortSide, 1080);
    NC_CHECK(expired.watermark.enabled);
}

// =============================================================================
// 导出集成（门控在 runExport 中真实生效）
// =============================================================================

NC_TEST(Entitlement, FreePolicyClampsAndWatermarks) {
    PlanFixture f(3840, 2160);  // 4K 工程
    const ExportPlan plan = f.plan();
    NC_CHECK_EQ(plan.width, 3840);
    FixedFactory readerFactory;
    CapturingWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/ent_free.mp4";
    config.writer.includeAudio = false;
    config.policy = makeExportPolicy(Entitlement{}, 0);  // 免费门控
    std::vector<std::pair<int, int>> frameSizes;
    const ExportRunResult result = runExport(
        plan, readerFactory, writerFactory, config,
        [&](double) { return true; }, nullptr);
    NC_CHECK(result.result.isOk());
    // 写入器被门控降级到 1920×1080
    NC_CHECK(writerFactory.last_ != nullptr);
    NC_CHECK_EQ(writerFactory.last_->config().width, 1920);
    NC_CHECK_EQ(writerFactory.last_->config().height, 1080);
    // 警告中有门控说明（两个：降级 + 水印）
    bool sawClamp = false;
    bool sawWatermark = false;
    for (const auto& w : result.stats.warnings) {
        if (w.find("降级") != std::string::npos) sawClamp = true;
        if (w.find("水印") != std::string::npos) sawWatermark = true;
    }
    NC_CHECK(sawClamp);
    NC_CHECK(sawWatermark);
    (void)frameSizes;
}

NC_TEST(Entitlement, ProPolicyKeepsResolutionAndNoWatermark) {
    PlanFixture f(3840, 2160);
    const ExportPlan plan = f.plan();
    FixedFactory readerFactory;
    CapturingWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/ent_pro.mp4";
    config.writer.includeAudio = false;
    Entitlement pro;
    pro.tier = MembershipTier::Pro;
    config.policy = makeExportPolicy(pro, 0);
    const ExportRunResult result =
        runExport(plan, readerFactory, writerFactory, config, nullptr, nullptr);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(writerFactory.last_->config().width, 3840);
    NC_CHECK_EQ(writerFactory.last_->config().height, 2160);
    for (const auto& w : result.stats.warnings) {
        NC_CHECK(w.find("水印") == std::string::npos);
    }
}

NC_TEST(Entitlement, DefaultPolicyNoGating) {
    // 未接入会员体系（默认策略）：行为与既有路径一致（不降级、无水印、无额外警告）
    PlanFixture f(1280, 720);
    const ExportPlan plan = f.plan();
    FixedFactory readerFactory;
    CapturingWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/ent_default.mp4";
    config.writer.includeAudio = false;
    const ExportRunResult result =
        runExport(plan, readerFactory, writerFactory, config, nullptr, nullptr);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(writerFactory.last_->config().width, 1280);
    NC_CHECK_EQ(writerFactory.last_->config().height, 720);
    for (const auto& w : result.stats.warnings) {
        NC_CHECK(w.find("会员门控") == std::string::npos);
    }
}

NC_TEST(Entitlement, WatermarkAppliedToClampedFrames) {
    // 4K 工程 + 免费策略 → 输出帧为 1920×1080 且右下角有真实水印像素
    PlanFixture f(3840, 2160);
    const ExportPlan plan = f.plan();
    FixedFactory readerFactory;
    CapturingWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/ent_wm.mp4";
    config.writer.includeAudio = false;
    config.policy = makeExportPolicy(Entitlement{}, 0);
    // 用自定义 sink 无法直连 runExport；改为直接驱动执行器校验水印像素
    ExportExecutorOptions options;
    options.watermark = config.policy.watermark;
    const ExportPlan clamped = [&]() {
        ExportPlan p = plan;
        int w = 0;
        int h = 0;
        if (clampSizeToShortSide(plan.width, plan.height, 1080, w, h)) {
            p.width = w;
            p.height = h;
        }
        return p;
    }();
    NC_CHECK_EQ(clamped.width, 1920);
    FixedFactory factory;
    int checked = 0;
    executeExport(
        clamped, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                NC_CHECK_EQ(frame.width, 1920);
                NC_CHECK_EQ(frame.height, 1080);
                // 右下角水印区域（16px 边距、宽 154）：存在亮像素
                WatermarkGeometry g;
                NC_CHECK(computeWatermarkGeometry(options.watermark, 1920, 1080, g));
                int bright = 0;
                for (int y = g.blockY; y < g.blockY + g.blockH; ++y) {
                    for (int x = g.blockX; x < g.blockX + g.blockW; ++x) {
                        const std::size_t idx =
                            (static_cast<std::size_t>(y) * 1920 + static_cast<std::size_t>(x)) * 4;
                        if (frame.rgba[idx] > 150) ++bright;
                    }
                }
                NC_CHECK(bright > 10);
                // 水印说明文字描述符存在
                bool sawCaption = false;
                for (const auto& item : frame.subtitles) {
                    if (item.text == "用剪辑软件制作") sawCaption = true;
                }
                NC_CHECK(sawCaption);
            }
            ++checked;
        },
        nullptr, nullptr, options, &f.project);
    NC_CHECK(checked > 0);
}
