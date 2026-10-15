#include "nairoclip/core/filters.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>

namespace nairoclip {
namespace core {

namespace {

/// 单像素色调映射参数（每个滤镜风格一组；便于强度插值）。
struct ToneParams {
    double brightness = 0.0;   ///< 亮度偏移（-1..1）
    double contrast = 1.0;     ///< 对比度（0..3）
    double saturation = 1.0;   ///< 饱和度（0..3）
    double temperature = 0.0;  ///< 色温（-1 冷 .. 1 暖）
    double tint = 0.0;         ///< 色调（-1 绿 .. 1 品红）
    double lift = 0.0;         ///< 暗部抬升（0..1；胶片/复古的「发灰」效果）
    double gamma = 1.0;        ///< 伽马（<1 提亮中间调；>1 压暗）
    double negative = 0.0;     ///< 负片反色强度（0..1）
    /// 通道增益（RGB 分别乘；用于青橙/双色调等风格）。
    double gainR = 1.0;
    double gainG = 1.0;
    double gainB = 1.0;
};

/// 风格参数表（文档 7.1 分类清单；每项视觉意图与名称一致）。
ToneParams paramsFor(FilterStyle style) {
    ToneParams p;
    switch (style) {
        case FilterStyle::None:
            return p;
        case FilterStyle::Movie:
            // 电影：青橙对比（阴影偏青、高光偏橙）+ 轻微褪色
            p.contrast = 1.15;
            p.saturation = 0.92;
            p.gainR = 1.06;
            p.gainB = 0.94;
            p.lift = 0.04;
            p.gamma = 0.98;
            break;
        case FilterStyle::Retro:
            // 复古：暖黄 + 低饱和 + 暗部抬升
            p.temperature = 0.35;
            p.saturation = 0.75;
            p.lift = 0.10;
            p.contrast = 0.95;
            p.gainG = 0.97;
            break;
        case FilterStyle::Fresh:
            // 清新：高亮 + 冷绿倾向 + 高饱和
            p.brightness = 0.08;
            p.saturation = 1.18;
            p.temperature = -0.12;
            p.gainG = 1.05;
            p.gamma = 0.92;
            break;
        case FilterStyle::BlackWhite:
            // 黑白：去色 + 对比强化
            p.saturation = 0.0;
            p.contrast = 1.20;
            break;
        case FilterStyle::Cyber:
            // 赛博：青紫双色调（阴影偏青、高光偏紫）+ 高对比
            p.contrast = 1.25;
            p.saturation = 1.10;
            p.gainR = 0.92;
            p.gainB = 1.14;
            p.gamma = 1.05;
            break;
        case FilterStyle::Japanese:
            // 日系：低对比 + 高明度 + 淡青
            p.contrast = 0.88;
            p.brightness = 0.10;
            p.lift = 0.06;
            p.gainB = 1.04;
            p.gainG = 1.02;
            p.saturation = 0.94;
            break;
        case FilterStyle::Film:
            // 胶片：密度曲线（暗部抬升 + 高光压肩）+ 轻微品红
            p.lift = 0.07;
            p.contrast = 0.94;
            p.gamma = 1.06;
            p.tint = 0.08;
            p.saturation = 0.96;
            break;
        case FilterStyle::WarmSun:
            // 暖阳：橙黄暖调 + 提亮
            p.temperature = 0.45;
            p.brightness = 0.06;
            p.gainR = 1.08;
            p.gainG = 1.02;
            break;
        case FilterStyle::CoolTone:
            // 冷调：蓝青 + 降饱和
            p.temperature = -0.40;
            p.saturation = 0.85;
            p.gainB = 1.10;
            break;
        case FilterStyle::HongKong:
            // 港风：浓郁红绿 + 高对比
            p.contrast = 1.22;
            p.saturation = 1.25;
            p.gainR = 1.06;
            p.gainG = 1.03;
            p.gainB = 0.92;
            break;
        case FilterStyle::HighGray:
            // 高级灰：低饱和 + 灰调 + 柔对比
            p.saturation = 0.60;
            p.contrast = 0.92;
            p.lift = 0.08;
            p.saturation = 0.60;
            break;
        case FilterStyle::Cream:
            // 奶油：柔光 + 低对比 + 暖白
            p.brightness = 0.10;
            p.contrast = 0.86;
            p.temperature = 0.20;
            p.lift = 0.12;
            p.gamma = 0.94;
            break;
        case FilterStyle::Negative:
            // 负片：反色
            p.negative = 1.0;
            break;
    }
    return p;
}

/// 幂等插值：按强度把参数从「单位值」过渡到目标值。
ToneParams lerpParams(const ToneParams& target, double t) {
    const ToneParams unit;
    const auto mix = [t](double a, double b) { return a + (b - a) * t; };
    ToneParams out;
    out.brightness = mix(unit.brightness, target.brightness);
    out.contrast = mix(unit.contrast, target.contrast);
    out.saturation = mix(unit.saturation, target.saturation);
    out.temperature = mix(unit.temperature, target.temperature);
    out.tint = mix(unit.tint, target.tint);
    out.lift = mix(unit.lift, target.lift);
    out.gamma = mix(unit.gamma, target.gamma);
    out.negative = mix(unit.negative, target.negative);
    out.gainR = mix(unit.gainR, target.gainR);
    out.gainG = mix(unit.gainG, target.gainG);
    out.gainB = mix(unit.gainB, target.gainB);
    return out;
}

/// 对单个归一化通道应用参数（含暗部抬升/伽马）。
void applyTone(double& r, double& g, double& b, const ToneParams& p) {
    // 负片（先反色，后续调整针对反色后画面）
    if (p.negative > 0.0) {
        r = r + (1.0 - r - r) * p.negative;
        g = g + (1.0 - g - g) * p.negative;
        b = b + (1.0 - b - b) * p.negative;
    }
    // 通道增益
    r *= p.gainR;
    g *= p.gainG;
    b *= p.gainB;
    // 色温（暖 = R↑B↓）
    r += p.temperature * 0.12;
    b -= p.temperature * 0.12;
    // 色调（品红 = R↑G↓；绿 = 反向）
    r += p.tint * 0.06;
    g -= p.tint * 0.06;
    // 亮度
    r += p.brightness;
    g += p.brightness;
    b += p.brightness;
    // 对比度（以 0.5 为轴）
    r = (r - 0.5) * p.contrast + 0.5;
    g = (g - 0.5) * p.contrast + 0.5;
    b = (b - 0.5) * p.contrast + 0.5;
    // 伽马（先钳制到正值域）
    if (p.gamma != 1.0) {
        const auto gamma = [&p](double v) {
            const double clamped = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
            return std::pow(clamped, p.gamma);
        };
        r = gamma(r);
        g = gamma(g);
        b = gamma(b);
    }
    // 饱和度（亮度加权）
    if (p.saturation != 1.0) {
        const double luma = r * 0.299 + g * 0.587 + b * 0.114;
        r = luma + (r - luma) * p.saturation;
        g = luma + (g - luma) * p.saturation;
        b = luma + (b - luma) * p.saturation;
    }
    // 暗部抬升（发灰）
    if (p.lift > 0.0) {
        r = r + (1.0 - r) * p.lift * (1.0 - r);
        g = g + (1.0 - g) * p.lift * (1.0 - g);
        b = b + (1.0 - b) * p.lift * (1.0 - b);
    }
}

}  // namespace

const char* filterStyleName(FilterStyle style) {
    switch (style) {
        case FilterStyle::None: return "none";
        case FilterStyle::Movie: return "movie";
        case FilterStyle::Retro: return "retro";
        case FilterStyle::Fresh: return "fresh";
        case FilterStyle::BlackWhite: return "black_white";
        case FilterStyle::Cyber: return "cyber";
        case FilterStyle::Japanese: return "japanese";
        case FilterStyle::Film: return "film";
        case FilterStyle::WarmSun: return "warm_sun";
        case FilterStyle::CoolTone: return "cool_tone";
        case FilterStyle::HongKong: return "hong_kong";
        case FilterStyle::HighGray: return "high_gray";
        case FilterStyle::Cream: return "cream";
        case FilterStyle::Negative: return "negative";
    }
    return "none";
}

const char* filterStyleDisplayName(FilterStyle style) {
    switch (style) {
        case FilterStyle::None: return "无";
        case FilterStyle::Movie: return "电影";
        case FilterStyle::Retro: return "复古";
        case FilterStyle::Fresh: return "清新";
        case FilterStyle::BlackWhite: return "黑白";
        case FilterStyle::Cyber: return "赛博";
        case FilterStyle::Japanese: return "日系";
        case FilterStyle::Film: return "胶片";
        case FilterStyle::WarmSun: return "暖阳";
        case FilterStyle::CoolTone: return "冷调";
        case FilterStyle::HongKong: return "港风";
        case FilterStyle::HighGray: return "高级灰";
        case FilterStyle::Cream: return "奶油";
        case FilterStyle::Negative: return "Negative";
    }
    return "无";
}

bool filterStyleFromString(const char* text, FilterStyle& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    // 英文名（含若干别名）
    for (int i = 0; i <= 13; ++i) {
        const auto style = static_cast<FilterStyle>(i);
        if (s == filterStyleName(style)) {
            out = style;
            return true;
        }
    }
    if (s == "blackwhite" || s == "bw" || s == "mono") { out = FilterStyle::BlackWhite; return true; }
    if (s == "warm" || s == "warmsun") { out = FilterStyle::WarmSun; return true; }
    if (s == "cool") { out = FilterStyle::CoolTone; return true; }
    if (s == "hongkong" || s == "hk") { out = FilterStyle::HongKong; return true; }
    if (s == "highgray" || s == "gray") { out = FilterStyle::HighGray; return true; }
    // 中文名（文档逐字）
    for (int i = 0; i <= 13; ++i) {
        const auto style = static_cast<FilterStyle>(i);
        if (s == filterStyleDisplayName(style)) {
            out = style;
            return true;
        }
    }
    return false;
}

bool filterStyleFromInt(int value, FilterStyle& out) {
    if (value < 0 || value > 13) {
        return false;
    }
    out = static_cast<FilterStyle>(value);
    return true;
}

bool applyFilterToRgba(std::uint8_t* pixels, int width, int height, FilterStyle style,
                       double intensity) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (style == FilterStyle::None || intensity <= 0.0) {
        return true;  // 零改动
    }
    intensity = std::clamp(intensity, 0.0, 100.0) / 100.0;
    const ToneParams params = lerpParams(paramsFor(style), intensity);
    const std::size_t count =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* px = pixels + i * 4;
        double r = px[0] / 255.0;
        double g = px[1] / 255.0;
        double b = px[2] / 255.0;
        applyTone(r, g, b, params);
        px[0] = static_cast<std::uint8_t>(std::clamp(r * 255.0, 0.0, 255.0) + 0.5);
        px[1] = static_cast<std::uint8_t>(std::clamp(g * 255.0, 0.0, 255.0) + 0.5);
        px[2] = static_cast<std::uint8_t>(std::clamp(b * 255.0, 0.0, 255.0) + 0.5);
    }
    return true;
}

const char* filterLibraryPurpose() {
    return "滤镜库：14 种整体色彩风格（文档 Part 4-B 7.1；强度 0..100 线性插值）";
}

}  // namespace core
}  // namespace nairoclip
