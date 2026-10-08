#include "test_framework.h"
#include "nairoclip/core/audio_mixer.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/serialization.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr double kPi = 3.141592653589793;
constexpr double kMusicFreq = 440.0;
constexpr double kVoiceFreq = 1000.0;

/// 按路径路由的读取器：voice → 1000Hz；其他（含 music/sfx）→ 440Hz。
/// 固定幅度 0.25，便于用 Goertzel 测量各轨独立能量。
class ToneReader : public MediaReader {
public:
    Result open(const std::string& path) override {
        if (path.empty()) {
            return Result::fail(ErrorCode::InvalidArgument, "路径为空");
        }
        voiceMode_ = path.find("voice") != std::string::npos;
        open_ = true;
        cursor_ = Rational::make(0);
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time& t) override {
        if (!open_) return Result::fail(ErrorCode::InvalidArgument, "未打开");
        cursor_ = t;
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame&) override { return false; }
    bool readAudioBlock(AudioBlock& o) override {
        if (!open_) return false;
        const int sampleRate = 48000;
        const int channels = 2;
        const std::int64_t frames = 1024;
        o.start = cursor_;
        o.sampleRate = sampleRate;
        o.channels = channels;
        o.samples.resize(static_cast<std::size_t>(frames) * channels, 0.0f);
        const double base = cursor_.toDouble();
        const double freq = voiceMode_ ? kVoiceFreq : kMusicFreq;
        for (std::int64_t i = 0; i < frames; ++i) {
            const double t = base + static_cast<double>(i) / sampleRate;
            const float v = static_cast<float>(0.25 * std::sin(2.0 * kPi * freq * t));
            for (int ch = 0; ch < channels; ++ch) {
                o.samples[static_cast<std::size_t>(i) * channels + static_cast<std::size_t>(ch)] = v;
            }
        }
        cursor_ = cursor_ + Rational::make(frames, sampleRate);
        return true;
    }
private:
    MediaProbeResult probe_;
    bool open_ = false;
    bool voiceMode_ = false;
    Time cursor_;
};

class ToneFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<ToneReader>(); }
};

double goertzelEnergy(const std::vector<float>& samples, int sampleRate, double freq) {
    const int n = static_cast<int>(samples.size());
    if (n < 64) return 0.0;
    const double w = 2.0 * kPi * freq / sampleRate;
    const double coeff = 2.0 * std::cos(w);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) {
        s0 = static_cast<double>(samples[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return std::sqrt(std::max(0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2)) / static_cast<double>(n);
}

struct ConsoleFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId musicTrack;
    TrackId voiceTrack;
    AssetId musicAsset;
    AssetId voiceAsset;
    ConsoleFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset m;
        m.id = AssetId{project.ids.next()};
        m.path = "/audio/music.mp4";
        m.type = MediaType::Audio;
        m.duration = Rational::make(6);
        project.assets.push_back(m);
        musicAsset = m.id;
        MediaAsset v;
        v.id = AssetId{project.ids.next()};
        v.path = "/audio/voice.mp4";
        v.type = MediaType::Audio;
        v.duration = Rational::make(6);
        project.assets.push_back(v);
        voiceAsset = v.id;
        editor.addTrack(seqId, TrackType::Audio, "音乐", &musicTrack);
        editor.addTrack(seqId, TrackType::Audio, "旁白", &voiceTrack);
        Clip mc; mc.asset = musicAsset; mc.start = Rational::make(0); mc.duration = Rational::make(2);
        editor.insertClip(seqId, musicTrack, mc);
        Clip vc; vc.asset = voiceAsset; vc.start = Rational::make(0); vc.duration = Rational::make(2);
        editor.insertClip(seqId, voiceTrack, vc);
    }
    /// 测量各轨在指定频点的能量（左声道）。
    void measure(double& musicEnergy, double& voiceEnergy) {
        const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
        ToneFactory factory;
        MixFormat format;
        format.sampleRate = 48000;
        format.channels = 2;
        AudioMixer mixer(format, factory);
        mixer.prepare(plan, project);
        OutputAudioBlock block;
        std::vector<float> left;
        int blocks = 0;
        while (mixer.readBlock(block) && blocks < 200) {
            for (std::size_t i = 0; i + 1 < block.samples.size(); i += 2) {
                left.push_back(block.samples[i]);
            }
            ++blocks;
        }
        musicEnergy = goertzelEnergy(left, 48000, kMusicFreq);
        voiceEnergy = goertzelEnergy(left, 48000, kVoiceFreq);
    }
};

}  // namespace

// =============================================================================
// 混音台：轨道增益 / 静音 / 独奏 / 主输出（文档第 3 部分「专业混音台视图」）
// =============================================================================

NC_TEST(MixerConsole, TrackGainAttenuates) {
    ConsoleFixture f;
    double baseMusic = 0, baseVoice = 0;
    f.measure(baseMusic, baseVoice);
    NC_CHECK(baseMusic > 0.05);
    // 音乐轨 -12dB（约 ×0.25）
    NC_CHECK(f.editor.setTrackGain(f.seqId, f.musicTrack, -12.0).isOk());
    double music = 0, voice = 0;
    f.measure(music, voice);
    NC_CHECK(music < baseMusic * 0.35);
    NC_CHECK(music > baseMusic * 0.15);
    NC_CHECK(std::fabs(voice - baseVoice) < 0.02);  // 旁白轨不受影响
}

NC_TEST(MixerConsole, TrackGainBoost) {
    ConsoleFixture f;
    double baseMusic = 0, baseVoice = 0;
    f.measure(baseMusic, baseVoice);
    // +6dB（约 ×2）——注意输出限幅，测量应显著上升
    NC_CHECK(f.editor.setTrackGain(f.seqId, f.musicTrack, 6.0).isOk());
    double music = 0, voice = 0;
    f.measure(music, voice);
    NC_CHECK(music > baseMusic * 1.5);
}

NC_TEST(MixerConsole, TrackGainRejectsOutOfRange) {
    ConsoleFixture f;
    NC_CHECK(!f.editor.setTrackGain(f.seqId, f.musicTrack, -80.0).isOk());
    NC_CHECK(!f.editor.setTrackGain(f.seqId, f.musicTrack, 20.0).isOk());
}

NC_TEST(MixerConsole, MuteSilencesTrack) {
    ConsoleFixture f;
    double baseMusic = 0, baseVoice = 0;
    f.measure(baseMusic, baseVoice);
    NC_CHECK(f.editor.setTrackMuted(f.seqId, f.musicTrack, true).isOk());
    double music = 0, voice = 0;
    f.measure(music, voice);
    NC_CHECK(music < 0.005);              // 音乐被静音
    NC_CHECK(std::fabs(voice - baseVoice) < 0.02);  // 旁白不受影响
}

NC_TEST(MixerConsole, SoloIsolatesTrack) {
    ConsoleFixture f;
    // 独奏旁白：音乐应被排除
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.voiceTrack, true).isOk());
    double music = 0, voice = 0;
    f.measure(music, voice);
    NC_CHECK(music < 0.005);   // 音乐排除
    NC_CHECK(voice > 0.05);    // 旁白保留
    // 再独奏音乐：两条轨均为 solo → 都保留（专业混音台语义：solo 是「选听」而非「互斥」）
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.musicTrack, true).isOk());
    f.measure(music, voice);
    NC_CHECK(music > 0.05);
    NC_CHECK(voice > 0.05);
    // 取消旁白独奏：只剩音乐 solo → 旁白被排除
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.voiceTrack, false).isOk());
    f.measure(music, voice);
    NC_CHECK(music > 0.05);
    NC_CHECK(voice < 0.005);
    // 取消全部独奏：两轨恢复
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.musicTrack, false).isOk());
    f.measure(music, voice);
    NC_CHECK(music > 0.05);
    NC_CHECK(voice > 0.05);
}

NC_TEST(MixerConsole, MasterGainScalesAllTracks) {
    ConsoleFixture f;
    double baseMusic = 0, baseVoice = 0;
    f.measure(baseMusic, baseVoice);
    NC_CHECK(f.editor.setMasterGain(f.seqId, -6.0).isOk());
    double music = 0, voice = 0;
    f.measure(music, voice);
    // 两条轨等比下降（约 ×0.5）
    NC_CHECK(music < baseMusic * 0.65);
    NC_CHECK(voice < baseVoice * 0.65);
    NC_CHECK(music > baseMusic * 0.35);
}

NC_TEST(MixerConsole, MasterGainRejectsOutOfRange) {
    ConsoleFixture f;
    NC_CHECK(!f.editor.setMasterGain(f.seqId, -100.0).isOk());
    NC_CHECK(!f.editor.setMasterGain(f.seqId, 30.0).isOk());
}

NC_TEST(MixerConsole, SoloWithMuteBothApplied) {
    ConsoleFixture f;
    // 音乐独奏 + 音乐静音：静音优先 → 输出全静音
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.musicTrack, true).isOk());
    NC_CHECK(f.editor.setTrackMuted(f.seqId, f.musicTrack, true).isOk());
    double music = 0, voice = 0;
    f.measure(music, voice);
    NC_CHECK(music < 0.005);
    NC_CHECK(voice < 0.005);  // 非独奏轨也被排除
}

NC_TEST(MixerConsole, TrackAudibleQuery) {
    ConsoleFixture f;
    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    ToneFactory factory;
    MixFormat format;
    format.sampleRate = 48000;
    format.channels = 2;
    AudioMixer mixer(format, factory);
    NC_CHECK(mixer.prepare(plan, f.project));
    NC_CHECK(mixer.trackAudible(f.musicTrack));
    NC_CHECK(mixer.trackAudible(f.voiceTrack));
    // 独奏音乐后：旁白不可闻（供 UI 高亮）
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.musicTrack, true).isOk());
    const ExportPlan plan2 = compileExportPlan(f.project, *f.project.mainSequence());
    AudioMixer mixer2(format, factory);
    NC_CHECK(mixer2.prepare(plan2, f.project));
    NC_CHECK(mixer2.trackAudible(f.musicTrack));
    NC_CHECK(!mixer2.trackAudible(f.voiceTrack));
}

// =============================================================================
// 撤销重做 + 序列化
// =============================================================================

NC_TEST(MixerConsole, UndoRedoAllControls) {
    ConsoleFixture f;
    NC_CHECK(f.editor.setTrackGain(f.seqId, f.musicTrack, -9.0).isOk());
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.voiceTrack, true).isOk());
    NC_CHECK(f.editor.setMasterGain(f.seqId, -3.0).isOk());
    const Track* track = findTrack(f.project, f.seqId, f.musicTrack);
    NC_CHECK(std::fabs(track->gainDb + 9.0) < 1e-9);
    // 撤销主输出
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.project.sequences[0].masterGainDb, 0.0);
    // 撤销独奏
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(!findTrack(f.project, f.seqId, f.voiceTrack)->solo);
    // 重做两次恢复
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK(findTrack(f.project, f.seqId, f.voiceTrack)->solo);
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK(std::fabs(f.project.sequences[0].masterGainDb + 3.0) < 1e-9);
}

NC_TEST(MixerConsole, SerializationRoundtrip) {
    ConsoleFixture f;
    NC_CHECK(f.editor.setTrackGain(f.seqId, f.musicTrack, -8.5).isOk());
    NC_CHECK(f.editor.setTrackSolo(f.seqId, f.voiceTrack, true).isOk());
    NC_CHECK(f.editor.setMasterGain(f.seqId, -2.0).isOk());

    const std::string text = saveProjectToNclip(f.project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    NC_CHECK(std::fabs(loaded.sequences[0].masterGainDb + 2.0) < 1e-9);
    const Track* music = loaded.sequences[0].findTrack(f.musicTrack);
    const Track* voice = loaded.sequences[0].findTrack(f.voiceTrack);
    NC_CHECK(music != nullptr && voice != nullptr);
    NC_CHECK(std::fabs(music->gainDb + 8.5) < 1e-9);
    NC_CHECK(voice->solo);
    NC_CHECK(!music->solo);
}

NC_TEST(MixerConsole, DefaultsOmittedFromFile) {
    ConsoleFixture f;
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("solo") == std::string::npos);
    NC_CHECK(text.find("gainDb") == std::string::npos);
    NC_CHECK(text.find("masterGainDb") == std::string::npos);
}

NC_TEST(MixerConsole, LoadClampsOutOfRangeGain) {
    // 手工构造越界 gainDb 的工程：载入应截断到合法范围（而非失败）
    const std::string text =
        R"({"format":"nclip","version":1,"project":{"name":"t"},"assets":[],)"
        R"("sequences":[{"id":1,"name":"s","frameRate":[30,1],"width":1920,"height":1080,)"
        R"("masterGainDb":99,"tracks":[{"id":2,"type":"audio","name":"a","gainDb":-999,)"
        R"("clips":[]}],"markers":[]}]})";
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    NC_CHECK_EQ(loaded.sequences[0].masterGainDb, 12.0);   // 上限截断
    NC_CHECK_EQ(loaded.sequences[0].tracks[0].gainDb, -60.0);  // 下限截断
}
