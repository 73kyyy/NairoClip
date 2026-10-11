// =============================================================================
// 隐形水印测试（M2.26，文档第 8 部分 3.1「隐形水印（事后追溯）」）
// =============================================================================
// 覆盖：
//   · 载荷：CRC-16/CCITT 标准向量、finalize/crcValid、账号哈希确定性；
//   · 容量：blockSize/redundancy 影响、容量不足拒绝；
//   · 嵌入/检测往返：无压缩、像素级扰动（加噪/亮度偏移）、多帧一致性；
//   · 参数校验：非法尺寸/配置/CRC 无效载荷不改变像素；
//   · 鲁棒性（真实 FFmpeg）：H.264 编码 → 解码 → 检测（记录真实结果）。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/invisible_watermark.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 生成测试图像（渐变 + 纹理，模拟真实画面）。
std::vector<std::uint8_t> makeImage(int width, int height, int seed = 0) {
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4, 255);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            // 渐变 + 伪随机纹理（防止平坦块让基线量化天然命中）
            const int noise = (x * 7 + y * 13 + seed * 31) % 40;
            rgba[idx + 0] = static_cast<std::uint8_t>((x * 255 / std::max(1, width) + noise) % 256);
            rgba[idx + 1] = static_cast<std::uint8_t>((y * 255 / std::max(1, height) + noise) % 256);
            rgba[idx + 2] = static_cast<std::uint8_t>((x + y + noise) % 256);
            rgba[idx + 3] = 255;
        }
    }
    return rgba;
}

/// 求两图每像素最大差异（用于验证「嵌入确实改变了像素」）。
int maxPixelDiff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    int maxDiff = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (i % 4 == 3) continue;  // 跳过 alpha
        const int diff = std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        if (diff > maxDiff) maxDiff = diff;
    }
    return maxDiff;
}

WatermarkPayload makePayload(std::uint32_t hash = 0xDEADBEEF, std::uint16_t seq = 7) {
    WatermarkPayload payload;
    payload.accountHash = hash;
    payload.exportSequence = seq;
    payload.finalize();
    return payload;
}

}  // namespace

// ---------------------------------------------------------------------------
// CRC 与载荷
// ---------------------------------------------------------------------------

NC_TEST(InvisibleWatermark, Crc16KnownVector) {
    // CRC-16/CCITT-FALSE("123456789") = 0x29B1（标准测试向量）
    const std::string data = "123456789";
    NC_CHECK_EQ(crc16Ccitt(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()),
                std::uint16_t(0x29B1));
    // 空数据 → 初值
    NC_CHECK_EQ(crc16Ccitt(nullptr, 0), std::uint16_t(0xFFFF));
}

NC_TEST(InvisibleWatermark, PayloadFinalizeAndValidate) {
    WatermarkPayload payload;
    payload.accountHash = 0x12345678;
    payload.exportSequence = 42;
    payload.finalize();
    NC_CHECK(payload.crcValid());
    NC_CHECK(payload.valid());
    // 篡改任一字段 → CRC 失效
    WatermarkPayload tampered = payload;
    tampered.accountHash ^= 0x1;
    NC_CHECK(!tampered.crcValid());
    tampered = payload;
    tampered.exportSequence ^= 0x1;
    NC_CHECK(!tampered.crcValid());
}

NC_TEST(InvisibleWatermark, AccountHashDeterministic) {
    const std::uint32_t a = accountHashOf("user@example.com");
    const std::uint32_t b = accountHashOf("user@example.com");
    const std::uint32_t c = accountHashOf("other@example.com");
    NC_CHECK_EQ(a, b);
    NC_CHECK(a != c);
    NC_CHECK_EQ(accountHashOf(""), std::uint32_t(0));
    NC_CHECK(a != 0);
}

// ---------------------------------------------------------------------------
// 容量
// ---------------------------------------------------------------------------

NC_TEST(InvisibleWatermark, CapacityComputation) {
    InvisibleWatermarkConfig config;  // 默认 blockSize=8, redundancy=16
    // 160x120 / 8 = 20x15 = 300 块 / 16 = 18 位（默认配置容量）
    NC_CHECK_EQ(invisibleCapacityBits(160, 120, config), 18);
    NC_CHECK(!invisibleCapacitySufficient(160, 120, config));
    // 但自适应配置（4px 块）下容量充足：40x30 = 1200 块 / 8 = 150 位
    const InvisibleWatermarkConfig adaptive = adaptiveWatermarkConfig(160, 120);
    NC_CHECK_EQ(adaptive.blockSize, 4);
    NC_CHECK(invisibleCapacitySufficient(160, 120, adaptive));
    NC_CHECK(invisibleCapacityBits(160, 120, adaptive) >= 64);
    // 640x480：默认配置充足（80x60 = 4800 块 / 16 = 300 位）
    NC_CHECK(invisibleCapacitySufficient(640, 480, config));
    NC_CHECK_EQ(invisibleCapacityBits(640, 480, config), 300);
    // 提高重复度 → 容量下降
    InvisibleWatermarkConfig heavy = config;
    heavy.redundancy = 32;
    NC_CHECK(invisibleCapacityBits(640, 480, heavy) <
             invisibleCapacityBits(640, 480, config));
    // 参数非法 → 0
    NC_CHECK_EQ(invisibleCapacityBits(0, 120, config), 0);
    InvisibleWatermarkConfig bad = config;
    bad.blockSize = 1;
    NC_CHECK_EQ(invisibleCapacityBits(160, 120, bad), 0);
    // 4x4 超小画面：4/4 × 4/4 = 1 块 → 必然不足
    NC_CHECK(!invisibleCapacitySufficient(4, 4, adaptive));
}

// ---------------------------------------------------------------------------
// 嵌入 / 检测往返
// ---------------------------------------------------------------------------

NC_TEST(InvisibleWatermark, RoundTripLossless) {
    const int w = 160, h = 120;
    auto image = makeImage(w, h);
    const auto original = image;
    const WatermarkPayload payload = makePayload(0xCAFEBABE, 123);
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload));
    // 嵌入确实改变了像素（且幅度有限，保持不可见）
    const int diff = maxPixelDiff(original, image);
    NC_CHECK(diff > 0);   // 有真实修改
    NC_CHECK(diff <= 20);  // 不可见性（QIM 步长 8 → 最大位移约一半步长）
    // 检测
    const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h);
    NC_CHECK(detected.detected);
    NC_CHECK_EQ(detected.payload.accountHash, payload.accountHash);
    NC_CHECK_EQ(detected.payload.exportSequence, payload.exportSequence);
    NC_CHECK_EQ(detected.payload.crc, payload.crc);
    NC_CHECK(detected.bitAgreement > 0.99);
}

NC_TEST(InvisibleWatermark, DetectOnCleanImageFails) {
    const int w = 160, h = 120;
    // 无水印图像 → CRC 校验失败（随机内容极不可能通过）
    auto clean = makeImage(w, h);
    const InvisibleDetectResult detected = detectInvisibleWatermark(clean.data(), w, h);
    NC_CHECK(!detected.detected);
    NC_CHECK(!detected.note.empty());
}

NC_TEST(InvisibleWatermark, SurvivesPixelNoise) {
    const int w = 160, h = 120;
    auto image = makeImage(w, h);
    const WatermarkPayload payload = makePayload(0x11112222, 5);
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload));
    // 加噪（±2 灰度；模拟轻微处理后残留）
    for (std::size_t i = 0; i < image.size(); ++i) {
        if (i % 4 == 3) continue;
        const int noise = static_cast<int>((i * 37) % 5) - 2;
        const int v = static_cast<int>(image[i]) + noise;
        image[i] = static_cast<std::uint8_t>(std::min(255, std::max(0, v)));
    }
    const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h);
    NC_CHECK(detected.detected);
    NC_CHECK_EQ(detected.payload.accountHash, payload.accountHash);
}

NC_TEST(InvisibleWatermark, SurvivesBrightnessShift) {
    const int w = 160, h = 120;
    auto image = makeImage(w, h);
    const WatermarkPayload payload = makePayload(0x33334444, 6);
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload));
    // 整体亮度偏移 +3（设计容差 = ±(quantStep/2 - 1) = ±3 @ step 8）
    for (std::size_t i = 0; i < image.size(); ++i) {
        if (i % 4 == 3) continue;
        const int v = static_cast<int>(image[i]) + 3;
        image[i] = static_cast<std::uint8_t>(std::min(255, std::max(0, v)));
    }
    const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h);
    NC_CHECK(detected.detected);
    NC_CHECK_EQ(detected.payload.accountHash, payload.accountHash);
    // 更大步长（16）→ 容差翻倍（±7）；同一 +3 偏移下更稳
    InvisibleWatermarkConfig wide;
    wide.quantStep = 16.0;
    auto image2 = makeImage(w, h);
    NC_CHECK(embedInvisibleWatermark(image2.data(), w, h, payload, wide));
    for (std::size_t i = 0; i < image2.size(); ++i) {
        if (i % 4 == 3) continue;
        const int v = static_cast<int>(image2[i]) + 6;  // step 16 下 +6 仍在容差内
        image2[i] = static_cast<std::uint8_t>(std::min(255, std::max(0, v)));
    }
    NC_CHECK(detectInvisibleWatermark(image2.data(), w, h, wide).detected);
}

NC_TEST(InvisibleWatermark, DifferentKeysAreIncompatible) {
    const int w = 160, h = 120;
    auto image = makeImage(w, h);
    InvisibleWatermarkConfig embedConfig;
    embedConfig.key = 0xAAAA;
    const WatermarkPayload payload = makePayload(0x55556666, 8);
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload, embedConfig));
    // 用错误密钥检测 → 块置换不同 → 应失败（不误报）
    InvisibleWatermarkConfig wrongConfig;
    wrongConfig.key = 0xBBBB;
    const InvisibleDetectResult wrong =
        detectInvisibleWatermark(image.data(), w, h, wrongConfig);
    NC_CHECK(!wrong.detected);
    // 正确密钥 → 成功
    NC_CHECK(detectInvisibleWatermark(image.data(), w, h, embedConfig).detected);
}

// 回归测试：饱和亮度区域（黑边/白块）的 QIM 嵌入必须「回读有效」。
// 背景（真实缺陷，端到端导出复现）：原实现按「距离最近」选量化级，不考虑像素钳制。
// 纯黑块编码 bit=1 时 -step/+step 距离相等，选了不可达的 -step → 解码回读 bit=0；
// 1080x1920 信箱导出（65% 饱和块）曾导致水印全部不可检出（CRC 失败）。
// 修复：候选量化级做「钳制模拟 + 回读校验」（embedBitInBlock）。
NC_TEST(InvisibleWatermark, SurvivesSaturatedLetterboxRegions) {
    const int w = 320, h = 240;
    // 构造信箱画面：上下 70 行纯黑（模拟横屏素材进竖屏画布），中间为渐变内容
    auto image = makeImage(w, h);
    for (int y = 0; y < h; ++y) {
        if (y >= 70 && y < h - 70) continue;
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            image[idx + 0] = 0;
            image[idx + 1] = 0;
            image[idx + 2] = 0;
        }
    }
    const WatermarkPayload payload = makePayload(0xDEADBEEF, 21);
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload));
    const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h);
    NC_CHECK(detected.detected);  // 修复前：CRC 失败（detected=false）
    NC_CHECK_EQ(detected.payload.accountHash, payload.accountHash);
    NC_CHECK_EQ(detected.payload.exportSequence, payload.exportSequence);
    NC_CHECK(detected.bitAgreement > 0.9);
    // 饱和块被正确抬升（bit=1 需向上移动）而非保持不可达：黑边像素应出现少量非零
    long long raised = 0;
    for (int y = 0; y < 70; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            if (image[idx] != 0) ++raised;
        }
    }
    NC_CHECK(raised > 0);
    // 全黑画面（极端饱和）：容量足够，但受黑块可达性限制——只要存在可抬升块，
    // 检测仍应成功；纯黑帧应能承载水印（全部块抬升）。
    std::vector<std::uint8_t> black(static_cast<std::size_t>(w) * h * 4, 255);
    for (std::size_t i = 0; i < black.size(); i += 4) {
        black[i] = 0;
        black[i + 1] = 0;
        black[i + 2] = 0;
    }
    const WatermarkPayload blackPayload = makePayload(0x01020304, 3);
    NC_CHECK(embedInvisibleWatermark(black.data(), w, h, blackPayload));
    const InvisibleDetectResult blackDetected = detectInvisibleWatermark(black.data(), w, h);
    NC_CHECK(blackDetected.detected);
    NC_CHECK_EQ(blackDetected.payload.accountHash, blackPayload.accountHash);
}

NC_TEST(InvisibleWatermark, OverwriteExistingPayload) {
    const int w = 160, h = 120;
    auto image = makeImage(w, h);
    const WatermarkPayload first = makePayload(0xAAAAAAAA, 1);
    const WatermarkPayload second = makePayload(0xBBBBBBBB, 2);
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, first));
    NC_CHECK(detectInvisibleWatermark(image.data(), w, h).payload.accountHash ==
             first.accountHash);
    // 二次嵌入 → 覆盖为第二载荷
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, second));
    const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h);
    NC_CHECK(detected.detected);
    NC_CHECK_EQ(detected.payload.accountHash, second.accountHash);
    NC_CHECK_EQ(detected.payload.exportSequence, second.exportSequence);
}

// ---------------------------------------------------------------------------
// 参数校验与防御
// ---------------------------------------------------------------------------

NC_TEST(InvisibleWatermark, RejectsInvalidArguments) {
    const int w = 160, h = 120;
    auto image = makeImage(w, h);
    const auto original = image;
    const WatermarkPayload payload = makePayload();
    // 空指针
    NC_CHECK(!embedInvisibleWatermark(nullptr, w, h, payload));
    NC_CHECK(!embedInvisibleWatermark(image.data(), 0, h, payload));
    NC_CHECK(!embedInvisibleWatermark(image.data(), w, -1, payload));
    // 非法配置
    InvisibleWatermarkConfig bad;
    bad.blockSize = 2;
    NC_CHECK(!embedInvisibleWatermark(image.data(), w, h, payload, bad));
    // CRC 无效载荷
    WatermarkPayload badPayload = payload;
    badPayload.crc ^= 0xFFFF;
    NC_CHECK(!embedInvisibleWatermark(image.data(), w, h, badPayload));
    // 像素未被改动
    NC_CHECK_EQ(maxPixelDiff(original, image), 0);
    // 检测侧参数校验
    NC_CHECK(!detectInvisibleWatermark(nullptr, w, h).detected);
    NC_CHECK(!detectInvisibleWatermark(image.data(), 0, h).detected);
}

NC_TEST(InvisibleWatermark, RejectsInsufficientCapacity) {
    // 16x16 → 自适应 4px 块后 4x4 = 16 块 < 64*1 → 拒绝（且不改像素）
    const int w = 16, h = 16;
    auto image = makeImage(w, h);
    const auto original = image;
    NC_CHECK(!embedInvisibleWatermark(image.data(), w, h, makePayload()));
    NC_CHECK_EQ(maxPixelDiff(original, image), 0);
    const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h);
    NC_CHECK(!detected.detected);
    NC_CHECK(detected.note.find("容量") != std::string::npos);
}

NC_TEST(InvisibleWatermark, ConfigVariations) {
    const int w = 320, h = 240;
    auto image = makeImage(w, h);
    const WatermarkPayload payload = makePayload(0x99887766, 99);
    // 更大块 + 更大步长（更鲁棒：抗更大亮度偏移）
    // 容量核算：320/16 × 240/16 = 300 块；redundancy=4 → 75 位 ≥ 64 ✓
    InvisibleWatermarkConfig robust;
    robust.blockSize = 16;
    robust.redundancy = 4;
    robust.quantStep = 16.0;
    NC_CHECK(invisibleCapacitySufficient(w, h, robust));
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload, robust));
    const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h, robust);
    NC_CHECK(detected.detected);
    NC_CHECK_EQ(detected.payload.accountHash, payload.accountHash);
    NC_CHECK_EQ(detected.payload.exportSequence, payload.exportSequence);
    // 高容量小步长配置：320/8 × 240/8 = 1200 块 / 8 = 150 位 ✓
    InvisibleWatermarkConfig highCapacity;
    highCapacity.blockSize = 8;
    highCapacity.redundancy = 8;
    highCapacity.quantStep = 8.0;
    NC_CHECK(invisibleCapacitySufficient(w, h, highCapacity));
    auto image2 = makeImage(w, h);
    NC_CHECK(embedInvisibleWatermark(image2.data(), w, h, payload, highCapacity));
    NC_CHECK(detectInvisibleWatermark(image2.data(), w, h, highCapacity).detected);
    // 容量不足配置（小画面 + 高冗余无法承载 → 正确拒绝）
    // 16x16 自适应 4px 块 = 16 块；redundancy 收敛后仍 < 64*1 需求
    InvisibleWatermarkConfig insufficient;
    insufficient.blockSize = 4;
    insufficient.redundancy = 2;
    NC_CHECK(!invisibleCapacitySufficient(16, 16, insufficient));
    std::vector<std::uint8_t> tiny(16 * 16 * 4, 128);
    const auto original = tiny;
    NC_CHECK(!embedInvisibleWatermark(tiny.data(), 16, 16, payload, insufficient));
    NC_CHECK_EQ(maxPixelDiff(original, tiny), 0);  // 拒绝时不改像素
    // 说明文案
    NC_CHECK(std::string(invisibleWatermarkPurpose()).find("溯源") != std::string::npos);
}

NC_TEST(InvisibleWatermark, MultiFrameConsistency) {
    // 模拟视频多帧：每帧嵌入同一载荷 → 每帧独立可检出
    const int w = 160, h = 120;
    const WatermarkPayload payload = makePayload(0x77778888, 12);
    for (int frame = 0; frame < 5; ++frame) {
        auto image = makeImage(w, h, frame);  // 每帧内容不同
        NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload));
        const InvisibleDetectResult detected = detectInvisibleWatermark(image.data(), w, h);
        NC_CHECK(detected.detected);
        NC_CHECK_EQ(detected.payload.accountHash, payload.accountHash);
    }
}

// ---------------------------------------------------------------------------
// 真实 FFmpeg 压缩往返（鲁棒性实测）
// ---------------------------------------------------------------------------

#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/media_ffmpeg_writer.h"
#include <fstream>

NC_TEST(InvisibleWatermark, SurvivesH264RoundTrip) {
    const int w = 320, h = 240;
    const WatermarkPayload payload = makePayload(0xABCDEF01, 42);
    // 1) 生成一帧水印图像 → 编码为单帧视频（真实 H.264）
    auto image = makeImage(w, h);
    NC_CHECK(embedInvisibleWatermark(image.data(), w, h, payload));
    const std::string path = "/tmp/wm_roundtrip.mp4";
    std::remove(path.c_str());
    {
        FFmpegMediaWriter writer;
        WriterConfig config;
        config.width = w;
        config.height = h;
        config.frameRate = fps30();
        config.crf = 18;  // 较高画质（水印设计目标：正常码率下可检出）
        config.includeAudio = false;
        NC_CHECK(writer.open(path, config).isOk());
        for (int i = 0; i < 10; ++i) {
            OutputVideoFrame frame;
            frame.timestamp = timeAtFrame(fps30(), i);
            frame.width = w;
            frame.height = h;
            frame.rgba = image;
            NC_CHECK(writer.writeVideoFrame(frame).isOk());
        }
        NC_CHECK(writer.finish().isOk());
    }
    // 2) 真实解码回读 → 检测
    FFmpegMediaReader reader;
    NC_CHECK(reader.open(path).isOk());
    VideoFrame frame;
    NC_CHECK(reader.readVideoFrame(frame));
    reader.close();
    const InvisibleDetectResult detected =
        detectInvisibleWatermark(frame.pixels.data(), frame.width, frame.height);
    // 记录实测结果（H.264 压缩对 QIM 水印的破坏程度是客观事实）
    std::printf("[实测] H.264 CRF18 往返：detected=%d agreement=%.3f note=%s\n",
                detected.detected ? 1 : 0, detected.bitAgreement, detected.note.c_str());
    // 水印设计目标：CRF18（高画质）下应可检出
    NC_CHECK(detected.detected);
    NC_CHECK_EQ(detected.payload.accountHash, payload.accountHash);
    NC_CHECK_EQ(detected.payload.exportSequence, payload.exportSequence);
}
#endif
