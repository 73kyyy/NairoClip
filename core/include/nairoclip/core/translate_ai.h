#pragma once
// =============================================================================
// 字幕翻译 AI（Translation AI）—— 文档 Part 4-A「AI工具 → 字幕翻译」
// =============================================================================
// 产品对应（原文）：
//   「字幕翻译 [按钮]：中文 → English（默认）
//     翻译范围 [Radio]：整条轨道 / 选中字幕  默认 整条轨道」
//
// 本模块实现真实推理链路（OPUS-MT zh-en，模型 models/translate_zh_en/）：
//   · 分词：SentencePiece Unigram（从 tokenizer.json 运行时加载词表；
//     Viterbi 最大似然切分 + Metaspace ▁ 空格编码）；
//   · 编码：encoder（input_ids + attention_mask → last_hidden_state）；
//   · 解码：decoder 贪心逐步生成（KV cache 复用；decoder_start=65000, eos=0）；
//   · 回写：逐条字幕替换 text（保留时间/样式），可选保留原文到注释位。
//
// 与既有模块的关系：
//   · speech_ai：语音 → 文字（Whisper）；translate_ai：文字 → 另一种文字；
//   · subtitle：本模块只改 text，不改时间与样式，因此不引入新的字幕结构；
//   · 词表缺失时明确失败（不伪造译文、不输出占位 token）。
//
// 可用性：需 ONNX Runtime + encoder/decoder/tokenizer 三件产物。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nairoclip/core/ai_engine.h"

namespace nairoclip {
namespace core {

/// 翻译方向（文档当前只要求中文 → English；预留反向）。
enum class TranslateDirection {
    ZhToEn = 0,  ///< 中文 → English（文档默认）
    EnToZh = 1,  ///< English → 中文（预留；需对应方向模型）
};

/// 翻译产物（逐句对应输入）。
struct TranslatedLine {
    std::string source;  ///< 原文
    std::string target;  ///< 译文（失败时为空，且 error 非空）
    std::string error;   ///< 该句失败原因（成功为空）
    int tokenCount = 0;  ///< 生成的 token 数（诊断用）
};

/// 翻译统计（供 UI 展示）。
struct TranslateStats {
    int linesTotal = 0;
    int linesOk = 0;
    int linesFailed = 0;
    double elapsedMs = 0.0;
};

/// 中英翻译器（会话复用；单实例单线程）。
/// @warning **非线程安全**。
///   内部 lastDegenerate_ 为共享可变状态，且 ONNX 会话的包装层无锁保护。
///   多线程翻译请各自持有独立 Translator 实例（每个实例加载一份模型，
///   内存代价约为模型大小；如需共享权重，需额外实现会话池，当前未做）。
///   批量翻译 `translateLines()` 在单线程内顺序执行，本身是安全的。
class Translator {
public:
    Translator();
    ~Translator();
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    /// 加载模型与词表（**分体导出**，必须三件）。
    /// - encoderPath：encoder_model.onnx；
    /// - decoderPath：decoder_model.onnx（无 past，首步用）；
    /// - decoderWithPastPath：decoder_with_past_model.onnx（后续步用）；
    /// - tokenizerJsonPath：tokenizer.json（vocab 与特殊 token）；
    /// 返回 false：任一件缺失或解析失败（fail-closed）。
    ///
    /// 为什么必须是分体而非 merged：实测 merged 导出（decoder_model_merged.onnx）
    /// 在 cache 分支下 present.*.encoder.* 从源长度收缩为 1，导致注意力失效、
    /// 第 4 个 token 起复读；分体导出的 encoder KV 由首步固定，
    /// 配合 beam search 可产出完整译文。
    bool load(const std::string& encoderPath, const std::string& decoderPath,
              const std::string& decoderWithPastPath, const std::string& tokenizerJsonPath,
              std::string* outputFailure = nullptr);
    bool loaded() const { return loaded_; }
    void unload();

    /// 单句翻译。
    /// - maxNewTokens：生成上限（0 = 默认 256）；
    /// - outputFailure：失败说明（可空）。
    bool translate(const std::string& text, std::string& outText, int maxNewTokens = 0,
                   std::string* outputFailure = nullptr);

    /// 批量翻译（逐句；单句失败不影响其他句）。
    bool translateLines(const std::vector<std::string>& lines, TranslateDirection direction,
                        std::vector<TranslatedLine>& outLines, TranslateStats* outStats = nullptr,
                        std::string* outputFailure = nullptr);

    /// 分词（公开以便诊断与测试；空词表时返回 false）。
    bool encodeText(const std::string& text, std::vector<std::int64_t>& outIds) const;

    /// 反分词（token id → 文本；跳过特殊 token）。
    std::string decodeIds(const std::vector<std::int64_t>& ids) const;

    /// 最近一次 translate() 的结果是否出现重复退化。
    /// 用途：上层可据此决定「采用 / 提示用户复核 / 换用其他译法」，
    /// 而不是让调用方在不知情的情况下拿到带重复的译文。
    bool lastResultDegenerate() const { return lastDegenerate_; }

    /// 已知能力边界（如实告知，见 docs/ROADMAP.md §5.4）：
    /// 当前锁定的模型对**短中文句**（≤6 字）易进入重复循环；
    /// 长句通常更稳定。建议上层对短句结果做人工复核或加入术语表约束。
    static const char* knownLimitation();

private:
    bool loaded_ = false;
    bool lastDegenerate_ = false;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// 纯文本替换规划（不依赖模型）：把译文按原字幕顺序套回，保持时间与样式。
/// 供平台层在「翻译成功后回写」时使用，确保回写逻辑单一事实来源。
struct SubtitleTextPatch {
    std::size_t cueIndex = 0;  ///< 原字幕序号
    std::string newText;       ///< 新文本（空 = 保持原样）
};

}  // namespace core
}  // namespace nairoclip
