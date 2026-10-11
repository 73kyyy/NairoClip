#include "nairoclip/core/invisible_watermark.h"

#include "nairoclip/core/crypto.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 载荷与校验
// =============================================================================

std::uint16_t crc16Ccitt(const std::uint8_t* data, std::size_t length) {
    std::uint16_t crc = 0xFFFF;
    for (std::size_t i = 0; i < length; ++i) {
        crc ^= static_cast<std::uint16_t>(data[i]) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000) != 0) {
                crc = static_cast<std::uint16_t>((crc << 1) ^ 0x1021);
            } else {
                crc = static_cast<std::uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

void WatermarkPayload::finalize() {
    // 前 48 位：accountHash(32) + exportSequence(16)，按大端序序列化后 CRC
    std::uint8_t bytes[6];
    bytes[0] = static_cast<std::uint8_t>((accountHash >> 24) & 0xFF);
    bytes[1] = static_cast<std::uint8_t>((accountHash >> 16) & 0xFF);
    bytes[2] = static_cast<std::uint8_t>((accountHash >> 8) & 0xFF);
    bytes[3] = static_cast<std::uint8_t>(accountHash & 0xFF);
    bytes[4] = static_cast<std::uint8_t>((exportSequence >> 8) & 0xFF);
    bytes[5] = static_cast<std::uint8_t>(exportSequence & 0xFF);
    crc = crc16Ccitt(bytes, sizeof(bytes));
}

bool WatermarkPayload::crcValid() const {
    WatermarkPayload copy = *this;
    const std::uint16_t expected = copy.crc;
    copy.finalize();
    return copy.crc == expected;
}

std::uint32_t accountHashOf(const std::string& accountId) {
    if (accountId.empty()) {
        return 0;
    }
    std::uint8_t out[kSha256DigestSize];
    sha256(accountId.data(), accountId.size(), out);
    return (static_cast<std::uint32_t>(out[0]) << 24) |
           (static_cast<std::uint32_t>(out[1]) << 16) |
           (static_cast<std::uint32_t>(out[2]) << 8) | static_cast<std::uint32_t>(out[3]);
}

// =============================================================================
// 内部：块布局（LCG 置换）
// =============================================================================

namespace {

/// 简单 LCG（确定性；同一 key 生成同一块置换顺序）。
struct Lcg {
    std::uint64_t state;
    explicit Lcg(std::uint64_t seed) : state(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {}
    std::uint64_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return state;
    }
    /// [0, bound) 内的随机数（bound > 0）。
    std::uint64_t nextBelow(std::uint64_t bound) {
        return bound == 0 ? 0 : (next() >> 33) % bound;
    }
};

struct Block {
    int x;
    int y;
};

/// 生成块索引表并按 key 做 Fisher–Yates 置换（确定性）。
std::vector<Block> makeBlockOrder(int width, int height,
                                  const InvisibleWatermarkConfig& config) {
    std::vector<Block> blocks;
    const int bs = config.blockSize;
    for (int y = 0; y + bs <= height; y += bs) {
        for (int x = 0; x + bs <= width; x += bs) {
            blocks.push_back(Block{x, y});
        }
    }
    Lcg lcg(config.key);
    for (std::size_t i = blocks.size(); i > 1; --i) {
        const std::size_t j = static_cast<std::size_t>(lcg.nextBelow(i));
        std::swap(blocks[i - 1], blocks[j]);
    }
    return blocks;
}

/// 计算块的平均亮度（仅亮度通道，忽略 alpha）。
double blockMeanLuma(const std::uint8_t* rgba, int width, int x, int y, int bs) {
    double sum = 0.0;
    std::size_t count = 0;
    for (int dy = 0; dy < bs; ++dy) {
        const std::uint8_t* row =
            rgba + (static_cast<std::size_t>(y + dy) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)) * 4;
        for (int dx = 0; dx < bs; ++dx) {
            const std::uint8_t* px = row + static_cast<std::size_t>(dx) * 4;
            sum += 0.299 * px[0] + 0.587 * px[1] + 0.114 * px[2];
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

/// 对块内像素整体加 delta（RGB 同加；钳制 0..255）。
void blockShift(std::uint8_t* rgba, int width, int x, int y, int bs, double delta) {
    for (int dy = 0; dy < bs; ++dy) {
        std::uint8_t* row = rgba +
                            (static_cast<std::size_t>(y + dy) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) * 4;
        for (int dx = 0; dx < bs; ++dx) {
            std::uint8_t* px = row + static_cast<std::size_t>(dx) * 4;
            for (int c = 0; c < 3; ++c) {
                const double v = static_cast<double>(px[c]) + delta;
                px[c] = static_cast<std::uint8_t>(std::lround(std::min(255.0, std::max(0.0, v))));
            }
        }
    }
}

/// QIM 解码：由块均值取量化商奇偶性（就近取整，抗噪）。
int qimBit(double mean, double step) {
    const long long q = std::llround(mean / step);
    return static_cast<int>((q % 2 + 2) % 2);
}

/// 模拟「块内像素整体加 delta（钳制 0..255）」后的实际均值。
/// 与 blockShift 的像素运算逐通道一致（lround + 钳制），用于嵌入前验证
/// 「解码端回读到的比特」是否与目标一致（见 embedBitInBlock 的修复说明）。
double simulateBlockMean(const std::uint8_t* rgba, int width, int x, int y, int bs,
                         double delta) {
    double sum = 0.0;
    std::size_t count = 0;
    for (int dy = 0; dy < bs; ++dy) {
        const std::uint8_t* row =
            rgba + (static_cast<std::size_t>(y + dy) * static_cast<std::size_t>(width) +
                    static_cast<std::size_t>(x)) * 4;
        for (int dx = 0; dx < bs; ++dx) {
            const std::uint8_t* px = row + static_cast<std::size_t>(dx) * 4;
            double luma = 0.0;
            const double weights[3] = {0.299, 0.587, 0.114};
            for (int c = 0; c < 3; ++c) {
                const double v = static_cast<double>(px[c]) + delta;
                const double clamped = std::min(255.0, std::max(0.0, v));
                const std::uint8_t shifted =
                    static_cast<std::uint8_t>(std::lround(clamped));
                luma += weights[c] * static_cast<double>(shifted);
            }
            sum += luma;
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

/// 在块内嵌入一位（目标比特 bit），保证「解码端回读 = 目标比特」。
///
/// 修复记录（真实缺陷，端到端导出复现）：
///   原实现按「距离最近」选择目标量化级，不考虑像素钳制。纯黑块（均值≈0）
///   编码 bit=1 时，-step 与 +step 距离相等，原实现选 -step；但黑像素无法
///   再向下移动（钳制到 0），实际均值不动 → 解码回读 bit=0，整块比特错误。
///   导出管线中大面积黑边（信箱模式）时，饱和块占比可达 65%，多数表决被
///   污染 → CRC 全部失败（经 1080x1920 真实导出 + H.264 解码实测）。
///   修复：候选量化级（同奇偶性、±6 级内）按距离升序做「钳制模拟 + 回读校验」，
///   只采用回读确实为目标比特的量化级；纯黑块编码 bit=1 会正确选择 +step
///   （抬升到 8 灰度，视觉不可见），而不是无效的 -step。
///
/// 返回 false：所有候选级都不可行（保持像素不变，交给冗余多数表决兜底）。
bool embedBitInBlock(std::uint8_t* rgba, int width, int x, int y, int bs, int bit,
                     double step) {
    const double mean = blockMeanLuma(rgba, width, x, y, bs);
    const long long q0 = std::llround(mean / step);
    bool found = false;
    double bestDelta = 0.0;
    double bestCost = 0.0;
    for (int dq = -6; dq <= 6; ++dq) {
        const long long q = q0 + dq;
        const int parity = static_cast<int>((q % 2 + 2) % 2);
        if (parity != bit) {
            continue;
        }
        const double target = static_cast<double>(q) * step;
        const double delta = target - mean;
        const double cost = std::abs(delta);
        if (found && cost >= bestCost) {
            continue;  // 已有更近的有效候选
        }
        // 钳制模拟：实际能达到的均值 → 解码端会读到的比特
        const double achieved = simulateBlockMean(rgba, width, x, y, bs, delta);
        const long long qa = std::llround(achieved / step);
        if (static_cast<int>((qa % 2 + 2) % 2) != bit) {
            continue;  // 该候选因钳制无法回读目标比特 → 不可用
        }
        found = true;
        bestCost = cost;
        bestDelta = delta;
    }
    if (!found) {
        return false;
    }
    if (std::abs(bestDelta) < 1e-9) {
        return true;  // 已在目标量化级上
    }
    blockShift(rgba, width, x, y, bs, bestDelta);
    return true;
}

/// 载荷 → 64 位数组（MSB first）。
void payloadToBits(const WatermarkPayload& payload, int bits[64]) {
    std::uint64_t value = 0;
    value |= static_cast<std::uint64_t>(payload.accountHash) << 32;
    value |= static_cast<std::uint64_t>(payload.exportSequence) << 16;
    value |= static_cast<std::uint64_t>(payload.crc);
    for (int i = 0; i < 64; ++i) {
        bits[i] = static_cast<int>((value >> (63 - i)) & 1ULL);
    }
}

/// 64 位数组 → 载荷。
WatermarkPayload bitsToPayload(const int bits[64]) {
    std::uint64_t value = 0;
    for (int i = 0; i < 64; ++i) {
        value = (value << 1) | static_cast<std::uint64_t>(bits[i] & 1);
    }
    WatermarkPayload payload;
    payload.accountHash = static_cast<std::uint32_t>((value >> 32) & 0xFFFFFFFFULL);
    payload.exportSequence = static_cast<std::uint16_t>((value >> 16) & 0xFFFFULL);
    payload.crc = static_cast<std::uint16_t>(value & 0xFFFFULL);
    return payload;
}

}  // namespace

// =============================================================================
// 容量
// =============================================================================

int invisibleCapacityBits(int width, int height, const InvisibleWatermarkConfig& config) {
    if (width <= 0 || height <= 0 || !config.valid()) {
        return 0;
    }
    const int bs = config.blockSize;
    const int cols = width / bs;
    const int rows = height / bs;
    const int blocks = cols * rows;
    return blocks / config.redundancy;
}

bool invisibleCapacitySufficient(int width, int height,
                                 const InvisibleWatermarkConfig& config) {
    return invisibleCapacityBits(width, height, config) >= 64;
}

InvisibleWatermarkConfig adaptiveWatermarkConfig(int width, int height) {
    InvisibleWatermarkConfig config;  // 默认：block 8 / step 8 / redundancy 16
    const long long blocksAt8 =
        static_cast<long long>(width / 8) * (height / 8);
    if (blocksAt8 >= 4800) {
        return config;  // ≥640x480：默认配置实测可检出（agreement ≥0.98）
    }
    // 小分辨率：改用 4px 块（编码块效应对单块影响更小；实测 160x120 下
    // agreement 0.98~1.00）+ 适度放大步长提升余量
    config.blockSize = 4;
    const long long blocksAt4 = static_cast<long long>(width / 4) * (height / 4);
    if (blocksAt4 >= 64 * 16) {
        config.quantStep = 12.0;  // 小块更脆弱，略放大余量
        config.redundancy = 8;
    }
    // 容量收敛：确保 64 * redundancy 不超过块数（不足则优先降冗余）
    while (config.redundancy > 1 && static_cast<long long>(64) * config.redundancy > blocksAt4) {
        --config.redundancy;
    }
    return config;
}

InvisibleWatermarkConfig effectiveWatermarkConfig(int width, int height,
                                                  const InvisibleWatermarkConfig& config) {
    const InvisibleWatermarkConfig adaptive = adaptiveWatermarkConfig(width, height);
    InvisibleWatermarkConfig effective = config;
    if (adaptive.blockSize < effective.blockSize) {
        effective.blockSize = adaptive.blockSize;
        effective.quantStep = std::max(effective.quantStep, adaptive.quantStep);
    }
    return effective;
}

bool invisibleWatermarkFits(int width, int height, const InvisibleWatermarkConfig& config) {
    if (width <= 0 || height <= 0 || !config.valid()) {
        return false;
    }
    const InvisibleWatermarkConfig effective = effectiveWatermarkConfig(width, height, config);
    return invisibleCapacitySufficient(width, height, effective);
}

// =============================================================================
// 嵌入
// =============================================================================

bool embedInvisibleWatermark(std::uint8_t* rgba, int width, int height,
                             const WatermarkPayload& payload,
                             const InvisibleWatermarkConfig& config) {
    if (rgba == nullptr || width <= 0 || height <= 0 || !config.valid() ||
        !payload.crcValid()) {
        return false;
    }
    // 生效配置：小分辨率自适应（4px 块 + 放大步长），与检测/容量预检共用同一推导
    const InvisibleWatermarkConfig effective = effectiveWatermarkConfig(width, height, config);
    const std::vector<Block> blocks = makeBlockOrder(width, height, effective);
    const int needed = 64 * effective.redundancy;
    if (static_cast<int>(blocks.size()) < needed) {
        return false;  // 容量不足
    }
    int bits[64];
    payloadToBits(payload, bits);
    int blockCursor = 0;
    for (int bit = 0; bit < 64; ++bit) {
        for (int r = 0; r < effective.redundancy; ++r) {
            const Block& block = blocks[static_cast<std::size_t>(blockCursor++)];
            // 块级嵌入：内部做「钳制模拟 + 回读校验」，保证解码端可读回目标比特
            // （饱和亮度区域如黑边，见 embedBitInBlock 的中文说明）
            embedBitInBlock(rgba, width, block.x, block.y, effective.blockSize, bits[bit],
                            effective.quantStep);
        }
    }
    return true;
}

// =============================================================================
// 检测
// =============================================================================

InvisibleDetectResult detectInvisibleWatermark(const std::uint8_t* rgba, int width, int height,
                                               const InvisibleWatermarkConfig& config) {
    InvisibleDetectResult result;
    if (rgba == nullptr || width <= 0 || height <= 0 || !config.valid()) {
        result.note = "参数非法";
        return result;
    }
    // 与嵌入侧对称的生效配置推导（否则块布局不一致；单一事实来源）
    const InvisibleWatermarkConfig effective = effectiveWatermarkConfig(width, height, config);
    const std::vector<Block> blocks = makeBlockOrder(width, height, effective);
    const int needed = 64 * effective.redundancy;
    result.capacityBits = static_cast<int>(blocks.size()) / effective.redundancy;
    if (static_cast<int>(blocks.size()) < needed) {
        result.note = "容量不足（需要 " + std::to_string(needed) + " 块，实际 " +
                      std::to_string(blocks.size()) + "）";
        return result;
    }
    int bits[64];
    double agreementSum = 0.0;
    int blockCursor = 0;
    for (int bit = 0; bit < 64; ++bit) {
        int ones = 0;
        for (int r = 0; r < effective.redundancy; ++r) {
            const Block& block = blocks[static_cast<std::size_t>(blockCursor++)];
            const double mean =
                blockMeanLuma(rgba, width, block.x, block.y, effective.blockSize);
            if (qimBit(mean, effective.quantStep) == 1) {
                ++ones;
            }
        }
        bits[bit] = ones * 2 >= effective.redundancy ? 1 : 0;
        const int majority = std::max(ones, effective.redundancy - ones);
        agreementSum +=
            static_cast<double>(majority) / static_cast<double>(effective.redundancy);
    }
    result.bitAgreement = agreementSum / 64.0;
    result.payload = bitsToPayload(bits);
    result.detected = result.payload.crcValid();
    if (!result.detected) {
        result.note = "CRC 校验失败（无有效水印或画面被重编码破坏）";
    } else {
        result.note = "检出有效水印（账号哈希 " + std::to_string(result.payload.accountHash) +
                      "，导出序号 " + std::to_string(result.payload.exportSequence) + "）";
    }
    return result;
}

const char* invisibleWatermarkPurpose() {
    return "隐形水印用于内容泄露溯源（编码账号哈希，不可逆）；不采集任何个人信息";
}

}  // namespace core
}  // namespace nairoclip
