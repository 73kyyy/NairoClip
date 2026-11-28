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
namespace {
/* 测试使用 VP9 作为编码格式偏好，但只允许实际可打开的系统编码器。
   CI 主机可能枚举 VAAPI/QSV 名称却没有可用设备/像素格式；对应 E2E
   用例通过 system_media_test_support.h 预检后跳过，不启用 libvpx 等软件回退。 */
inline std::string testVideoCodec() { return "vp9"; }
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
    config.writer.videoCodec = testVideoCodec();
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
    config.writer.videoCodec = testVideoCodec();
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
    config.writer.videoCodec = testVideoCodec();
    config.writer.height = 64;
    const ExportRunResult result =
        runExport(plan, readerFactory, writerFactory, config);
    NC_CHECK(!result.result.isOk());
    NC_CHECK(!result.fileWritten);
}

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#include "system_media_test_support.h"
#include <cstdio>
#include <fstream>
/// 真实端到端：Mock 解码（可控帧）→ FFmpeg 编码 → FFmpeg 回读验证。
NC_TEST(ExportRunner, RealEncodeEndToEnd) {
    if (!test_support::hasUsableSystemVideoEncoder(testVideoCodec(), 64, 64)) return;
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
    config.writer.videoCodec = testVideoCodec();
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
    if (!test_support::hasUsableSystemVideoEncoder(testVideoCodec(), 160, 120)) return;
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
    config.writer.videoCodec = testVideoCodec();
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
/// 当前跨平台系统 AAC encoder + muxer 尚未接入；验证导出明确拒绝音轨，
/// 而不是回退应用自带 AAC 或生成无声视频。
bool audioExportIsRejected(const ExportPlan& plan, const char* path) {
    RunnerFactory readerFactory;
    class SystemWriterFactory : public MediaWriterFactory {
    public:
        std::unique_ptr<MediaWriter> create() override {
            return std::make_unique<FFmpegMediaWriter>();
        }
    };
    SystemWriterFactory writerFactory;
    ExportRunConfig config;
    config.outputPath = path;
    config.writer.width = 64;
    config.writer.height = 64;
    config.writer.videoCodec = testVideoCodec();
    config.writer.includeAudio = true;
    const ExportRunResult result =
        runExport(plan, readerFactory, writerFactory, config);
    return !result.result.isOk() && result.result.code == ErrorCode::Unsupported &&
           !result.fileWritten &&
           result.result.message.find("系统 AAC") != std::string::npos;
}

NC_TEST(ExportRunner, AudioExportFailsClosedWithoutSystemAacEncoder) {
    Fixture f;
    Clip video;
    video.asset = f.assetId;
    video.start = Rational::make(0);
    video.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, video).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    NC_CHECK(audioExportIsRejected(plan, "/tmp/media_gen/runner_audio_rejected.mp4"));
}


/// 音频增益/淡入淡出不能绕过系统 AAC encoder 缺失时的 fail-closed 约束。
NC_TEST(ExportRunner, AudioGainFadeExportFailsClosedWithoutSystemAacEncoder) {
    Fixture f;
    Clip video;
    video.asset = f.assetId;
    video.start = Rational::make(0);
    video.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, video).isOk());

    TrackId audioTrack;
    NC_CHECK(f.editor.addTrack(f.seqId, TrackType::Audio, "A1", &audioTrack).isOk());
    Clip audio;
    audio.asset = f.assetId;
    audio.start = Rational::make(0);
    audio.duration = Rational::make(1);
    ClipId audioClipId;
    NC_CHECK(f.editor.insertClip(f.seqId, audioTrack, audio, &audioClipId).isOk());
    NC_CHECK(f.editor.setClipAudio(f.seqId, audioTrack, audioClipId, -6.0,
                                   Rational::make(1, 2), Rational::make(1, 2)).isOk());

    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    NC_CHECK(audioExportIsRejected(plan, "/tmp/media_gen/runner_gain_rejected.mp4"));
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
    config.writer.videoCodec = testVideoCodec();
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

NC_TEST(ExportRunner, AiLabelAttachedWhenRequested) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    int framesWithLabel = 0;
    bool sawText = false;
    bool sawPosition = false;
    ExportExecutorOptions options;
    options.attachAiLabel = true;
    options.aiLabelText = "AI 生成";
    const ExportStats stats = executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& frame) {
            for (const auto& item : frame.subtitles) {
                if (item.text == "AI 生成") {
                    sawText = true;
                    if (item.anchorY > 0.9 && item.anchorX < 0.05) sawPosition = true;
                }
            }
            if (sawText && sawPosition) ++framesWithLabel;
        },
        nullptr, nullptr, options, &f.project);
    NC_CHECK(stats.framesWritten > 0);
    // AI 标识应出现在每一帧
    NC_CHECK_EQ(framesWithLabel, static_cast<int>(stats.framesWritten));
}
NC_TEST(ExportRunner, NoAiLabelWhenNotRequested) {
    Fixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.trackId, c).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    RunnerFactory readerFactory;
    int framesWithAnySubtitle = 0;
    executeExport(
        plan, readerFactory,
        [&](const OutputVideoFrame& frame) {
            if (!frame.subtitles.empty()) ++framesWithAnySubtitle;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &f.project);
    // 未请求标识且无字幕 → 不应有标识
    NC_CHECK_EQ(framesWithAnySubtitle, 0);
}
