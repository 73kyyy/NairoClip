// =============================================================================
// AI 生成合成内容标识测试（M2.29，《人工智能生成合成内容标识办法》
// + 文档「一、AI生成内容标识办法合规」）
// =============================================================================
// 覆盖：
//   · 隐式标识（元数据）：构建/回读/MP4 容器兼容写法（comment 压缩字段）；
//   · 显式标识（音频提示音）：生成/峰值/淡入淡出/首尾叠加/参数校验；
//   · 与隐形水印的职责分离（不同机制、不同用途，共存不冲突）；
//   · 端到端（真实 FFmpeg）：导出写入元数据（ffprobe 可读）+ 提示音真实落盘。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/ai_marking.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

// ---------------------------------------------------------------------------
// 隐式标识（元数据）
// ---------------------------------------------------------------------------

NC_TEST(AiMarking, MetadataBuildAndParse) {
    const std::vector<AiMetadataEntry> entries = buildAiMetadata("NairoClip", "TTS 智能配音");
    NC_CHECK(entries.size() >= 3);
    // comment 含可读说明 + 机器可读压缩字段（MP4 兼容）
    NC_CHECK_EQ(entries[0].key, std::string("comment"));
    NC_CHECK(entries[0].value.find("人工智能生成合成") != std::string::npos);
    NC_CHECK(entries[0].value.find("ai_generated=true") != std::string::npos);
    NC_CHECK(entries[0].value.find("ai_generated_feature=TTS 智能配音") != std::string::npos);
    // 独立机器可读字段（MKV/WebM 保留）
    bool hasFlag = false, hasFeature = false;
    for (const auto& entry : entries) {
        if (entry.key == "ai_generated") {
            hasFlag = true;
            NC_CHECK_EQ(entry.value, std::string("true"));
        }
        if (entry.key == "ai_generated_feature") hasFeature = true;
    }
    NC_CHECK(hasFlag);
    NC_CHECK(hasFeature);
    // 回读
    std::string feature;
    NC_CHECK(parseAiMetadata(entries, &feature));
    NC_CHECK_EQ(feature, std::string("TTS 智能配音"));
    // 只有 comment 的场景（MP4 只保留标准 tag）也能识别
    std::vector<AiMetadataEntry> commentOnly = {
        {"comment", "本内容由人工智能生成合成（NairoClip）；ai_generated=true；"
                    "ai_generated_feature=AI 超分辨率"}};
    std::string feature2;
    NC_CHECK(parseAiMetadata(commentOnly, &feature2));
    NC_CHECK_EQ(feature2, std::string("AI 超分辨率"));
    // 无标识的普通文件 → 不误报
    std::vector<AiMetadataEntry> plain = {{"comment", "我的旅行vlog"}, {"encoder", "Lavf60"}};
    NC_CHECK(!parseAiMetadata(plain, nullptr));
}

NC_TEST(AiMarking, MetadataDeterministicAndNoProducedBy) {
    // 确定性（同输入同输出）
    const auto a = buildAiMetadata("NairoClip", "X");
    const auto b = buildAiMetadata("NairoClip", "X");
    NC_CHECK_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        NC_CHECK_EQ(a[i].key, b[i].key);
        NC_CHECK_EQ(a[i].value, b[i].value);
    }
    // 空 producedBy：不产生 feature 字段
    const auto c = buildAiMetadata("NairoClip", "");
    for (const auto& entry : c) {
        NC_CHECK(entry.key != "ai_generated_feature");
    }
    // 空 generator 名 → 用默认值
    const auto d = buildAiMetadata("", "TTS");
    NC_CHECK(d[0].value.find("NairoClip") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 显式标识（音频提示音）
// ---------------------------------------------------------------------------

NC_TEST(AiMarking, ToneGeneration) {
    AiAudioMarkConfig config;
    const std::vector<float> tone = generateAiToneMarks(config);
    // 两段 × 0.3s × 48000 × 2ch = 57600
    NC_CHECK_EQ(tone.size(), std::size_t(57600));
    // 峰值 = amplitude（淡入淡出后）
    float peak = 0.0f;
    for (float value : tone) peak = std::max(peak, std::fabs(value));
    NC_CHECK(std::fabs(peak - 0.25f) < 0.01f);
    // 首尾接近 0（淡入淡出，防爆音）
    NC_CHECK(std::fabs(tone.front()) < 0.01f);
    NC_CHECK(std::fabs(tone.back()) < 0.01f);
    // 禁用末尾 → 只有一段
    AiAudioMarkConfig startOnly;
    startOnly.atEnd = false;
    NC_CHECK_EQ(generateAiToneMarks(startOnly).size(), std::size_t(28800));
    // 非法配置（两段都禁用）→ 空
    AiAudioMarkConfig invalid;
    invalid.atStart = false;
    invalid.atEnd = false;
    NC_CHECK(generateAiToneMarks(invalid).empty());
    // 频率校验：880Hz 单段 0.3s 零交叉数 ≈ 2 × 0.3 × 880 = 528（两段共 ~1056）
    // 注意交错布局：每帧 2 个样本（L,R），左声道位于偶数索引（步长 = 2 帧 = 4 样本）
    int crossings = 0;
    const std::size_t frameStride = 2;  // 声道数
    for (std::size_t i = frameStride; i < tone.size(); i += frameStride) {
        const float previous = tone[i - frameStride];
        const float current = tone[i];
        if ((previous < 0.0f && current > 0.0f) || (previous > 0.0f && current < 0.0f)) {
            ++crossings;
        }
    }
    NC_CHECK(crossings >= 980 && crossings <= 1130);
}

NC_TEST(AiMarking, ApplyMarksToBuffer) {
    const int sr = 48000, ch = 2;
    // 1 秒主内容（440Hz）
    std::vector<float> audio(static_cast<std::size_t>(sr) * ch);
    for (int i = 0; i < sr; ++i) {
        const float value = 0.3f * std::sin(2.0 * 3.14159265 * 440.0 * i / sr);
        audio[static_cast<std::size_t>(i) * ch] = value;
        audio[static_cast<std::size_t>(i) * ch + 1] = value;
    }
    const std::size_t originalSize = audio.size();
    AiAudioMarkConfig config;
    NC_CHECK(applyAiAudioMarks(audio, ch, sr, config));
    // 前置 0.3s + 原内容 + 后置 0.3s
    NC_CHECK_EQ(audio.size(), originalSize + 57600);
    // 起始段为提示音（880Hz），能量 > 0
    double startRms = 0.0;
    for (int i = 2400 * ch; i < (2400 + 9600) * ch; i += ch) {
        startRms += audio[static_cast<std::size_t>(i)] * audio[static_cast<std::size_t>(i)];
    }
    startRms = std::sqrt(startRms / 9600);
    NC_CHECK(startRms > 0.05);
    // 中段仍是主内容（440Hz 段）
    double midRms = 0.0;
    for (int i = (2400 + 14400 + 38400) * ch; i < (2400 + 14400 + 38400 + 9600) * ch; i += ch) {
        midRms += audio[static_cast<std::size_t>(i)] * audio[static_cast<std::size_t>(i)];
    }
    midRms = std::sqrt(midRms / 9600);
    NC_CHECK(midRms > 0.15);  // 440Hz 0.3 幅值 → RMS ≈ 0.21
    // 参数校验：采样率不匹配 / 缓冲不完整
    std::vector<float> bad(10, 0.0f);
    NC_CHECK(!applyAiAudioMarks(bad, ch, 44100, config));  // 采样率不匹配
    std::vector<float> odd(3, 0.0f);
    AiAudioMarkConfig mono;
    mono.channels = 2;
    NC_CHECK(!applyAiAudioMarks(odd, 2, sr, mono));  // 缓冲不完整（3 % 2 != 0）
}

NC_TEST(AiMarking, ComplianceStatementAndRoleSeparation) {
    const std::string statement = aiMarkingComplianceStatement();
    NC_CHECK(statement.find("标识办法") != std::string::npos);
    NC_CHECK(statement.find("显式标识") != std::string::npos);
    NC_CHECK(statement.find("隐式标识") != std::string::npos);
    NC_CHECK(statement.find("不可关闭") != std::string::npos);
    // 与隐形水印职责分离：本模块文案不涉及「溯源/账号」（那是水印的职责）
    NC_CHECK(statement.find("账号") == std::string::npos);
}

// ---------------------------------------------------------------------------
// 端到端（真实 FFmpeg：元数据写入 + 提示音落盘）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#include <cstdio>
#include <fstream>
#include <iterator>

namespace {

bool writeMarkedFile(const char* path, bool withAudioMarks,
                     const std::vector<AiMetadataEntry>& entries) {
    const int sr = 48000, ch = 2;
    std::vector<float> audio(static_cast<std::size_t>(sr) * ch, 0.0f);  // 静音 1s
    if (withAudioMarks) {
        AiAudioMarkConfig config;
        config.sampleRate = sr;
        config.channels = ch;
        if (!applyAiAudioMarks(audio, ch, sr, config)) return false;
    }
    FFmpegMediaWriter writer;
    WriterConfig writerConfig;
    writerConfig.width = 64;
    writerConfig.height = 48;
    writerConfig.frameRate = fps30();
    writerConfig.includeAudio = true;
    writerConfig.audioSampleRate = sr;
    writerConfig.audioChannels = ch;
    for (const auto& entry : entries) {
        writerConfig.aiMetadata.emplace_back(entry.key, entry.value);
    }
    if (!writer.open(path, writerConfig).isOk()) return false;
    for (int i = 0; i < 40; ++i) {
        OutputVideoFrame frame;
        frame.width = 64;
        frame.height = 48;
        frame.rgba.assign(static_cast<std::size_t>(64) * 48 * 4, 128);
        for (std::size_t j = 3; j < frame.rgba.size(); j += 4) frame.rgba[j] = 255;
        frame.timestamp = timeAtFrame(fps30(), i);
        writer.writeVideoFrame(frame);
    }
    for (std::size_t off = 0; off < audio.size(); off += 1024 * ch) {
        const std::size_t count = std::min(static_cast<std::size_t>(1024 * ch),
                                           audio.size() - off);
        OutputAudioBlock block;
        block.sampleRate = sr;
        block.channels = ch;
        block.start = timeAtFrame(fps30(), 0);
        block.samples.assign(audio.begin() + static_cast<std::ptrdiff_t>(off),
                             audio.begin() + static_cast<std::ptrdiff_t>(off + count));
        writer.writeAudioBlock(block);
    }
    return writer.finish().isOk();
}

double segmentRms(const std::vector<float>& samples, int channels, int startFrame,
                  int frames) {
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < frames; ++i) {
        const std::size_t index = static_cast<std::size_t>(startFrame + i) *
                                  static_cast<std::size_t>(channels);
        if (index >= samples.size()) break;
        sum += samples[index] * samples[index];
        ++count;
    }
    return count > 0 ? std::sqrt(sum / count) : 0.0;
}

}  // namespace

NC_TEST(AiMarkingE2E, ToneMarksWrittenToRealFile) {
    // 对比有/无提示音的两份真实导出（静音主内容 → 差异只来自提示音）
    const std::vector<AiMetadataEntry> entries = buildAiMetadata("NairoClip", "TTS 智能配音");
    NC_CHECK(writeMarkedFile("/tmp/media_gen/ai_mark_with.mp4", true, entries));
    NC_CHECK(writeMarkedFile("/tmp/media_gen/ai_mark_without.mp4", false, entries));
    // 解码对比
    auto decode = [](const char* path) {
        std::vector<float> samples;
        FFmpegMediaReader reader;
        if (!reader.open(path).isOk()) return samples;
        AudioBlock block;
        while (reader.readAudioBlock(block)) {
            samples.insert(samples.end(), block.samples.begin(), block.samples.end());
        }
        reader.close();
        return samples;
    };
    const std::vector<float> with = decode("/tmp/media_gen/ai_mark_with.mp4");
    const std::vector<float> without = decode("/tmp/media_gen/ai_mark_without.mp4");
    NC_CHECK(!with.empty());
    NC_CHECK(!without.empty());
    // 起始段（0.05~0.25s）：有提示音 >> 无提示音
    const double startWith = segmentRms(with, 2, 2400, 9600);
    const double startWithout = segmentRms(without, 2, 2400, 9600);
    std::printf("[实测] 起始段 RMS：有提示音=%.4f 无提示音=%.4f\n", startWith, startWithout);
    NC_CHECK(startWith > 0.05);
    NC_CHECK(startWithout < 0.01);
    // 样本数差值 ≈ 2 段提示音（57600）
    const long long diff = static_cast<long long>(with.size()) -
                           static_cast<long long>(without.size());
    NC_CHECK(diff > 50000 && diff < 65000);
}

NC_TEST(AiMarkingE2E, MetadataReadableFromRealFile) {
    // 元数据写入真实 MP4 后，用 C++ 侧读回（FFmpegMediaReader::probe 不含元数据，
    // 此处直接读文件字节验证 comment 落盘——与 ffprobe 等价的轻量检查）。
    const std::vector<AiMetadataEntry> entries = buildAiMetadata("NairoClip", "TTS 智能配音");
    NC_CHECK(writeMarkedFile("/tmp/media_gen/ai_mark_meta.mp4", false, entries));
    std::ifstream file("/tmp/media_gen/ai_mark_meta.mp4", std::ios::binary);
    NC_CHECK(file.good());
    std::string bytes((std::istreambuf_iterator<char>(file)),
                      std::istreambuf_iterator<char>());
    NC_CHECK(bytes.find("ai_generated=true") != std::string::npos);
    NC_CHECK(bytes.find("NairoClip") != std::string::npos);
}
#endif
