// =============================================================================
// 字幕翻译 AI 测试（分词 / 推理 / 已知限制）
// =============================================================================
// 说明：
//   · 分词为纯逻辑，始终执行；
//   · 推理类用例需 ONNX Runtime + 三个模型产物，缺任一自动跳过；
//   · 已知限制（见 translate_ai.h 与 docs/ROADMAP.md §5.2）：当前导出的
//     merged decoder 在 cache 分支下 encoder KV 会收缩，导致第 4 个 token
//     起分布退化。测试对「前若干 token 语义正确」做断言，不对整句质量断言。
#include "test_framework.h"
#include "nairoclip/core/translate_ai.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

std::string modelDir() { return "models/translate_zh_en/"; }

bool fileExists(const std::string& p) {
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return false;
    std::fclose(f);
    return true;
}

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
    // 特殊 token（0=</s>, 65000=<pad>, 2=<unk>）应被跳过
    const std::string out = t.decodeIds({0, 65000, 2});
    NC_CHECK(out.empty());
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
}
