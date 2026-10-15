#include "nairoclip/core/background_fill.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace nairoclip {
namespace core {

namespace {

/// 确定性哈希噪声（与全项目其他效果模块同一风格：murmur3 finalizer）。
double hashNoise2D(std::uint32_t x, std::uint32_t y, std::uint32_t salt) {
    std::uint64_t h = (static_cast<std::uint64_t>(x) << 32) | static_cast<std::uint64_t>(y);
    h ^= static_cast<std::uint64_t>(salt) * 0x9E3779B97F4A7C15ull;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdull;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ull;
    h ^= h >> 33;
    return static_cast<double>(h % 1000000ull) / 1000000.0;
}

/// 把样式纹理填充到画布（仅在空白区域；sourceMask = 0 处）。
void fillWithTexture(std::uint8_t* canvas, const std::uint8_t* sourceMask, int width,
                     int height, const std::vector<std::uint8_t>& texture) {
    if (texture.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) {
        return;
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            if (sourceMask[idx] != 0) {
                continue;  // 素材区域保持原样
            }
            const std::uint8_t* src = texture.data() + idx * 4;
            std::uint8_t* dst = canvas + idx * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = 255;
        }
    }
}

}  // namespace

const char* backgroundModeName(BackgroundMode mode) {
    switch (mode) {
        case BackgroundMode::None: return "none";
        case BackgroundMode::Color: return "color";
        case BackgroundMode::Blur: return "blur";
        case BackgroundMode::Style: return "style";
    }
    return "none";
}

const char* depthEffectModeName(DepthEffectMode mode) {
    switch (mode) {
        case DepthEffectMode::None: return "none";
        case DepthEffectMode::Bokeh: return "bokeh";
        case DepthEffectMode::Fog: return "fog";
        case DepthEffectMode::ColorBands: return "color_bands";
        case DepthEffectMode::Parallax: return "parallax";
    }
    return "none";
}

bool depthEffectModeFromInt(int value, DepthEffectMode& out) {
    if (value < 0 || value > 4) {
        return false;
    }
    out = static_cast<DepthEffectMode>(value);
    return true;
}

bool backgroundModeFromString(const char* text, BackgroundMode& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (s == "none" || s == "无") { out = BackgroundMode::None; return true; }
    if (s == "color" || s == "颜色") { out = BackgroundMode::Color; return true; }
    if (s == "blur" || s == "模糊") { out = BackgroundMode::Blur; return true; }
    if (s == "style" || s == "画布样式") { out = BackgroundMode::Style; return true; }
    return false;
}

bool backgroundModeFromInt(int value, BackgroundMode& out) {
    if (value < 0 || value > 3) {
        return false;
    }
    out = static_cast<BackgroundMode>(value);
    return true;
}

const char* canvasStyleName(CanvasStyle style) {
    switch (style) {
        case CanvasStyle::None: return "none";
        case CanvasStyle::Wood: return "wood";
        case CanvasStyle::Paper: return "paper";
        case CanvasStyle::Linen: return "linen";
        case CanvasStyle::Concrete: return "concrete";
        case CanvasStyle::Marble: return "marble";
        case CanvasStyle::GradientWarm: return "gradient_warm";
        case CanvasStyle::GradientCool: return "gradient_cool";
        case CanvasStyle::GradientMono: return "gradient_mono";
        case CanvasStyle::Carbon: return "carbon";
        case CanvasStyle::Grid: return "grid";
        case CanvasStyle::Dots: return "dots";
        case CanvasStyle::Bricks: return "bricks";
        case CanvasStyle::Fabric: return "fabric";
        case CanvasStyle::Sand: return "sand";
        case CanvasStyle::Neon: return "neon";
    }
    return "none";
}

const char* canvasStyleDisplayName(CanvasStyle style) {
    switch (style) {
        case CanvasStyle::None: return "无";
        case CanvasStyle::Wood: return "木纹";
        case CanvasStyle::Paper: return "纸张";
        case CanvasStyle::Linen: return "亚麻";
        case CanvasStyle::Concrete: return "水泥";
        case CanvasStyle::Marble: return "大理石";
        case CanvasStyle::GradientWarm: return "暖色渐变";
        case CanvasStyle::GradientCool: return "冷色渐变";
        case CanvasStyle::GradientMono: return "灰阶渐变";
        case CanvasStyle::Carbon: return "碳纤维";
        case CanvasStyle::Grid: return "网格";
        case CanvasStyle::Dots: return "圆点";
        case CanvasStyle::Bricks: return "砖块";
        case CanvasStyle::Fabric: return "织物";
        case CanvasStyle::Sand: return "沙粒";
        case CanvasStyle::Neon: return "霓虹";
    }
    return "无";
}

bool canvasStyleFromInt(int value, CanvasStyle& out) {
    if (value < 0 || value > 15) {
        return false;
    }
    out = static_cast<CanvasStyle>(value);
    return true;
}

bool generateCanvasStyle(CanvasStyle style, int width, int height,
                         std::vector<std::uint8_t>& rgba) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 255);
    const auto put = [&rgba, width](int x, int y, double r, double g, double b) {
        std::uint8_t* px = rgba.data() +
                           (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)) * 4;
        px[0] = static_cast<std::uint8_t>(std::clamp(r, 0.0, 255.0));
        px[1] = static_cast<std::uint8_t>(std::clamp(g, 0.0, 255.0));
        px[2] = static_cast<std::uint8_t>(std::clamp(b, 0.0, 255.0));
        px[3] = 255;
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double u = static_cast<double>(x) / std::max(1, width);
            const double v = static_cast<double>(y) / std::max(1, height);
            const double noise = hashNoise2D(static_cast<std::uint32_t>(x),
                                             static_cast<std::uint32_t>(y), 991);
            switch (style) {
                case CanvasStyle::None:
                    put(x, y, 0, 0, 0);
                    break;
                case CanvasStyle::Wood: {
                    // 木纹：横向条纹（正弦 + 噪声扰动）
                    const double grain = std::sin((v * 60.0 + noise * 1.2) * 3.14159) * 0.5 + 0.5;
                    const double base = 120.0 + grain * 60.0;
                    put(x, y, base + 40.0, base * 0.75, base * 0.5);
                    break;
                }
                case CanvasStyle::Paper: {
                    const double base = 232.0 + noise * 14.0;
                    put(x, y, base, base - 4.0, base - 14.0);
                    break;
                }
                case CanvasStyle::Linen: {
                    // 亚麻：交织纹理
                    const double weave = (static_cast<int>(x / 3) + static_cast<int>(y / 3)) % 2;
                    const double base = 200.0 + weave * 12.0 + noise * 8.0;
                    put(x, y, base, base - 6.0, base - 18.0);
                    break;
                }
                case CanvasStyle::Concrete: {
                    const double base = 130.0 + noise * 40.0;
                    put(x, y, base, base, base + 3.0);
                    break;
                }
                case CanvasStyle::Marble: {
                    // 大理石：噪声曲线纹路
                    const double vein =
                        std::sin((u * 8.0 + std::sin(v * 5.0) * 2.0) * 3.14159) * 0.5 + 0.5;
                    const double base = 190.0 + vein * 50.0 + noise * 8.0;
                    put(x, y, base, base, base + 5.0);
                    break;
                }
                case CanvasStyle::GradientWarm:
                    put(x, y, 240.0 - v * 60.0, 170.0 - v * 60.0, 90.0 - v * 40.0);
                    break;
                case CanvasStyle::GradientCool:
                    put(x, y, 90.0 - v * 40.0, 150.0 - v * 50.0, 230.0 - v * 50.0);
                    break;
                case CanvasStyle::GradientMono: {
                    const double base = 220.0 - v * 160.0;
                    put(x, y, base, base, base);
                    break;
                }
                case CanvasStyle::Carbon: {
                    // 碳纤维：斜向格纹
                    const double cell = (static_cast<int>((x + y) / 4)) % 2;
                    const double base = 40.0 + cell * 18.0 + noise * 6.0;
                    put(x, y, base, base, base + 4.0);
                    break;
                }
                case CanvasStyle::Grid: {
                    const bool line = (x % 16 == 0) || (y % 16 == 0);
                    const double base = line ? 90.0 : 205.0;
                    put(x, y, base, base, base);
                    break;
                }
                case CanvasStyle::Dots: {
                    const int cx = x % 14 - 7;
                    const int cy = y % 14 - 7;
                    const bool dot = (cx * cx + cy * cy) < 6;
                    const double base = dot ? 90.0 : 210.0;
                    put(x, y, base, base, base);
                    break;
                }
                case CanvasStyle::Bricks: {
                    const int row = y / 12;
                    const int offset = (row % 2) * 16;
                    const bool mortar =
                        (y % 12 < 2) || ((x + offset) % 32 < 2);
                    const double base = mortar ? 190.0 : 150.0 + noise * 20.0;
                    put(x, y, base + 22.0, base * 0.72, base * 0.55);
                    break;
                }
                case CanvasStyle::Fabric: {
                    const double weave =
                        std::sin(u * 120.0 * 3.14159) * std::sin(v * 120.0 * 3.14159);
                    const double base = 170.0 + weave * 18.0 + noise * 8.0;
                    put(x, y, base, base - 10.0, base - 26.0);
                    break;
                }
                case CanvasStyle::Sand: {
                    const double base = 200.0 + noise * 30.0;
                    put(x, y, base + 20.0, base, base * 0.8);
                    break;
                }
                case CanvasStyle::Neon: {
                    const double glow = std::sin(u * 8.0 * 3.14159 + v * 4.0) * 0.5 + 0.5;
                    put(x, y, 40.0 + glow * 60.0, 20.0 + glow * 30.0, 90.0 + glow * 120.0);
                    break;
                }
            }
        }
    }
    return true;
}

bool applyBackgroundFill(std::uint8_t* canvas, const std::uint8_t* source,
                         const std::uint8_t* sourceMask, int width, int height,
                         BackgroundMode mode, std::uint32_t colorRgba, double intensity,
                         const RenderQuality& quality) {
    if (canvas == nullptr || sourceMask == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (mode == BackgroundMode::None) {
        return true;  // 零改动
    }
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    switch (mode) {
        case BackgroundMode::Color: {
            // 文档 11.1「颜色」：纯色填充空白区域
            const std::uint8_t r = static_cast<std::uint8_t>((colorRgba >> 24) & 0xFF);
            const std::uint8_t g = static_cast<std::uint8_t>((colorRgba >> 16) & 0xFF);
            const std::uint8_t b = static_cast<std::uint8_t>((colorRgba >> 8) & 0xFF);
            for (std::size_t i = 0; i < count; ++i) {
                if (sourceMask[i] != 0) {
                    continue;  // 素材区域保持原样
                }
                std::uint8_t* px = canvas + i * 4;
                px[0] = r;
                px[1] = g;
                px[2] = b;
                px[3] = 255;
            }
            return true;
        }
        case BackgroundMode::Blur: {
            // 文档 11.1「模糊」：素材放大 + 高斯模糊后铺满空白区域。
            // 实现：对 source（须提供）做盒式模糊（半径随模糊度与质量分级），
            //       按画布坐标中心对称放大采样（铺满），写入空白区域。
            if (source == nullptr) {
                return false;  // 模糊模式需要基底
            }
            const int radius =
                std::max(1, static_cast<int>(std::lround(intensity / 100.0 * 24.0 *
                                                         quality.sampleBudget)));
            std::vector<std::uint8_t> blurred(count * 4);
            // 分离式盒式模糊（水平 + 垂直；边缘钳制）
            std::vector<std::uint8_t> temp(count * 4);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int sum[3] = {0, 0, 0};
                    int n = 0;
                    for (int dx = -radius; dx <= radius; ++dx) {
                        const int xx = std::clamp(x + dx, 0, width - 1);
                        const std::uint8_t* px =
                            source + (static_cast<std::size_t>(y) *
                                          static_cast<std::size_t>(width) +
                                      static_cast<std::size_t>(xx)) *
                                         4;
                        sum[0] += px[0];
                        sum[1] += px[1];
                        sum[2] += px[2];
                        ++n;
                    }
                    std::uint8_t* dst = temp.data() +
                                        (static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x)) *
                                            4;
                    dst[0] = static_cast<std::uint8_t>(sum[0] / n);
                    dst[1] = static_cast<std::uint8_t>(sum[1] / n);
                    dst[2] = static_cast<std::uint8_t>(sum[2] / n);
                    dst[3] = 255;
                }
            }
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    int sum[3] = {0, 0, 0};
                    int n = 0;
                    for (int dy = -radius; dy <= radius; ++dy) {
                        const int yy = std::clamp(y + dy, 0, height - 1);
                        const std::uint8_t* px =
                            temp.data() + (static_cast<std::size_t>(yy) *
                                               static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(x)) *
                                              4;
                        sum[0] += px[0];
                        sum[1] += px[1];
                        sum[2] += px[2];
                        ++n;
                    }
                    std::uint8_t* dst = blurred.data() +
                                        (static_cast<std::size_t>(y) *
                                             static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(x)) *
                                            4;
                    dst[0] = static_cast<std::uint8_t>(sum[0] / n);
                    dst[1] = static_cast<std::uint8_t>(sum[1] / n);
                    dst[2] = static_cast<std::uint8_t>(sum[2] / n);
                    dst[3] = 255;
                }
            }
            // 中心对称放大采样（素材放大铺满）→ 写入空白区域
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t idx =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                        static_cast<std::size_t>(x);
                    if (sourceMask[idx] != 0) {
                        continue;
                    }
                    // 放大 1.4 倍采样（中心锚点）
                    const double scale = 1.4;
                    const int sx = static_cast<int>(
                        std::lround((x - width * 0.5) / scale + width * 0.5));
                    const int sy = static_cast<int>(
                        std::lround((y - height * 0.5) / scale + height * 0.5));
                    const int cx = std::clamp(sx, 0, width - 1);
                    const int cy = std::clamp(sy, 0, height - 1);
                    const std::uint8_t* src =
                        blurred.data() + (static_cast<std::size_t>(cy) *
                                              static_cast<std::size_t>(width) +
                                          static_cast<std::size_t>(cx)) *
                                             4;
                    std::uint8_t* dst = canvas + idx * 4;
                    dst[0] = src[0];
                    dst[1] = src[1];
                    dst[2] = src[2];
                    dst[3] = 255;
                }
            }
            return true;
        }
        case BackgroundMode::Style: {
            // 文档 11.1「画布样式」：预制纹理铺满空白区域。
            CanvasStyle style = CanvasStyle::None;
            const int styleIndex = static_cast<int>(std::lround(intensity));
            if (!canvasStyleFromInt(styleIndex, style) || style == CanvasStyle::None) {
                return false;  // 非法样式编号
            }
            std::vector<std::uint8_t> texture;
            if (!generateCanvasStyle(style, width, height, texture)) {
                return false;
            }
            fillWithTexture(canvas, sourceMask, width, height, texture);
            return true;
        }
        case BackgroundMode::None:
            return true;
    }
    return false;
}

const char* backgroundFillPurpose() {
    return "背景填充：为片段留白区域填色/模糊/纹理（文档 Part 4-C 第 11 节；三 Tab 互斥）";
}

}  // namespace core
}  // namespace nairoclip
