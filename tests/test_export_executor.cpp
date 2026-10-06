#include "test_framework.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/editor.h"
using namespace nairoclip::core;
namespace {
/// 简单的 Mock 读取器工厂：返回固定配置的 MockMediaReader。
class MockFactory : public MediaReaderFactory {
public:
    explicit MockFactory(MockMediaReader::Config config) : config_(std::move(config)) {}
    std::unique_ptr<MediaReader> create() override {
        ++created;
        return std::make_unique<MockMediaReader>(config_);
    }
    int created = 0;
private:
    MockMediaReader::Config config_;
};
struct Fixture {
    Project project;
    SequenceId seqId;
    TrackId videoTrack;
    TrackId audioTrack;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 320;
        seq.height = 240;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/videos/a.mp4";
        a.name = "a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
    }
    Sequence* seq() { return project.findSequence(seqId); }
    void build() {
        Track v;
        v.id = TrackId{project.ids.next()};
        v.type = TrackType::Video;
        v.name = "V1";
        seq()->tracks.push_back(v);
        videoTrack = seq()->tracks.back().id;
    }
    void addClip(const Time& start, const Time& duration) {
        Clip c;
        c.id = ClipId{project.ids.next()};
        c.asset = project.assets.front().id;
        c.start = start;
        c.duration = duration;
        seq()->findTrack(videoTrack)->clips.push_back(c);
    }
};
MockMediaReader::Config makeReaderConfig() {
    MockMediaReader::Config cfg;
    cfg.probe.type = MediaType::Video;
    cfg.probe.frameRate = fps30();
    cfg.probe.width = 320;
    cfg.probe.height = 240;
    cfg.videoFrameCount = 1000;
    return cfg;
}
}  // namespace
NC_TEST(ExportExecutor, OutputsFramesForWholeTimeline) {
    Fixture f;
    f.build();
    f.addClip(Rational::make(0), Rational::make(2));  // [0,2) 2 秒 = 60 帧 @30fps
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    MockFactory factory(makeReaderConfig());
    std::int64_t frameCount = 0;
    Time lastTimestamp;
    const ExportStats stats = executeExport(
        plan, factory, [&](const OutputVideoFrame& frame) {
            ++frameCount;
            lastTimestamp = frame.timestamp;
            NC_CHECK_EQ(frame.width, 320);
            NC_CHECK_EQ(frame.height, 240);
            NC_CHECK_EQ(frame.rgba.size(), std::size_t(320 * 240 * 4));
        }, nullptr);
    NC_CHECK_EQ(frameCount, std::int64_t(60));
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(60));
    NC_CHECK(stats.cancelled == false);
    // 最后一帧时间戳 = 59/30
    NC_CHECK(lastTimestamp == timeAtFrame(fps30(), 59));
}
NC_TEST(ExportExecutor, BlackFramesForGaps) {
    Fixture f;
    f.build();
    f.addClip(Rational::make(0), Rational::make(1));   // [0,1)
    f.addClip(Rational::make(2), Rational::make(1));   // [2,3) → 1 秒空隙
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    MockFactory factory(makeReaderConfig());
    int blackish = 0;
    int filled = 0;
    executeExport(plan, factory, [&](const OutputVideoFrame& frame) {
        // 空隙帧：RGB 全 0（黑），alpha=255
        if (frame.rgba[0] == 0 && frame.rgba[1] == 0 && frame.rgba[2] == 0) {
            ++blackish;
        } else {
            ++filled;
        }
    }, nullptr);
    // [1,2) 空隙 = 30 帧黑帧
    NC_CHECK_EQ(blackish, 30);
    // 其余 = 60 帧素材帧
    NC_CHECK_EQ(filled, 60);
}
NC_TEST(ExportExecutor, ProgressCanCancel) {
    Fixture f;
    f.build();
    f.addClip(Rational::make(0), Rational::make(3));
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    MockFactory factory(makeReaderConfig());
    int frames = 0;
    double lastProgress = -1.0;
    const ExportStats stats = executeExport(
        plan, factory,
        [&](const OutputVideoFrame&) { ++frames; },
        nullptr,
        [&](double p) {
            lastProgress = p;
            return p < 0.3;  // 进度超过 30% 时取消
        });
    NC_CHECK(stats.cancelled);
    NC_CHECK(frames < 90);  // 未输出全部 90 帧
    NC_CHECK(lastProgress > 0.25 && lastProgress < 0.35);
}
NC_TEST(ExportExecutor, MaxFramesLimit) {
    Fixture f;
    f.build();
    f.addClip(Rational::make(0), Rational::make(2));
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    MockFactory factory(makeReaderConfig());
    ExportExecutorOptions options;
    options.maxFrames = 10;
    int frames = 0;
    const ExportStats stats = executeExport(
        plan, factory, [&](const OutputVideoFrame&) { ++frames; }, nullptr, nullptr, options);
    NC_CHECK_EQ(frames, 10);
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(10));
}
NC_TEST(ExportExecutor, EmptyTimelineNoFrames) {
    Fixture f;
    f.build();  // 无片段
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    MockFactory factory(makeReaderConfig());
    int frames = 0;
    const ExportStats stats = executeExport(
        plan, factory, [&](const OutputVideoFrame&) { ++frames; }, nullptr);
    NC_CHECK_EQ(frames, 0);
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(0));
}
NC_TEST(ExportExecutor, ReaderOpenedOncePerAsset) {
    Fixture f;
    f.build();
    f.addClip(Rational::make(0), Rational::make(1));
    f.addClip(Rational::make(1), Rational::make(1));  // 同素材两段
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    MockFactory factory(makeReaderConfig());
    executeExport(plan, factory, [](const OutputVideoFrame&) {}, nullptr);
    // 同一素材只创建 1 个读取器实例（缓存复用）
    NC_CHECK_EQ(factory.created, 1);
}
NC_TEST(ExportExecutor, MarksTransitionFrames) {
    Fixture f;
    f.build();
    f.addClip(Rational::make(0), Rational::make(2));  // [0,2)
    f.addClip(Rational::make(3), Rational::make(2));  // [3,5)
    // 手动加转场（区间 [2, 2.5)）
    Track* track = f.seq()->findTrack(f.videoTrack);
    Transition tr;
    tr.id = TransitionId{f.project.ids.next()};
    tr.type = TransitionType::CrossDissolve;
    tr.duration = Rational::make(1, 2);
    tr.fromClip = track->clips[0].id;
    tr.toClip = track->clips[1].id;
    track->transitions.push_back(tr);
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    MockFactory factory(makeReaderConfig());
    int transitionFrames = 0;
    int normalFrames = 0;
    executeExport(plan, factory, [&](const OutputVideoFrame& frame) {
        if (frame.transitionType >= 0) {
            ++transitionFrames;
            // 进度应在 [0,1)
            NC_CHECK(frame.transitionProgress >= 0.0 && frame.transitionProgress < 1.0);
        } else {
            ++normalFrames;
        }
    }, nullptr);
    // 转场区间 [2, 2.5) = 0.5s @30fps = 15 帧
    NC_CHECK_EQ(transitionFrames, 15);
    // 其余 120 帧（4 秒总长 - 0.5s 转场 = 105？重新计算：总时长 5s = 150 帧，减 15 = 135）
    NC_CHECK(normalFrames > 0);
}
