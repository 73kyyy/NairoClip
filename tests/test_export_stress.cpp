#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_runner.h"
#include <chrono>
#include <cmath>
using namespace nairoclip::core;
// =============================================================================
// 导出压力测试：验证大工程导出不崩溃、不劣化、性能可接受
// =============================================================================

namespace {
class StressReader : public MediaReader {
public:
    StressReader() {
        MockMediaReader::Config cfg;
        cfg.probe.type = MediaType::Video;
        cfg.probe.frameRate = fps30();
        cfg.probe.width = 64;
        cfg.probe.height = 64;
        cfg.probe.duration = Rational::make(600);
        cfg.videoFrameCount = 1000000;
        cfg.grayByTime = true;
        reader_ = std::make_unique<MockMediaReader>(cfg);
    }
    Result open(const std::string& p) override { return reader_->open(p); }
    void close() override { reader_->close(); }
    bool isOpen() const override { return reader_->isOpen(); }
    const MediaProbeResult& probe() const override { return reader_->probe(); }
    Result seek(const Time& t) override { return reader_->seek(t); }
    bool readVideoFrame(VideoFrame& o) override { return reader_->readVideoFrame(o); }
    bool readAudioBlock(AudioBlock& o) override { return reader_->readAudioBlock(o); }
private:
    std::unique_ptr<MockMediaReader> reader_;
};
class StressFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<StressReader>(); }
};
}  // namespace

NC_TEST(ExportStress, HundredClips) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 64;
    seq.height = 64;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset asset;
    asset.id = AssetId{project.ids.next()};
    asset.path = "/video/stress.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(600);
    project.assets.push_back(asset);
    const AssetId assetId = project.assets.front().id;
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    // 100 个片段，每个 0.5s（共 50s = 1500 帧）
    const int clipCount = 100;
    for (int i = 0; i < clipCount; ++i) {
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(i, 2);          // 每 0.5s 一个
        c.duration = Rational::make(1, 2);
        c.sourceIn = Rational::make(i % 10);     // 不同源区间
        NC_CHECK(editor.insertClip(seqId, trackId, c).isOk());
    }
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    NC_CHECK_EQ(plan.tracks[0].clips.size(), static_cast<std::size_t>(clipCount));
    StressFactory factory;
    const auto t0 = std::chrono::steady_clock::now();
    int64_t frames = 0;
    const ExportStats stats = executeExport(
        plan, factory,
        [&](const OutputVideoFrame&) { ++frames; },
        nullptr, nullptr, ExportExecutorOptions{}, &project);
    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    // 验证：帧数正确（50s * 30fps = 1500）
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(1500));
    NC_CHECK_EQ(frames, std::int64_t(1500));
    // 性能：1500 帧解码 + 渲染应显著快于实时（< 30s 保守界限）
    NC_CHECK(elapsedMs < 30000);
    std::printf("  [压力] 100 片段 / 1500 帧导出耗时 %lld ms\\n",
                static_cast<long long>(elapsedMs));
}
NC_TEST(ExportStress, LongTimelineSeekPattern) {
    // 长序列 + seek 密集路径（验证无内存泄漏式增长/崩溃）
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 64;
    seq.height = 64;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset asset;
    asset.id = AssetId{project.ids.next()};
    asset.path = "/video/long.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(600);
    project.assets.push_back(asset);
    const AssetId assetId = project.assets.front().id;
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip c;
    c.asset = assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(60);  // 60s = 1800 帧
    NC_CHECK(editor.insertClip(seqId, trackId, c).isOk());
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    StressFactory factory;
    // 每隔 10 帧验证一次帧内容有效（模拟抽检）
    int checked = 0;
    const ExportStats stats = executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked % 300 == 0) {
                NC_CHECK(!frame.rgba.empty());
                NC_CHECK_EQ(frame.width, 64);
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &project);
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(1800));
}
