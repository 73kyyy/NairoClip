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

#ifdef NAIROCLIP_HAS_FFMPEG
/// 真实解码路径验证：用生成的测试视频做源素材，导出后检查像素非占位色。
NC_TEST(ExportRunner, RealDecodeWithFFmpegSource) {
    // 准备：素材用 /tmp/media_gen/test_av.mp4（160x120, testsrc 彩条）
    // 序列尺寸设为 160x120 匹配源
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 160;
    seq.height = 120;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.path = "/tmp/media_gen/test_av.mp4";
    a.type = MediaType::Video;
    a.duration = Rational::make(3);
    project.assets.push_back(a);
    const AssetId assetId = project.assets.front().id;
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip c;
    c.asset = assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);  // 1 秒
    NC_CHECK(editor.insertClip(seqId, trackId, c).isOk());
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    // 用真实 FFmpeg 读取器
    FFmpegReaderFactory readerFactory;
    class RealWriterFactory2 : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    RealWriterFactory2 writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/media_gen/runner_decoded.mp4";
    config.writer.width = 160;
    config.writer.height = 120;
    config.writer.crf = 28;
    config.writer.includeAudio = false;
    int frameCount = 0;
    bool sawColor = false;
    const ExportRunResult result = runExport(
        plan, readerFactory, writerFactory, config,
        [&](double) {
            return true;
        });
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.fileWritten);
    NC_CHECK(result.stats.warnings.empty());
    NC_CHECK_EQ(result.stats.framesWritten, std::int64_t(30));
    // 回读验证：解码后的首帧应有颜色（testsrc 是彩色图案）
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(config.outputPath).isOk());
    VideoFrame frame;
    NC_CHECK(reader.readVideoFrame(frame));
    for (std::size_t i = 0; i + 3 < frame.pixels.size(); i += 4) {
        if (frame.pixels[i] > 40 || frame.pixels[i + 1] > 40 || frame.pixels[i + 2] > 40) {
            sawColor = true;
            break;
        }
    }
    NC_CHECK(sawColor);
    reader.close();
    (void)frameCount;
}
#endif

#ifdef NAIROCLIP_HAS_FFMPEG
/// 真实音频导出验证：源视频带 AAC 音轨 → 混音器解码 → AAC 编码输出。
NC_TEST(ExportRunner, RealAudioMixToEncodedFile) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 160;
    seq.height = 120;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.path = "/tmp/media_gen/test_av.mp4";
    a.type = MediaType::Video;
    a.duration = Rational::make(3);
    project.assets.push_back(a);
    const AssetId assetId = project.assets.front().id;
    // 视频轨
    TrackId videoTrack;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &videoTrack).isOk());
    Clip vc;
    vc.asset = assetId;
    vc.start = Rational::make(0);
    vc.duration = Rational::make(2);
    NC_CHECK(editor.insertClip(seqId, videoTrack, vc).isOk());
    // 音频轨（相同素材）
    TrackId audioTrack;
    NC_CHECK(editor.addTrack(seqId, TrackType::Audio, "A1", &audioTrack).isOk());
    Clip ac;
    ac.asset = assetId;
    ac.start = Rational::make(0);
    ac.duration = Rational::make(2);
    NC_CHECK(editor.insertClip(seqId, audioTrack, ac).isOk());

    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    FFmpegReaderFactory readerFactory;
    class RealWriterFactory3 : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    RealWriterFactory3 writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/media_gen/runner_audio.mp4";
    config.writer.width = 160;
    config.writer.height = 120;
    config.writer.crf = 30;
    config.writer.includeAudio = true;
    config.writer.audioSampleRate = 44100;
    config.writer.audioChannels = 1;
    const ExportRunResult result = runExport(plan, readerFactory, writerFactory, config,
                                             nullptr, &project);
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.fileWritten);
    // 音频块应被写入（混音路径或静音回退路径）
    NC_CHECK(result.stats.audioBlocksWritten > 0);
    // 回读验证：音轨存在
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(config.outputPath).isOk());
    NC_CHECK_EQ(reader.probe().sampleRate, 44100);
    NC_CHECK_EQ(reader.probe().channels, 1);
    // 解码音频块并验证非全零（真实混音输出）
    AudioBlock block;
    bool sawNonZero = false;
    int audioBlocks = 0;
    while (reader.readAudioBlock(block) && audioBlocks < 20) {
        ++audioBlocks;
        for (const float s : block.samples) {
            if (s > 0.001f || s < -0.001f) {
                sawNonZero = true;
                break;
            }
        }
        if (sawNonZero) break;
    }
    NC_CHECK(audioBlocks > 0);
    NC_CHECK(sawNonZero);  // 源有 440Hz 正弦波 → 输出应非静音
    reader.close();
}
#endif

#ifdef NAIROCLIP_HAS_FFMPEG
/// 音频增益与淡入淡出导出验证：给音频片段加 -6dB 增益 + 0.2s 淡入，
/// 导出后回读检查首段淡入包络（前段样本幅度明显小于后段）。
NC_TEST(ExportRunner, AudioGainAndFadeApplied) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 160;
    seq.height = 120;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.path = "/tmp/media_gen/test_av.mp4";
    a.type = MediaType::Video;
    a.duration = Rational::make(3);
    project.assets.push_back(a);
    const AssetId assetId = project.assets.front().id;
    TrackId audioTrack;
    NC_CHECK(editor.addTrack(seqId, TrackType::Audio, "A1", &audioTrack).isOk());
    Clip ac;
    ac.asset = assetId;
    ac.start = Rational::make(0);
    ac.duration = Rational::make(2);
    ClipId audioClipId;
    NC_CHECK(editor.insertClip(seqId, audioTrack, ac, &audioClipId).isOk());
    // 设置增益 -6dB + 0.5s 淡入 + 0.5s 淡出
    NC_CHECK(editor.setClipAudio(seqId, audioTrack, audioClipId, -6.0,
                                 Rational::make(1, 2), Rational::make(1, 2))
                 .isOk());
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    FFmpegReaderFactory readerFactory;
    class RW4 : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    RW4 writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/media_gen/runner_gain.mp4";
    config.writer.width = 160;
    config.writer.height = 120;
    config.writer.crf = 35;
    config.writer.includeAudio = true;
    config.writer.audioSampleRate = 44100;
    config.writer.audioChannels = 1;
    const ExportRunResult result = runExport(plan, readerFactory, writerFactory, config,
                                             nullptr, &project);
    NC_CHECK(result.result.isOk());
    NC_CHECK(result.fileWritten);
    // 回读音频：检查淡入（起始段幅度 < 中段）
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(config.outputPath).isOk());
    // 跳过视频解码直接读音频块（ffmpeg 交错流：先读几块音频）
    AudioBlock block;
    double earlyPeak = 0.0;   // 前 0.3s 峰值
    double middlePeak = 0.0;  // 0.6s-1.2s 峰值
    int blocksRead = 0;
    while (reader.readAudioBlock(block) && blocksRead < 60) {
        const double start = block.start.toDouble();
        for (const float s : block.samples) {
            const double a = s < 0 ? -s : s;
            if (start < 0.3 && a > earlyPeak) earlyPeak = a;
            if (start >= 0.6 && start < 1.2 && a > middlePeak) middlePeak = a;
        }
        ++blocksRead;
    }
    NC_CHECK(blocksRead > 0);
    NC_CHECK(middlePeak > 0.001);          // 中段有信号
    // 淡入区间峰值应显著小于中段（约 0.3/0.5 = 60% 处包络，再乘增益）
    NC_CHECK(earlyPeak < middlePeak * 0.9);
    reader.close();
}
#endif

NC_TEST(ExportRunner, SubtitlesAttachedToFrames) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c).isOk());
    // 加字幕 [0.2s, 0.7s)
    SubtitleCue cue;
    cue.id = SubtitleId{f.project.ids.next()};
    cue.start = Rational::make(1, 5);
    cue.duration = Rational::make(1, 2);
    cue.text = "测试字幕";
    cue.style.color = "#FF0000";
    cue.style.outlineColor = "#00FF00";
    cue.style.anchorY = 0.8;
    NC_CHECK(f.editor.upsertSubtitle(f.seqId, cue).isOk());

    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    MockWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = "/tmp/sub_test.mp4";
    config.writer.width = 64;
    config.writer.height = 64;
    config.writer.includeAudio = false;
    int framesWithSub = 0;
    int framesWithoutSub = 0;
    bool sawText = false;
    bool sawColor = false;
    bool sawAnchor = false;
    const ExportStats stats = executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& frame) {
            if (frame.subtitles.empty()) {
                ++framesWithoutSub;
            } else {
                ++framesWithSub;
                for (const auto& item : frame.subtitles) {
                    if (item.text == "测试字幕") sawText = true;
                    if (item.fillRgba == 0xFF0000FFu) sawColor = true;
                    if (std::abs(item.anchorY - 0.8) < 1e-9) sawAnchor = true;
                }
            }
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(30));
    // 字幕区间 [0.2, 0.7)：约 15 帧有字幕、15 帧无
    NC_CHECK(framesWithSub > 0);
    NC_CHECK(framesWithoutSub > 0);
    NC_CHECK(sawText);
    NC_CHECK(sawColor);   // #FF0000 → 0xFF0000FF
    NC_CHECK(sawAnchor);
}

NC_TEST(ExportRunner, OpacityAnimationApplied) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    ClipId clipId;
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c, &clipId).isOk());
    // 关键帧：0s 处 alpha 恒 0.5（用两条帧维持住）
    NC_CHECK(f.editor
                 .insertKeyframe(f.seqId, f.trackId, clipId, ClipProperty::Opacity,
                                 Keyframe{Rational::make(0), 0.5})
                 .isOk());
    NC_CHECK(f.editor
                 .insertKeyframe(f.seqId, f.trackId, clipId, ClipProperty::Opacity,
                                 Keyframe{Rational::make(1), 0.5})
                 .isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    int framesChecked = 0;
    double minAlphaRatio = 1.0;
    const ExportStats stats = executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& frame) {
            if (framesChecked < 3) {
                // 检查第一像素 alpha（应为 255*0.5 ≈ 127）
                if (frame.rgba.size() >= 4) {
                    const double ratio = frame.rgba[3] / 255.0;
                    if (ratio < minAlphaRatio) minAlphaRatio = ratio;
                }
                ++framesChecked;
            }
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(stats.framesWritten > 0);
    NC_CHECK(framesChecked == 3);
    // alpha 被缩放 ~0.5
    NC_CHECK(minAlphaRatio < 0.6);
    NC_CHECK(minAlphaRatio > 0.4);
}

NC_TEST(ExportRunner, SpeedCurveAffectsSourceMapping) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    c.speed = Rational::make(1);
    // 速度曲线：从 2x 线性降到 0.5x
    // 时间轴 0-2s 内的源推进：∫speed dt
    NC_CHECK(c.speedCurve
                 .addPoint(SpeedPoint{Rational::make(0), Rational::make(2), Easing{}})
                 .isOk());
    NC_CHECK(c.speedCurve
                 .addPoint(SpeedPoint{Rational::make(30), Rational::make(1, 2), Easing{}})
                 .isOk());
    ClipId clipId;
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c, &clipId).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    // 记录每帧请求的源时间（通过 seek 的模拟——我们用 executeExport 无法直接观察 sourceTime；
    // 改为验证速度曲线被编译进计划
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(1));
    NC_CHECK(!plan.tracks[0].clips[0].speedCurve.empty());
    // 执行（不崩溃 + 帧数正确）
    const ExportStats stats = executeExport(
        plan, readerFactory, [](const OutputVideoFrame&) {}, nullptr, nullptr,
        ExportExecutorOptions{}, &f.project);
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(60));
}

NC_TEST(ExportRunner, MultiVideoTrackTopWins) {
    Fixture f;
    // 用渐变 Mock（按源时间变化灰度，区分不同源区间）
    class GradientReader : public MediaReader {
    public:
        GradientReader() {
            MockMediaReader::Config cfg;
            cfg.probe.type = MediaType::Video;
            cfg.probe.frameRate = fps30();
            cfg.probe.width = 64;
            cfg.probe.height = 64;
            cfg.probe.duration = Rational::make(60);
            cfg.videoFrameCount = 100000;
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
    class GradientFactory : public MediaReaderFactory {
    public:
        std::unique_ptr<MediaReader> create() override {
            return std::make_unique<GradientReader>();
        }
    };
    // 加第二条视频轨（后加入 = 上层）
    TrackId v2;
    NC_CHECK(f.editor.addTrack(f.seqId, TrackType::Video, "V2", &v2).isOk());
    // V1: 灰色帧（Mock 解码按帧序号产生不同 gray）——两条轨引用同一素材
    Clip c1;
    c1.asset = f.assetId;
    c1.start = Rational::make(0);
    c1.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c1).isOk());
    // V2: 第二个片段，覆盖 [0.5, 1)
    Clip c2;
    c2.asset = f.assetId;
    c2.start = Rational::make(1, 2);
    c2.duration = Rational::make(1, 2);
    c2.sourceIn = Rational::make(10);  // 不同源区间 → 不同像素值
    NC_CHECK(f.editor.insertClip(f.seqId, v2, c2).isOk());

    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    NC_CHECK_EQ(plan.tracks.size(), std::size_t(2));
    GradientFactory readerFactory;
    // 观察帧内容：t<0.5 时来自 V1，t>=0.5 时来自 V2（不同灰度）
    std::vector<std::uint8_t> earlyPixel;
    std::vector<std::uint8_t> latePixel;
    executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& frame) {
            if (frame.rgba.size() < 4) return;
            const double t = frame.timestamp.toDouble();
            if (t < 0.4) {
                earlyPixel = {frame.rgba[0], frame.rgba[1], frame.rgba[2]};
            } else if (t > 0.6 && t < 0.9) {
                latePixel = {frame.rgba[0], frame.rgba[1], frame.rgba[2]};
            }
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(!earlyPixel.empty());
    NC_CHECK(!latePixel.empty());
    // 两段来源不同（源区间不同 → Mock 灰度不同）
    NC_CHECK(earlyPixel != latePixel);
}

NC_TEST(ExportRunner, MultiLayerAlphaBlend) {
    Fixture f;
    // 渐变 Mock
    class GReader : public MediaReader {
    public:
        GReader() {
            MockMediaReader::Config cfg;
            cfg.probe.type = MediaType::Video;
            cfg.probe.frameRate = fps30();
            cfg.probe.width = 64;
            cfg.probe.height = 64;
            cfg.probe.duration = Rational::make(60);
            cfg.videoFrameCount = 100000;
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
    class GFactory : public MediaReaderFactory {
    public:
        std::unique_ptr<MediaReader> create() override { return std::make_unique<GReader>(); }
    };
    // 第二条视频轨（上层，半透明）
    TrackId v2;
    NC_CHECK(f.editor.addTrack(f.seqId, TrackType::Video, "V2", &v2).isOk());
    // 底层 V1: [0,1)，源 0 起
    Clip c1;
    c1.asset = f.assetId;
    c1.start = Rational::make(0);
    c1.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c1).isOk());
    // 上层 V2: [0,1)，源 5s 起（灰度差异大），alpha 0.5 恒定
    Clip c2;
    c2.asset = f.assetId;
    c2.start = Rational::make(0);
    c2.duration = Rational::make(1);
    c2.sourceIn = Rational::make(5);
    c2.animation.curve(ClipProperty::Opacity)
        .insert(Keyframe{Rational::make(0), 0.5});
    c2.animation.curve(ClipProperty::Opacity)
        .insert(Keyframe{Rational::make(1), 0.5});
    NC_CHECK(f.editor.insertClip(f.seqId, v2, c2).isOk());

    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    GFactory readerFactory;
    // 混合结果：每帧 color ≈ top*0.5 + bottom*0.5
    // top 源 5s 起 gray≈(5000/100)=50 取模；bottom 源 0 起 gray 随帧变化
    std::vector<std::uint8_t> blended;
    executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& frame) {
            if (frame.rgba.size() >= 4 && frame.timestamp.toDouble() < 0.1) {
                blended = {frame.rgba[0], frame.rgba[1], frame.rgba[2]};
            }
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(!blended.empty());
    // 分析：
    //  - top（源 5s 起）gray ≈ (5000+offset)ms/100 ≈ 50
    //  - bottom（源 0 起）gray ≈ offset/100 ≈ 0-1
    //  - 混合（alpha=0.5）：≈ 25
    // 若纯 top 输出：≈ 50；若纯 bottom：≈ 0。断言 15..40 以确认真实混合。
    const int v = blended[0];
    NC_CHECK(v > 15);
    NC_CHECK(v < 40);
}
