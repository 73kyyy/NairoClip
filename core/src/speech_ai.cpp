#include "nairoclip/core/speech_ai.h"

#include "nairoclip/core/json.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <map>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kSampleRate = 16000;
constexpr int kMelBins = 80;
constexpr int kFFTSize = 400;   ///< Whisper: n_fft = 400（25ms @16kHz）
constexpr int kHopLength = 160; ///< Whisper: hop = 160（10ms）
constexpr int kNyquist = kSampleRate / 2;

/// Mel 刻度（Hz ↔ mel；Whisper 使用 HTK mel 公式）。
double hzToMel(double hz) {
    return 1127.0 * std::log(1.0 + hz / 700.0);
}
double melToHz(double mel) {
    return 700.0 * (std::exp(mel / 1127.0) - 1.0);
}

/// 构造 Mel 滤波器组（80 × (kFFTSize/2+1)）。
const std::vector<std::vector<double>>& melFilterbank() {
    static const std::vector<std::vector<double>> bank = [] {
        std::vector<std::vector<double>> filters(kMelBins);
        const int bins = kFFTSize / 2 + 1;
        const double melMin = hzToMel(0.0);
        const double melMax = hzToMel(static_cast<double>(kNyquist));
        std::vector<double> melPoints(kMelBins + 2);
        for (int i = 0; i < kMelBins + 2; ++i) {
            melPoints[static_cast<std::size_t>(i)] =
                melMin + (melMax - melMin) * i / (kMelBins + 1);
        }
        std::vector<double> hzPoints(kMelBins + 2);
        for (int i = 0; i < kMelBins + 2; ++i) {
            hzPoints[static_cast<std::size_t>(i)] = melToHz(melPoints[static_cast<std::size_t>(i)]);
        }
        std::vector<int> binPoints(kMelBins + 2);
        for (int i = 0; i < kMelBins + 2; ++i) {
            binPoints[static_cast<std::size_t>(i)] = static_cast<int>(std::floor(
                (kFFTSize + 1) * hzPoints[static_cast<std::size_t>(i)] / kSampleRate));
        }
        for (int m = 0; m < kMelBins; ++m) {
            auto& filter = filters[static_cast<std::size_t>(m)];
            filter.assign(static_cast<std::size_t>(bins), 0.0);
            const int left = binPoints[static_cast<std::size_t>(m)];
            const int center = binPoints[static_cast<std::size_t>(m + 1)];
            const int right = binPoints[static_cast<std::size_t>(m + 2)];
            for (int k = left; k < center && k < bins; ++k) {
                if (center > left) {
                    filter[static_cast<std::size_t>(k)] =
                        static_cast<double>(k - left) / (center - left);
                }
            }
            for (int k = center; k < right && k < bins; ++k) {
                if (right > center) {
                    filter[static_cast<std::size_t>(k)] =
                        static_cast<double>(right - k) / (right - center);
                }
            }
        }
        return filters;
    }();
    return bank;
}

/// 原地基-2 FFT（迭代 Cooley-Tukey；长度须为 2 的幂）。
void fft(std::vector<double>& re, std::vector<double>& im) {
    const int n = static_cast<int>(re.size());
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[static_cast<std::size_t>(i)], re[static_cast<std::size_t>(j)]);
            std::swap(im[static_cast<std::size_t>(i)], im[static_cast<std::size_t>(j)]);
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = -2.0 * kPi / len;
        const double wRe = std::cos(angle);
        const double wIm = std::sin(angle);
        for (int i = 0; i < n; i += len) {
            double curRe = 1.0;
            double curIm = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k;
                const int b = i + k + len / 2;
                const double evenRe = re[static_cast<std::size_t>(a)];
                const double evenIm = im[static_cast<std::size_t>(a)];
                const double oddRe = re[static_cast<std::size_t>(b)] * curRe -
                                     im[static_cast<std::size_t>(b)] * curIm;
                const double oddIm = re[static_cast<std::size_t>(b)] * curIm +
                                     im[static_cast<std::size_t>(b)] * curRe;
                re[static_cast<std::size_t>(a)] = evenRe + oddRe;
                im[static_cast<std::size_t>(a)] = evenIm + oddIm;
                re[static_cast<std::size_t>(b)] = evenRe - oddRe;
                im[static_cast<std::size_t>(b)] = evenIm - oddIm;
                const double nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }
}

/// 线性重采样（源 → 16kHz 单声道）。
std::vector<float> resampleTo16k(const float* samples, std::size_t frameCount,
                                 int sampleRate) {
    // B0-6 边界审计：本函数原先只有调用方 `computeMelSpectrogram` 在守卫
    // （它检查了 frameCount == 0），自身无防护。而 frameCount 为 0 时
    // `frameCount - 1` 会下溢成 SIZE_MAX，`std::min` 取到它 → samples[i1]
    // 越界读。作为「可能被新调用者复用的内部工具」，在这里补自身守卫。
    if (samples == nullptr || frameCount == 0 || sampleRate <= 0) {
        return {};
    }
    if (sampleRate == kSampleRate) {
        return std::vector<float>(samples, samples + frameCount);
    }
    const double ratio = static_cast<double>(sampleRate) / kSampleRate;
    const std::size_t outCount =
        static_cast<std::size_t>(static_cast<double>(frameCount) / ratio);
    std::vector<float> out(outCount, 0.0f);
    for (std::size_t i = 0; i < outCount; ++i) {
        const double src = static_cast<double>(i) * ratio;
        const std::size_t i0 = static_cast<std::size_t>(src);
        const std::size_t i1 = std::min(frameCount - 1, i0 + 1);
        const double frac = src - static_cast<double>(i0);
        const float a = i0 < frameCount ? samples[i0] : 0.0f;
        const float b = samples[i1];
        out[i] = static_cast<float>(a * (1.0 - frac) + b * frac);
    }
    return out;
}

/// Whisper 起始 token 序列（简化）：
///   <|startoftranscript|> = 50258
///   中文 = 50260 / 英语 = 50259 / 日语 = 50266 / 韩语 = 50264
///   <|transcribe|> = 50359 / <|notimestamps|> = 50363
constexpr int kStartOfTranscript = 50258;
constexpr int kEndOfText = 50257;

/// 少量多语言 token → 文本（tiny 多语言模型的词表极大，无法内嵌全表；
/// 本实现把 token id 转为「占位标记 + id」，由平台层可选的 BPE 词表
/// 做最终解码。这样不伪造识别结果：无词表时输出明确标注）。
std::string tokenToTextPlaceholder(int tokenId) {
    if (tokenId == kEndOfText) {
        return std::string();
    }
    // 明确标注：真实部署时应由平台层提供 tokenizer.json 做 BPE 解码。
    return "[" + std::to_string(tokenId) + "]";
}

}  // namespace

const char* speechLanguageName(SpeechLanguage language) {
    switch (language) {
        case SpeechLanguage::Auto: return "auto";
        case SpeechLanguage::Chinese: return "zh";
        case SpeechLanguage::English: return "en";
        case SpeechLanguage::Japanese: return "ja";
        case SpeechLanguage::Korean: return "ko";
    }
    return "auto";
}

bool speechLanguageFromInt(int value, SpeechLanguage& out) {
    if (value < 0 || value > 4) {
        return false;
    }
    out = static_cast<SpeechLanguage>(value);
    return true;
}

int speechLanguageToken(SpeechLanguage language) {
    switch (language) {
        case SpeechLanguage::Chinese: return 50260;
        case SpeechLanguage::English: return 50259;
        case SpeechLanguage::Japanese: return 50266;
        case SpeechLanguage::Korean: return 50264;
        case SpeechLanguage::Auto: return -1;  // 自动检测（本实现按中文提示）
    }
    return -1;
}

// =============================================================================
// Mel 频谱（Whisper 标准；静态方法供测试）
// =============================================================================

bool SpeechRecognizer::computeMelSpectrogram(const float* samples, std::size_t frameCount,
                                             int sampleRate, int frames,
                                             std::vector<float>& outMel) {
    if (samples == nullptr || frameCount == 0 || sampleRate <= 0 || frames <= 0) {
        return false;
    }
    const std::vector<float> pcm16k = resampleTo16k(samples, frameCount, sampleRate);
    outMel.assign(static_cast<std::size_t>(kMelBins) * static_cast<std::size_t>(frames), 0.0f);
    const auto& filterbank = melFilterbank();
    // Hann 窗（400 点）
    std::vector<double> window(kFFTSize);
    for (int i = 0; i < kFFTSize; ++i) {
        window[static_cast<std::size_t>(i)] =
            0.5 * (1.0 - std::cos(2.0 * kPi * i / kFFTSize));
    }
    std::vector<double> re(kFFTSize);
    std::vector<double> im(kFFTSize);
    // Whisper 使用 512 点 FFT（400 补零到 512）
    constexpr int kFFT = 512;
    re.resize(kFFT);
    im.resize(kFFT);
    for (int frame = 0; frame < frames; ++frame) {
        const std::size_t start = static_cast<std::size_t>(frame) * kHopLength;
        // 取窗（越界补零）
        std::fill(re.begin(), re.end(), 0.0);
        std::fill(im.begin(), im.end(), 0.0);
        for (int i = 0; i < kFFTSize; ++i) {
            const std::size_t idx = start + static_cast<std::size_t>(i);
            const double sample =
                idx < pcm16k.size() ? pcm16k[idx] : 0.0;
            re[static_cast<std::size_t>(i)] = sample * window[static_cast<std::size_t>(i)];
        }
        fft(re, im);
        // 功率谱 → Mel
        const int bins = kFFT / 2 + 1;
        std::vector<double> power(static_cast<std::size_t>(bins), 0.0);
        for (int k = 0; k < bins; ++k) {
            power[static_cast<std::size_t>(k)] =
                re[static_cast<std::size_t>(k)] * re[static_cast<std::size_t>(k)] +
                im[static_cast<std::size_t>(k)] * im[static_cast<std::size_t>(k)];
        }
        for (int m = 0; m < kMelBins; ++m) {
            double sum = 0.0;
            const auto& filter = filterbank[static_cast<std::size_t>(m)];
            for (int k = 0; k < bins && k < static_cast<int>(filter.size()); ++k) {
                sum += filter[static_cast<std::size_t>(k)] *
                       power[static_cast<std::size_t>(k)];
            }
            // 对数压缩（Whisper 标准化：log10 + 范围钳制）
            const double logSpec = std::log10(std::max(1e-10, sum));
            outMel[static_cast<std::size_t>(m) * static_cast<std::size_t>(frames) +
                   static_cast<std::size_t>(frame)] = static_cast<float>(logSpec);
        }
    }
    // Whisper 标准化：(x - max) → clamp(-8, +8) → (x + 4) / 4
    float mx = outMel[0];
    for (float v : outMel) {
        mx = std::max(mx, v);
    }
    for (auto& v : outMel) {
        v = std::max(v - mx, -8.0f);
        v = (v + 4.0f) / 4.0f;
    }
    return true;
}

// =============================================================================
// 识别器
// =============================================================================

struct SpeechRecognizer::Impl {
    AiSession encoder;
    AiSession decoder;
    /// BPE 词表（token 字符串 → id；来自 tokenizer.json 的 model.vocab）。
    std::map<std::string, int> vocab;
    /// 反查表（id → token 字符串）。
    std::map<int, std::string> idToToken;
    /// added_tokens（特殊 token，含 <|startoftranscript|> 等）。
    std::map<int, std::string> addedTokens;
    /// 是否已加载词表。
    bool tokenizerReady = false;
};

SpeechRecognizer::SpeechRecognizer() : impl_(new (std::nothrow) Impl()) {}
SpeechRecognizer::~SpeechRecognizer() = default;

bool SpeechRecognizer::load(const std::string& encoderPath, const std::string& decoderPath,
                            std::string* outputFailure) {
    loaded_ = false;
    if (impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "内存分配失败";
        return false;
    }
    if (!aiEngineAvailable()) {
        if (outputFailure != nullptr) {
            *outputFailure = "AI 推理引擎不可用（未链接 ONNX Runtime）";
        }
        return false;
    }
    if (!impl_->encoder.load(encoderPath, AiBackend::Cpu, outputFailure)) {
        return false;
    }
    if (!impl_->decoder.load(decoderPath, AiBackend::Cpu, outputFailure)) {
        impl_->encoder.unload();
        return false;
    }
    loaded_ = true;
    return true;
}

void SpeechRecognizer::unload() {
    if (impl_ != nullptr) {
        impl_->encoder.unload();
        impl_->decoder.unload();
    }
    loaded_ = false;
}

bool SpeechRecognizer::loadTokenizer(const std::string& tokenizerJsonPath,
                                      std::string* outputFailure) {
    if (impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "内存分配失败";
        return false;
    }
    std::ifstream file(tokenizerJsonPath, std::ios::binary);
    if (!file.good()) {
        if (outputFailure != nullptr) *outputFailure = "词表文件不存在：" + tokenizerJsonPath;
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());
    JsonValue root;
    JsonParseError parseError;
    if (!parseJson(text, root, parseError) || !root.isObject()) {
        if (outputFailure != nullptr) {
            *outputFailure = "词表 JSON 解析失败：" + parseError.message;
        }
        return false;
    }
    impl_->vocab.clear();
    impl_->idToToken.clear();
    impl_->addedTokens.clear();
    // model.vocab：{token: id} 对象
    if (root.has("model")) {
        const JsonValue& model = root.get("model");
        if (model.has("vocab") && model.get("vocab").isObject()) {
            const JsonValue& vocab = model.get("vocab");
            for (const auto& member : vocab.members()) {
                const int id = static_cast<int>(member.second.asInteger(-1));
                if (id >= 0) {
                    impl_->vocab[member.first] = id;
                    impl_->idToToken[id] = member.first;
                }
            }
        }
    }
    // added_tokens：[{id, content}, ...]
    if (root.has("added_tokens") && root.get("added_tokens").isArray()) {
        const JsonValue& added = root.get("added_tokens");
        for (std::size_t i = 0; i < added.size(); ++i) {
            const JsonValue& entry = added.at(i);
            if (!entry.isObject()) continue;
            const int id = static_cast<int>(entry.get("id").asInteger(-1));
            const std::string content = entry.get("content").asString();
            if (id >= 0 && !content.empty()) {
                impl_->addedTokens[id] = content;
                if (impl_->idToToken.find(id) == impl_->idToToken.end()) {
                    impl_->idToToken[id] = content;
                }
            }
        }
    }
    impl_->tokenizerReady = !impl_->idToToken.empty();
    if (!impl_->tokenizerReady) {
        if (outputFailure != nullptr) *outputFailure = "词表为空（model.vocab 缺失）";
        return false;
    }
    return true;
}

bool SpeechRecognizer::tokenizerLoaded() const {
    return impl_ != nullptr && impl_->tokenizerReady;
}

// ---------------------------------------------------------------------------
// ByteLevel BPE 解码（GPT-2 / Whisper 标准）
// ---------------------------------------------------------------------------
// 说明：Whisper 的 tokenizer.json 使用 {"decoder": {"type": "ByteLevel"}}——
//   token 字符串是 **byte-to-unicode 映射后**的文本（例如空格写成 'Ġ' = U+0120，
//   0x20 → 256 的映射表）。正确解码分两步：
//     1) 把 token 字符串按「byte-to-unicode」逆映射回原始字节序列；
//     2) 把字节序列按 UTF-8 解释为文本。
//   此前实现只把 'Ġ' 直接替换成空格（对纯 ASCII 勉强可用），
//   对中文/日文等多字节 UTF-8 会产出乱码（实测合成音频输出乱码）。
namespace {

/// GPT-2 byte-to-unicode 映射的逆表（unicode 码点 → 字节值）。
/// 构建规则（与 transformers 一致）：
///   · 可见字符（'!' 到 '~'、'¡' 到 '¬'、'®' 到 'ÿ'）映射到自身；
///   · 其余 256 - 188 = 68 个字节依次映射到 256 起的码点。
struct ByteLevelDecoder {
    std::map<std::uint32_t, std::uint8_t> unicodeToByte;

    ByteLevelDecoder() {
        // 可见字节集合（GPT-2 定义）
        const auto isVisible = [](int b) {
            return (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
        };
        int nextCodepoint = 256;
        for (int b = 0; b < 256; ++b) {
            if (isVisible(b)) {
                unicodeToByte[static_cast<std::uint32_t>(b)] =
                    static_cast<std::uint8_t>(b);
            } else {
                unicodeToByte[static_cast<std::uint32_t>(nextCodepoint)] =
                    static_cast<std::uint8_t>(b);
                ++nextCodepoint;
            }
        }
    }
};

const ByteLevelDecoder& byteLevelDecoder() {
    static const ByteLevelDecoder decoder;
    return decoder;
}

/// UTF-8 码点解码（取一个码点与长度；非法返回 0 长度）。
std::size_t decodeUtf8(const std::string& text, std::size_t pos, std::uint32_t& codepoint) {
    if (pos >= text.size()) {
        return 0;
    }
    const unsigned char c0 = static_cast<unsigned char>(text[pos]);
    if (c0 < 0x80) {
        codepoint = c0;
        return 1;
    }
    int extra = 0;
    std::uint32_t value = 0;
    if ((c0 & 0xE0) == 0xC0) {
        extra = 1;
        value = c0 & 0x1F;
    } else if ((c0 & 0xF0) == 0xE0) {
        extra = 2;
        value = c0 & 0x0F;
    } else if ((c0 & 0xF8) == 0xF0) {
        extra = 3;
        value = c0 & 0x07;
    } else {
        return 0;  // 非法起始字节
    }
    if (pos + static_cast<std::size_t>(extra) >= text.size()) {
        return 0;
    }
    for (int i = 1; i <= extra; ++i) {
        const unsigned char ci = static_cast<unsigned char>(text[pos + static_cast<std::size_t>(i)]);
        if ((ci & 0xC0) != 0x80) {
            return 0;
        }
        value = (value << 6) | (ci & 0x3F);
    }
    codepoint = value;
    return static_cast<std::size_t>(extra + 1);
}

}  // namespace

std::string SpeechRecognizer::decodeTokens(const std::vector<int>& tokens) const {
    if (impl_ == nullptr || !impl_->tokenizerReady) {
        return std::string();
    }
    // ① 拼接 token 字符串（跳过特殊 token）
    std::string merged;
    for (int token : tokens) {
        if (impl_->addedTokens.find(token) != impl_->addedTokens.end()) {
            continue;  // 特殊 token（<|...|>）不参与文本
        }
        const auto it = impl_->idToToken.find(token);
        if (it == impl_->idToToken.end()) {
            continue;
        }
        merged += it->second;
    }
    // ② byte-to-unicode 逆映射 → 原始字节
    std::string bytes;
    bytes.reserve(merged.size());
    const auto& inverse = byteLevelDecoder().unicodeToByte;
    std::size_t pos = 0;
    while (pos < merged.size()) {
        std::uint32_t codepoint = 0;
        const std::size_t length = decodeUtf8(merged, pos, codepoint);
        if (length == 0) {
            ++pos;  // 跳过非法字节（防御）
            continue;
        }
        const auto found = inverse.find(codepoint);
        if (found != inverse.end()) {
            bytes.push_back(static_cast<char>(found->second));
        } else {
            // 不在映射表：按原样保留（已是 UTF-8 文本）
            bytes.append(merged, pos, length);
        }
        pos += length;
    }
    // ③ 首尾空白清理（Whisper 输出常有前导空格）
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t begin = 0;
    std::size_t end = bytes.size();
    while (begin < end && isSpace(bytes[begin])) ++begin;
    while (end > begin && isSpace(bytes[end - 1])) --end;
    return bytes.substr(begin, end - begin);
}

bool SpeechRecognizer::transcribe(const float* samples, std::size_t frameCount, int sampleRate,
                                  SpeechLanguage language, std::vector<SpeechSegment>& outSegments,
                                  const std::function<bool(double)>& progress,
                                  std::string* outputFailure) {
    outSegments.clear();
    if (!loaded_ || impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "语音识别模型未加载";
        return false;
    }
    if (samples == nullptr || frameCount == 0 || sampleRate <= 0) {
        if (outputFailure != nullptr) *outputFailure = "音频数据为空或参数非法";
        return false;
    }
    // ① Mel 频谱（30 秒窗口；长音频分段处理）
    const double secondsTotal = static_cast<double>(frameCount) / sampleRate;
    const double windowSeconds = 30.0;
    const int windowCount = std::max(1, static_cast<int>(std::ceil(secondsTotal / windowSeconds)));
    for (int w = 0; w < windowCount; ++w) {
        const std::size_t startFrame =
            static_cast<std::size_t>(static_cast<double>(w) * windowSeconds * sampleRate);
        if (startFrame >= frameCount) {
            break;
        }
        const std::size_t count = std::min(
            frameCount - startFrame,
            static_cast<std::size_t>(windowSeconds * sampleRate));
        std::vector<float> mel;
        if (!computeMelSpectrogram(samples + startFrame, count, sampleRate, melFrames(), mel)) {
            if (outputFailure != nullptr) *outputFailure = "Mel 频谱计算失败";
            return false;
        }
        // ② 编码
        AiTensor input;
        input.shape = {1, kMelBins, melFrames()};
        input.dataFloat = std::move(mel);
        std::vector<AiTensor> encOuts;
        if (!impl_->encoder.run({input}, encOuts, outputFailure)) {
            return false;
        }
        if (encOuts.empty()) {
            if (outputFailure != nullptr) *outputFailure = "编码输出为空";
            return false;
        }
        // 进度分段（单调）：编码 0.0→0.30，解码 0.30→0.95，收尾 1.0
        if (progress != nullptr &&
            !progress(0.30 * static_cast<double>(w + 1) / windowCount)) {
            if (outputFailure != nullptr) *outputFailure = "用户取消";
            return false;
        }
        // ③ 解码（真实自回归：贪心 + KV cache 复用）
        // 说明：Whisper 解码器为 19 输入 / 17 输出结构——
        //   input_ids(Int64) + encoder_hidden_states + 16 个 past_key_values
        //   + use_cache_branch(Bool)；
        //   输出 logits + 16 个 present_key_values（下轮作为 past 传回）。
        std::vector<std::int64_t> tokenIds;
        tokenIds.push_back(kStartOfTranscript);
        const int langToken = speechLanguageToken(language);
        if (langToken > 0) {
            tokenIds.push_back(langToken);
        }
        tokenIds.push_back(50359);  // <|transcribe|>
        tokenIds.push_back(50363);  // <|notimestamps|>
        // KV cache：首步为 [1,6,0,64] 空（层数 4 × 4 类 = 16 个）
        const int kLayerCount = 4;
        const int kHeadCount = 6;
        const int kHeadDim = 64;
        std::vector<AiTensor> past;  // 16 个
        past.reserve(16);
        for (int layer = 0; layer < kLayerCount; ++layer) {
            for (int k = 0; k < 4; ++k) {
                AiTensor kv;
                kv.shape = {1, kHeadCount, 0, kHeadDim};
                kv.dataFloat.clear();
                past.push_back(std::move(kv));
            }
        }
        bool firstStep = true;
        std::vector<std::int64_t> generated;  // 生成的 token（不含提示）
        constexpr int kMaxNewTokens = 128;    // 单窗口上限（防失控）
        constexpr int kEotToken = 50257;
        for (int step = 0; step < kMaxNewTokens; ++step) {
            std::vector<AiTensor> inputs;
            AiTensor ids;
            ids.type = AiTensorType::Int64;
            // 首步传入全部提示 token；后续仅传新 token（KV cache 已含历史）
            if (firstStep) {
                ids.shape = {1, static_cast<std::int64_t>(tokenIds.size())};
                ids.dataInt64 = tokenIds;
            } else {
                ids.shape = {1, 1};
                ids.dataInt64 = {tokenIds.back()};
            }
            inputs.push_back(std::move(ids));
            inputs.push_back(encOuts[0]);
            for (const auto& kv : past) {
                inputs.push_back(kv);
            }
            AiTensor branch;
            branch.type = AiTensorType::Bool;
            branch.shape = {1};
            branch.dataBytes = {firstStep ? static_cast<std::uint8_t>(0)
                                          : static_cast<std::uint8_t>(1)};
            inputs.push_back(std::move(branch));
            std::vector<AiTensor> decOuts;
            if (!impl_->decoder.run(inputs, decOuts, outputFailure)) {
                return false;
            }
            if (decOuts.empty()) {
                if (outputFailure != nullptr) *outputFailure = "解码输出为空";
                return false;
            }
            // 取最后位置的 argmax
            const AiTensor& logits = decOuts[0];
            if (logits.shape.size() != 3) {
                if (outputFailure != nullptr) *outputFailure = "logits 形状非法";
                return false;
            }
            const std::int64_t seqLen = logits.shape[1];
            const std::int64_t vocab = logits.shape[2];
            const std::int64_t last = seqLen - 1;
            int best = 0;
            float bestVal = -1e30f;
            for (std::int64_t v = 0; v < vocab; ++v) {
                const float val = logits.dataFloat[static_cast<std::size_t>(last * vocab + v)];
                if (val > bestVal) {
                    bestVal = val;
                    best = static_cast<int>(v);
                }
            }
            if (best == kEotToken) {
                break;  // 正常结束
            }
            generated.push_back(static_cast<std::int64_t>(best));
            tokenIds.push_back(static_cast<std::int64_t>(best));
            // 更新 KV cache（present.* → past）
            if (decOuts.size() >= 17) {
                for (int i = 0; i < 16; ++i) {
                    past[static_cast<std::size_t>(i)] = decOuts[static_cast<std::size_t>(i + 1)];
                }
            }
            firstStep = false;
            if (progress != nullptr) {
                // 解码进度（单调）：窗口起点 + 步数占比 × 窗口份额
                const double windowShare = 0.65 / static_cast<double>(windowCount);
                const double base = 0.30 + windowShare * static_cast<double>(w);
                const double stepPart =
                    windowShare * static_cast<double>(step + 1) / kMaxNewTokens;
                const double p = base + stepPart;
                if (!progress(std::min(0.95, p))) {
                    if (outputFailure != nullptr) *outputFailure = "用户取消";
                    return false;
                }
            }
        }
        // 文本解码：优先用 BPE 词表；未加载则明确标注（不伪造文本）
        std::string text;
        if (impl_->tokenizerReady) {
            text = decodeTokens(std::vector<int>(generated.begin(), generated.end()));
        } else {
            // 无词表：输出 token 标记（明确告知调用方需提供 tokenizer.json）
            for (std::int64_t token : generated) {
                text += tokenToTextPlaceholder(static_cast<int>(token));
            }
        }
        SpeechSegment segment;
        segment.startSeconds = static_cast<double>(startFrame) / sampleRate;
        segment.endSeconds = segment.startSeconds + static_cast<double>(count) / sampleRate;
        segment.text = text;
        outSegments.push_back(std::move(segment));
        if (progress != nullptr && !progress(1.0)) {
            if (outputFailure != nullptr) *outputFailure = "用户取消";
            return false;
        }
    }
    return true;
}

const char* speechAiPurpose() {
    return "语音转字幕 AI：Whisper-tiny ONNX 真实推理（Mel 频谱 + 编码器 + 解码链路；"
           "文档 Part 4-A 3.2；BPE 词表解码由平台层提供）";
}

}  // namespace core
}  // namespace nairoclip
