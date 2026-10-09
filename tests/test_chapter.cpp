// =============================================================================
// 章节自动生成测试（M2.23，文档第 7 部分「🧪AI工具 → 章节自动生成」）
// =============================================================================
// 覆盖：
//   · 纯算法层：候选边界收集/聚类/最短时长/最长硬切/首章从 0 起/覆盖末章
//   · 标题：自动生成格式、线索词、手动重命名、重新生成不覆盖手动标题
//   · 落盘：标记写入（新增/幂等更新）、撤销事务整体回退、redo 确定性
//   · 重建：由序列标记读回章节（时间/标题/颜色筛选）
//   · 真实素材端到端（FFmpeg）：采样 → 检测 → 落盘 → 撤销
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/chapter.h"
#include "nairoclip/core/commands.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/scene_detect.h"

#include <cmath>
#include <fstream>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 构造镜头边界（秒）。
ShotBoundary shot(double startSec, double endSec, double motion = 0.5) {
    ShotBoundary s;
    s.start = Rational::make(static_cast<std::int64_t>(startSec * 1000), 1000);
    s.end = Rational::make(static_cast<std::int64_t>(endSec * 1000), 1000);
    s.motionScore = motion;
    return s;
}

/// 构造语音保留区间（秒）。
SpeechActivityResult::KeepSegment keep(double startSec, double endSec) {
    SpeechActivityResult::KeepSegment seg;
    seg.start = Rational::make(static_cast<std::int64_t>(startSec * 1000), 1000);
    seg.end = Rational::make(static_cast<std::int64_t>(endSec * 1000), 1000);
    return seg;
}

Time secs(double v) {
    return Rational::make(static_cast<std::int64_t>(v * 1000), 1000);
}

/// 构造逐帧特征（时间戳秒）。
FrameFeature feature(double tSec, double r, double g, double b, double diff) {
    FrameFeature f;
    f.timestamp = secs(tSec);
    f.avgR = r;
    f.avgG = g;
    f.avgB = b;
    f.histogramDiff = diff;
    return f;
}

}  // namespace

// ---------------------------------------------------------------------------
// 检测：基础
// ---------------------------------------------------------------------------

NC_TEST(Chapter, RejectsInvalidConfig) {
    ChapterDetectConfig config;
    config.minChapterSeconds = 0.0;  // 非法
    const auto result = detectChapters({}, {}, secs(100), config);
    NC_CHECK(!result.valid());
    NC_CHECK(!result.note.empty());
}

NC_TEST(Chapter, RejectsEmptyDuration) {
    const auto result = detectChapters({}, {}, Time{});
    NC_CHECK(!result.valid());
    NC_CHECK(result.note.find("时长") != std::string::npos);
}

NC_TEST(Chapter, NoBoundariesYieldsSingleChapter) {
    const auto result = detectChapters({}, {}, secs(60));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(1));
    NC_CHECK(result.chapters[0].start.isZero());
    NC_CHECK_EQ(result.chapters[0].end, secs(60));
    NC_CHECK_EQ(result.chapters[0].index, 1);
    NC_CHECK_EQ(result.chapters[0].boundary, Chapter::Boundary::Start);
    NC_CHECK(!result.chapters[0].title.empty());
    NC_CHECK(result.valid());
}

NC_TEST(Chapter, FirstChapterAlwaysStartsAtZero) {
    // 首个场景边界在 10s，但首章必须从 0 开始
    const auto result = detectChapters({shot(0, 30, 0.2), shot(30, 60, 0.5)}, {}, secs(60));
    NC_CHECK(result.valid());
    NC_CHECK(result.chapters.front().start.isZero());
    NC_CHECK(result.chapters.size() >= 2);
}

NC_TEST(Chapter, SceneBoundariesProduceChapters) {
    // 三个镜头：0-30 / 30-60 / 60-90，边界 30 与 60
    const auto shots = std::vector<ShotBoundary>{shot(0, 30, 0.2), shot(30, 60, 0.6),
                                                 shot(60, 90, 0.7)};
    const auto result = detectChapters(shots, {}, secs(90));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(3));
    NC_CHECK_EQ(result.chapters[1].start, secs(30));
    NC_CHECK_EQ(result.chapters[2].start, secs(60));
    NC_CHECK_EQ(result.chapters[2].end, secs(90));
    NC_CHECK_EQ(result.chapters[1].boundary, Chapter::Boundary::SceneCut);
    // 摘要包含数量与来源统计
    NC_CHECK(result.summary.find("3 个章节") != std::string::npos);
    NC_CHECK(result.summary.find("场景") != std::string::npos);
}

NC_TEST(Chapter, MinChapterMergesShortBoundaries) {
    // 边界 30 与 35（间隔 5s < 最短 20s）→ 35 被丢弃
    const auto shots = std::vector<ShotBoundary>{shot(0, 30), shot(30, 35), shot(35, 90)};
    const auto result = detectChapters(shots, {}, secs(90));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(2));
    NC_CHECK_EQ(result.chapters[1].start, secs(30));
    NC_CHECK_EQ(result.candidateBoundaries, std::size_t(2));  // 候选保留统计
}

NC_TEST(Chapter, MaxChapterForcesHardCut) {
    ChapterDetectConfig config;
    config.maxChapterSeconds = 60.0;
    const auto result = detectChapters({}, {}, secs(180), config);
    NC_CHECK(result.chapters.size() >= 3);
    for (const auto& c : result.chapters) {
        NC_CHECK(c.duration().toDouble() <= 60.0 + 1e-6);
    }
    NC_CHECK_EQ(result.chapters.back().end, secs(180));
    // 硬切章节的 boundary 标记为 Forced（首章为 Start）
    NC_CHECK_EQ(result.chapters[1].boundary, Chapter::Boundary::Forced);
}

NC_TEST(Chapter, MaxChaptersLimitDropsWeakest) {
    ChapterDetectConfig config;
    config.maxChapters = 3;
    // 4 个强边界：强度递减
    const auto shots = std::vector<ShotBoundary>{
        shot(0, 30, 0.9), shot(30, 60, 0.8), shot(60, 90, 0.7), shot(90, 120, 0.25),
        shot(120, 150, 0.2)};
    const auto result = detectChapters(shots, {}, secs(150), config);
    NC_CHECK_EQ(result.chapters.size(), std::size_t(3));
    NC_CHECK(!result.warnings.empty());
}

NC_TEST(Chapter, SilenceBoundariesFromSpeechGaps) {
    // 语音：0-20 / 30-50（10s 长静音在 20-30）→ 边界取中点 25s
    const std::vector<SpeechActivityResult::KeepSegment> speech = {keep(0, 20), keep(30, 50)};
    const auto result = detectChapters({}, speech, secs(50));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(2));
    NC_CHECK_EQ(result.chapters[1].start, secs(25));
    NC_CHECK_EQ(result.chapters[1].boundary, Chapter::Boundary::Silence);
}

NC_TEST(Chapter, ShortSilenceDoesNotCreateBoundary) {
    // 静音 1s < 阈值 1.5s → 不产生边界
    const std::vector<SpeechActivityResult::KeepSegment> speech = {keep(0, 20), keep(21, 50)};
    const auto result = detectChapters({}, speech, secs(50));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(1));
}

NC_TEST(Chapter, SilenceDetectionCanBeDisabled) {
    ChapterDetectConfig config;
    config.useSilenceDetect = false;
    const std::vector<SpeechActivityResult::KeepSegment> speech = {keep(0, 20), keep(30, 50)};
    const auto result = detectChapters({}, speech, secs(50), config);
    NC_CHECK_EQ(result.chapters.size(), std::size_t(1));
}

NC_TEST(Chapter, SceneDetectionCanBeDisabled) {
    ChapterDetectConfig config;
    config.useSceneDetect = false;
    const auto shots = std::vector<ShotBoundary>{shot(0, 30), shot(30, 60)};
    const auto result = detectChapters(shots, {}, secs(60), config);
    NC_CHECK_EQ(result.chapters.size(), std::size_t(1));
}

NC_TEST(Chapter, SceneAndSilenceMergeToBoth) {
    // 场景边界 30s 与静音中点 30.5s（窗口内）→ 合并为 Both
    const auto shots = std::vector<ShotBoundary>{shot(0, 30), shot(30, 60)};
    const std::vector<SpeechActivityResult::KeepSegment> speech = {keep(0, 29), keep(32, 60)};
    const auto result = detectChapters(shots, speech, secs(60));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(2));
    NC_CHECK_EQ(result.chapters[1].boundary, Chapter::Boundary::Both);
}

NC_TEST(Chapter, ChaptersCoverFullDuration) {
    const auto shots = std::vector<ShotBoundary>{shot(0, 30), shot(30, 60), shot(60, 100)};
    const auto result = detectChapters(shots, {}, secs(100));
    NC_CHECK(result.valid());
    // 连续无缝覆盖 [0, 100]
    NC_CHECK(result.chapters.front().start.isZero());
    NC_CHECK_EQ(result.chapters.back().end, secs(100));
    for (std::size_t i = 0; i + 1 < result.chapters.size(); ++i) {
        NC_CHECK_EQ(result.chapters[i].end, result.chapters[i + 1].start);
    }
}

NC_TEST(Chapter, BoundaryBeyondDurationIgnored) {
    // 边界位于 200s（超出时长 60s）→ 忽略
    const auto shots = std::vector<ShotBoundary>{shot(0, 30), shot(200, 230)};
    const auto result = detectChapters(shots, {}, secs(60));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(1));
}

// ---------------------------------------------------------------------------
// 标题
// ---------------------------------------------------------------------------

NC_TEST(Chapter, AutoTitleFormat) {
    Chapter chapter;
    chapter.index = 3;
    ChapterDetectConfig config;
    const std::string title = suggestChapterTitle(chapter, config);
    NC_CHECK_EQ(title, std::string("第 3 章"));
    NC_CHECK(isAutoGeneratedTitle(title, config));
}

NC_TEST(Chapter, AutoTitleWithClues) {
    Chapter chapter;
    chapter.index = 2;
    chapter.boundary = Chapter::Boundary::SceneCut;
    chapter.averageRms = 0.15;       // 人声清晰
    chapter.hasVisualStats = true;
    chapter.averageBrightness = 0.8;  // 画面明亮
    chapter.averageMotion = 0.5;      // 动作丰富
    const std::string title = suggestChapterTitle(chapter);
    NC_CHECK(title.find("第 2 章") == 0);
    NC_CHECK(title.find("人声清晰") != std::string::npos);
    NC_CHECK(title.find("画面明亮") != std::string::npos);
    NC_CHECK(isAutoGeneratedTitle(title));
}

NC_TEST(Chapter, ManualTitleNotRecognizedAsAuto) {
    NC_CHECK(!isAutoGeneratedTitle("开场白"));
    NC_CHECK(!isAutoGeneratedTitle("第 3 章 自定义说明"));
    NC_CHECK(!isAutoGeneratedTitle("第 章"));
}

NC_TEST(Chapter, RenameChapter) {
    ChapterDetectResult result;
    Chapter chapter;
    chapter.index = 1;
    chapter.start = Time{};
    chapter.end = secs(60);
    chapter.title = "第 1 章";
    result.chapters.push_back(chapter);
    NC_CHECK(renameChapter(result, 0, "  开场介绍  "));
    NC_CHECK_EQ(result.chapters[0].title, std::string("开场介绍"));
    // 越界/空白拒绝
    NC_CHECK(!renameChapter(result, 5, "X"));
    NC_CHECK(!renameChapter(result, 0, "   "));
    NC_CHECK_EQ(result.chapters[0].title, std::string("开场介绍"));
}

NC_TEST(Chapter, SuggestTitlesKeepsManualEdit) {
    ChapterDetectResult result;
    Chapter a;
    a.index = 1;
    a.start = Time{};
    a.end = secs(60);
    a.title = "手动标题";
    Chapter b;
    b.index = 2;
    b.start = secs(60);
    b.end = secs(120);
    result.chapters.push_back(a);
    result.chapters.push_back(b);
    suggestChapterTitles(result);
    NC_CHECK_EQ(result.chapters[0].title, std::string("手动标题"));  // 手动保留
    NC_CHECK(!result.chapters[1].title.empty());                     // 空标题补全
    NC_CHECK(isAutoGeneratedTitle(result.chapters[1].title));
}

// ---------------------------------------------------------------------------
// 由特征检测（真实数据流）
// ---------------------------------------------------------------------------

NC_TEST(Chapter, DetectFromFeaturesWithSceneCuts) {
    std::vector<FrameFeature> features;
    // 0-20s：红色稳定；20-40s：蓝色稳定（20s 处切换）
    for (double t = 0.0; t < 40.0; t += 1.0) {
        const bool second = t >= 20.0;
        features.push_back(feature(t, second ? 0.1 : 0.9, 0.1, second ? 0.9 : 0.1,
                                   t == 20.0 ? 0.9 : 0.01));
    }
    const auto result = detectChaptersFromFeatures(features, {}, secs(40));
    NC_CHECK(result.valid());
    NC_CHECK(result.chapters.size() >= 2);
    // 统计已填充（第二段亮度更低）
    const Chapter& second = result.chapters[1];
    NC_CHECK(second.hasVisualStats);
    NC_CHECK(second.averageBrightness < result.chapters[0].averageBrightness);
}

NC_TEST(Chapter, DetectFromFeaturesWithAudio) {
    // 音频：0-10s 有声，10-20s 静音 → 静音中点为 15s
    std::vector<AudioBlock> blocks;
    for (int i = 0; i < 40; ++i) {
        AudioBlock b;
        b.start = Rational::make(i * 512, 48000);
        b.sampleRate = 48000;
        b.channels = 1;
        const double t = static_cast<double>(i) * 512.0 / 48000.0;
        const double amp = t < 10.0 ? 0.3 : 0.0;
        b.samples.resize(512);
        for (int s = 0; s < 512; ++s) {
            b.samples[static_cast<std::size_t>(s)] =
                static_cast<float>(amp * std::sin(2.0 * 3.14159265 * 440.0 *
                                                  (t + static_cast<double>(s) / 48000.0)));
        }
        blocks.push_back(std::move(b));
    }
    ChapterDetectConfig config;
    config.minChapterSeconds = 5.0;
    const auto result = detectChaptersFromFeatures({}, blocks, secs(25), config);
    NC_CHECK(result.valid());
    // 至少检测到一个静音边界
    bool hasSilence = false;
    for (const auto& c : result.chapters) {
        if (c.boundary == Chapter::Boundary::Silence || c.boundary == Chapter::Boundary::Both) {
            hasSilence = true;
        }
    }
    NC_CHECK(hasSilence);
    // 语音强度统计：首章有声、静音章近乎为零
    NC_CHECK(result.chapters.front().averageRms > 0.1);
}

// ---------------------------------------------------------------------------
// 落盘与重建
// ---------------------------------------------------------------------------

namespace {

/// 建一个最小工程：1 视频轨 + 1 片段（60s），内容时长 60s。
struct ChapterFixture {
    Project project;
    TimelineEditor* editor = nullptr;
    SequenceId sequenceId;
    TrackId trackId;
    ClipId clipId;

    ChapterFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        sequenceId = seq.id;
        project.sequences.push_back(std::move(seq));
        editor = new TimelineEditor(project);
        editor->addTrack(sequenceId, TrackType::Video, "V1", &trackId);
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "clip.mp4";
        asset.path = "/videos/clip.mp4";
        asset.type = MediaType::Video;
        asset.duration = secs(60);
        editor->importAsset(std::move(asset));
        const MediaAsset* a = project.assets.empty() ? nullptr : &project.assets.back();
        Clip clip;
        clip.id = ClipId{project.ids.next()};
        clip.asset = a != nullptr ? a->id : AssetId{};
        clip.start = Time{};
        clip.duration = secs(60);
        editor->insertClip(sequenceId, trackId, std::move(clip));
        const Track* track = project.findSequence(sequenceId)->findTrack(trackId);
        clipId = track != nullptr && !track->clips.empty() ? track->clips.front().id : ClipId{};
    }

    ~ChapterFixture() { delete editor; }
};

}  // namespace

NC_TEST(Chapter, ApplyWritesMarkers) {
    ChapterFixture fx;
    const auto shots = std::vector<ShotBoundary>{shot(0, 20), shot(20, 40), shot(40, 60)};
    auto result = detectChapters(shots, {}, secs(60));
    NC_CHECK_EQ(result.chapters.size(), std::size_t(3));

    const ChapterApplyResult applied = applyChapters(*fx.editor, fx.project, fx.sequenceId, result);
    NC_CHECK(applied.result.isOk());
    NC_CHECK_EQ(applied.markersAdded, std::size_t(3));
    NC_CHECK_EQ(applied.markersUpdated, std::size_t(0));

    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    NC_CHECK(seq != nullptr);
    NC_CHECK_EQ(seq->markers.size(), std::size_t(3));
    // 全部章节标记（颜色 + 标题）
    for (const auto& m : seq->markers) {
        NC_CHECK_EQ(std::string(m.color), std::string(chapterMarkerColor()));
    }
    NC_CHECK_EQ(seq->markers[1].name, result.chapters[1].title);
    // 章节获得标记 ID
    for (const auto& c : result.chapters) {
        NC_CHECK(c.markerId.isValid());
    }
}

NC_TEST(Chapter, ApplyIsIdempotentOnRegenerate) {
    ChapterFixture fx;
    const auto shots = std::vector<ShotBoundary>{shot(0, 20), shot(20, 40), shot(40, 60)};
    auto first = detectChapters(shots, {}, secs(60));
    applyChapters(*fx.editor, fx.project, fx.sequenceId, first);
    // 重新生成同一内容（新结果对象；按颜色 + 起点匹配既有标记）
    auto second = detectChapters(shots, {}, secs(60));
    const ChapterApplyResult again =
        applyChapters(*fx.editor, fx.project, fx.sequenceId, second);
    NC_CHECK(again.result.isOk());
    NC_CHECK_EQ(again.markersAdded, std::size_t(0));
    NC_CHECK_EQ(again.markersUpdated, std::size_t(3));
    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    NC_CHECK_EQ(seq->markers.size(), std::size_t(3));  // 无重复标记
}

NC_TEST(Chapter, ApplyUpdatesTitleOnRename) {
    ChapterFixture fx;
    auto result = detectChapters({}, {}, secs(60));
    applyChapters(*fx.editor, fx.project, fx.sequenceId, result);
    // 手动重命名后再次落盘 → 更新既有标记
    NC_CHECK(renameChapter(result, 0, "自定义开场"));
    const ChapterApplyResult again =
        applyChapters(*fx.editor, fx.project, fx.sequenceId, result);
    NC_CHECK(again.result.isOk());
    NC_CHECK_EQ(again.markersUpdated, std::size_t(1));
    NC_CHECK_EQ(again.markersAdded, std::size_t(0));
    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    NC_CHECK_EQ(seq->markers.size(), std::size_t(1));
    NC_CHECK_EQ(seq->markers[0].name, std::string("自定义开场"));
}

NC_TEST(Chapter, ApplyRejectsEmptyChapters) {
    ChapterFixture fx;
    ChapterDetectResult empty;
    const ChapterApplyResult applied =
        applyChapters(*fx.editor, fx.project, fx.sequenceId, empty);
    NC_CHECK(!applied.result.isOk());
    NC_CHECK_EQ(fx.project.findSequence(fx.sequenceId)->markers.size(), std::size_t(0));
}

NC_TEST(Chapter, ApplyRejectsMissingSequence) {
    ChapterFixture fx;
    auto result = detectChapters({}, {}, secs(60));
    const ChapterApplyResult applied =
        applyChapters(*fx.editor, fx.project, SequenceId{9999}, result);
    NC_CHECK(!applied.result.isOk());
}

NC_TEST(Chapter, ApplyUndoRedoRoundTrip) {
    ChapterFixture fx;
    const auto shots = std::vector<ShotBoundary>{shot(0, 20), shot(20, 40), shot(40, 60)};
    auto result = detectChapters(shots, {}, secs(60));
    applyChapters(*fx.editor, fx.project, fx.sequenceId, result);
    NC_CHECK_EQ(fx.project.findSequence(fx.sequenceId)->markers.size(), std::size_t(3));
    // 一条撤销整体回退
    NC_CHECK(fx.editor->undo().isOk());
    NC_CHECK_EQ(fx.project.findSequence(fx.sequenceId)->markers.size(), std::size_t(0));
    // 重做（ID 确定性：与首次落盘相同）
    NC_CHECK(fx.editor->redo().isOk());
    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    NC_CHECK_EQ(seq->markers.size(), std::size_t(3));
    for (std::size_t i = 0; i < result.chapters.size(); ++i) {
        NC_CHECK_EQ(seq->markers[i].id.value, result.chapters[i].markerId.value);
    }
    // 历史面板显示事务名
    const auto names = fx.editor->undoHistoryNames();
    NC_CHECK(!names.empty());
    NC_CHECK_EQ(names.back(), std::string("生成章节"));
}

NC_TEST(Chapter, ReadChaptersFromSequence) {
    ChapterFixture fx;
    const auto shots = std::vector<ShotBoundary>{shot(0, 20), shot(20, 45)};
    auto result = detectChapters(shots, {}, secs(60));
    // 手动命名两章（模拟用户编辑）
    renameChapter(result, 0, "开场");
    renameChapter(result, 1, "正片");
    applyChapters(*fx.editor, fx.project, fx.sequenceId, result);

    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    const std::vector<Chapter> read = readChaptersFromSequence(*seq);
    NC_CHECK_EQ(read.size(), std::size_t(2));
    NC_CHECK_EQ(read[0].title, std::string("开场"));
    NC_CHECK_EQ(read[1].title, std::string("正片"));
    NC_CHECK(read[0].start.isZero());
    NC_CHECK_EQ(read[1].start, secs(20));
    NC_CHECK_EQ(read[1].end, secs(60));  // 末章覆盖到内容结束
    // 首章边界来源为 Start，其余为 Unknown（标记不携带来源）
    NC_CHECK_EQ(read[0].boundary, Chapter::Boundary::Start);
    NC_CHECK_EQ(read[1].boundary, Chapter::Boundary::Unknown);
    // markerId 回读一致
    NC_CHECK_EQ(read[0].markerId.value, result.chapters[0].markerId.value);
}

NC_TEST(Chapter, ReadIgnoresNonChapterMarkers) {
    ChapterFixture fx;
    // 手工加一个普通标记（非章节色）
    Marker m;
    m.id = MarkerId{fx.project.ids.next()};
    m.time = secs(15);
    m.name = "片头待调";
    m.color = "#FF8A5B";
    fx.editor->upsertMarker(fx.sequenceId, m);
    // 无章节标记 → 空列表
    const Sequence* seq = fx.project.findSequence(fx.sequenceId);
    NC_CHECK_EQ(readChaptersFromSequence(*seq).size(), std::size_t(0));
}

// ---------------------------------------------------------------------------
// 真实素材端到端（FFmpeg）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"

namespace {
bool chapterMediaExists(const char* path) {
    std::ifstream f(path);
    return f.good();
}
}  // namespace

NC_TEST(Chapter, RealMediaEndToEnd) {
    const char* path = "/tmp/media_gen/two_shots.mp4";
    if (!chapterMediaExists(path)) {
        return;  // 素材未生成：跳过（与既有 FFmpeg 用例同一约定）
    }
    // 工程：两镜头素材（4s）放入视频轨
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(std::move(seq));
    TimelineEditor editor(project);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seq.id, TrackType::Video, "V1", &trackId).isOk());
    MediaAsset asset;
    asset.id = AssetId{project.ids.next()};
    asset.name = "two_shots.mp4";
    asset.path = path;
    asset.type = MediaType::Video;
    asset.duration = secs(4);
    const AssetId assetId = editor.importAsset(std::move(asset));
    Clip clip;
    clip.id = ClipId{project.ids.next()};
    clip.asset = assetId;
    clip.start = Time{};
    clip.duration = secs(4);
    NC_CHECK(editor.insertClip(seq.id, trackId, std::move(clip)).isOk());

    // 1) 真实采样（0.2s 步长）
    FFmpegReaderFactory factory;
    ChapterSampleConfig sampleConfig;
    sampleConfig.videoStepSeconds = 0.2;
    const SequenceChapterSamples samples =
        sampleSequenceForChapters(project, seq.id, &factory, sampleConfig);
    NC_CHECK(samples.usable());
    NC_CHECK(!samples.features.empty());
    // 4s / 0.2 = 20 个采样点（允许 ±1）
    NC_CHECK(samples.features.size() >= 15);

    // 2) 检测（放宽最短章节，让 4s 素材能出 2 章）
    ChapterDetectConfig config;
    config.minChapterSeconds = 1.0;
    config.maxChapterSeconds = 0.0;
    config.sceneThreshold = 0.3;
    const ChapterDetectResult result = detectChaptersFromFeatures(
        samples.features, samples.audio, samples.timelineDuration, config);
    NC_CHECK(result.valid());
    // 两镜头素材应在 2s 附近切出边界
    NC_CHECK(result.chapters.size() >= 2);
    if (result.chapters.size() >= 2) {
        const double boundary = result.chapters[1].start.toDouble();
        NC_CHECK(boundary > 1.5 && boundary < 2.6);
    }

    // 3) 落盘 + 撤销 + 重做
    ChapterDetectResult mutableResult = result;
    const ChapterApplyResult applied = applyChapters(editor, project, seq.id, mutableResult);
    NC_CHECK(applied.result.isOk());
    NC_CHECK(applied.markersAdded >= 2);
    NC_CHECK_EQ(project.findSequence(seq.id)->markers.size(), applied.markersAdded);
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(project.findSequence(seq.id)->markers.size(), std::size_t(0));
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(project.findSequence(seq.id)->markers.size(), applied.markersAdded);

    // 4) 重建
    const std::vector<Chapter> read = readChaptersFromSequence(*project.findSequence(seq.id));
    NC_CHECK_EQ(read.size(), mutableResult.chapters.size());
    NC_CHECK_EQ(read[0].title, mutableResult.chapters[0].title);
}
#endif
