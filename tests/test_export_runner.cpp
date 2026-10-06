#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/export_runner.h"
using namespace nairoclip::core;
namespace {
class RunnerMockReader : public MediaReader {
public:
    explicit RunnerMockReader() {
        cfg_.probe.type = MediaType::Video;
        cfg_.probe.frameRate = fps30();
        cfg_.probe.width = 64;
        cfg_.probe.height = 64;
        cfg_.probe.duration = Rational::make(60);
        cfg_.videoFrameCount = 100000;
        reader_ = std::make_unique<MockMediaReader>(cfg_);
    }
    Result open(const std::string& path) override { return reader_->open(path); }
    void close() override { reader_->close(); }
    bool isOpen() const override { return reader_->isOpen(); }
    const MediaProbeResult& probe() const override { return reader_->probe(); }
    Result seek(const Time& t) override { return reader_->seek(t); }
    bool readVideoFrame(VideoFrame& out) override { return reader_->readVideoFrame(out); }
    bool readAudioBlock(AudioBlock& out) override { return reader_->readAudioBlock(out); }
private:
    MockMediaReader::Config cfg_;
    std::unique_ptr<MockMediaReader> reader_;
};
class RunnerFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<RunnerMockReader>(); }
};
struct Fixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 64;
        seq.height = 64;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/video/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
    }
};
}  // namespace
NC_TEST(ExportRunner, SuccessfulExport) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);  // 1 秒 = 30 帧
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    MockWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/export_test.mp4";
    config.writer.width = 64;
    config.writer.height = 64;
    config.writer.includeAudio = false;
    const ExportRunResult result =
        runExport(plan, readerFactory, writerFactory, config);
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.fileWritten);
    NC_CHECK_EQ(result.stats.framesWritten, std::int64_t(30));
    NC_CHECK(!result.stats.cancelled);
}
NC_TEST(ExportRunner, CancelledExportAborts) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    MockWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/export_cancel.mp4";
    config.writer.width = 64;
    config.writer.height = 64;
    config.writer.includeAudio = false;
    // 进度到 30% 时取消
    int progressCalls = 0;
    const ExportRunResult result = runExport(plan, readerFactory, writerFactory, config,
                                             [&](double p) {
                                                 ++progressCalls;
                                                 return p < 0.3;
                                             });
    NC_CHECK(!result.result.isOk());
    NC_CHECK(result.result.code == ErrorCode::Cancelled);
    NC_CHECK(result.stats.cancelled);
    NC_CHECK(!result.fileWritten);
    NC_CHECK(progressCalls > 0);
}
NC_TEST(ExportRunner, WriterOpenFailurePropagates) {
    Fixture f;
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    MockWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "";  // 空路径 → open 失败
    config.writer.width = 64;
    config.writer.height = 64;
    const ExportRunResult result =
        runExport(plan, readerFactory, writerFactory, config);
    NC_CHECK(!result.result.isOk());
    NC_CHECK(!result.fileWritten);
}

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#include <cstdio>
#include <fstream>
/// 真实端到端：Mock 解码（可控帧）→ FFmpeg 编码 → FFmpeg 回读验证。
NC_TEST(ExportRunner, RealEncodeEndToEnd) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);  // 30 帧
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    class RealWriterFactory : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    RealWriterFactory writerFactory;
    const char* path = "/tmp/media_gen/runner_e2e.mp4";
    std::remove(path);
    ExportRunConfig config;
    config.outputPath = path;
    config.writer.width = 64;
    config.writer.height = 64;
    config.writer.frameRate = fps30();
    config.writer.crf = 30;
    config.writer.includeAudio = false;
    const ExportRunResult result = runExport(plan, readerFactory, writerFactory, config);
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.fileWritten);
    NC_CHECK_EQ(result.stats.framesWritten, std::int64_t(30));
    // 文件存在
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    NC_CHECK(file.good());
    NC_CHECK(file.tellg() > 1000);
    // FFmpeg 回读：真实可解码
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(path).isOk());
    NC_CHECK_EQ(reader.probe().width, 64);
    int decoded = 0;
    VideoFrame frame;
    while (reader.readVideoFrame(frame) && decoded < 100) {
        ++decoded;
    }
    NC_CHECK_EQ(decoded, 30);
    reader.close();
}
#endif
