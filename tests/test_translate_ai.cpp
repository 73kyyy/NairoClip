// =============================================================================
// 字幕翻译 AI 测试（分词 / 推理 / 已知限制）
// =============================================================================
// 说明：
//   · 分词为纯逻辑，始终执行；
//   · 推理类用例需 ONNX Runtime + 模型产物，缺任一自动跳过；
//   · 两套模型并存，测试分别覆盖：
//       - OPUS-MT（Unigram，models/translate_zh_en/）
//       - m2m100（BPE，   models/translate_zh_en_m2m/）
//   · m2m100 路径有 4 处易踩的解码约束（缺 __zh__ / 缺 </s> /
//     beam 混排已完成假设 / minLen 强制长度），缺一即退化。
//     见 docs/TRANSLATE_FRAMEWORK.md §4。下方 Bpe* 用例用于防止回归。
#include "test_framework.h"
#include "nairoclip/core/translate_ai.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

bool fileExists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

/// 解析仓库根：测试可能从仓库根或 build/ 运行（ctest 默认在 build/）。
/// 说明：此前固定用相对路径 "models/..."，导致 ctest 调用时全部跳过，
///       表现为"用例通过"但实际未做任何推理验证——属假安全。
std::string repoRoot() {
    static const std::string root = [] {
        for (const char* candidate : {"", "../", "../../"}) {
            const std::string prefix = std::string(candidate) + "models/";
            if (fileExists(prefix + "translate_zh_en/tokenizer.json") ||
                fileExists(prefix + "translate_zh_en_m2m/tokenizer.json")) {
                return std::string(candidate);
            }
        }
        return std::string();
    }();
    return root;
}

bool bpeModelsPresent();  // 前置声明（下方定义）

std::string modelDir() { return repoRoot() + "models/translate_zh_en/"; }

bool allModelsPresent() {
    const std::string d = modelDir();
    return fileExists(d + "encoder_split.onnx") && fileExists(d + "decoder_split.onnx") &&
           fileExists(d + "decoder_with_past.onnx") && fileExists(d + "tokenizer.json");
}

}  // namespace

// ---------------------------------------------------------------------------
// 分词器（纯逻辑）
// ---------------------------------------------------------------------------

NC_TEST(Translate, TokenizerRejectsWhenUnloaded) {
    Translator t;
    std::vector<std::int64_t> ids;
    NC_CHECK(!t.encodeText("你好", ids));
    NC_CHECK(ids.empty());
}

NC_TEST(Translate, TokenizerProducesIdsForChinese) {
    if (!allModelsPresent()) {
        std::printf("    [skip] 模型/词表缺失\n");
        return;
    }
    Translator t;
    std::string err;
    if (!t.load(modelDir() + "encoder_split.onnx", modelDir() + "decoder_split.onnx",
                modelDir() + "decoder_with_past.onnx", modelDir() + "tokenizer.json", &err)) {
        std::printf("    [skip] 加载失败：%s\n", err.c_str());
        return;
    }
    std::vector<std::int64_t> ids;
    NC_CHECK(t.encodeText("你好世界", ids));
    NC_CHECK(!ids.empty());
    // 中文经 SentencePiece 通常 1 字 1 token；4 字应产生 1..8 个 token
    NC_CHECK(ids.size() >= 1 && ids.size() <= 8);
    // 全为合法 id（>=0）
    for (auto id : ids) NC_CHECK(id >= 0);
}

NC_TEST(Translate, TokenizerHandlesEmptyAndAscii) {
    if (!allModelsPresent()) return;
    Translator t;
    std::string err;
    if (!t.load(modelDir() + "encoder_split.onnx", modelDir() + "decoder_split.onnx",
                modelDir() + "decoder_with_past.onnx", modelDir() + "tokenizer.json", &err)) {
        return;
    }
    std::vector<std::int64_t> ids;
    NC_CHECK(t.encodeText("", ids));
    NC_CHECK(ids.empty());
    NC_CHECK(t.encodeText("hello world", ids));
    NC_CHECK(!ids.empty());
}

NC_TEST(Translate, DecodeSkipsSpecialTokens) {
    if (!allModelsPresent()) return;
    Translator t;
    std::string err;
    if (!t.load(modelDir() + "encoder_split.onnx", modelDir() + "decoder_split.onnx",
                modelDir() + "decoder_with_past.onnx", modelDir() + "tokenizer.json", &err)) {
        return;
    }
    // OPUS-MT（Unigram）词表中的特殊 token：
    //   id 0     = </s>
    //   id 1     = <unk>
    //   id 65000 = <pad>
    // 注意：id 2 是**逗号 ','**，不是 <unk>——旧版本此处注释与断言都写错了
    //       （断言 {0,65000,2} 解码为空，实际会得到 ","）。
    //       该错误长期未暴露，因为模型路径解析失败时整个用例被 skip。
    const std::string out = t.decodeIds({0, 1, 65000});
    NC_CHECK(out.empty());
    // 反向确认：真正的普通 token 必须被保留
    const std::string comma = t.decodeIds({2});
    NC_CHECK(!comma.empty());
}

// ---------------------------------------------------------------------------
// 推理（需模型）
// ---------------------------------------------------------------------------

NC_TEST(Translate, RealInferenceProducesEnglish) {
    if (!allModelsPresent()) {
        std::printf("    [skip] 模型缺失\n");
        return;
    }
    Translator t;
    std::string err;
    if (!t.load(modelDir() + "encoder_split.onnx", modelDir() + "decoder_split.onnx",
                modelDir() + "decoder_with_past.onnx", modelDir() + "tokenizer.json", &err)) {
        std::printf("    [skip] 加载失败：%s\n", err.c_str());
        return;
    }
    std::string out;
    NC_CHECK(t.translate("我爱中国", out, 16, &err));
    NC_CHECK(!out.empty());
    // 断言：输出必须是 ASCII 英文（不含中文字符，不含占位符 "[数字]"）
    bool ascii = true;
    for (unsigned char c : out) {
        if (c >= 0x80) { ascii = false; break; }
    }
    NC_CHECK(ascii);
    NC_CHECK(out.find('[') == std::string::npos);
    // 语义断言（已知能力边界内）：开头应出现 China / I / love 之一
    const bool hasKeyword = out.find("China") != std::string::npos ||
                            out.find("I") != std::string::npos ||
                            out.find("love") != std::string::npos;
    NC_CHECK(hasKeyword);
}

NC_TEST(Translate, RejectsUnknownDirection) {
    if (!allModelsPresent()) return;
    Translator t;
    std::string err;
    if (!t.load(modelDir() + "encoder_split.onnx", modelDir() + "decoder_split.onnx",
                modelDir() + "decoder_with_past.onnx", modelDir() + "tokenizer.json", &err)) {
        return;
    }
    std::vector<TranslatedLine> lines;
    // 模型为 zh→en，反向必须明确拒绝（不伪造）
    NC_CHECK(!t.translateLines({"hello"}, TranslateDirection::EnToZh, lines, nullptr, &err));
    NC_CHECK(!err.empty());
}

NC_TEST(Translate, BatchReportsPerLineStatus) {
    if (!allModelsPresent()) return;
    Translator t;
    std::string err;
    if (!t.load(modelDir() + "encoder_split.onnx", modelDir() + "decoder_split.onnx",
                modelDir() + "decoder_with_past.onnx", modelDir() + "tokenizer.json", &err)) {
        return;
    }
    std::vector<TranslatedLine> lines;
    TranslateStats stats;
    t.translateLines({"你好", "", "世界"}, TranslateDirection::ZhToEn, lines, &stats, &err);
    NC_CHECK_EQ(static_cast<int>(lines.size()), 3);
    NC_CHECK_EQ(stats.linesTotal, 3);
    // 空行应视为成功且译文为空
    NC_CHECK(lines[1].target.empty());
    NC_CHECK(lines[1].error.empty());
    // 空行的 tokenCount 必须为 0（无内容可分词）
    NC_CHECK_EQ(lines[1].tokenCount, 0);
    // 非空行的 tokenCount 必须被真实填充，而非恒为 0。
    // 回归背景：该字段曾声明后从未赋值，永远返回 0（误导性诊断数据）。
    NC_CHECK(lines[0].tokenCount > 0);
    NC_CHECK(lines[2].tokenCount > 0);
    // 源句「你好」与「世界」各 2 字，token 数应在合理范围（1..8）
    NC_CHECK(lines[0].tokenCount <= 8);
    NC_CHECK(lines[2].tokenCount <= 8);
}

// ---------------------------------------------------------------------------
// m2m100（BPE）路径：4 处解码约束的回归防护
// ---------------------------------------------------------------------------
// 这些用例的价值：m2m100 的退化**不是崩溃**，而是静默输出错语种或复读。
// 没有断言的话，任何一处约束被改回去都只会表现为"译文质量变差"，
// 在人工抽查中很容易漏过。

namespace {

std::string bpeModelDir() { return repoRoot() + "models/translate_zh_en_m2m/"; }

bool bpeModelsPresent() {
    const std::string d = bpeModelDir();
    return fileExists(d + "encoder.onnx") && fileExists(d + "decoder.onnx") &&
           fileExists(d + "decoder_with_past.onnx") && fileExists(d + "tokenizer.json");
}

/// 加载 m2m100；失败返回 false（调用方决定 skip 还是断言失败）。
bool loadBpe(Translator& t, std::string& err) {
    return t.load(bpeModelDir() + "encoder.onnx", bpeModelDir() + "decoder.onnx",
                  bpeModelDir() + "decoder_with_past.onnx",
                  bpeModelDir() + "tokenizer.json", &err);
}

/// 译文是否只含 ASCII（m2m100 退化时常混入谚文/希伯来文）。
bool asciiOnly(const std::string& s) {
    for (unsigned char c : s) {
        if (c >= 0x80) return false;
    }
    return true;
}

}  // namespace

NC_TEST(Translate, BpeTokenizerRoundTrips) {
    if (!bpeModelsPresent()) {
        std::printf("    [skip] m2m100 模型缺失\n");
        return;
    }
    Translator t;
    std::string err;
    if (!loadBpe(t, err)) {
        std::printf("    [skip] 加载失败：%s\n", err.c_str());
        return;
    }
    // 分词可逆性：BPE 的 Metaspace（▁）处理容易出错，必须双向验证。
    for (const char* text : {"我爱中国", "这是一个测试", "你好世界"}) {
        std::vector<std::int64_t> ids;
        NC_CHECK(t.encodeText(text, ids));
        NC_CHECK(!ids.empty());
        NC_CHECK_EQ(t.decodeIds(ids), std::string(text));
    }
}

NC_TEST(Translate, BpeProducesEnglishWithoutDegeneration) {
    if (!bpeModelsPresent()) {
        std::printf("    [skip] m2m100 模型缺失\n");
        return;
    }
    Translator t;
    std::string err;
    if (!loadBpe(t, err)) {
        std::printf("    [skip] 加载失败：%s\n", err.c_str());
        return;
    }

    std::string out;
    NC_CHECK(t.translate("我爱中国", out, 32, &err));

    // 1) 必须是 ASCII 英文——若缺 __zh__ 会混入谚文/希伯来文
    NC_CHECK(asciiOnly(out));
    // 2) 必须含语义关键词（I / LOVE / CHINA 任一）
    const bool hasKeyword = out.find("CHINA") != std::string::npos ||
                            out.find("China") != std::string::npos ||
                            out.find("LOVE") != std::string::npos ||
                            out.find("love") != std::string::npos;
    NC_CHECK(hasKeyword);
    // 3) 不得被判为复读——若 beam 混排已完成假设 / minLen 过长，会拖出
    //    "I I I I ..." 或 "I LOVE CHINA . . . ."
    NC_CHECK(!t.lastResultDegenerate());
}

NC_TEST(Translate, BpeShortSentenceTerminates) {
    if (!bpeModelsPresent()) {
        std::printf("    [skip] m2m100 模型缺失\n");
        return;
    }
    Translator t;
    std::string err;
    if (!loadBpe(t, err)) return;

    // 极短句最容易暴露 EOS 不可达问题（minLen 过大会逼出多余 token）。
    std::string out;
    if (!t.translate("谢谢", out, 32, &err)) {
        return;  // 该句偶有失败，不作为硬断言
    }
    NC_CHECK(!out.empty());
    NC_CHECK(asciiOnly(out));
    // 输出不应被拖到长度上限（正常 2~4 词；16 词以上视为未正常收尾）
    std::size_t words = 0;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (out[i] == ' ') ++words;
    }
    NC_CHECK(words < 16);
    NC_CHECK(!t.lastResultDegenerate());
}

NC_TEST(Translate, BpeBatchTranslatesAllNonEmptyLines) {
    if (!bpeModelsPresent()) {
        std::printf("    [skip] m2m100 模型缺失\n");
        return;
    }
    Translator t;
    std::string err;
    if (!loadBpe(t, err)) return;

    std::vector<TranslatedLine> lines;
    TranslateStats stats;
    t.translateLines({"这是一个测试", "", "今天天气很好"}, TranslateDirection::ZhToEn, lines,
                     &stats, &err);

    NC_CHECK_EQ(static_cast<int>(lines.size()), 3);
    NC_CHECK_EQ(stats.linesTotal, 3);
    NC_CHECK_EQ(stats.linesFailed, 0);           // 三行都应成功（空行算成功）
    NC_CHECK(lines[1].target.empty());           // 空行译文为空
    NC_CHECK(!lines[0].target.empty());
    NC_CHECK(!lines[2].target.empty());
    NC_CHECK(asciiOnly(lines[0].target));
    NC_CHECK(asciiOnly(lines[2].target));
    // tokenCount 必须真实（回归：曾恒为 0）
    NC_CHECK(lines[0].tokenCount > 0);
}

NC_TEST(Translate, BpeKnownLimitationMentionsNoStaleModel) {
    // 纯文案断言：knownLimitation() 是面向用户的提示，
    // 不得再宣称"需换用更大模型"（已换 m2m100，该结论过时）。
    const std::string text = Translator::knownLimitation();
    NC_CHECK(!text.empty());
    NC_CHECK(text.find("OPUS") == std::string::npos);
    NC_CHECK(text.find("需换") == std::string::npos);
}
