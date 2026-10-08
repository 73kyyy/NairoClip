#include "test_framework.h"
#include "nairoclip/core/ai_check.h"
#include "nairoclip/core/editor.h"

#include <cmath>
#include <memory>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr double kPi = 3.141592653589793;

/// Mock 音频读取器：按振幅生成指定频率正弦（可制造削顶样本）。
class LevelReader : public MediaReader {
public:
    explicit LevelReader(float amplitude, double freq = 440.0, bool silent = false)
        : amplitude_(amplitude), freq_(freq), silent_(silent) {
        cfg_.probe.type = MediaType::Audio;
        cfg_.probe.sampleRate = 48000;
        cfg_.probe.channels = 2;
        cfg_.probe.duration = Rational::make(10);
        cfg_.audioBlockCount = 1000000;
        cfg_.audioBlockFrames = 1024;
        reader_ = std::make_unique<MockMediaReader>(cfg_);
    }
    Result open(const std::string& p) override { return reader_->open(p); }
    void close() override { reader_->close(); }
    bool isOpen() const override { return reader_->isOpen(); }
    const MediaProbeResult& probe() const override { return reader_->probe(); }
    Result seek(const Time& t) override { return reader_->seek(t); }
    bool readVideoFrame(VideoFrame& o) override { return reader_->readVideoFrame(o); }
    bool readAudioBlock(AudioBlock& o) override {
        if (!reader_->readAudioBlock(o)) {
            return false;
        }
        if (silent_) {
            std::fill(o.samples.begin(), o.samples.end(), 0.0f);
            return true;
        }
        // 重写为指定振幅的正弦（时间连续）
        const double base = o.start.toDouble();
        for (std::size_t i = 0; i < o.samples.size(); ++i) {
            const std::size_t frame = i / static_cast<std::size_t>(o.channels);
            const double t = base + static_cast<double>(frame) / o.sampleRate;
            o.samples[i] = static_cast<float>(amplitude_ * std::sin(2.0 * kPi * freq_ * t));
        }
        return true;
    }

private:
    MockMediaReader::Config cfg_;
    std::unique_ptr<MockMediaReader> reader_;
    float amplitude_ = 0.5f;
    double freq_ = 440.0;
    bool silent_ = false;
};

/// 工厂：按路径选择振幅（voice → 可配置）。
class LevelFactory : public MediaReaderFactory {
public:
    explicit LevelFactory(float loudAmplitude, bool silentLoud = false)
        : loudAmplitude_(loudAmplitude), silentLoud_(silentLoud) {}
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<LevelReader>(loudAmplitude_, 440.0, silentLoud_);
    }

private:
    float loudAmplitude_ = 0.5f;
    bool silentLoud_ = false;
};

struct CheckFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId audioTrack;
    TrackId videoTrack;
    AssetId audioAsset;
    AssetId videoAsset;
    CheckFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 1920;
        seq.height = 1080;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/audio/music.mp4";
        a.type = MediaType::Audio;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        audioAsset = a.id;
        MediaAsset v;
        v.id = AssetId{project.ids.next()};
        v.path = "/video/a.mp4";
        v.type = MediaType::Video;
        v.duration = Rational::make(60);
        project.assets.push_back(v);
        videoAsset = v.id;
        editor.addTrack(seqId, TrackType::Audio, "音乐", &audioTrack);
        editor.addTrack(seqId, TrackType::Video, "V1", &videoTrack);
    }
    SubtitleId addCue(double start, double duration, const std::string& text) {
        SubtitleCue cue;
        cue.id = SubtitleId{project.ids.next()};
        cue.start = Rational::make(static_cast<std::int64_t>(start * 1000), 1000);
        cue.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
        cue.text = text;
        editor.upsertSubtitle(seqId, cue);
        return cue.id;
    }
    ClipId addAudioClip(double start, double duration) {
        Clip c;
        c.asset = audioAsset;
        c.start = Rational::make(static_cast<std::int64_t>(start * 1000), 1000);
        c.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
        ClipId id;
        editor.insertClip(seqId, audioTrack, c, &id);
        return id;
    }
    ClipId addVideoClip(double start, double duration) {
        Clip c;
        c.asset = videoAsset;
        c.start = Rational::make(static_cast<std::int64_t>(start * 1000), 1000);
        c.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
        ClipId id;
        editor.insertClip(seqId, videoTrack, c, &id);
        return id;
    }
};

}  // namespace

// =============================================================================
// 字幕重叠检测（文档 4.5：⚠️ 字幕重叠 (2处) 00:12 [跳转查看]）
// =============================================================================

NC_TEST(AiCheck, DetectsSubtitleOverlap) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "前一条");
    f.addCue(2.0, 2.0, "后一条");  // [1,3) 与 [2,4) 重叠 1 秒
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(1));
    const auto& issue = report.issues[0];
    NC_CHECK_EQ(issue.time, Rational::make(2));      // 定位点 = 后一条起点
    NC_CHECK(issue.autoFixable);
    NC_CHECK(issue.measuredValue > 0.9);             // 重叠约 1 秒
    NC_CHECK(issue.message.find("字幕重叠") != std::string::npos);
}

NC_TEST(AiCheck, AdjacentSubtitlesNotFlagged) {
    // 首尾相接（b.start == a.end）不算重叠
    CheckFixture f;
    f.addCue(1.0, 1.0, "A");
    f.addCue(2.0, 1.0, "B");
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(0));
}

NC_TEST(AiCheck, ToleranceAbsorbsTinyOverlap) {
    // 1ms 重叠在默认容差（20ms）内不计
    CheckFixture f;
    f.addCue(1.0, 1.0, "A");        // [1, 2)
    f.addCue(1.999, 1.0, "B");      // 重叠 1ms
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(0));
}

NC_TEST(AiCheck, DetectsMultipleOverlaps) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "A");   // [1,3)
    f.addCue(2.0, 2.0, "B");   // [2,4) 与 A 重叠
    f.addCue(3.0, 2.0, "C");   // [3,5) 与 B 重叠（A 已结束）
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(2));
}

NC_TEST(AiCheck, IgnoresDisabledCues) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "A");
    const SubtitleId second = f.addCue(2.0, 2.0, "B");
    // 禁用后一条 → 不报重叠
    SubtitleCue cue = *f.project.sequences[0].subtitles.findCue(second);
    cue.enabled = false;
    f.editor.upsertSubtitle(f.seqId, cue);
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(0));
}

// =============================================================================
// 字幕质量（过短 / 空字幕）
// =============================================================================

NC_TEST(AiCheck, DetectsTooShortSubtitle) {
    CheckFixture f;
    f.addCue(1.0, 0.2, "一闪而过");  // 200ms < 500ms 阈值
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleTooShort), std::size_t(1));
    NC_CHECK(!report.issues[0].autoFixable);  // 需手动
}

NC_TEST(AiCheck, DetectsEmptySubtitle) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "   ");  // 全空白
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleEmpty), std::size_t(1));
}

NC_TEST(AiCheck, NotesWhenNoSubtitles) {
    CheckFixture f;
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    bool sawNote = false;
    for (const auto& note : report.notes) {
        if (note.find("未检测到字幕") != std::string::npos) sawNote = true;
    }
    NC_CHECK(sawNote);
}

// =============================================================================
// 音频峰值（文档 4.5：⚠️ 音频峰值过高 (1处) 01:03 [跳转查看]）
// =============================================================================

NC_TEST(AiCheck, DetectsClippingPeaks) {
    CheckFixture f;
    f.addAudioClip(0.0, 3.0);
    LevelFactory factory(1.0f);  // 满刻度正弦（峰值 1.0 > 0.995）
    const AiCheckReport report = checkSequence(f.project, f.seqId, &factory);
    NC_CHECK(report.countOf(AiCheckIssueType::AudioPeakClipping) >= std::size_t(1));
    const auto& issue = report.issues[0];
    NC_CHECK(issue.autoFixable);
    NC_CHECK(issue.measuredValue > 0.99);  // 真实峰值被携带
    NC_CHECK(issue.severity == AiCheckSeverity::Error);
    NC_CHECK(issue.message.find("峰值过高") != std::string::npos);
}

NC_TEST(AiCheck, NormalLevelsNoClippingIssue) {
    CheckFixture f;
    f.addAudioClip(0.0, 3.0);
    LevelFactory factory(0.5f);  // -6dB 正常电平
    const AiCheckReport report = checkSequence(f.project, f.seqId, &factory);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::AudioPeakClipping), std::size_t(0));
}

NC_TEST(AiCheck, NotesWhenNoDecoder) {
    CheckFixture f;
    f.addAudioClip(0.0, 3.0);
    const AiCheckReport report = checkSequence(f.project, f.seqId, nullptr);
    bool sawNote = false;
    for (const auto& note : report.notes) {
        if (note.find("跳过音频峰值检查") != std::string::npos) sawNote = true;
    }
    NC_CHECK(sawNote);
}

NC_TEST(AiCheck, DetectsSilentClip) {
    CheckFixture f;
    f.addAudioClip(0.0, 3.0);
    LevelFactory factory(0.0f, true);  // 全静音
    const AiCheckReport report = checkSequence(f.project, f.seqId, &factory);
    NC_CHECK(report.countOf(AiCheckIssueType::AudioSilentClip) >= std::size_t(1));
}

// =============================================================================
// 视频空隙
// =============================================================================

NC_TEST(AiCheck, DetectsVideoGap) {
    CheckFixture f;
    f.addVideoClip(0.0, 1.0);   // [0,1)
    f.addVideoClip(2.0, 1.0);   // [2,3) → 1 秒空隙 > 0.5 阈值
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::ClipGap), std::size_t(1));
    NC_CHECK_EQ(report.issues[0].time, Rational::make(1));  // 空隙起点
    NC_CHECK(!report.issues[0].autoFixable);
}

NC_TEST(AiCheck, SmallGapsIgnored) {
    CheckFixture f;
    f.addVideoClip(0.0, 1.0);
    f.addVideoClip(1.2, 1.0);  // 200ms 空隙 < 500ms 阈值
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::ClipGap), std::size_t(0));
}

// =============================================================================
// 汇总与排序
// =============================================================================

NC_TEST(AiCheck, SummaryText) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "A");
    f.addCue(2.0, 2.0, "B");     // 1 处重叠
    f.addAudioClip(0.0, 2.0);
    LevelFactory factory(1.0f);  // 峰值过高
    const AiCheckReport report = checkSequence(f.project, f.seqId, &factory);
    NC_CHECK(!report.empty());
    const std::string summary = report.summary();
    NC_CHECK(summary.find("字幕重叠") != std::string::npos);
    NC_CHECK(summary.find("音频峰值过高") != std::string::npos);
    NC_CHECK(report.hasAutoFixable());
}

NC_TEST(AiCheck, IssuesSortedBySeverityThenTime) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "A");
    f.addCue(2.0, 2.0, "B");       // Warning（重叠）
    f.addCue(5.0, 0.2, "短");      // Info（过短）
    f.addAudioClip(0.0, 2.0);
    LevelFactory factory(1.0f);    // Error（峰值）
    const AiCheckReport report = checkSequence(f.project, f.seqId, &factory);
    NC_CHECK(report.issues.size() >= std::size_t(3));
    // 排序规则：Error → Warning → Info
    int previousSeverity = 99;
    for (const auto& issue : report.issues) {
        const int severity = static_cast<int>(issue.severity);
        NC_CHECK(severity <= previousSeverity);
        previousSeverity = severity;
    }
    NC_CHECK(report.issues[0].severity == AiCheckSeverity::Error);
}

NC_TEST(AiCheck, InvalidConfigRejected) {
    CheckFixture f;
    AiCheckConfig config;
    config.audioPeakThreshold = 1.5;  // 非法
    const AiCheckReport report = checkSequence(f.project, f.seqId, nullptr, config);
    NC_CHECK(report.empty());
    bool sawNote = false;
    for (const auto& note : report.notes) {
        if (note.find("配置非法") != std::string::npos) sawNote = true;
    }
    NC_CHECK(sawNote);
}

NC_TEST(AiCheck, MissingSequenceReturnsReport) {
    const Project empty;
    const AiCheckReport report = checkSequence(empty, SequenceId{999});
    NC_CHECK(report.empty());
    NC_CHECK(!report.notes.empty());
}

// =============================================================================
// 修复（经编辑器命令、可撤销）
// =============================================================================

NC_TEST(AiCheckFix, FixesSubtitleOverlap) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "前一条");  // [1,3)
    f.addCue(2.0, 2.0, "后一条");  // [2,4) → 重叠
    AiCheckConfig config;
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.issues.size(), std::size_t(1));
    const Result r = fixIssue(f.editor, f.project, f.seqId, report.issues[0], config);
    NC_CHECK(r.isOk());
    // 修复后：后一条起点 = 前一条结束 = 3.0，不再重叠
    const AiCheckReport after = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(after.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(0));
    // 可撤销
    NC_CHECK(f.editor.undo().isOk());
    const AiCheckReport undone = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(undone.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(1));
}

NC_TEST(AiCheckFix, FixesClippingWithPreciseGain) {
    CheckFixture f;
    const ClipId clipId = f.addAudioClip(0.0, 3.0);
    LevelFactory factory(1.0f);
    AiCheckConfig config;
    const AiCheckReport report = checkSequence(f.project, f.seqId, &factory, config);
    NC_CHECK(report.countOf(AiCheckIssueType::AudioPeakClipping) >= std::size_t(1));
    const Result r =
        fixIssue(f.editor, f.project, f.seqId, report.issues[0], config);
    NC_CHECK(r.isOk());
    // 增益应被降低（原 0dB → 负值）
    const Track* track = findTrack(f.project, f.seqId, f.audioTrack);
    NC_CHECK(track != nullptr);
    const Clip* clip = track->findClip(clipId);
    NC_CHECK(clip != nullptr);
    NC_CHECK(clip->audioGainDb < 0.0);
    // 精确性：目标 = 0.995 × 0.95 = 0.94525 → 增益 ≈ -0.489dB
    // （正弦峰值 1.0 的理论值；量化后实际略低于 1.0，因此用范围校验）
    const double expectedDb = 20.0 * std::log10(0.995 * 0.95 / 1.0);
    NC_CHECK(clip->audioGainDb <= expectedDb + 0.02);   // 不低于目标线
    NC_CHECK(clip->audioGainDb >= expectedDb - 0.05);  // 贴近目标线
    // 撤销恢复
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(std::fabs(findTrack(f.project, f.seqId, f.audioTrack)
                           ->findClip(clipId)
                           ->audioGainDb) < 1e-9);
}

NC_TEST(AiCheckFix, UnsupportedTypesRejected) {
    CheckFixture f;
    f.addCue(1.0, 0.2, "短");
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.issues.size(), std::size_t(1));
    const Result r = fixIssue(f.editor, f.project, f.seqId, report.issues[0]);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Unsupported);
}

NC_TEST(AiCheckFix, FixAllGroupsIntoOneHistoryEntry) {
    CheckFixture f;
    f.addCue(1.0, 2.0, "A");
    f.addCue(2.0, 2.0, "B");  // 重叠 1
    f.addCue(3.5, 2.0, "C");  // 与 B [2,4) 重叠
    const std::size_t undoBefore = f.editor.undoCount();
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(report.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(2));

    const AiCheckFixResult fixResult =
        fixAllIssues(f.editor, f.project, f.seqId, report.issues);
    NC_CHECK_EQ(fixResult.fixed, std::size_t(2));
    // 事务合并：新增 1 条历史（不是 2 条）
    NC_CHECK_EQ(f.editor.undoCount(), undoBefore + 1);
    const auto names = f.editor.undoHistoryNames();
    NC_CHECK_EQ(names.back(), std::string("AI 检查自动修复"));
    // 修复后无重叠
    const AiCheckReport after = checkSequence(f.project, f.seqId);
    NC_CHECK_EQ(after.countOf(AiCheckIssueType::SubtitleOverlap), std::size_t(0));
    // 一次撤销全部回退
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(checkSequence(f.project, f.seqId).countOf(AiCheckIssueType::SubtitleOverlap),
                std::size_t(2));
}

NC_TEST(AiCheckFix, FixAllCountsSkipped) {
    CheckFixture f;
    f.addCue(1.0, 0.2, "短");     // 不支持自动修复
    f.addCue(2.0, 2.0, "A");
    f.addCue(3.0, 2.0, "B");      // 重叠（可修复）
    const AiCheckReport report = checkSequence(f.project, f.seqId);
    const AiCheckFixResult result = fixAllIssues(f.editor, f.project, f.seqId, report.issues);
    NC_CHECK_EQ(result.fixed, std::size_t(1));
    NC_CHECK_EQ(result.skipped, std::size_t(1));
    NC_CHECK_EQ(result.failed, std::size_t(0));
}

// =============================================================================
// 类型名称
// =============================================================================

NC_TEST(AiCheck, TypeNames) {
    NC_CHECK_EQ(std::string(aiCheckIssueName(AiCheckIssueType::SubtitleOverlap)),
                std::string("字幕重叠"));
    NC_CHECK_EQ(std::string(aiCheckIssueName(AiCheckIssueType::AudioPeakClipping)),
                std::string("音频峰值过高"));
    NC_CHECK_EQ(std::string(aiCheckIssueName(AiCheckIssueType::ClipGap)),
                std::string("视频空隙"));
}
