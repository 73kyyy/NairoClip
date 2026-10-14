// =============================================================================
// 音频降噪测试（文档「🎵音频 → 降噪 → 开关/强度(弱中强)」）
// =============================================================================
// 覆盖：
//   1. 强度枚举/名称/字符串/整数解析与参数映射；
//   2. 流式 DSP 契约：时轴零偏移、总样本守恒（多种块尺寸）、直通（Off）、
//      确定性、多通道独立；
//   3. 降噪效果：纯噪声抑制量（三档区分）、音节突发（真实语音特征）保留；
//   4. 集成：Clip 属性 → 命令/撤销 → 序列化往返 → C ABI。
// 说明：降噪效果测试信号为「噪声 + 音节突发正弦」（贴近真实口播的幅度
//       起伏）；不用持续稳态纯音（谱减法对稳态音有压制倾向，非目标场景）。
#include "test_framework.h"
#include "nairoclip/core/audio_denoise.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr double kPi = 3.14159265358979323846;

/// 生成「噪声 + 音节突发」测试信号：
/// - 前 burstSeconds 秒：每 periodSeconds 中 onSeconds 发声（300Hz 正弦，
///   幅度 amplitude），其余为噪声；
/// - 其余时间：纯噪声。
std::vector<float> makeSpeechLikeSignal(int totalFrames, int sampleRate, double amplitude = 0.4,
                                        double onSeconds = 0.15, double periodSeconds = 0.2) {
    std::vector<float> signal(static_cast<std::size_t>(totalFrames), 0.0f);
    std::mt19937 rng(20240714);
    std::uniform_real_distribution<double> noiseDist(-1.0, 1.0);
    const double burstSeconds = 2.0;
    for (int i = 0; i < totalFrames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        double v = noiseDist(rng) * 0.08;
        if (t < burstSeconds) {
            const double phase = std::fmod(t, periodSeconds);
            if (phase < onSeconds) {
                v += amplitude * std::sin(2.0 * kPi * 300.0 * t);
            }
        }
        signal[static_cast<std::size_t>(i)] = static_cast<float>(v);
    }
    return signal;
}

/// 以固定输出块尺寸驱动降噪器（模拟混音器的块式驱动）。
std::vector<float> processBlockwise(const std::vector<float>& input, int sampleRate,
                                    DenoiseStrength strength, int channels, int blockFrames) {
    AudioDenoiser denoiser(strength, channels);
    std::vector<float> output;
    const int totalFrames = static_cast<int>(input.size() / static_cast<std::size_t>(channels));
    int pos = 0;
    while (pos < totalFrames) {
        const int feed = denoiser.feedFramesFor(blockFrames);
        const int avail = std::min(feed, totalFrames - pos);
        const bool isFinal = (pos + avail >= totalFrames);
        denoiser.pushAndPull(input.data() + static_cast<std::size_t>(pos) * channels, avail,
                             isFinal, output);
        pos += avail;
    }
    (void)sampleRate;
    return output;
}

double rmsRange(const std::vector<float>& x, int channels, double startSeconds, double endSeconds,
                int sampleRate) {
    const std::size_t frames = x.size() / static_cast<std::size_t>(channels);
    const std::size_t from = static_cast<std::size_t>(startSeconds * sampleRate);
    const std::size_t to = std::min(static_cast<std::size_t>(endSeconds * sampleRate), frames);
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = from; i < to; ++i) {
        const double v = x[i * static_cast<std::size_t>(channels)];
        sum += v * v;
        ++count;
    }
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

/// Goertzel：区间内 300Hz 分量幅度（每帧）。
double toneAmplitude(const std::vector<float>& x, int channels, double startSeconds,
                     double endSeconds, int sampleRate) {
    const std::size_t frames = x.size() / static_cast<std::size_t>(channels);
    const std::size_t from = static_cast<std::size_t>(startSeconds * sampleRate);
    const std::size_t to = std::min(static_cast<std::size_t>(endSeconds * sampleRate), frames);
    const double w = 2.0 * kPi * 300.0 / sampleRate;
    double re = 0.0;
    double im = 0.0;
    std::size_t count = 0;
    for (std::size_t i = from; i < to; ++i) {
        const double v = x[i * static_cast<std::size_t>(channels)];
        re += v * std::cos(w * static_cast<double>(i));
        im += v * std::sin(w * static_cast<double>(i));
        ++count;
    }
    return count > 0 ? std::sqrt(re * re + im * im) / static_cast<double>(count) : 0.0;
}

/// 音节突发窗口的 300Hz 幅度（7 个突发的中段平均；避开起止边沿）。
double burstTone(const std::vector<float>& x, int channels, int sampleRate) {
    double total = 0.0;
    for (int k = 1; k < 8; ++k) {
        const double start = k * 0.2 + 0.03;
        const double end = k * 0.2 + 0.13;
        total += toneAmplitude(x, channels, start, end, sampleRate);
    }
    return total / 7.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 枚举 / 解析 / 参数映射
// ---------------------------------------------------------------------------

NC_TEST(AudioDenoise, StrengthNames) {
    NC_CHECK_EQ(std::string(denoiseStrengthName(DenoiseStrength::Off)), "off");
    NC_CHECK_EQ(std::string(denoiseStrengthName(DenoiseStrength::Weak)), "weak");
    NC_CHECK_EQ(std::string(denoiseStrengthName(DenoiseStrength::Medium)), "medium");
    NC_CHECK_EQ(std::string(denoiseStrengthName(DenoiseStrength::Strong)), "strong");
}

NC_TEST(AudioDenoise, StrengthFromString) {
    DenoiseStrength s;
    NC_CHECK(denoiseStrengthFromString("off", s) && s == DenoiseStrength::Off);
    NC_CHECK(denoiseStrengthFromString("WEAK", s) && s == DenoiseStrength::Weak);
    NC_CHECK(denoiseStrengthFromString("Medium", s) && s == DenoiseStrength::Medium);
    NC_CHECK(denoiseStrengthFromString("strong", s) && s == DenoiseStrength::Strong);
    // 中文（文档「弱中强」）
    NC_CHECK(denoiseStrengthFromString("弱", s) && s == DenoiseStrength::Weak);
    NC_CHECK(denoiseStrengthFromString("中", s) && s == DenoiseStrength::Medium);
    NC_CHECK(denoiseStrengthFromString("强", s) && s == DenoiseStrength::Strong);
    NC_CHECK(denoiseStrengthFromString("关闭", s) && s == DenoiseStrength::Off);
    // 非法
    NC_CHECK(!denoiseStrengthFromString("ultra", s));
    NC_CHECK(!denoiseStrengthFromString("", s));
    NC_CHECK(!denoiseStrengthFromString(nullptr, s));
}

NC_TEST(AudioDenoise, StrengthFromInt) {
    DenoiseStrength s;
    NC_CHECK(denoiseStrengthFromInt(0, s) && s == DenoiseStrength::Off);
    NC_CHECK(denoiseStrengthFromInt(1, s) && s == DenoiseStrength::Weak);
    NC_CHECK(denoiseStrengthFromInt(2, s) && s == DenoiseStrength::Medium);
    NC_CHECK(denoiseStrengthFromInt(3, s) && s == DenoiseStrength::Strong);
    NC_CHECK(!denoiseStrengthFromInt(4, s));
    NC_CHECK(!denoiseStrengthFromInt(-1, s));
}

NC_TEST(AudioDenoise, TuningMapping) {
    // 文档「强度(弱中强)」：强度越高过减因子越大、谱底越低
    const DenoiseTuning weak = denoiseTuningFor(DenoiseStrength::Weak);
    const DenoiseTuning medium = denoiseTuningFor(DenoiseStrength::Medium);
    const DenoiseTuning strong = denoiseTuningFor(DenoiseStrength::Strong);
    NC_CHECK(weak.overSubtraction > 1.0);
    NC_CHECK(medium.overSubtraction > weak.overSubtraction);
    NC_CHECK(strong.overSubtraction > medium.overSubtraction);
    NC_CHECK(weak.spectralFloor > medium.spectralFloor);
    NC_CHECK(medium.spectralFloor > strong.spectralFloor);
    // Off 为数学等价直通
    const DenoiseTuning off = denoiseTuningFor(DenoiseStrength::Off);
    NC_CHECK_EQ(off.overSubtraction, 1.0);
    NC_CHECK_EQ(off.spectralFloor, 1.0);
}

// ---------------------------------------------------------------------------
// 2. 流式 DSP 契约
// ---------------------------------------------------------------------------

NC_TEST(AudioDenoise, OffIsPassthrough) {
    std::vector<float> input(48000);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (auto& v : input) v = dist(rng);
    AudioDenoiser denoiser(DenoiseStrength::Off, 1);
    NC_CHECK(!denoiser.active());
    std::vector<float> output;
    const std::size_t produced = denoiser.pushAndPull(input.data(), 48000, true, output);
    NC_CHECK_EQ(produced, std::size_t(48000));
    NC_CHECK_EQ(output.size(), input.size());
    bool identical = true;
    for (std::size_t i = 0; i < input.size(); ++i) {
        if (output[i] != input[i]) { identical = false; break; }
    }
    NC_CHECK(identical);
}

NC_TEST(AudioDenoise, FeedFramesForFirstBlockIncludesLookahead) {
    AudioDenoiser denoiser(DenoiseStrength::Medium, 1);
    // 首块多要半窗前视（512）
    NC_CHECK_EQ(denoiser.feedFramesFor(1024), 1024 + 512);
    // Off：直通，无前视
    AudioDenoiser off(DenoiseStrength::Off, 1);
    NC_CHECK_EQ(off.feedFramesFor(1024), 1024);
}

NC_TEST(AudioDenoise, ZeroTimeOffsetForImpulse) {
    // 脉冲输入位置 == 输出峰值位置（无时轴偏移）
    const int sampleRate = 48000;
    std::vector<float> input(static_cast<std::size_t>(sampleRate), 0.0f);
    const int impulseAt = 20000;
    input[static_cast<std::size_t>(impulseAt)] = 1.0f;
    const std::vector<float> output =
        processBlockwise(input, sampleRate, DenoiseStrength::Medium, 1, 1024);
    NC_CHECK_EQ(output.size(), input.size());
    int peak = 0;
    double peakValue = 0.0;
    for (std::size_t i = 0; i < output.size(); ++i) {
        if (std::abs(output[i]) > peakValue) {
            peakValue = std::abs(output[i]);
            peak = static_cast<int>(i);
        }
    }
    NC_CHECK_EQ(peak, impulseAt);
    NC_CHECK(peakValue > 0.5);
}

NC_TEST(AudioDenoise, SampleCountConservedAcrossBlockSizes) {
    // 任意块尺寸下：输出总帧数 == 输入总帧数（含非整窗长度）
    const int sampleRate = 48000;
    std::vector<float> input(static_cast<std::size_t>(sampleRate * 2 + 777), 0.0f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> dist(-0.1f, 0.1f);
    for (auto& v : input) v = dist(rng);
    for (int block : {512, 1024, 941, 2048, 300}) {
        const std::vector<float> output =
            processBlockwise(input, sampleRate, DenoiseStrength::Weak, 1, block);
        NC_CHECK_EQ(output.size(), input.size());
    }
}

NC_TEST(AudioDenoise, Deterministic) {
    const int sampleRate = 48000;
    std::vector<float> input = makeSpeechLikeSignal(sampleRate * 2, sampleRate);
    const std::vector<float> first =
        processBlockwise(input, sampleRate, DenoiseStrength::Medium, 1, 1024);
    const std::vector<float> second =
        processBlockwise(input, sampleRate, DenoiseStrength::Medium, 1, 1024);
    NC_CHECK_EQ(first.size(), second.size());
    bool identical = first.size() == second.size();
    for (std::size_t i = 0; identical && i < first.size(); ++i) {
        if (first[i] != second[i]) identical = false;
    }
    NC_CHECK(identical);
}

NC_TEST(AudioDenoise, MultiChannelIndependent) {
    // 双声道：通道 0 为语音信号，通道 1 为静音；输出通道 1 应接近静音
    const int sampleRate = 48000;
    const std::vector<float> mono = makeSpeechLikeSignal(sampleRate * 3, sampleRate);
    std::vector<float> stereo(mono.size() * 2, 0.0f);
    for (std::size_t i = 0; i < mono.size(); ++i) {
        stereo[i * 2] = mono[i];
        stereo[i * 2 + 1] = 0.0f;
    }
    const std::vector<float> output =
        processBlockwise(stereo, sampleRate, DenoiseStrength::Medium, 2, 1024);
    NC_CHECK_EQ(output.size(), stereo.size());
    // 通道 1 全零输入 → 输出应接近零（独立处理）
    double maxAbs = 0.0;
    for (std::size_t i = 1; i < output.size(); i += 2) {
        maxAbs = std::max(maxAbs, static_cast<double>(std::abs(output[i])));
    }
    NC_CHECK(maxAbs < 1e-6);
    // 通道 0 仍有语音突发能量保留
    NC_CHECK(rmsRange(output, 2, 0.3, 1.9, sampleRate) > 0.05);
}

NC_TEST(AudioDenoise, ResetClearsState) {
    AudioDenoiser denoiser(DenoiseStrength::Strong, 1);
    std::vector<float> input(1024, 0.1f);
    std::vector<float> output;
    denoiser.pushAndPull(input.data(), 1024, false, output);
    denoiser.reset();
    // reset 后 feedFramesFor 恢复首块语义（含前视）
    NC_CHECK_EQ(denoiser.feedFramesFor(1024), 1024 + AudioDenoiser::hopSize());
    // 再次处理同样输入 → 与首次结果一致（状态已清）
    std::vector<float> first;
    AudioDenoiser fresh(DenoiseStrength::Strong, 1);
    fresh.pushAndPull(input.data(), 1024, false, first);
    std::vector<float> second;
    denoiser.pushAndPull(input.data(), 1024, false, second);
    NC_CHECK_EQ(first.size(), second.size());
    bool identical = first.size() == second.size();
    for (std::size_t i = 0; identical && i < first.size(); ++i) {
        if (first[i] != second[i]) identical = false;
    }
    NC_CHECK(identical);
}

// ---------------------------------------------------------------------------
// 3. 降噪效果
// ---------------------------------------------------------------------------

NC_TEST(AudioDenoise, NoiseSuppressionByStrength) {
    // 纯噪声（2-4s 区间）：三档抑制量应递增且显著
    const int sampleRate = 48000;
    const std::vector<float> input = makeSpeechLikeSignal(sampleRate * 4, sampleRate);
    const double rmsIn = rmsRange(input, 1, 2.5, 3.9, sampleRate);
    double suppression[3] = {0.0, 0.0, 0.0};
    const DenoiseStrength strengths[3] = {DenoiseStrength::Weak, DenoiseStrength::Medium,
                                          DenoiseStrength::Strong};
    for (int i = 0; i < 3; ++i) {
        const std::vector<float> output =
            processBlockwise(input, sampleRate, strengths[i], 1, 1024);
        const double rmsOut = rmsRange(output, 1, 2.5, 3.9, sampleRate);
        suppression[i] = 20.0 * std::log10((rmsOut + 1e-12) / (rmsIn + 1e-12));
    }
    // 每档至少 8dB 抑制；强档至少 15dB；且强度递增
    NC_CHECK(suppression[0] < -8.0);
    NC_CHECK(suppression[1] < -14.0);
    NC_CHECK(suppression[2] < -20.0);
    NC_CHECK(suppression[1] < suppression[0]);
    NC_CHECK(suppression[2] < suppression[1]);
}

NC_TEST(AudioDenoise, SpeechBurstsPreserved) {
    // 音节突发（真实语音特征）：三档均应保留大部分能量（>-8dB；
    // 强档设计上更激进、允许牺牲少量细节，故阈值略宽），且强度越高保留越低。
    const int sampleRate = 48000;
    const std::vector<float> input = makeSpeechLikeSignal(sampleRate * 4, sampleRate);
    const double toneIn = burstTone(input, 1, sampleRate);
    NC_CHECK(toneIn > 0.05);  // 信号自检：突发存在
    const DenoiseStrength strengths[3] = {DenoiseStrength::Weak, DenoiseStrength::Medium,
                                          DenoiseStrength::Strong};
    double keepDb[3] = {0.0, 0.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        const std::vector<float> output = processBlockwise(input, sampleRate, strengths[i], 1, 1024);
        const double toneOut = burstTone(output, 1, sampleRate);
        keepDb[i] = 20.0 * std::log10((toneOut + 1e-12) / (toneIn + 1e-12));
        NC_CHECK(keepDb[i] > -8.0);
    }
    // 强度越高越激进（保留量单调下降）
    NC_CHECK(keepDb[1] < keepDb[0]);
    NC_CHECK(keepDb[2] < keepDb[1]);
}

NC_TEST(AudioDenoise, NoiseEstimateConvergesToStableSuppression) {
    // 长纯噪声：后期应保持稳定抑制（不因估计漂移失效）
    const int sampleRate = 48000;
    std::mt19937 rng(4242);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    std::vector<float> input(static_cast<std::size_t>(sampleRate * 6), 0.0f);
    for (auto& v : input) v = static_cast<float>(dist(rng) * 0.08);
    const std::vector<float> output =
        processBlockwise(input, sampleRate, DenoiseStrength::Medium, 1, 1024);
    const double early = rmsRange(output, 1, 1.0, 2.0, sampleRate);
    const double late = rmsRange(output, 1, 4.5, 5.9, sampleRate);
    // 稳定段抑制仍然显著（late 远小于输入 0.08/sqrt(3)≈0.046）
    NC_CHECK(late < 0.02);
    // 早/晚抑制量同量级（无漂移失效）
    NC_CHECK(late < early * 3.0 + 1e-6);
}

// ---------------------------------------------------------------------------
// 4. 集成：命令 / 撤销 / 序列化 / C ABI
// ---------------------------------------------------------------------------

namespace {

struct DenoiseFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    ClipId clipId;

    DenoiseFixture() {
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
        editor.addTrack(seqId, TrackType::Audio, "A1", &trackId);
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(10);
        editor.insertClip(seqId, trackId, c, &clipId);
    }
};

}  // namespace

NC_TEST(AudioDenoise, EditorSetAndUndo) {
    DenoiseFixture f;
    NC_CHECK_EQ(static_cast<int>(f.project.mainSequence()->tracks[0].clips[0].denoise), 0);
    NC_CHECK(f.editor.setClipDenoise(f.seqId, f.trackId, f.clipId, DenoiseStrength::Strong).isOk());
    NC_CHECK_EQ(static_cast<int>(f.project.mainSequence()->tracks[0].clips[0].denoise), 3);
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(static_cast<int>(f.project.mainSequence()->tracks[0].clips[0].denoise), 0);
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(static_cast<int>(f.project.mainSequence()->tracks[0].clips[0].denoise), 3);
    // 关闭（off）为合法操作
    NC_CHECK(f.editor.setClipDenoise(f.seqId, f.trackId, f.clipId, DenoiseStrength::Off).isOk());
    NC_CHECK_EQ(static_cast<int>(f.project.mainSequence()->tracks[0].clips[0].denoise), 0);
}

NC_TEST(AudioDenoise, EditorRejectsMissingClip) {
    DenoiseFixture f;
    const Result r = f.editor.setClipDenoise(f.seqId, f.trackId, ClipId{9999},
                                             DenoiseStrength::Weak);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::NotFound);
}

NC_TEST(AudioDenoise, CommandNameMentionsStrength) {
    DenoiseFixture f;
    NC_CHECK(f.editor.setClipDenoise(f.seqId, f.trackId, f.clipId, DenoiseStrength::Weak).isOk());
    const std::vector<std::string> names = f.editor.undoHistoryNames();
    NC_CHECK(!names.empty());
    NC_CHECK(names.back().find("弱") != std::string::npos);
}

NC_TEST(AudioDenoise, SerializationRoundtrip) {
    DenoiseFixture f;
    NC_CHECK(f.editor.setClipDenoise(f.seqId, f.trackId, f.clipId, DenoiseStrength::Strong).isOk());
    const std::string text = saveProjectToNclip(f.project);
    // off 以外的强度应写入 JSON
    NC_CHECK(text.find("\"denoise\"") != std::string::npos);
    NC_CHECK(text.find("strong") != std::string::npos);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK_EQ(static_cast<int>(loaded.mainSequence()->tracks[0].clips[0].denoise), 3);
}

NC_TEST(AudioDenoise, SerializationOmitsOff) {
    DenoiseFixture f;
    // 默认（off）不写入（保持 diff 稳定）
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"denoise\"") == std::string::npos);
}

NC_TEST(AudioDenoise, SerializationTolerantToInvalidValue) {
    DenoiseFixture f;
    NC_CHECK(f.editor.setClipDenoise(f.seqId, f.trackId, f.clipId, DenoiseStrength::Weak).isOk());
    std::string text = saveProjectToNclip(f.project);
    // 把非法强度值写入 → 载入应降级为 off（工程可打开）
    const std::size_t pos = text.find("weak");
    NC_CHECK(pos != std::string::npos);
    text.replace(pos, 4, "ultra");
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK_EQ(static_cast<int>(loaded.mainSequence()->tracks[0].clips[0].denoise), 0);
}

NC_TEST(AudioDenoise, CapiSetGetDenoise) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t asset =
        nc_import_asset(editor, "a.wav", "/audio/a.wav", 1, 60000000);
    NC_CHECK(asset != 0);
    (void)nc_add_track(editor, "A1", 1);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 0, 5000000);
    NC_CHECK(clip != 0);
    // 默认 off
    NC_CHECK_EQ(nc_get_clip_denoise(editor, 0, clip), 0);
    // 设置 strong
    NC_CHECK_EQ(nc_set_clip_denoise(editor, 0, clip, 3), NC_OK);
    NC_CHECK_EQ(nc_get_clip_denoise(editor, 0, clip), 3);
    // 非法强度
    NC_CHECK_EQ(nc_set_clip_denoise(editor, 0, clip, 7), NC_ERROR_INVALID_ARGUMENT);
    // 关闭
    NC_CHECK_EQ(nc_set_clip_denoise(editor, 0, clip, 0), NC_OK);
    NC_CHECK_EQ(nc_get_clip_denoise(editor, 0, clip), 0);
    // NULL 安全
    NC_CHECK_EQ(nc_set_clip_denoise(nullptr, 0, clip, 1), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_clip_denoise(nullptr, 0, clip), -1);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 5. 导出集成（真实素材 + 真实混音；无 FFmpeg 时条件编译排除）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/audio_mixer.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_ffmpeg.h"

#include <fstream>

namespace {

bool denoiseMediaExists() {
    std::ifstream f("/tmp/media_gen/denoise_src.wav");
    return f.good();
}

/// 用真实素材 + 真实 FFmpeg 读取器跑混音（返回交错样本；单声道）。
std::vector<float> mixDenoiseFixture(DenoiseStrength strength, double* firstBlockStart) {
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
    a.path = "/tmp/media_gen/denoise_src.wav";
    a.type = MediaType::Audio;
    a.duration = Rational::make(4);
    project.assets.push_back(a);
    const AssetId assetId = project.assets.front().id;
    TrackId trackId;
    editor.addTrack(seqId, TrackType::Audio, "A1", &trackId);
    Clip c;
    c.asset = assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(4);
    ClipId clipId;
    editor.insertClip(seqId, trackId, c, &clipId);
    if (strength != DenoiseStrength::Off) {
        editor.setClipDenoise(seqId, trackId, clipId, strength);
    }
    const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
    FFmpegReaderFactory factory;
    AudioMixer mixer(MixFormat{48000, 2}, factory);
    if (!mixer.prepare(plan, project)) {
        return {};
    }
    std::vector<float> all;
    OutputAudioBlock block;
    bool first = true;
    while (mixer.readBlock(block)) {
        if (first) {
            if (firstBlockStart != nullptr) *firstBlockStart = block.start.toDouble();
            first = false;
        }
        all.insert(all.end(), block.samples.begin(), block.samples.end());
    }
    return all;
}

}  // namespace

NC_TEST(AudioDenoise, MixerAppliesDenoiseToRealMedia) {
    if (!denoiseMediaExists()) return;
    double startOff = -1.0;
    double startOn = -1.0;
    const std::vector<float> off = mixDenoiseFixture(DenoiseStrength::Off, &startOff);
    const std::vector<float> on = mixDenoiseFixture(DenoiseStrength::Medium, &startOn);
    NC_CHECK(!off.empty());
    NC_CHECK(!on.empty());
    // 时轴对齐：首块自 0 开始；总帧数守恒（块对齐不丢样本）
    NC_CHECK(std::abs(startOff) < 1e-9);
    NC_CHECK(std::abs(startOn) < 1e-9);
    NC_CHECK_EQ(off.size(), on.size());
    // 噪声段（2.5-3.9s）RMS 应显著下降
    const double noiseIn = rmsRange(off, 2, 2.5, 3.9, 48000);
    const double noiseOut = rmsRange(on, 2, 2.5, 3.9, 48000);
    NC_CHECK(noiseIn > 0.02);
    NC_CHECK(20.0 * std::log10((noiseOut + 1e-12) / (noiseIn + 1e-12)) < -10.0);
    // 语音突发（前 2s 突发中段）保留
    const double toneIn = burstTone(off, 2, 48000);
    const double toneOut = burstTone(on, 2, 48000);
    NC_CHECK(toneIn > 0.05);
    NC_CHECK(20.0 * std::log10((toneOut + 1e-12) / (toneIn + 1e-12)) > -8.0);
}

#endif  // NAIROCLIP_HAS_FFMPEG
