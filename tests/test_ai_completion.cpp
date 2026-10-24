// =============================================================================
// AI 未完成接口补齐验证（第14批）
// =============================================================================
// 1. AiTensorType::Int64 / Bool（Whisper 自回归前提，此前缺失）
// 2. 0 长度维度合法性（Whisper 首步 KV cache 为 [1,6,0,64]）
// 3. Whisper 完整自回归解码（19 输入 / 17 输出 / KV cache 复用）
// 4. ByteLevel BPE 解码（Whisper/GPT-2 标准；此前输出乱码）
// 5. 镜头追踪缩放 + 渲染路径真实消费
#include "test_framework.h"
#include "nairoclip/core/ai_engine.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/speech_ai.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

bool whisperAvailable() {
    if (!aiEngineAvailable()) return false;
    FILE* f = std::fopen("/workspace/deps/whisper_tiny_decoder.onnx", "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

bool tokenizerAvailable() {
    FILE* f = std::fopen("/workspace/deps/whisper_tiny_tokenizer.json", "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

bool isValidUtf8(const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t len = 1;
        if (c < 0x80) len = 1;
        else if ((c & 0xE0) == 0xC0) len = 2;
        else if ((c & 0xF0) == 0xE0) len = 3;
        else if ((c & 0xF8) == 0xF0) len = 4;
        else return false;
        if (i + len > text.size()) return false;
        for (std::size_t k = 1; k < len; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) return false;
        }
        i += len;
    }
    return true;
}

}  // namespace

NC_TEST(AiCompletion, Int64TensorType) {
    AiTensor tensor;
    tensor.type = AiTensorType::Int64;
    tensor.shape = {1, 4};
    tensor.dataInt64 = {50258, 50259, 50359, 50363};
    NC_CHECK(tensor.valid());
    NC_CHECK_EQ(tensor.elementCount(), std::size_t(4));
    NC_CHECK_EQ(aiTensorByteSize(tensor), std::size_t(4 * 8));
    tensor.dataInt64 = {1, 2};
    NC_CHECK(!tensor.valid());
}

NC_TEST(AiCompletion, BoolTensorType) {
    AiTensor tensor;
    tensor.type = AiTensorType::Bool;
    tensor.shape = {1};
    tensor.dataBytes = {1};
    NC_CHECK(tensor.valid());
    NC_CHECK_EQ(aiTensorByteSize(tensor), std::size_t(1));
}

NC_TEST(AiCompletion, ZeroLengthDimIsValid) {
    AiTensor tensor;
    tensor.shape = {1, 6, 0, 64};
    NC_CHECK(tensor.valid());
    NC_CHECK_EQ(tensor.elementCount(), std::size_t(0));
    AiTensor bad;
    bad.shape = {1, -1, 64};
    bad.dataFloat.assign(64, 0.0f);
    NC_CHECK(!bad.valid());
    AiTensor empty;
    NC_CHECK(!empty.valid());
}

NC_TEST(AiCompletion, WhisperAutoregressiveDecode) {
    if (!whisperAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    NC_CHECK(recognizer.load("/workspace/deps/whisper_tiny_encoder.onnx",
                             "/workspace/deps/whisper_tiny_decoder.onnx", &failure));
    if (tokenizerAvailable()) {
        NC_CHECK(recognizer.loadTokenizer("/workspace/deps/whisper_tiny_tokenizer.json",
                                          &failure));
    }
    const int sampleRate = 16000;
    std::vector<float> pcm(static_cast<std::size_t>(sampleRate), 0.0f);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        pcm[i] = static_cast<float>(0.3 * std::sin(2.0 * 3.14159265 * 220.0 * t));
    }
    std::vector<SpeechSegment> segments;
    double lastProgress = -1.0;
    bool monotonic = true;
    const bool ok = recognizer.transcribe(
        pcm.data(), pcm.size(), sampleRate, SpeechLanguage::Chinese, segments,
        [&](double p) {
            if (p < lastProgress - 1e-9) monotonic = false;
            lastProgress = p;
            return true;
        },
        &failure);
    NC_CHECK(ok);
    NC_CHECK(!segments.empty());
    NC_CHECK(monotonic);
    NC_CHECK(segments[0].startSeconds >= 0.0);
    NC_CHECK(segments[0].endSeconds > 0.5);
}

NC_TEST(AiCompletion, WhisperDecodeProducesValidText) {
    if (!whisperAvailable() || !tokenizerAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    NC_CHECK(recognizer.load("/workspace/deps/whisper_tiny_encoder.onnx",
                             "/workspace/deps/whisper_tiny_decoder.onnx", &failure));
    NC_CHECK(recognizer.loadTokenizer("/workspace/deps/whisper_tiny_tokenizer.json", &failure));
    const int sampleRate = 16000;
    std::vector<float> pcm(static_cast<std::size_t>(sampleRate / 2), 0.0f);
    for (std::size_t i = 0; i < pcm.size(); ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        pcm[i] = static_cast<float>(0.25 * std::sin(2.0 * 3.14159265 * 300.0 * t));
    }
    std::vector<SpeechSegment> segments;
    NC_CHECK(recognizer.transcribe(pcm.data(), pcm.size(), sampleRate,
                                   SpeechLanguage::Chinese, segments, nullptr, &failure));
    NC_CHECK(!segments.empty());
    NC_CHECK(!segments[0].text.empty());
    NC_CHECK(isValidUtf8(segments[0].text));
}

NC_TEST(AiCompletion, ByteLevelDecodeCorrectness) {
    if (!tokenizerAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    NC_CHECK(recognizer.loadTokenizer("/workspace/deps/whisper_tiny_tokenizer.json", &failure));
    // **精确内容对照**（与 tokenizer.json 的 vocab 一致；仅做 UTF-8 合法性
    // 检查不足以捕获「未做 byte-to-unicode 逆映射」的缺陷——已由变异测试确认）。
    // token 15947 = "Hello"
    NC_CHECK_EQ(recognizer.decodeTokens({15947}), std::string("Hello"));
    // token 15496 = "Ne"
    NC_CHECK_EQ(recognizer.decodeTokens({15496}), std::string("Ne"));
    // 组合：15947("Hello") + 11(",")
    NC_CHECK_EQ(recognizer.decodeTokens({15947, 11}), std::string("Hello,"));
    // 不应出现 byte-to-unicode 残留（'Ġ' = U+0120 = 0xC4 0xA0）
    NC_CHECK(recognizer.decodeTokens({15947}).find("\xC4\xA0") == std::string::npos);
    // **多字节 UTF-8**（关键）：这是 ByteLevel 逆映射的必要性所在——
    // 纯 ASCII token 即使不做映射也恰好正确，只有多字节 token 才能暴露缺陷。
    // token 1654 = "我"，2131 = "好"（Whisper BPE 表；raw 形式为 "æĪĳ"）
    NC_CHECK_EQ(recognizer.decodeTokens({1654}), std::string("我"));
    NC_CHECK_EQ(recognizer.decodeTokens({2131}), std::string("好"));
    NC_CHECK_EQ(recognizer.decodeTokens({1654, 2131}), std::string("我好"));
    // 中文组合必须仍是有效 UTF-8
    NC_CHECK(isValidUtf8(recognizer.decodeTokens({1654, 2131})));
    // 特殊 token 过滤
    const std::string decoded = recognizer.decodeTokens({15947, 11, 220, 50257});
    NC_CHECK(decoded.find("<|") == std::string::npos);
    NC_CHECK(isValidUtf8(decoded));
}

NC_TEST(AiCompletion, DecodeFiltersSpecialTokens) {
    if (!tokenizerAvailable()) return;
    SpeechRecognizer recognizer;
    std::string failure;
    NC_CHECK(recognizer.loadTokenizer("/workspace/deps/whisper_tiny_tokenizer.json", &failure));
    NC_CHECK_EQ(recognizer.decodeTokens({50258, 50259, 50359, 50363}), std::string());
    const std::string text = recognizer.decodeTokens({50258, 15496, 50257});
    NC_CHECK(!text.empty());
    NC_CHECK(text.find("50258") == std::string::npos);
}

NC_TEST(AiCompletion, TrackingScaleRoundtrip) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t subtitle = nc_upsert_subtitle(editor, 0, 5000000, "跟随文字");
    NC_CHECK(subtitle != 0);
    const double times[3] = {0.0, 1000000.0, 2000000.0};
    const double xs[3] = {0.3, 0.5, 0.7};
    const double ys[3] = {0.4, 0.5, 0.6};
    const double scales[3] = {1.0, 1.5, 2.0};
    NC_CHECK_EQ(nc_set_camera_tracking_scaled(editor, subtitle, times, xs, ys, scales, 3, 1),
                NC_OK);
    double scale = -1.0;
    NC_CHECK_EQ(nc_tracking_point_get_scaled(editor, subtitle, 1, nullptr, nullptr, nullptr,
                                             &scale),
                NC_OK);
    NC_CHECK(std::abs(scale - 1.5) < 1e-9);
    const double wild[1] = {100.0};
    const double t1[1] = {0.0};
    const double c1[1] = {0.5};
    NC_CHECK_EQ(nc_set_camera_tracking_scaled(editor, subtitle, t1, c1, c1, wild, 1, 1), NC_OK);
    NC_CHECK_EQ(nc_tracking_point_get_scaled(editor, subtitle, 0, nullptr, nullptr, nullptr,
                                             &scale),
                NC_OK);
    NC_CHECK(std::abs(scale - 10.0) < 1e-9);
    NC_CHECK_EQ(nc_set_camera_tracking(editor, subtitle, times, xs, ys, 3, 0), NC_OK);
    NC_CHECK_EQ(nc_tracking_point_get_scaled(editor, subtitle, 0, nullptr, nullptr, nullptr,
                                             &scale),
                NC_OK);
    NC_CHECK(std::abs(scale - 1.0) < 1e-9);
    nc_editor_destroy(editor);
}

NC_TEST(AiCompletion, TrackingConsumedByRenderer) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    SubtitleCue cue;
    cue.id = SubtitleId{project.ids.next()};
    cue.start = Rational::make(0);
    cue.duration = Rational::make(5);
    cue.text = "跟随";
    NC_CHECK(editor.upsertSubtitle(seqId, cue).isOk());
    Sequence* sequence = project.mainSequence();
    NC_CHECK(sequence != nullptr);
    SubtitleCue* stored = sequence->subtitles.findCue(cue.id);
    NC_CHECK(stored != nullptr);
    stored->tracking.clear();
    SubtitleCue::TrackingPoint p0;
    p0.time = Rational::make(0);
    p0.centerX = 0.2;
    p0.centerY = 0.3;
    p0.scale = 1.0;
    stored->tracking.push_back(p0);
    SubtitleCue::TrackingPoint p1;
    p1.time = Rational::make(2);
    p1.centerX = 0.8;
    p1.centerY = 0.7;
    p1.scale = 2.0;
    stored->tracking.push_back(p1);
    stored->trackingMode = 1;
    const std::vector<SubtitleRenderItem> atStart =
        subtitlesForTest(project, seqId, Rational::make(0));
    NC_CHECK_EQ(atStart.size(), std::size_t(1));
    NC_CHECK(std::abs(atStart[0].anchorX - 0.2) < 1e-6);
    NC_CHECK(std::abs(atStart[0].anchorY - 0.3) < 1e-6);
    NC_CHECK(std::abs(atStart[0].scale - 1.0) < 1e-6);
    const std::vector<SubtitleRenderItem> atMid =
        subtitlesForTest(project, seqId, Rational::make(1));
    NC_CHECK_EQ(atMid.size(), std::size_t(1));
    NC_CHECK(std::abs(atMid[0].anchorX - 0.5) < 1e-6);
    NC_CHECK(std::abs(atMid[0].anchorY - 0.5) < 1e-6);
    NC_CHECK(std::abs(atMid[0].scale - 1.5) < 1e-6);
    const std::vector<SubtitleRenderItem> atEnd =
        subtitlesForTest(project, seqId, Rational::make(2));
    NC_CHECK_EQ(atEnd.size(), std::size_t(1));
    NC_CHECK(std::abs(atEnd[0].anchorX - 0.8) < 1e-6);
    NC_CHECK(std::abs(atEnd[0].scale - 2.0) < 1e-6);
}

NC_TEST(AiCompletion, TrackingModeZeroIgnoresScale) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    SubtitleCue cue;
    cue.id = SubtitleId{project.ids.next()};
    cue.start = Rational::make(0);
    cue.duration = Rational::make(5);
    cue.text = "仅位置";
    NC_CHECK(editor.upsertSubtitle(seqId, cue).isOk());
    SubtitleCue* stored = project.mainSequence()->subtitles.findCue(cue.id);
    NC_CHECK(stored != nullptr);
    stored->tracking.clear();
    SubtitleCue::TrackingPoint p;
    p.time = Rational::make(0);
    p.centerX = 0.25;
    p.centerY = 0.75;
    p.scale = 3.0;
    stored->tracking.push_back(p);
    stored->trackingMode = 0;
    const std::vector<SubtitleRenderItem> items =
        subtitlesForTest(project, seqId, Rational::make(1));
    NC_CHECK_EQ(items.size(), std::size_t(1));
    NC_CHECK(std::abs(items[0].anchorX - 0.25) < 1e-6);
    NC_CHECK(std::abs(items[0].scale - 1.0) < 1e-6);
}
