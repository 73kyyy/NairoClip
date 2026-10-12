#include "nairoclip/core/chroma_key.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nairoclip {
namespace core {

namespace {

/// BT.601 Cb（相对中性轴 128 的偏移）。
double cbOf(double r, double g, double b) {
    return -0.168736 * r - 0.331264 * g + 0.5 * b;
}

/// BT.601 Cr（相对中性轴 128 的偏移）。
double crOf(double r, double g, double b) {
    return 0.5 * r - 0.418688 * g - 0.081312 * b;
}

}  // namespace

// =============================================================================
// 名称与解析
// =============================================================================

std::uint32_t chromaKeyColorFromString(const char* text) {
    if (text == nullptr) {
        return 0xFFFFFFFFu;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (s == "green" || s == "绿" || s == "#00ff00" || s == "00ff00") return 0x00FF00u;
    if (s == "blue" || s == "蓝" || s == "#0000ff" || s == "0000ff") return 0x0000FFu;
    if (s == "magenta" || s == "洋红" || s == "#ff00ff" || s == "ff00ff") return 0xFF00FFu;
    if (s.rfind("0x", 0) == 0) {
        s = s.substr(2);
    } else if (!s.empty() && s[0] == '#') {
        s = s.substr(1);
    }
    if (s.size() != 6) {
        return 0xFFFFFFFFu;
    }
    std::uint32_t value = 0;
    for (char c : s) {
        int digit = -1;
        if (c >= '0' && c <= '9') {
            digit = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            digit = c - 'a' + 10;
        } else {
            return 0xFFFFFFFFu;
        }
        value = (value << 4) | static_cast<std::uint32_t>(digit);
    }
    return value;
}

const char* chromaKeyColorName(std::uint32_t rgb) {
    switch (rgb & 0xFFFFFFu) {
        case 0x00FF00u: return "green";
        case 0x0000FFu: return "blue";
        case 0xFF00FFu: return "magenta";
        default: return "custom";
    }
}

// =============================================================================
// 内部：键控几何（方向投影）
// =============================================================================

namespace {

/// 键控参考（键控色的色度向量及其长度）。
struct KeyGeometry {
    double cb = 0.0;   ///< 键控色 Cb 偏移（相对 128）
    double cr = 0.0;   ///< 键控色 Cr 偏移（相对 128）
    double len = 0.0;  ///< |v_key|（= sqrt(cb² + cr²)）
};

KeyGeometry keyGeometryOf(std::uint32_t rgb) {
    const double r = static_cast<double>((rgb >> 16) & 0xFF);
    const double g = static_cast<double>((rgb >> 8) & 0xFF);
    const double b = static_cast<double>(rgb & 0xFF);
    KeyGeometry geo;
    geo.cb = cbOf(r, g, b);
    geo.cr = crOf(r, g, b);
    geo.len = std::sqrt(geo.cb * geo.cb + geo.cr * geo.cr);
    return geo;
}

/// 键控相似度 a（0..1；1 = 与键控色同色相且幅度相当）。
/// - 度量：a = cos(v_px, v_key) × sqrt(min(|v_px| / |v_key|, 1))；
///   · 方向余弦项：区分「同色相」与「异色相」（黄绿衣物与绿幕夹角 > 40° → 分离）；
///   · 幅度软阈值项（sqrt）：浅绿（白光冲淡的绿幕，长度减半）保留 ~0.71 相似度
///     → 仍被完整抠除；而阴影绿（长度 ~0.33 倍）保留 ~0.57 → 自然保护（不被误抠）；
/// - 近灰像素（|v_px| ≈ 0）无色彩信息 → a = 0（全保留）；
/// - 反向色（红/肤/品红）→ cos < 0 → a = 0（全保留）。
double keySimilarity(double cb, double cr, const KeyGeometry& geo) {
    const double lenSq = geo.cb * geo.cb + geo.cr * geo.cr;
    if (lenSq < 1.0) {
        return 0.0;  // 键控色接近中性（无色彩）→ 无法键控
    }
    const double pxLenSq = cb * cb + cr * cr;
    if (pxLenSq < 1.0) {
        return 0.0;  // 像素近灰（无色彩）→ 不是键控色
    }
    const double pxLen = std::sqrt(pxLenSq);
    const double keyLen = std::sqrt(lenSq);
    const double cos = (cb * geo.cb + cr * geo.cr) / (pxLen * keyLen);
    if (cos <= 0.0) {
        return 0.0;  // 反向色 → 全保留
    }
    const double ratio = pxLen < keyLen ? pxLen / keyLen : 1.0;
    const double a = cos * std::sqrt(ratio);
    if (a < 0.0) return 0.0;
    if (a > 1.0) return 1.0;
    return a;
}

/// 由相似度计算前景覆盖率（0..1）。
double coverageFromSimilarity(double a, const ChromaKey& key) {
    const double d = 1.0 - a;  // 键控距离
    if (d <= key.innerThreshold) {
        return 0.0;
    }
    if (d >= key.outerThreshold) {
        return 1.0;
    }
    double t = (d - key.innerThreshold) / (key.outerThreshold - key.innerThreshold);
    t = t < 0.0 ? 0.0 : (t > 1.0 ? 1.0 : t);
    return t * t * (3.0 - 2.0 * t);  // smoothstep（边缘自然）
}

}  // namespace

// =============================================================================
// 覆盖率
// =============================================================================

double chromaKeyCoverageAt(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                           const ChromaKey& key) {
    if (!key.valid()) {
        return -1.0;
    }
    const KeyGeometry geo = keyGeometryOf(key.keyColorRgb);
    const double a = keySimilarity(cbOf(r, g, b), crOf(r, g, b), geo);
    return coverageFromSimilarity(a, key);
}

double chromaKeyCoverageRatio(const std::uint8_t* rgba, int width, int height,
                              const ChromaKey& key, int step) {
    if (rgba == nullptr || width <= 0 || height <= 0 || step < 1 || !key.valid()) {
        return -1.0;
    }
    const KeyGeometry geo = keyGeometryOf(key.keyColorRgb);
    double sum = 0.0;
    std::size_t count = 0;
    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            const std::size_t idx =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4;
            const double a = keySimilarity(cbOf(rgba[idx], rgba[idx + 1], rgba[idx + 2]),
                                           crOf(rgba[idx], rgba[idx + 1], rgba[idx + 2]), geo);
            sum += coverageFromSimilarity(a, key);
            ++count;
        }
    }
    return count > 0 ? sum / static_cast<double>(count) : -1.0;
}

// =============================================================================
// 帧级应用
// =============================================================================

bool applyChromaKeyToFrame(std::uint8_t* rgba, int width, int height, const ChromaKey& key,
                           ChromaKeyBlendMode mode, std::uint32_t backgroundRgba) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!key.valid()) {
        return false;
    }
    const KeyGeometry geo = keyGeometryOf(key.keyColorRgb);
    if (geo.len < 1.0) {
        return false;  // 键控色接近中性 → 无法键控（防御；默认绿幕不触发）
    }
    const std::uint8_t bgR = static_cast<std::uint8_t>((backgroundRgba >> 24) & 0xFF);
    const std::uint8_t bgG = static_cast<std::uint8_t>((backgroundRgba >> 16) & 0xFF);
    const std::uint8_t bgB = static_cast<std::uint8_t>((backgroundRgba >> 8) & 0xFF);
    const std::uint8_t bgA = static_cast<std::uint8_t>(backgroundRgba & 0xFF);

    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::uint8_t* px = rgba + i * 4;
        const double r = static_cast<double>(px[0]);
        const double g = static_cast<double>(px[1]);
        const double b = static_cast<double>(px[2]);
        const double cb = cbOf(r, g, b);
        const double cr = crOf(r, g, b);
        const double a = keySimilarity(cb, cr, geo);
        const double coverage = coverageFromSimilarity(a, key);

        if (mode == ChromaKeyBlendMode::Transparent) {
            if (coverage < 0.999999) {
                const double newAlpha = static_cast<double>(px[3]) * coverage;
                px[3] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, newAlpha + 0.5)));
            }
        } else {
            // Background：被抠像素（coverage < 1）按前景权重混入背景色
            if (coverage < 0.999999) {
                const double fg = coverage;
                const double bg = 1.0 - coverage;
                px[0] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, r * fg + bgR * bg + 0.5)));
                px[1] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, g * fg + bgG * bg + 0.5)));
                px[2] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, b * fg + bgB * bg + 0.5)));
                px[3] = bgA;
            }
        }

        // 溢色抑制（通道法，行业标准做法）：仅对「保留为前景」（覆盖率 > 0.5）
        // 的像素，把键控主通道超出其余通道的溢色部分收拢回中性（消除键控色反光）。
        //  - 绿色键控：G 超出 max(R,B) 的部分按强度收拢；
        //  - 蓝色键控：B 超出 max(R,G) 的部分按强度收拢；
        //  - 洋红键控：min(R,B) 超出 G 的部分对 R/B 同步收拢；
        //  - 红色键控：R 超出 max(G,B) 的部分按强度收拢。
        if (key.spillSuppression > 0.0 && coverage > 0.5) {
            const int kr = static_cast<int>((key.keyColorRgb >> 16) & 0xFF);
            const int kg = static_cast<int>((key.keyColorRgb >> 8) & 0xFF);
            const int kb = static_cast<int>(key.keyColorRgb & 0xFF);
            const double keep = 1.0 - key.spillSuppression;
            int r = px[0];
            int g = px[1];
            int b = px[2];
            if (kg >= kr && kg >= kb) {
                // 绿系：G 超出 max(R,B) 的溢色
                const int limit = r > b ? r : b;
                if (g > limit) {
                    g = limit + static_cast<int>((g - limit) * keep + 0.5);
                }
            } else if (kb >= kr && kb >= kg) {
                // 蓝系：B 超出 max(R,G) 的溢色
                const int limit = r > g ? r : g;
                if (b > limit) {
                    b = limit + static_cast<int>((b - limit) * keep + 0.5);
                }
            } else if (kr >= kg && kr >= kb) {
                if (kb > kg) {
                    // 洋红系（R、B 高而 G 低）：min(R,B) 超出 G 的溢色
                    const int limit = r < b ? r : b;
                    if (limit > g) {
                        const int cut = static_cast<int>((limit - g) * key.spillSuppression + 0.5);
                        r -= cut;
                        b -= cut;
                    }
                } else if (r > g && r > b) {
                    // 红系：R 超出 max(G,B) 的溢色
                    const int limit = g > b ? g : b;
                    if (r > limit) {
                        r = limit + static_cast<int>((r - limit) * keep + 0.5);
                    }
                }
            }
            px[0] = static_cast<std::uint8_t>(r < 0 ? 0 : (r > 255 ? 255 : r));
            px[1] = static_cast<std::uint8_t>(g < 0 ? 0 : (g > 255 ? 255 : g));
            px[2] = static_cast<std::uint8_t>(b < 0 ? 0 : (b > 255 ? 255 : b));
        }
    }
    return true;
}

const char* chromaKeyPurpose() {
    return "色度抠像（绿幕）：在本地逐像素分离键控色背景（无需联网与模型）；为 Pro 权益";
}

// =============================================================================
// 质量档位与门控
// =============================================================================

bool mattingQualityFromInt(int value, MattingQuality& out) {
    switch (value) {
        case 0: out = MattingQuality::Basic; return true;
        case 1: out = MattingQuality::HighPrecision; return true;
        default: return false;
    }
}

const char* mattingQualityName(MattingQuality quality) {
    switch (quality) {
        case MattingQuality::Basic: return "basic";
        case MattingQuality::HighPrecision: return "high_precision";
    }
    return "basic";
}

ChromaKey chromaKeyForQuality(const ChromaKey& key, MattingQuality quality) {
    ChromaKey effective = key;
    if (quality == MattingQuality::Basic) {
        // 基础版（文档「精度 85%」心智）：过渡带至少 0.30 宽（处理更粗）、
        // 溢色抑制上限 0.4（更快、更保守）。
        effective.spillSuppression = std::min(0.4, key.spillSuppression);
        if (effective.outerThreshold - effective.innerThreshold < 0.30) {
            effective.outerThreshold = std::min(1.0, effective.innerThreshold + 0.30);
        }
    }
    return effective;
}

MattingAccess mattingAccessFor(bool proActive, MattingQuality requested) {
    MattingAccess access;
    if (proActive) {
        access.allowed = true;
        access.quality = requested;
        access.reason = requested == MattingQuality::HighPrecision
                            ? "Pro 权益：色度抠像（高精度版）可用"
                            : "Pro 权益：色度抠像（基础版）可用";
        return access;
    }
    access.allowed = false;
    access.quality = MattingQuality::Basic;
    access.showPaywall = true;
    access.reason = "🔒 智能抠像（色度/绿幕）为 Pro 功能；升级 Pro 解锁";
    return access;
}

}  // namespace core
}  // namespace nairoclip
