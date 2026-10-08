#include "test_framework.h"
#include "nairoclip/core/auto_duck.h"
#include "nairoclip/core/audio_mixer.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr double kPi = 3.141592653589793;

/// 合成音频块：语音段（window 区间内为非零正弦）+ 其余静音。
/// 用于验证 VAD → 压低区间生成。
std::vector<AudioBlock> makeVoiceTrack(double seconds, int sampleRate,
                                       const std::vector<std::pair<double, double>>& voiceWindow) {
    const std::size_t totalFrames = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> samples(totalFrames, 0.0f);
    for (const auto& window : voiceWindow) {
        const std::size_t start = static_cast<std::size_t>(window.first * sampleRate);
        const std::size_t end = static_cast<std::size_t>(window.second * sampleRate);
        for (std::size_t i = start; i < end && i < totalFrames; ++i) {
            const double t = static_cast<double>(i) / sampleRate;
            // 300Hz 语音近似（幅度 0.3，明显高于阈值）
            samples[i] = static_cast<float>(0.3 * std::sin(2.0 * kPi * 300.0 * t));
        }
    }
    std::vector<AudioBlock> blocks;
    const std::size_t blockFrames = 1024;
    for (std::size_t offset = 0; offset < totalFrames; offset += blockFrames) {
        const std::size_t count = std::min(blockFrames, totalFrames - offset);
        AudioBlock block;
        block.start = Rational::make(static_cast<std::int64_t>(offset), sampleRate);
        block.sampleRate = sampleRate;
        block.channels = 1;
        block.samples.assign(samples.begin() + static_cast<std::ptrdiff_t>(offset),
                             samples.begin() + static_cast<std::ptrdiff_t>(offset + count));
        blocks.push_back(std::move(block));
    }
    return blocks;
}

/// 恒定音量音轨（模拟音乐）。
std::vector<AudioBlock> makeMusicTrack(double seconds, int sampleRate, float amplitude) {
    const std::size_t totalFrames = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<AudioBlock> blocks;
    const std::size_t blockFrames = 1024;
    for (std::size_t offset = 0; offset < totalFrames; offset += blockFrames) {
        const std::size_t count = std::min(blockFrames, totalFrames - offset);
        AudioBlock block;
        block.start = Rational::make(static_cast<std::int64_t>(offset), sampleRate);
        block.sampleRate = sampleRate;
        block.channels = 1;
        block.samples.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const double t = static_cast<double>(offset + i) / sampleRate;
            block.samples[i] = static_cast<float>(amplitude * std::sin(2.0 * kPi * 220.0 * t));
        }
        blocks.push_back(std::move(block));
    }
    return blocks;
}

MockMediaReader::Config audioConfig(double duration) {
    MockMediaReader::Config cfg;
    cfg.probe.type = MediaType::Audio;
    cfg.probe.frameRate = fps30();
    cfg.probe.sampleRate = 48000;
    cfg.probe.channels = 2;
    cfg.probe.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
    cfg.videoFrameCount = 0;
    cfg.audioBlockCount = 1000000;
    cfg.audioBlockFrames = 1024;
    return cfg;
}

}  // namespace

// =============================================================================
// 闪避分析核心（真实 RMS VAD）
// =============================================================================

NC_TEST(AutoDuck, DetectsVoiceWindow) {
    // 2s 语音在 [1,3)，总长 5s
    const auto blocks = makeVoiceTrack(5.0, 48000, {{1.0, 3.0}});
    AutoDuckConfig config;
    config.targetGain = 0.25;
    config.smoothTransitions = false;
    const AutoDuckResult result = analyzeAutoDuck(blocks, config);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.regions.size(), std::size_t(1));
    // 区间应覆盖语音窗口（允许一帧误差）
    NC_CHECK(result.regions[0].start.toDouble() <= 1.05);
    NC_CHECK(result.regions[0].end.toDouble() >= 2.95);
    NC_CHECK(std::fabs(result.regions[0].gain - 0.25) < 1e-9);
    NC_CHECK(result.voiceSeconds > 1.8);
}

NC_TEST(AutoDuck, MergesShortGapsWithHold) {
    // 两段语音间隔 0.15s（< hold 0.25s）→ 合并为单区间
    const auto blocks = makeVoiceTrack(5.0, 48000, {{1.0, 2.0}, {2.15, 3.0}});
    AutoDuckConfig config;
    config.holdSeconds = 0.25;
    config.smoothTransitions = false;
    const AutoDuckResult result = analyzeAutoDuck(blocks, config);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.regions.size(), std::size_t(1));
    NC_CHECK(result.regions[0].end.toDouble() > 2.9);
}

NC_TEST(AutoDuck, KeepsLongGapsSeparate) {
    // 两段语音间隔 1.0s（> hold 0.25s）→ 保持为两个区间
    const auto blocks = makeVoiceTrack(6.0, 48000, {{1.0, 2.0}, {3.0, 4.0}});
    AutoDuckConfig config;
    config.holdSeconds = 0.25;
    config.smoothTransitions = false;
    const AutoDuckResult result = analyzeAutoDuck(blocks, config);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.regions.size(), std::size_t(2));
}

NC_TEST(AutoDuck, AttackReleaseExpandRegion) {
    // attack/release 应扩展区间边界（提前压低、延后恢复）
    const auto blocks = makeVoiceTrack(5.0, 48000, {{1.0, 2.0}});
    AutoDuckConfig config;
    config.attackSeconds = 0.2;
    config.releaseSeconds = 0.4;
    config.smoothTransitions = true;
    const AutoDuckResult result = analyzeAutoDuck(blocks, config);
    NC_CHECK(result.valid());
    NC_CHECK_EQ(result.regions.size(), std::size_t(1));
    // 起点应早于 1.0（提前 0.2s），终点应晚于 2.0（延后 0.4s）
    NC_CHECK(result.regions[0].start.toDouble() < 0.95);
    NC_CHECK(result.regions[0].end.toDouble() > 2.3);
}

NC_TEST(AutoDuck, SensitivityAffectsDetection) {
    // 低电平语音（0.05 幅度）：低灵敏度检测不到，高灵敏度可检测
    const auto quiet = makeVoiceTrack(4.0, 48000, {{1.0, 3.0}});
    // 手动构造低幅度：把样本乘以 0.1（0.3 → 0.03）
    auto quietBlocks = quiet;
    for (auto& block : quietBlocks) {
        for (auto& s : block.samples) {
            s *= 0.1f;
        }
    }
    AutoDuckConfig lowSensitivity;
    lowSensitivity.voiceThreshold = AutoDuckSettings{}.voiceThreshold();  // 默认 0.5 → 0.11
    const AutoDuckResult lowResult = analyzeAutoDuck(quietBlocks, lowSensitivity);
    auto highSettings = AutoDuckSettings{};
    highSettings.sensitivity = 1.0;  // 高灵敏度 → 阈值 0.02
    AutoDuckConfig highSensitivity;
    highSensitivity.voiceThreshold = highSettings.voiceThreshold();
    const AutoDuckResult highResult = analyzeAutoDuck(quietBlocks, highSensitivity);
    // 高灵敏度应检出（低灵敏度可能检不到）
    NC_CHECK(highResult.valid());
    NC_CHECK(highResult.regions.size() >= lowResult.regions.size());
}

NC_TEST(AutoDuck, EmptyInputIsInvalidWithNote) {
    const AutoDuckResult result = analyzeAutoDuck({});
    NC_CHECK(!result.valid());
    NC_CHECK(!result.note.empty());
}

NC_TEST(AutoDuck, SilenceYieldsNoRegions) {
    const auto blocks = makeMusicTrack(3.0, 48000, 0.0f);  // 全静音
    const AutoDuckResult result = analyzeAutoDuck(blocks);
    NC_CHECK(!result.valid());
    NC_CHECK(!result.note.empty());
}

NC_TEST(AutoDuck, TooShortInputIsInvalid) {
    const auto blocks = makeVoiceTrack(0.01, 48000, {{0.0, 0.01}});
    const AutoDuckResult result = analyzeAutoDuck(blocks);
    NC_CHECK(!result.valid());
}

// =============================================================================
// 增益查询
// =============================================================================

NC_TEST(AutoDuck, GainAtQueriesRegions) {
    std::vector<DuckRegion> regions;
    DuckRegion region;
    region.start = Rational::make(1);
    region.end = Rational::make(3);
    region.gain = 0.25;
    regions.push_back(region);
    // 区间外 → 1.0
    NC_CHECK_EQ(duckGainAt(regions, Rational::make(0)), 1.0);
    NC_CHECK_EQ(duckGainAt(regions, Rational::make(4)), 1.0);
    // 区间内 → 0.25
    NC_CHECK_EQ(duckGainAt(regions, Rational::make(2)), 0.25);
    // 边界：start 包含、end 不含
    NC_CHECK_EQ(duckGainAt(regions, Rational::make(1)), 0.25);
    NC_CHECK_EQ(duckGainAt(regions, Rational::make(3)), 1.0);
}

NC_TEST(AutoDuck, TargetGainFromSettings) {
    AutoDuckSettings settings;
    settings.duckAmountDb = 6.0;
    const double gain = duckTargetGain(settings);
    // -6dB ≈ 0.501
    NC_CHECK(std::fabs(gain - 0.501) < 0.01);
    settings.duckAmountDb = 0.0;  // 0dB = 不压低
    NC_CHECK_EQ(duckTargetGain(settings), 1.0);
    settings.duckAmountDb = 20.0;  // -20dB = 0.1
    NC_CHECK(std::fabs(duckTargetGain(settings) - 0.1) < 0.01);
}

// =============================================================================
// 编辑器命令（撤销/重做 + 参数校验）
// =============================================================================

namespace {
struct DuckFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId audioTrack;
    DuckFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        editor.addTrack(seqId, TrackType::Audio, "A1", &audioTrack);
    }
};
}  // namespace

NC_TEST(AutoDuck, EditorSetAndUndo) {
    DuckFixture f;
    AutoDuckSettings settings;
    settings.enabled = true;
    settings.sensitivity = 0.7;
    settings.duckAmountDb = 9.0;
    NC_CHECK(f.editor.setTrackAutoDuck(f.seqId, f.audioTrack, settings).isOk());
    const Track* track = findTrack(f.project, f.seqId, f.audioTrack);
    NC_CHECK(track->autoDuck.enabled);
    NC_CHECK(std::fabs(track->autoDuck.sensitivity - 0.7) < 1e-9);
    NC_CHECK(std::fabs(track->autoDuck.duckAmountDb - 9.0) < 1e-9);
    // 撤销恢复默认
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK(!track->autoDuck.enabled);
    // 重做恢复
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK(track->autoDuck.enabled);
}

NC_TEST(AutoDuck, EditorRejectsInvalidSettings) {
    DuckFixture f;
    AutoDuckSettings bad;
    bad.sensitivity = 1.5;  // 超范围
    NC_CHECK(!f.editor.setTrackAutoDuck(f.seqId, f.audioTrack, bad).isOk());
    AutoDuckSettings bad2;
    bad2.duckAmountDb = 40.0;  // 超范围
    NC_CHECK(!f.editor.setTrackAutoDuck(f.seqId, f.audioTrack, bad2).isOk());
    // 失败不入撤销栈
    NC_CHECK_EQ(f.editor.undoCount(), std::size_t(1));  // 仅「添加轨道」
}

NC_TEST(AutoDuck, EditorRejectsVideoTrack) {
    DuckFixture f;
    TrackId videoTrack;
    NC_CHECK(f.editor.addTrack(f.seqId, TrackType::Video, "V1", &videoTrack).isOk());
    AutoDuckSettings settings;
    settings.enabled = true;
    NC_CHECK(!f.editor.setTrackAutoDuck(f.seqId, videoTrack, settings).isOk());
}

// =============================================================================
// 序列化持久化
// =============================================================================

NC_TEST(AutoDuck, SerializationRoundtrip) {
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    Track track;
    track.id = TrackId{project.ids.next()};
    track.type = TrackType::Audio;
    track.name = "音乐";
    track.autoDuck.enabled = true;
    track.autoDuck.sensitivity = 0.8;
    track.autoDuck.duckAmountDb = 15.0;
    track.autoDuck.attackSeconds = 0.1;
    track.autoDuck.releaseSeconds = 0.5;
    track.autoDuck.holdSeconds = 0.3;
    seq.tracks.push_back(track);
    project.sequences.push_back(seq);

    const std::string text = saveProjectToNclip(project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    const Track& loadedTrack = loaded.sequences[0].tracks[0];
    NC_CHECK(loadedTrack.autoDuck.enabled);
    NC_CHECK(std::fabs(loadedTrack.autoDuck.sensitivity - 0.8) < 1e-9);
    NC_CHECK(std::fabs(loadedTrack.autoDuck.duckAmountDb - 15.0) < 1e-9);
    NC_CHECK(std::fabs(loadedTrack.autoDuck.attackSeconds - 0.1) < 1e-9);
    NC_CHECK(std::fabs(loadedTrack.autoDuck.releaseSeconds - 0.5) < 1e-9);
    NC_CHECK(std::fabs(loadedTrack.autoDuck.holdSeconds - 0.3) < 1e-9);
}

NC_TEST(AutoDuck, DefaultOmittedFromFile) {
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    Track track;
    track.id = TrackId{project.ids.next()};
    track.type = TrackType::Audio;
    seq.tracks.push_back(track);
    project.sequences.push_back(seq);
    const std::string text = saveProjectToNclip(project);
    // 默认设置不写入（保持旧工程 diff 稳定）
    NC_CHECK(text.find("autoDuck") == std::string::npos);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK(!loaded.sequences[0].tracks[0].autoDuck.enabled);
}

// =============================================================================
// 混音器集成：自动闪避在混音输出中真实生效
// =============================================================================
// 验证链路：轨道属性（enabled）→ prepare 构建闪避计划 → mixInto 应用增益。
// 测量方法：音乐轨 440Hz、旁白轨 1000Hz（不同频率），用 Goertzel 测 440Hz
// 能量即可从混音结果中单独观察音乐被压低的效果（不受旁白能量干扰）。

namespace {

constexpr double kMusicFreq = 440.0;
constexpr double kVoiceFreq = 1000.0;

/// 按路径路由的读取器（与生产代码行为一致：open(path) 决定内容）。
/// - path 含 "voice" → 窗口语音（[1.5,3.0) 内 1000Hz，其余静音）
/// - 其他 → 音乐（恒定 440Hz）
class RoutingReader : public MediaReader {
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
        for (std::int64_t i = 0; i < frames; ++i) {
            const double t = base + static_cast<double>(i) / sampleRate;
            float v = 0.0f;
            if (voiceMode_) {
                if (t >= 1.5 && t < 3.0) {
                    v = static_cast<float>(0.3 * std::sin(2.0 * kPi * kVoiceFreq * t));
                }
            } else {
                v = static_cast<float>(0.25 * std::sin(2.0 * kPi * kMusicFreq * t));
            }
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

/// 混音测试工厂：每次 create() 返回按路径路由的读取器。
class DuckFactory2 : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<RoutingReader>();
    }
};

/// Goertzel 单频能量。
double goertzelEnergy(const std::vector<float>& samples, int sampleRate, double freq) {
    const int n = static_cast<int>(samples.size());
    if (n < 64) return 0.0;
    const double w = 2.0 * kPi * freq / sampleRate;
    const double cw = std::cos(w);
    const double coeff = 2.0 * cw;
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i) {
        s0 = static_cast<double>(samples[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / static_cast<double>(n);
}

}  // namespace

NC_TEST(AutoDuckMixer, DucksMusicDuringVoice) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset music; music.id = AssetId{project.ids.next()};
    music.path = "/audio/music.wav"; music.type = MediaType::Audio; music.duration = Rational::make(6);
    project.assets.push_back(music);
    MediaAsset voice; voice.id = AssetId{project.ids.next()};
    voice.path = "/audio/voice.wav"; voice.type = MediaType::Audio; voice.duration = Rational::make(6);
    project.assets.push_back(voice);
    TrackId musicTrack, voiceTrack;
    editor.addTrack(seqId, TrackType::Audio, "音乐", &musicTrack);
    editor.addTrack(seqId, TrackType::Audio, "旁白", &voiceTrack);
    Clip mc; mc.asset = music.id; mc.start = Rational::make(0); mc.duration = Rational::make(6);
    editor.insertClip(seqId, musicTrack, mc);
    Clip vc; vc.asset = voice.id; vc.start = Rational::make(0); vc.duration = Rational::make(6);
    editor.insertClip(seqId, voiceTrack, vc);
    AutoDuckSettings duck;
    duck.enabled = true;
    duck.sensitivity = 0.6;
    duck.duckAmountDb = 12.0;
    NC_CHECK(editor.setTrackAutoDuck(seqId, musicTrack, duck).isOk());

    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    DuckFactory2 factory;
    MixFormat format; format.sampleRate = 48000; format.channels = 2;
    AudioMixer mixer(format, factory);
    NC_CHECK(mixer.prepare(plan, project));

    // 闪避区间应已生成（供时间轴绘制）
    const auto regions = mixer.duckRegions(musicTrack);
    NC_CHECK(!regions.empty());
    NC_CHECK(std::fabs(regions[0].gain - duckTargetGain(duck)) < 1e-6);

    // 采集三段音乐（440Hz）能量
    OutputAudioBlock block;
    std::vector<float> before, during, after;
    int blocks = 0;
    while (mixer.readBlock(block) && blocks < 400) {
        const double t = block.start.toDouble();
        std::vector<float> left;
        for (std::size_t i = 0; i + 1 < block.samples.size(); i += 2) {
            left.push_back(block.samples[i]);
        }
        if (t < 1.0) before.insert(before.end(), left.begin(), left.end());
        else if (t >= 2.0 && t < 2.9) during.insert(during.end(), left.begin(), left.end());
        else if (t > 4.5) after.insert(after.end(), left.begin(), left.end());
        ++blocks;
    }
    const double eBefore = goertzelEnergy(before, 48000, kMusicFreq);
    const double eDuring = goertzelEnergy(during, 48000, kMusicFreq);
    const double eAfter = goertzelEnergy(after, 48000, kMusicFreq);
    NC_CHECK(eBefore > 0.05);            // 旁白前音乐正常
    NC_CHECK(eDuring < eBefore * 0.5);   // 旁白中音乐被压低（约 1/4）
    NC_CHECK(eDuring > eBefore * 0.1);   // 未被完全静音
    NC_CHECK(eAfter > eBefore * 0.7);    // 旁白后恢复
}

NC_TEST(AutoDuckMixer, DisabledDuckLeavesMusicUnchanged) {
    // 未启用闪避：音乐能量应保持恒定（对照组）
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset music; music.id = AssetId{project.ids.next()};
    music.path = "/audio/music.wav"; music.type = MediaType::Audio; music.duration = Rational::make(6);
    project.assets.push_back(music);
    TrackId musicTrack;
    editor.addTrack(seqId, TrackType::Audio, "音乐", &musicTrack);
    Clip mc; mc.asset = music.id; mc.start = Rational::make(0); mc.duration = Rational::make(6);
    editor.insertClip(seqId, musicTrack, mc);

    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    DuckFactory2 factory;
    MixFormat format; format.sampleRate = 48000; format.channels = 2;
    AudioMixer mixer(format, factory);
    NC_CHECK(mixer.prepare(plan, project));
    NC_CHECK(mixer.duckRegions(musicTrack).empty());  // 无闪避区间

    OutputAudioBlock block;
    std::vector<float> a, b;
    int blocks = 0;
    while (mixer.readBlock(block) && blocks < 400) {
        const double t = block.start.toDouble();
        std::vector<float> left;
        for (std::size_t i = 0; i + 1 < block.samples.size(); i += 2) {
            left.push_back(block.samples[i]);
        }
        if (t < 2.0) a.insert(a.end(), left.begin(), left.end());
        else if (t >= 3.0 && t < 5.0) b.insert(b.end(), left.begin(), left.end());
        ++blocks;
    }
    const double ea = goertzelEnergy(a, 48000, kMusicFreq);
    const double eb = goertzelEnergy(b, 48000, kMusicFreq);
    NC_CHECK(ea > 0.05);
    NC_CHECK(std::fabs(ea - eb) < 0.02);  // 能量恒定（无压低）
}
