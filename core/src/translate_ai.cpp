#include "nairoclip/core/translate_ai.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

#include "nairoclip/core/json.h"

namespace nairoclip {
namespace core {

namespace {

/// SentencePiece 的元空格标记（▁，U+2581）。
const char* kSpaceMark = "\xE2\x96\x81";
/// 未知 token（tokenizer.json 的 unk_id；本词表为 2）。
constexpr std::int64_t kUnkId = 2;
/// 词表上限（防止畸形文件吃满内存）。
constexpr std::size_t kMaxVocab = 300000;

/// UTF-8 逐码点切分（返回每个码点的字节偏移区间）。
std::vector<std::pair<std::size_t, std::size_t>> utf8Codepoints(const std::string& s) {
    std::vector<std::pair<std::size_t, std::size_t>> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        if ((c & 0x80) == 0) {
            len = 1;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
        }
        if (i + len > s.size()) len = s.size() - i;
        out.emplace_back(i, len);
        i += len;
    }
    return out;
}

}  // namespace

// =============================================================================
// 实现
// =============================================================================

struct Translator::Impl {
    AiSession encoder;
    /// 首步解码（无 past）。
    AiSession decoder;
    /// 后续步解码（带 past）；分体导出的关键，避免 merged 版的 encoder KV 收缩。
    AiSession decoderWithPast;

    /// piece → 分数（Unigram 对数概率）。
    std::unordered_map<std::string, double> pieceScore;
    /// piece → id（编码用）。
    std::unordered_map<std::string, std::int64_t> pieceToId;
    /// id → piece（反查；解码用）。
    std::vector<std::string> idToPiece;

    std::int64_t decoderStartId = 65000;
    std::int64_t eosId = 0;
    std::int64_t padId = 65000;
    std::int64_t unkId = 2;
    /// 目标语言标记（m2m100 族需要；opus 族为 -1 表示不需要）。
    std::int64_t targetLangId = -1;
    /// 源语言标记（m2m100 族需要；opus 族为 -1）。
    /// 重要性：缺失时模型不知道输入语言，实测译文会严重退化
    ///（混入韩文/希伯来文、单词复读）。
    std::int64_t sourceLangId = -1;
    /// 分词算法：false = SentencePiece Unigram（opus）；true = BPE（m2m100）。
    bool useBpe = false;
    int numLayers = 6;
    int numHeads = 8;
    int headDim = 64;

    bool vocabReady = false;
};

Translator::Translator() : impl_(new Impl()) {}
Translator::~Translator() = default;

bool Translator::load(const std::string& encoderPath, const std::string& decoderPath,
                      const std::string& decoderWithPastPath, const std::string& tokenizerJsonPath,
                      std::string* outputFailure) {
    loaded_ = false;
    impl_->vocabReady = false;

    // ---- 词表（SentencePiece Unigram） ----
    std::ifstream file(tokenizerJsonPath, std::ios::binary);
    if (!file) {
        if (outputFailure != nullptr) *outputFailure = "词表文件不存在：" + tokenizerJsonPath;
        return false;
    }
    const std::string content((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    JsonValue root;
    JsonParseError parseErr;
    if (!parseJson(content, root, parseErr)) {
        if (outputFailure != nullptr) *outputFailure = "词表解析失败：" + parseErr.message;
        return false;
    }
    const JsonValue& model = root.get("model");
    if (!model.isObject()) {
        if (outputFailure != nullptr) *outputFailure = "词表缺少 model 对象";
        return false;
    }
    // 两种结构都要支持：
    //   · Unigram（opus）：model.vocab = [[piece, score], ...]（数组）
    //   · BPE（m2m100）：  model.vocab = {piece: id, ...}（对象）
    const JsonValue& vocab = model.get("vocab");
    const bool vocabIsArray = vocab.isArray();
    const bool vocabIsObject = vocab.isObject();
    if (!vocabIsArray && !vocabIsObject) {
        if (outputFailure != nullptr) *outputFailure = "词表缺少 model.vocab";
        return false;
    }
    const std::size_t n = vocab.size();
    if (n == 0 || n > kMaxVocab) {
        if (outputFailure != nullptr) *outputFailure = "词表大小非法";
        return false;
    }
    impl_->pieceScore.clear();
    impl_->pieceToId.clear();
    impl_->pieceScore.reserve(n);
    impl_->pieceToId.reserve(n * 2);

    if (vocabIsArray) {
        impl_->idToPiece.assign(n, std::string());
        for (std::size_t i = 0; i < n; ++i) {
            const JsonValue& item = vocab.at(i);
            if (!item.isArray() || item.size() < 1) continue;
            const std::string piece = item.at(0).asString();
            const double score = item.size() >= 2 ? item.at(1).asNumber(0.0) : 0.0;
            impl_->pieceScore.emplace(piece, score);
            impl_->pieceToId.emplace(piece, static_cast<std::int64_t>(i));
            if (i < impl_->idToPiece.size()) impl_->idToPiece[i] = piece;
        }
    } else {
        // BPE：id 由值给出（可能不连续），先求最大 id 再分配反查表
        std::int64_t maxId = 0;
        for (const auto& kv : vocab.members()) {
            const std::int64_t id = kv.second.asInteger(-1);
            if (id > maxId) maxId = id;
        }
        impl_->idToPiece.assign(static_cast<std::size_t>(maxId) + 1, std::string());
        for (const auto& kv : vocab.members()) {
            const std::int64_t id = kv.second.asInteger(-1);
            if (id < 0) continue;
            impl_->pieceToId.emplace(kv.first, id);
            // BPE 无 log-prob，统一给 0（不影响编码：编码按最长匹配，不用分数）
            impl_->pieceScore.emplace(kv.first, 0.0);
            impl_->idToPiece[static_cast<std::size_t>(id)] = kv.first;
        }
    }
    // ---- added_tokens 补入词表 ----
    // BPE 模型的特殊/语言标记（__zh__ / __en__ 等）只在 added_tokens 里，
    // 不在 model.vocab 中。若缺失，编码时会被切碎成字节级片段，
    // 导致译文混入无关语言（实测出现韩文/希伯来文）。
    {
        const JsonValue& added = root.get("added_tokens");
        if (added.isArray()) {
            for (std::size_t i = 0; i < added.size(); ++i) {
                const JsonValue& t = added.at(i);
                const std::string content = t.get("content").asString();
                const std::int64_t id = t.get("id").asInteger(-1);
                if (content.empty() || id < 0) continue;
                impl_->pieceToId.emplace(content, id);
                impl_->pieceScore.emplace(content, 0.0);
                if (static_cast<std::size_t>(id) >= impl_->idToPiece.size()) {
                    impl_->idToPiece.resize(static_cast<std::size_t>(id) + 1);
                }
                impl_->idToPiece[static_cast<std::size_t>(id)] = content;
            }
        }
    }

    if (impl_->pieceToId.empty()) {
        if (outputFailure != nullptr) *outputFailure = "词表为空";
        return false;
    }
    impl_->vocabReady = true;

    // ---- 分词算法识别 ----
    // m2m100 族用 BPE；opus 族用 SentencePiece Unigram。
    // 判据直接取 tokenizer.json 的 model.type，避免写死。
    const std::string modelType = model.get("type").asString();
    impl_->useBpe = (modelType == "BPE");

    // ---- 特殊 token（从 added_tokens 自动识别，缺省值按族回落）----
    impl_->decoderStartId = impl_->useBpe ? 2 : 65000;
    impl_->eosId = impl_->useBpe ? 2 : 0;
    impl_->padId = impl_->useBpe ? 1 : 65000;
    impl_->unkId = impl_->useBpe ? 3 : 2;
    impl_->targetLangId = -1;
    impl_->sourceLangId = -1;

    const JsonValue& added = root.get("added_tokens");
    if (added.isArray()) {
        for (std::size_t i = 0; i < added.size(); ++i) {
            const JsonValue& t = added.at(i);
            const std::string c = t.get("content").asString();
            const std::int64_t id = t.get("id").asInteger(-1);
            if (id < 0) continue;
            if (c == "<pad>") impl_->padId = id;
            else if (c == "</s>") impl_->eosId = id;
            else if (c == "<unk>") impl_->unkId = id;
            // m2m100 族的语言标记：
            //   源语言 __zh__ 加在 encoder 输入前；
            //   目标语言 __en__ 作为 decoder 首步的 forced BOS。
            else if (c == "__en__") impl_->targetLangId = id;
            else if (c == "__zh__") impl_->sourceLangId = id;
        }
    }
    // m2m100 的 decoder_start 与 eos 同值（config: 均为 2）
    if (impl_->useBpe) impl_->decoderStartId = impl_->eosId;
    if (impl_->useBpe && (impl_->targetLangId < 0 || impl_->sourceLangId < 0)) {
        if (outputFailure != nullptr) {
            *outputFailure = "词表缺少 __zh__ / __en__ 语言标记（m2m100 族必需）";
        }
        return false;
    }

    // ---- 三个模型（分体导出） ----
    if (!impl_->encoder.load(encoderPath, AiBackend::Cpu, outputFailure)) return false;
    if (!impl_->decoder.load(decoderPath, AiBackend::Cpu, outputFailure)) {
        impl_->encoder.unload();
        return false;
    }
    if (!impl_->decoderWithPast.load(decoderWithPastPath, AiBackend::Cpu, outputFailure)) {
        impl_->encoder.unload();
        impl_->decoder.unload();
        return false;
    }

    // 从 decoder 输入形状推导 KV 头数与维度
    impl_->numLayers = 0;
    impl_->numHeads = 8;
    impl_->headDim = 64;
    for (const auto& in : impl_->decoder.inputs()) {
        if (in.shape.size() == 4) {
            if (in.shape[1] > 0) impl_->numHeads = static_cast<int>(in.shape[1]);
            if (in.shape[3] > 0) impl_->headDim = static_cast<int>(in.shape[3]);
        }
    }
    for (const auto& out : impl_->decoder.outputs()) {
        if (out.name.rfind("present.", 0) == 0 && out.name.find(".decoder.key") != std::string::npos) {
            ++impl_->numLayers;
        }
    }
    if (impl_->numLayers <= 0) impl_->numLayers = 6;

    loaded_ = true;
    return true;
}

void Translator::unload() {
    impl_->encoder.unload();
    impl_->decoder.unload();
    impl_->decoderWithPast.unload();
    impl_->vocabReady = false;
    loaded_ = false;
}

bool Translator::encodeText(const std::string& text, std::vector<std::int64_t>& outIds) const {
    outIds.clear();
    if (!impl_->vocabReady) return false;
    if (text.empty()) return true;

    // ---- BPE（m2m100 族）：最长匹配切分 ----
    // 说明：完整 BPE 需要处理 merge ranks；实测本词表对中文以单字为主，
    // 最长匹配已能得到与参考实现一致的分词结果（见 tests/test_translate_ai.cpp）。
    if (impl_->useBpe) {
        std::string norm;
        norm.reserve(text.size());
        bool prevSpace = true;
        for (char ch : text) {
            const unsigned char c = static_cast<unsigned char>(ch);
            const bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
            if (isSpace) {
                if (!prevSpace) norm.push_back(' ');
                prevSpace = true;
            } else {
                norm.push_back(ch);
                prevSpace = false;
            }
        }
        while (!norm.empty() && norm.back() == ' ') norm.pop_back();

        const std::size_t maxPiece = 24;
        std::size_t i = 0;
        while (i < norm.size()) {
            std::size_t bestLen = 0;
            std::int64_t bestId = impl_->unkId;
            for (std::size_t len = 1; len <= maxPiece && i + len <= norm.size(); ++len) {
                const std::string cand = norm.substr(i, len);
                const auto it = impl_->pieceToId.find(cand);
                if (it != impl_->pieceToId.end()) {
                    bestLen = len;
                    bestId = it->second;
                }
            }
            if (bestLen == 0) {
                // 未知字节：按 UTF-8 码点长度跳过，记为 unk
                const auto cps = utf8Codepoints(norm.substr(i));
                bestLen = cps.empty() ? 1 : cps[0].second;
                outIds.push_back(impl_->unkId);
            } else {
                outIds.push_back(bestId);
                i += bestLen;
                continue;
            }
            i += bestLen;
        }
        return true;
    }

    // 规范化空白 + 前缀空格（Metaspace add_prefix_space=true）
    std::string norm;
    norm.reserve(text.size() + 4);
    bool prevSpace = true;
    for (char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool isSpace = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (isSpace) {
            if (!prevSpace) norm.push_back(' ');
            prevSpace = true;
        } else {
            norm.push_back(ch);
            prevSpace = false;
        }
    }
    if (!norm.empty() && norm.back() == ' ') norm.pop_back();

    std::string withPrefix;
    withPrefix.reserve(norm.size() + 3);
    withPrefix.append(kSpaceMark);
    for (char ch : norm) {
        if (ch == ' ') withPrefix.append(kSpaceMark);
        else withPrefix.push_back(ch);
    }

    // Unigram Viterbi：最大化 Σ log p(piece)
    const auto cps = utf8Codepoints(withPrefix);
    const std::size_t len = cps.size();
    constexpr double kNegInf = -std::numeric_limits<double>::infinity();
    std::vector<double> best(len + 1, kNegInf);
    std::vector<std::size_t> bestLen(len + 1, 0);
    std::vector<std::string> bestPiece(len + 1);
    best[0] = 0.0;

    constexpr std::size_t kMaxPieceChars = 16;
    for (std::size_t i = 0; i < len; ++i) {
        if (best[i] == kNegInf) continue;
        const std::size_t maxJ = std::min(len, i + kMaxPieceChars);
        for (std::size_t j = i + 1; j <= maxJ; ++j) {
            const std::size_t begin = cps[i].first;
            const std::size_t end = cps[j - 1].first + cps[j - 1].second;
            const std::string cand = withPrefix.substr(begin, end - begin);
            const auto it = impl_->pieceScore.find(cand);
            if (it == impl_->pieceScore.end()) continue;
            const double score = best[i] + it->second;
            if (score > best[j]) {
                best[j] = score;
                bestLen[j] = j - i;
                bestPiece[j] = cand;
            }
        }
        if (best[i + 1] == kNegInf) {
            const std::size_t begin = cps[i].first;
            best[i + 1] = best[i] - 100.0;
            bestLen[i + 1] = 1;
            bestPiece[i + 1] = withPrefix.substr(begin, cps[i].second);
        }
    }

    std::vector<std::string> pieces;
    for (std::size_t pos = len; pos > 0;) {
        const std::size_t l = bestLen[pos];
        if (l == 0) break;
        pieces.push_back(bestPiece[pos]);
        pos -= l;
    }
    std::reverse(pieces.begin(), pieces.end());

    for (const auto& p : pieces) {
        const auto it = impl_->pieceToId.find(p);
        outIds.push_back(it != impl_->pieceToId.end() ? it->second : impl_->unkId);
    }
    return true;
}

std::string Translator::decodeIds(const std::vector<std::int64_t>& ids) const {
    std::string out;
    for (std::int64_t id : ids) {
        if (id < 0 || static_cast<std::size_t>(id) >= impl_->idToPiece.size()) continue;
        const std::string& p = impl_->idToPiece[static_cast<std::size_t>(id)];
        if (p.empty() || p == "<pad>" || p == "</s>" || p == "<unk>") continue;
        out += p;
    }
    std::string res;
    res.reserve(out.size());
    std::size_t i = 0;
    const std::string mark = kSpaceMark;
    while (i < out.size()) {
        if (out.compare(i, mark.size(), mark) == 0) {
            res.push_back(' ');
            i += mark.size();
        } else {
            res.push_back(out[i]);
            ++i;
        }
    }
    std::string final_;
    final_.reserve(res.size());
    bool prevSpace = true;
    for (char c : res) {
        if (c == ' ') {
            if (!prevSpace) final_.push_back(' ');
            prevSpace = true;
        } else {
            final_.push_back(c);
            prevSpace = false;
        }
    }
    while (!final_.empty() && final_.back() == ' ') final_.pop_back();
    return final_;
}

namespace {

/// 从 logits 取最后一个位置的行指针。
inline const float* lastLogitsRow(const AiTensor& logits, std::size_t* outVocab) {
    const std::size_t vocab = logits.shape.empty() ? logits.dataFloat.size()
                                                   : static_cast<std::size_t>(logits.shape.back());
    *outVocab = vocab;
    if (logits.dataFloat.size() < vocab) return nullptr;
    return logits.dataFloat.data() + (logits.dataFloat.size() - vocab);
}

}  // namespace

bool Translator::translate(const std::string& text, std::string& outText, int maxNewTokens,
                           std::string* outputFailure) {
    outText.clear();
    lastDegenerate_ = false;
    if (!loaded_) {
        if (outputFailure != nullptr) *outputFailure = "模型未加载";
        return false;
    }
    if (text.empty()) return true;

    std::vector<std::int64_t> srcIds;
    if (!encodeText(text, srcIds)) {
        if (outputFailure != nullptr) *outputFailure = "分词失败（词表未就绪）";
        return false;
    }
    if (srcIds.empty()) return true;

    // m2m100 族必须在源序列前加源语言标记（__zh__），否则模型无法判定输入语言，
    // 实测会输出错误语种并陷入单词复读（"one one one ..." / 混入谚文）。
    if (impl_->useBpe && impl_->sourceLangId >= 0) {
        // m2m100 的 encoder 输入格式必须是：[__zh__, ...tokens..., </s>]
        // · 缺 __zh__：模型判不出源语言，输出错语种并复读；
        // · 缺末尾 </s>：encoder 末位不是句界标记，解码质量显著劣化
        //   （实测输出 "I I I I ..." / "We We We ..." 直至长度上限）。
        srcIds.insert(srcIds.begin(), impl_->sourceLangId);
        srcIds.push_back(impl_->eosId);
    }

    // ---- encoder ----
    AiTensor ids, mask;
    ids.type = AiTensorType::Int64;
    ids.shape = {1, static_cast<std::int64_t>(srcIds.size())};
    ids.dataInt64 = srcIds;
    mask.type = AiTensorType::Int64;
    mask.shape = ids.shape;
    mask.dataInt64.assign(srcIds.size(), 1);

    std::vector<AiTensor> encOut;
    if (!impl_->encoder.run({ids, mask}, encOut, outputFailure) || encOut.empty()) {
        if (outputFailure != nullptr && outputFailure->empty()) *outputFailure = "编码失败";
        return false;
    }
    const AiTensor hidden = encOut[0];

    // ---- beam search（config num_beams = 6）----
    // 说明：Marian/OPUS 的训练解码方式是 beam search；纯贪心会陷入短语复读
    //（实测 merged 与分体导出均如此，且分体配合 beam 可产出完整译文）。
    const int limit = maxNewTokens > 0 ? maxNewTokens : 128;
    constexpr int kBeams = 6;
    // 最小生成长度 = 1（不限制）。
    // 历史版本曾用 min(8, 源token数) 抑制"过早收尾"，那是对 OPUS-MT 的临时规避。
    // m2m100 的 EOS 预测本身可靠（正确译文末尾 p(EOS) 即为最高分），
    // 强制最小长度反而会在正确句子后逼出多余 token（实测
    // "This video clip is great." 被拖成 "... great. . . ."）。
    const int minLen = 1;
    // GNMT 长度惩罚指数（<1 抑制过短句；Marian 常用 0.6~1.0）
    constexpr float kLengthPenalty = 0.6f;

    struct Hypothesis {
        float score = 0.0f;
        std::vector<std::int64_t> tokens;
        std::map<std::string, AiTensor> cache;  ///< present.* → 张量（按 past_key_values.* 键存）
        bool done = false;
    };

    // 首步：用 decoder_model（无 past）
    // · opus 族：仅传 decoder_start_token；
    // · m2m100 族：传 [decoder_start, __en__]，用 forced BOS 指定目标语言
    //   （实测只传 __en__ 会输出 " to to to ..." 的退化结果）。
    AiTensor firstIds;
    firstIds.type = AiTensorType::Int64;
    if (impl_->targetLangId >= 0) {
        firstIds.shape = {1, 2};
        firstIds.dataInt64 = {impl_->decoderStartId, impl_->targetLangId};
    } else {
        firstIds.shape = {1, 1};
        firstIds.dataInt64 = {impl_->decoderStartId};
    }

    std::vector<AiTensor> firstOut;
    if (!impl_->decoder.run({mask, firstIds, hidden}, firstOut, outputFailure) ||
        firstOut.empty()) {
        if (outputFailure != nullptr && outputFailure->empty()) *outputFailure = "首步解码失败";
        return false;
    }

    // 首步 cache：present.N.x → past_key_values.N.x
    std::map<std::string, AiTensor> baseCache;
    for (std::size_t i = 1; i < firstOut.size(); ++i) {
        const std::string presentName = impl_->decoder.outputs()[i].name;
        if (presentName.rfind("present.", 0) != 0) continue;
        baseCache["past_key_values." + presentName.substr(std::strlen("present."))] = firstOut[i];
    }

    // 首步 logits → top-k 初始假设
    std::size_t vocab = 0;
    const float* firstRow = lastLogitsRow(firstOut[0], &vocab);
    if (firstRow == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "首步 logits 为空";
        return false;
    }
    std::vector<std::pair<float, std::int64_t>> firstTop;
    firstTop.reserve(vocab);
    for (std::size_t i = 0; i < vocab; ++i) {
        if (static_cast<std::int64_t>(i) == impl_->padId) continue;
        firstTop.emplace_back(firstRow[i], static_cast<std::int64_t>(i));
    }
    const std::size_t keep = std::min<std::size_t>(firstTop.size(), kBeams);
    std::partial_sort(firstTop.begin(), firstTop.begin() + static_cast<std::ptrdiff_t>(keep),
                      firstTop.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    std::vector<Hypothesis> beams;
    /// 已生成 EOS 的假设单独保存，不再参与扩展，但参与最终择优。
    /// 若与活跃假设混在同一向量里按累积分排序，长序列的累积分天然更高，
    /// 已完成（且长度更短）的正确译文会被挤出 top-k，最终表现为
    /// 「正确译文 + 尾部无限复读」。
    std::vector<Hypothesis> finished;
    // 首步只保留 1 条假设。
    // 实测对照（同一份代码，仅改此处的 1 → kBeams）：
    //   首步 1 条：「我们明天去公园散步」→ "Tomorrow we take a walk in the park."
    //   首步 6 条：同句 → "I am going to a walk in the park."（语义尚可但不地道）
    //              「谢谢」→ "Thank you."（优于 1 条时的 "Thanks to."）
    // 两者互有得失。当前选择 1 条：整句自然度权重高于极短句，
    // 且后续步的 top-k 展开仍能产生多束候选（分叉从第二步开始）。
    // 若后续要做短句专项优化，可改为按源句长度动态选择（短句 → kBeams）。
    for (std::size_t k = 0; k < 1; ++k) {
        Hypothesis h;
        h.score = firstTop[k].first;
        h.tokens.push_back(firstTop[k].second);
        h.cache = baseCache;
        h.done = (firstTop[k].second == impl_->eosId);
        if (h.done) {
            finished.push_back(std::move(h));
        } else {
            beams.push_back(std::move(h));
        }
    }

    // 逐步扩展（用 decoder_with_past）
    for (int step = 1; step < limit; ++step) {
        struct Cand {
            float score;
            std::size_t parent;
            std::int64_t token;
            std::map<std::string, AiTensor> cache;
        };
        std::vector<Cand> cands;

        for (std::size_t bi = 0; bi < beams.size(); ++bi) {
            const Hypothesis& h = beams[bi];
            if (h.done || h.tokens.empty()) continue;

            AiTensor stepIds;
            stepIds.type = AiTensorType::Int64;
            stepIds.shape = {1, 1};
            stepIds.dataInt64 = {h.tokens.back()};

            std::vector<AiTensor> inputs;
            for (const auto& in : impl_->decoderWithPast.inputs()) {
                if (in.name == "input_ids") {
                    inputs.push_back(stepIds);
                } else if (in.name == "encoder_attention_mask") {
                    inputs.push_back(mask);  // 恒为源长度（其他长度会触发 broadcast 错误）
                } else if (in.name.rfind("past_key_values.", 0) == 0) {
                    const auto it = h.cache.find(in.name);
                    inputs.push_back(it != h.cache.end() ? it->second : AiTensor{});
                } else {
                    inputs.push_back(AiTensor{});
                }
            }

            std::vector<AiTensor> out;
            if (!impl_->decoderWithPast.run(inputs, out, nullptr) || out.empty()) {
                continue;
            }

            // KV 更新：**只覆盖 decoder 侧**，encoder 侧沿用父 beam 的值。
            // 依据：decoder_with_past 的输出不含 present.*.encoder.*（encoder KV
            // 由首步 decoder_model 产出后固定不变）。若整体重建 cache，
            // encoder 槽位会缺失 → 模型报「输入张量无效」。
            std::map<std::string, AiTensor> nextCache = h.cache;
            for (std::size_t i = 1; i < out.size(); ++i) {
                const std::string presentName = impl_->decoderWithPast.outputs()[i].name;
                if (presentName.rfind("present.", 0) != 0) continue;
                const std::string suffix = presentName.substr(std::strlen("present."));
                if (suffix.find(".encoder.") != std::string::npos) continue;  // 保持首步值
                nextCache["past_key_values." + suffix] = out[i];
            }

            std::size_t v = 0;
            const float* row = lastLogitsRow(out[0], &v);
            if (row == nullptr) continue;

            // 取 top-(kBeams) 候选，排除 pad
            const bool allowEos = static_cast<int>(h.tokens.size()) >= minLen;

            std::vector<std::pair<float, std::int64_t>> top;
            top.reserve(v);
            for (std::size_t i = 0; i < v; ++i) {
                if (static_cast<std::int64_t>(i) == impl_->padId) continue;
                if (!allowEos && static_cast<std::int64_t>(i) == impl_->eosId) continue;
                top.emplace_back(row[i], static_cast<std::int64_t>(i));
            }
            const std::size_t take = std::min<std::size_t>(top.size(), kBeams);
            if (take == 0) continue;
            std::partial_sort(top.begin(), top.begin() + static_cast<std::ptrdiff_t>(take), top.end(),
                              [](const auto& a, const auto& b) { return a.first > b.first; });

            for (std::size_t k = 0; k < take; ++k) {
                Cand c;
                c.score = h.score + top[k].first;
                c.parent = bi;
                c.token = top[k].second;
                c.cache = nextCache;
                cands.push_back(std::move(c));
            }
        }

        if (cands.empty()) break;
        // 达到长度上限的前一步：把候选限制为 EOS（config forced_eos_token_id = 0），
        // 避免输出被硬截断成不完整短语。
        if (step == limit - 1) {
            auto it = std::find_if(cands.begin(), cands.end(), [&](const Cand& c) {
                return c.token == impl_->eosId;
            });
            if (it != cands.end()) {
                Cand eosCand = *it;
                cands.clear();
                cands.push_back(eosCand);
            }
        }

        const std::size_t keepN =
            std::min<std::size_t>(cands.size(), static_cast<std::size_t>(kBeams));
        std::partial_sort(cands.begin(), cands.begin() + static_cast<std::ptrdiff_t>(keepN),
                          cands.end(), [](const Cand& a, const Cand& b) { return a.score > b.score; });

        std::vector<Hypothesis> next;
        next.reserve(keepN);
        for (std::size_t k = 0; k < keepN; ++k) {
            Hypothesis h;
            h.score = cands[k].score;
            h.tokens = beams[cands[k].parent].tokens;
            h.tokens.push_back(cands[k].token);
            h.cache = std::move(cands[k].cache);
            h.done = (cands[k].token == impl_->eosId);
            if (h.done) {
                finished.push_back(std::move(h));  // 不再参与扩展
            } else {
                next.push_back(std::move(h));
            }
        }
        beams = std::move(next);
        if (beams.empty()) break;

        // 提前收敛：已完成假设已足够多，且最优活跃假设无法再超越
        // （这里采用简单启发式：finished 达到 kBeams 即停止）。
        if (finished.size() >= static_cast<std::size_t>(kBeams)) break;
    }

    // 选分数/长度归一化最优的假设：已完成假设优先（它们真正生成了 EOS），
    // 只有在没有任何假设生成 EOS 时才回退到被长度上限截断的活跃假设。
    const Hypothesis* best = nullptr;
    float bestNorm = -std::numeric_limits<float>::infinity();
    const std::vector<Hypothesis>& pool = finished.empty() ? beams : finished;
    for (const auto& h : pool) {
        if (h.tokens.empty()) continue;
        const float len = static_cast<float>(h.tokens.size());
        // GNMT 长度归一化：score / ((5 + len) / 6)^alpha
        // alpha 取 0.6 是基于实测：alpha=1.0 时偏好极短句（只输出 "I love"），
        // alpha=0 时偏好长句但易受尾部退化影响，0.6 在两者间取得平衡。
        const float norm = h.score / std::pow((5.0f + len) / 6.0f, kLengthPenalty);
        if (norm > bestNorm) {
            bestNorm = norm;
            best = &h;
        }
    }
    if (best == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "未生成任何候选";
        return false;
    }

    std::vector<std::int64_t> finalTokens;
    for (auto t : best->tokens) {
        if (t == impl_->eosId) break;
        finalTokens.push_back(t);
    }

    // 复读检测（不截断，只标记）
    // ---------------------------------------------------------------------
    // 实测结论：OPUS-MT-ZH-EN（77M）对中文短句存在固有退化——对同一输入，
    // 6 条 beam 会收敛到同一退化路径（如 "park park park ..."），
    // EOS logit 仅 1~2.4（普通 token 为 6~9），说明模型本身不倾向收尾。
    //
    // 处置原则：**不做激进截断**。实测任何截断阈值都会把正常句（如
    // "We are going to the park for a walk"）削成残句（"We are going"），
    // 反而降低可用性。因此保留完整输出，仅通过返回值语义告知调用方
    // 该句可能含重复；由上层决定是否采用。
    //
    // 后续改进方向（需换模型，见 docs/ROADMAP.md §5.4）：
    //   使用更大规模的翻译模型（如 m2m100-418M / mbart-large-50），
    //   它们对短句的收尾更稳定。
    bool degenerate = false;
    {
        const std::size_t n = finalTokens.size();
        for (std::size_t pos = 0; pos + 2 <= n; ++pos) {
            if (finalTokens[pos] == finalTokens[pos + 1] &&
                finalTokens[pos] == finalTokens[pos + 2]) {
                degenerate = true;
                break;
            }
        }
        if (!degenerate) {
            for (std::size_t pos = 0; pos + 4 <= n; ++pos) {
                if (finalTokens[pos] == finalTokens[pos + 2] &&
                    finalTokens[pos + 1] == finalTokens[pos + 3]) {
                    degenerate = true;
                    break;
                }
            }
        }
    }
    lastDegenerate_ = degenerate;

    outText = decodeIds(finalTokens);
    if (outText.empty()) {
        if (outputFailure != nullptr) *outputFailure = "译文为空（模型仅生成结束符）";
        return false;
    }
    return true;
}

const char* Translator::knownLimitation() {
    return "OPUS-MT-ZH-EN（77M）对短中文句（约 6 字以内）存在复读倾向，"
           "译文可能出现重复词；长句通常稳定。该限制来自模型本身，"
           "需换用更大规模模型方可根治（见 docs/ROADMAP.md §5.4）。";
}

bool Translator::translateLines(const std::vector<std::string>& lines, TranslateDirection direction,
                                std::vector<TranslatedLine>& outLines, TranslateStats* outStats,
                                std::string* outputFailure) {
    outLines.clear();
    outLines.reserve(lines.size());
    if (direction != TranslateDirection::ZhToEn) {
        if (outputFailure != nullptr) *outputFailure = "当前仅支持中文→英文（模型限制）";
        return false;
    }
    const auto t0 = std::chrono::steady_clock::now();
    TranslateStats stats;
    stats.linesTotal = static_cast<int>(lines.size());
    for (const auto& src : lines) {
        TranslatedLine line;
        line.source = src;
        if (src.empty()) {
            outLines.push_back(line);
            ++stats.linesOk;
            continue;
        }
        std::string err;
        int tokens = 0;
        std::string out;
        if (translate(src, out, 0, &err)) {
            line.target = out;
            ++stats.linesOk;
        } else {
            line.error = err.empty() ? "翻译失败" : err;
            ++stats.linesFailed;
        }
        line.tokenCount = tokens;
        outLines.push_back(line);
    }
    const auto t1 = std::chrono::steady_clock::now();
    stats.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (outStats != nullptr) *outStats = stats;
    if (stats.linesFailed > 0 && stats.linesOk == 0) {
        if (outputFailure != nullptr) *outputFailure = "全部句子翻译失败";
        return false;
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
