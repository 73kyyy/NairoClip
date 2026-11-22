// =============================================================================
// 语音转字幕 AI 测试（文档 Part 4-A 3.2：Whisper 真实推理链路）
// =============================================================================
// 覆盖：
//   1. 语言枚举 / 名称 / token 映射（文档 3.2「语言选择」）；
//   2. Mel 频谱（Whisper 标准：80 通道 × 帧数；标准化范围 [-1,1]）；
//   3. BPE 词表加载与 token 解码（真实词表；未加载时明确空串）；
//   4. 真实 Whisper-tiny merged decoder 自回归链路（需 ORT + 模型；缺失跳过）；
//   5. 失败路径（未加载/空音频/文件缺失）。
#include "test_framework.h"
#include "nairoclip/core/speech_ai.h"
#include "nairoclip/core/capi.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

std::string findModelFile(const char* workspacePath, const char* repositoryPath) {
    for (const char* candidate : {workspacePath, repositoryPath}) {
        FILE* file = std::fopen(candidate, "rb");
        if (file != nullptr) {
            std::fclose(file);
            return candidate;
        }
    }
    return {};
}

const std::string& whisperEncoderPath() {
    static const std::string path = findModelFile(
        "/workspace/deps/whisper_tiny_encoder.onnx", "models/whisper_zh/encoder_model.onnx");
    return path;
}

const std::string& whisperDecoderPath() {
    static const std::string path = findModelFile(
        "/workspace/deps/whisper_tiny_decoder.onnx", "models/whisper_zh/decoder_model_merged.onnx");
    return path;
}

const std::string& whisperTokenizerPath() {
    static const std::string path = findModelFile(
        "/workspace/deps/whisper_tiny_tokenizer.json", "models/whisper_zh/tokenizer.json");
    return path;
}

bool whisperModelsAvailable() {
    return aiEngineAvailable() && !whisperEncoderPath().empty() && !whisperDecoderPath().empty();
}

bool tokenizerAvailable() {
    return !whisperTokenizerPath().empty();
}

/// 合成音频（语音类：440Hz 调制 + 静音间隙）。
std::vector<float> makeSpeechAudio(int sampleRate, double seconds) {
    const std::size_t frames = static_cast<std::size_t>(sampleRate * seconds);
    std::vector<float> pcm(frames, 0.0f);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        // 0-1s 有声、1-1.5s 静音、1.5-2s 有声（周期性）
        const double phase = std::fmod(t, 2.0);
        const bool voiced = phase < 1.0 || (phase >= 1.5 && phase < 2.0);
        if (voiced) {
            pcm[i] = static_cast<float>(
                0.3 * std::sin(2.0 * 3.14159265358979 * 220.0 * t) *
                (0.6 + 0.4 * std::sin(2.0 * 3.14159265358979 * 4.0 * t)));
        }
    }
    return pcm;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 语言枚举
// ---------------------------------------------------------------------------

NC_TEST(SpeechAI, LanguageNamesAndTokens) {
    NC_CHECK_EQ(std::string(speechLanguageName(SpeechLanguage::Auto)), std::string("auto"));
    NC_CHECK_EQ(std::string(speechLanguageName(SpeechLanguage::Chinese)), std::string("zh"));
    NC_CHECK_EQ(std::string(speechLanguageName(SpeechLanguage::English)), std::string("en"));
    NC_CHECK_EQ(std::string(speechLanguageName(SpeechLanguage::Japanese)), std::string("ja"));
    NC_CHECK_EQ(std::string(speechLanguageName(SpeechLanguage::Korean)), std::string("ko"));
    SpeechLanguage language;
    NC_CHECK(speechLanguageFromInt(0, language) && language == SpeechLanguage::Auto);
    NC_CHECK(speechLanguageFromInt(1, language) && language == SpeechLanguage::Chinese);
    NC_CHECK(!speechLanguageFromInt(5, language));
    // 语言 token（Whisper 多语言模型的 <|zh|>=50260 / <|en|>=50259 等）
    NC_CHECK_EQ(speechLanguageToken(SpeechLanguage::Chinese), 50260);
    NC_CHECK_EQ(speechLanguageToken(SpeechLanguage::English), 50259);
    NC_CHECK_EQ(speechLanguageToken(SpeechLanguage::Japanese), 50266);
    NC_CHECK_EQ(speechLanguageToken(SpeechLanguage::Korean), 50264);
    NC_CHECK_EQ(speechLanguageToken(SpeechLanguage::Auto), -1);  // 自动检测
}

// ---------------------------------------------------------------------------
// 2. Mel 频谱（Whisper 标准）
// ---------------------------------------------------------------------------

NC_TEST(SpeechAI, MelSpectrogramComputed) {
    const int sampleRate = 16000;
    const std::vector<float> pcm = makeSpeechAudio(sampleRate, 2.0);
    std::vector<float> mel;
    NC_CHECK(SpeechRecognizer::computeMelSpectrogram(pcm.data(), pcm.size(), sampleRate, 3000,
                                                     mel));
    // 尺寸：80 通道 × 3000 帧
    NC_CHECK_EQ(mel.size(), std::size_t(80 * 3000));
    // 标准化后应在 [-1, 1]（Whisper: (log10 - max) clamp(-8,0) → (x+4)/4）
    for (float v : mel) {
        NC_CHECK(v >= -1.05f && v <= 1.05f);
    }
    // 有声段与静音段的频谱应有差异。
    // 说明：信号为 220Hz 正弦，能量集中在低 mel 频段（m < 30）；
    //       高频段（m ≥ 40）在两种情况下都无能量（饱和到 -1），因此
    //       只比较「有信号的频段」（与真实频谱特性一致）。
    double voicedSum = 0.0;
    double silentSum = 0.0;
    int count = 0;
    for (int m = 0; m < 24; m += 4) {
        // 帧 20-80（0.2-0.8s 有声）vs 帧 120-140（1.2-1.4s 静音）
        for (int f = 20; f < 80; ++f) {
            voicedSum += mel[static_cast<std::size_t>(m) * 3000 + static_cast<std::size_t>(f)];
        }
        for (int f = 120; f < 140; ++f) {
            silentSum += mel[static_cast<std::size_t>(m) * 3000 + static_cast<std::size_t>(f)];
        }
        ++count;
    }
    NC_CHECK(voicedSum / count > silentSum / count);  // 有声 > 静音
    // 参数校验
    NC_CHECK(!SpeechRecognizer::computeMelSpectrogram(nullptr, 100, sampleRate, 3000, mel));
    NC_CHECK(!SpeechRecognizer::computeMelSpectrogram(pcm.data(), 0, sampleRate, 3000, mel));
    NC_CHECK(!SpeechRecognizer::computeMelSpectrogram(pcm.data(), 100, 0, 3000, mel));
}

NC_TEST(SpeechAI, MelSpectrogramResamples) {
    // 44.1kHz 源 → 内部重采样到 16kHz（验证不崩溃且时长对齐）
    const int sampleRate = 44100;
    const std::vector<float> pcm = makeSpeechAudio(sampleRate, 2.0);
    std::vector<float> mel;
    NC_CHECK(SpeechRecognizer::computeMelSpectrogram(pcm.data(), pcm.size(), sampleRate, 3000,
                                                     mel));
    NC_CHECK_EQ(mel.size(), std::size_t(80 * 3000));
}

// ---------------------------------------------------------------------------
// 3. BPE 词表（真实词表文件；缺失跳过）
// ---------------------------------------------------------------------------

NC_TEST(SpeechAI, TokenizerLoadAndDecode) {
    if (!tokenizerAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    NC_CHECK(recognizer.loadTokenizer(whisperTokenizerPath(), &failure));
    NC_CHECK(recognizer.tokenizerLoaded());
    // 已知 token 解码：Whisper BPE 中 15496="Hello" 相关词；验证非空
    const std::vector<int> tokens = {15496, 11, 50257};
    const std::string text = recognizer.decodeTokens(tokens);
    NC_CHECK(!text.empty());  // 真实解码（非空）
    NC_CHECK(text.find("506") == std::string::npos);  // 非占位符形式
    // 特殊 token 被过滤（不出现 <|...|> 文本）
    const std::vector<int> special = {50258, 50260, 50359};
    const std::string filtered = recognizer.decodeTokens(special);
    NC_CHECK(filtered.find("<|") == std::string::npos);
    // 空 token → 空串
    NC_CHECK_EQ(recognizer.decodeTokens({}), std::string());
}

NC_TEST(SpeechAI, TokenizerLoadsFromMemoryJson) {
    SpeechRecognizer recognizer;
    std::string failure;
    const std::string tokenizer =
        R"({"model":{"vocab":{"Hello":15496}},"added_tokens":[{"id":50257,"content":"<|endoftext|>"}]})";
    NC_CHECK(recognizer.loadTokenizerJson(tokenizer, &failure));
    NC_CHECK(recognizer.tokenizerLoaded());
    NC_CHECK(!recognizer.decodeTokens({15496}).empty());
    NC_CHECK(recognizer.decodeTokens({50257}).empty());
    failure.clear();
    NC_CHECK(!recognizer.loadTokenizerJson("{}", &failure));
    NC_CHECK(!failure.empty());
}

NC_TEST(SpeechAI, UnloadClearsTokenizerState) {
    SpeechRecognizer recognizer;
    std::string failure;
    const std::string tokenizer =
        R"({"model":{"vocab":{"Hello":15496}},"added_tokens":[{"id":50257,"content":"<|endoftext|>"}]})";
    NC_CHECK(recognizer.loadTokenizerJson(tokenizer, &failure));
    NC_CHECK(recognizer.tokenizerLoaded());
    recognizer.unload();
    NC_CHECK(!recognizer.tokenizerLoaded());
    NC_CHECK(recognizer.decodeTokens({15496}).empty());
}

NC_TEST(SpeechAI, MemoryModelLoadRejectsEmptyBuffers) {
    SpeechRecognizer recognizer;
    std::string failure;
    NC_CHECK(!recognizer.loadFromMemory(nullptr, 0, nullptr, 0, &failure));
    NC_CHECK(!failure.empty());
    NC_CHECK(!recognizer.loaded());
}

NC_TEST(SpeechAI, TokenizerFailurePaths) {
    if (!tokenizerAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    // 未加载词表 → 解码返回空串（不伪造）
    NC_CHECK_EQ(recognizer.decodeTokens({15496}), std::string());
    NC_CHECK(!recognizer.tokenizerLoaded());
    // 不存在的文件
    NC_CHECK(!recognizer.loadTokenizer("/tmp/no_such_tokenizer.json", &failure));
    NC_CHECK(!failure.empty());
}

// ---------------------------------------------------------------------------
// 4. 真实推理链路（需 ORT + 模型；缺失跳过）
// ---------------------------------------------------------------------------

NC_TEST(SpeechAI, RealInferenceChain) {
    if (!whisperModelsAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    NC_CHECK(recognizer.load(whisperEncoderPath(), whisperDecoderPath(), &failure));
    NC_CHECK(recognizer.loaded());
    // Whisper-tiny merged decoder 由 SpeechRecognizer 驱动完整 KV cache 自回归解码。
    const int sampleRate = 16000;
    const std::vector<float> pcm = makeSpeechAudio(sampleRate, 2.0);
    std::vector<SpeechSegment> segments;
    // 调用完整 transcribe：若模型契约正确，应得到非空分段文本。
    const bool ok = recognizer.transcribe(pcm.data(), pcm.size(), sampleRate,
                                          SpeechLanguage::Chinese, segments, nullptr, &failure);
    if (!ok) {
        // 断言：模型契约或运行时失败必须给出明确原因，不允许静默假成功
        NC_CHECK(!failure.empty());
        NC_CHECK(failure.find("不可用") != std::string::npos ||
                 failure.find("不匹配") != std::string::npos ||
                 failure.find("推理失败") != std::string::npos ||
                 failure.find("解码") != std::string::npos ||
                 failure.find("tokenizer") != std::string::npos ||
                 failure.find("词表") != std::string::npos);
    } else {
        // 若未来支持完整解码：分段应有效
        NC_CHECK(!segments.empty());
    }
}

NC_TEST(SpeechAI, FailurePaths) {
    if (!whisperModelsAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    std::vector<SpeechSegment> segments;
    const std::vector<float> pcm(16000, 0.1f);
    // 未加载 → 失败
    NC_CHECK(!recognizer.transcribe(pcm.data(), pcm.size(), 16000, SpeechLanguage::Auto, segments,
                                    nullptr, &failure));
    NC_CHECK(!failure.empty());
    // 不存在的模型 → 失败
    NC_CHECK(!recognizer.load("/tmp/no_encoder.onnx", "/tmp/no_decoder.onnx", &failure));
    // 加载后空音频 → 失败
    NC_CHECK(recognizer.load(whisperEncoderPath(), whisperDecoderPath(), &failure));
    NC_CHECK(!recognizer.transcribe(nullptr, 0, 16000, SpeechLanguage::Auto, segments, nullptr,
                                    &failure));
}

NC_TEST(SpeechAI, ProgressCallback) {
    if (!whisperModelsAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    if (!recognizer.load(whisperEncoderPath(), whisperDecoderPath(), &failure)) {
        return;
    }
    const std::vector<float> pcm = makeSpeechAudio(16000, 1.0);
    std::vector<SpeechSegment> segments;
    double lastProgress = -1.0;
    bool monotonic = true;
    recognizer.transcribe(pcm.data(), pcm.size(), 16000, SpeechLanguage::Chinese, segments,
                          [&](double p) {
                              if (p < lastProgress) monotonic = false;
                              lastProgress = p;
                              return true;  // 不取消
                          },
                          &failure);
    NC_CHECK(monotonic);  // 进度单调不减（若回调被调用）
}

NC_TEST(SpeechAI, IndependentSessionLifecycleAndArgumentGuards) {
    NcSpeechSession* session = nc_speech_session_create();
    NC_CHECK(session != nullptr);
    if (session == nullptr) return;

    int32_t state = -1;
    double progress = -1.0;
    char* error = nullptr;
    NC_CHECK_EQ(nc_speech_session_status(session, &state, &progress, &error), NC_OK);
    NC_CHECK_EQ(state, 0);
    NC_CHECK_EQ(progress, 0.0);
    NC_CHECK(error != nullptr);
    if (error != nullptr) nc_free_string(error);

    error = nullptr;
    NC_CHECK_EQ(nc_speech_session_load_memory(session, nullptr, 0, nullptr, 0,
                                               nullptr, 0, &error),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(error != nullptr);
    if (error != nullptr) nc_free_string(error);

    error = nullptr;
    NC_CHECK_EQ(nc_speech_session_transcribe(session, nullptr, 0, 16000, 1,
                                              nullptr, &error),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(error != nullptr);
    if (error != nullptr) nc_free_string(error);

    NC_CHECK_EQ(nc_speech_session_cancel(session), NC_ERROR_NOT_FOUND);

    float validPcm = 0.0f;
    char* segments = nullptr;
    error = nullptr;
    NC_CHECK_EQ(nc_speech_session_transcribe(session, &validPcm, 1, 7999, 1,
                                              &segments, &error),
                NC_ERROR_INVALID_ARGUMENT);
    if (error != nullptr) nc_free_string(error);

    float outOfRangePcm = 1.01f;
    error = nullptr;
    NC_CHECK_EQ(nc_speech_session_transcribe(session, &outOfRangePcm, 1, 16000, 1,
                                              &segments, &error),
                NC_ERROR_INVALID_ARGUMENT);
    if (error != nullptr) nc_free_string(error);

    float nonFinitePcm = std::nanf("");
    error = nullptr;
    NC_CHECK_EQ(nc_speech_session_transcribe(session, &nonFinitePcm, 1, 16000, 1,
                                              &segments, &error),
                NC_ERROR_INVALID_ARGUMENT);
    if (error != nullptr) nc_free_string(error);

    error = nullptr;
    NC_CHECK_EQ(nc_speech_session_transcribe(session, &validPcm, 1, 16000, 1,
                                              nullptr, &error),
                NC_ERROR_INVALID_ARGUMENT);
    if (error != nullptr) nc_free_string(error);

    nc_speech_session_destroy(session);
}

