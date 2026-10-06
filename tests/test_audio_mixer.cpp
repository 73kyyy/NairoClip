#include "test_framework.h"
#include "nairoclip/core/audio_mixer.h"
#include "nairoclip/core/editor.h"
#include <cmath>
using namespace nairoclip::core;
namespace {
class MixMockFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        MockMediaReader::Config cfg;
        cfg.probe.type = MediaType::Audio;
        cfg.probe.sampleRate = 48000;
        cfg.probe.channels = 2;
        cfg.probe.duration = Rational::make(60);
        cfg.audioBlockCount = 100000;  // 足够多
        cfg.audioBlockFrames = 1024;
        return std::make_unique<MockMediaReader>(cfg);
    }
};
struct Fixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId audioTrack;
    AssetId assetId;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/audio/a.wav";
        a.type = MediaType::Audio;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Audio, "A1", &audioTrack);
    }
    ClipId addClip(const Time& start, const Time& duration) {
        Clip c;
        c.asset = assetId;
        c.start = start;
        c.duration = duration;
        ClipId id;
        editor.insertClip(seqId, audioTrack, c, &id);
        return id;
    }
};
}  // namespace
NC_TEST(AudioMixer, DbToLinear) {
    NC_CHECK(std::abs(AudioMixer::dbToLinear(0.0) - 1.0) < 1e-9);
    NC_CHECK(std::abs(AudioMixer::dbToLinear(-6.0) - 0.501187) < 1e-3);
    NC_CHECK(std::abs(AudioMixer::dbToLinear(6.0) - 1.99526) < 1e-3);
    NC_CHECK(AudioMixer::dbToLinear(-60.0) == 0.0);
    NC_CHECK(AudioMixer::dbToLinear(-100.0) == 0.0);
}
NC_TEST(AudioMixer, PrepareExtractsAudioClips) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(2));
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    MixMockFactory factory;
    AudioMixer mixer(MixFormat{48000, 2}, factory);
    NC_CHECK(mixer.prepare(plan, f.project));
    NC_CHECK(mixer.duration() == plan.duration);
}
NC_TEST(AudioMixer, PrepareEmptyWithoutAudio) {
    Fixture f;
    // 无音频片段（空轨道）
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    MixMockFactory factory;
    AudioMixer mixer(MixFormat{48000, 2}, factory);
    NC_CHECK(!mixer.prepare(plan, f.project));
}
NC_TEST(AudioMixer, ReadBlocksCoverDuration) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(1));  // 1 秒
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    MixMockFactory factory;
    AudioMixer mixer(MixFormat{48000, 2}, factory);
    NC_CHECK(mixer.prepare(plan, f.project));
    int blocks = 0;
    OutputAudioBlock block;
    Time lastStart;
    while (mixer.readBlock(block)) {
        ++blocks;
        lastStart = block.start;
        NC_CHECK_EQ(block.sampleRate, 48000);
        NC_CHECK_EQ(block.channels, 2);
        NC_CHECK_EQ(block.samples.size(), std::size_t(1024 * 2));
        // 硬限幅验证：所有样本在 [-1, 1]
        for (const float s : block.samples) {
            NC_CHECK(s >= -1.0f && s <= 1.0f);
        }
    }
    // 1 秒 = 48000 帧 / 1024 = 47 块（46.875 向上取整 47）
    NC_CHECK_EQ(blocks, 47);
    NC_CHECK(lastStart < Rational::make(1));
}
NC_TEST(AudioMixer, FadeEnvelopeApplied) {
    Fixture f;
    const ClipId clip = f.addClip(Rational::make(0), Rational::make(1));
    // 设置淡入 0.5s
    NC_CHECK(f.editor.setClipAudio(f.seqId, f.audioTrack, clip, 0.0, Rational::make(1, 2),
                                   Time{})
                 .isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    MixMockFactory factory;
    AudioMixer mixer(MixFormat{48000, 2}, factory);
    NC_CHECK(mixer.prepare(plan, f.project));
    // 第一块能量（含淡入起点附近）应小于后续块
    OutputAudioBlock first;
    NC_CHECK(mixer.readBlock(first));
    double firstEnergy = 0.0;
    for (const float s : first.samples) {
        firstEnergy += std::abs(s);
    }
    // 跳到 0.7s 之后（越过淡入），比较能量
    NC_CHECK(mixer.seek(fromMilliseconds(700)).isOk());
    OutputAudioBlock later;
    NC_CHECK(mixer.readBlock(later));
    double laterEnergy = 0.0;
    for (const float s : later.samples) {
        laterEnergy += std::abs(s);
    }
    NC_CHECK(laterEnergy > firstEnergy);
}
NC_TEST(AudioMixer, GainReducesSampleMagnitude) {
    Fixture f;
    const ClipId clip = f.addClip(Rational::make(0), Rational::make(1));
    NC_CHECK(f.editor.setClipAudio(f.seqId, f.audioTrack, clip, -20.0, Time{}, Time{}).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    MixMockFactory factory;
    AudioMixer mixer(MixFormat{48000, 2}, factory);
    NC_CHECK(mixer.prepare(plan, f.project));
    OutputAudioBlock block;
    NC_CHECK(mixer.readBlock(block));
    double maxAbs = 0.0;
    for (const float s : block.samples) {
        maxAbs = std::max(maxAbs, static_cast<double>(std::abs(s)));
    }
    // Mock 信号幅度 0.25，-20dB（0.1x）后 < 0.03
    NC_CHECK(maxAbs < 0.03);
}
NC_TEST(AudioMixer, SeekNegativeRejected) {
    MixMockFactory factory;
    AudioMixer mixer(MixFormat{48000, 2}, factory);
    NC_CHECK(!mixer.seek(Rational::make(-1)).isOk());
}
