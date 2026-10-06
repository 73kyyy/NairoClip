#include "test_framework.h"
#include "nairoclip/core/auto_edit.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/scene_detect.h"
#include "nairoclip/core/serialization.h"
using namespace nairoclip::core;
// =============================================================================
// 端到端工作流测试：模拟真实用户的完整操作链路
// =============================================================================
// 链路：导入素材 → 分析参考风格 → 自动剪辑 → 加字幕 → 预览检查 →
//       导出（帧驱动）→ 存档 → 读档 → 校验一致性

namespace {
class E2EFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        MockMediaReader::Config cfg;
        cfg.probe.type = MediaType::Video;
        cfg.probe.frameRate = fps30();
        cfg.probe.width = 64;
        cfg.probe.height = 64;
        cfg.probe.duration = Rational::make(120);
        cfg.probe.sampleRate = 48000;
        cfg.probe.channels = 2;
        cfg.videoFrameCount = 1000000;
        cfg.audioBlockCount = 1000000;
        cfg.audioBlockFrames = 1024;
        return std::make_unique<MockMediaReader>(cfg);
    }
};
ShotBoundary shot(double s, double e) {
    ShotBoundary b;
    b.start = Rational::make(static_cast<std::int64_t>(s * 1000), 1000);
    b.end = Rational::make(static_cast<std::int64_t>(e * 1000), 1000);
    return b;
}
}  // namespace

NC_TEST(E2E, FullUserWorkflow) {
    // ---- 1) 新建工程 + 导入素材 ----
    Project project;
    project.name = "端到端测试";
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 64;
    seq.height = 64;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset asset;
    asset.name = "我的素材.mp4";
    asset.path = "/videos/mine.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(120);
    const AssetId assetId = editor.importAsset(asset);
    NC_CHECK(assetId.isValid());
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "主视频", &trackId).isOk());

    // ---- 2) 分析参考素材风格（镜头边界 → 节奏画像） ----
    std::vector<FrameFeature> refFeatures;
    for (int i = 0; i < 30; ++i) {
        FrameFeature ff;
        ff.timestamp = Rational::make(i, 2);  // 0.5s 间隔
        ff.avgR = 0.5;
        ff.avgG = 0.5;
        ff.avgB = 0.5;
        ff.histogramDiff = (i % 10 == 0 && i > 0) ? 0.9 : 0.01;  // 每 5 秒一刀
        refFeatures.push_back(ff);
    }
    const auto shots = detectShots(refFeatures, Rational::make(15));
    NC_CHECK(shots.size() >= std::size_t(3));
    StyleProfile profile;
    profile.rhythm = analyzeRhythm(shots);
    profile.color = analyzeColor(0.7, 0.5, 0.4, 0.6, 0.5);
    NC_CHECK(profile.valid());

    // ---- 3) 自动剪辑（参考风格生成） ----
    AutoEditOptions options;
    options.applyColor = true;
    options.transactionName = "参考风格生成";
    auto editResult = applyAutoEdit(editor, project, seqId, trackId, profile,
                                    {{assetId, Rational::make(60)}}, options);
    NC_CHECK(editResult.result.isOk());
    NC_CHECK(editResult.clipsInserted > 0);
    const Track* track = findTrack(project, seqId, trackId);
    NC_CHECK(!track->clips.empty());
    NC_CHECK(track->invariantsHold());

    // ---- 4) 添加字幕 ----
    SubtitleCue cue;
    cue.id = SubtitleId{project.ids.next()};
    cue.start = Rational::make(0);
    cue.duration = Rational::make(2);
    cue.text = "你好，这是参考风格生成的视频";
    NC_CHECK(editor.upsertSubtitle(seqId, cue).isOk());

    // ---- 5) 预览检查 ----
    E2EFactory factory;
    PreviewRenderer preview(project, factory);
    const PreviewFrame frame = preview.renderAt(Rational::make(1));
    NC_CHECK(frame.clipId != 0);
    NC_CHECK_EQ(frame.subtitles.size(), std::size_t(1));
    NC_CHECK_EQ(frame.subtitles[0], std::string("你好，这是参考风格生成的视频"));

    // ---- 6) 导出（限帧保护） ----
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    ExportExecutorOptions exportOptions;
    exportOptions.maxFrames = 30;
    int framesWritten = 0;
    const ExportStats stats = executeExport(
        plan, factory, [&](const OutputVideoFrame& f) {
            ++framesWritten;
            NC_CHECK(!f.rgba.empty());
        }, nullptr, nullptr, exportOptions);
    NC_CHECK_EQ(stats.framesWritten, std::int64_t(30));
    NC_CHECK(!stats.cancelled);

    // ---- 7) 存档 ----
    const std::string saved = saveProjectToNclip(project);
    NC_CHECK(!saved.empty());

    // ---- 8) 读档 + 一致性 ----
    Project loaded;
    NC_CHECK(loadProjectFromNclip(saved, loaded).result.isOk());
    NC_CHECK_EQ(loaded.name, std::string("端到端测试"));
    const Track& lt = loaded.mainSequence()->tracks[0];
    NC_CHECK_EQ(lt.clips.size(), track->clips.size());
    NC_CHECK_EQ(loaded.mainSequence()->subtitles.size(), std::size_t(1));
    for (std::size_t i = 0; i < lt.clips.size(); ++i) {
        NC_CHECK(lt.clips[i].start == track->clips[i].start);
        NC_CHECK(lt.clips[i].duration == track->clips[i].duration);
        NC_CHECK_EQ(lt.clips[i].effects.size(), track->clips[i].effects.size());
    }

    // ---- 9) 撤销链路健全性（全部回退） ----
    // 记录当前撤销栈
    std::size_t undos = 0;
    while (editor.canUndo()) {
        NC_CHECK(editor.undo().isOk());
        ++undos;
        if (undos > 200) break;  // 防死循环
    }
    NC_CHECK(undos > 0);
    // 全部回退后：无片段、无字幕、无轨道
    NC_CHECK(project.mainSequence()->tracks.empty() ||
             project.mainSequence()->tracks[0].clips.empty());
}
