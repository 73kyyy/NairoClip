#include "nairoclip/core/blend_modes.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

const char* blendModeName(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal: return "normal";
        case BlendMode::Screen: return "screen";
        case BlendMode::Overlay: return "overlay";
        case BlendMode::Darken: return "darken";
        case BlendMode::HardLight: return "hard_light";
        case BlendMode::Multiply: return "multiply";
        case BlendMode::Lighten: return "lighten";
        case BlendMode::ColorDodge: return "color_dodge";
        case BlendMode::ColorBurn: return "color_burn";
        case BlendMode::LinearBurn: return "linear_burn";
        case BlendMode::LinearDodge: return "linear_dodge";
        case BlendMode::SoftLight: return "soft_light";
        case BlendMode::LinearLight: return "linear_light";
        case BlendMode::PinLight: return "pin_light";
        case BlendMode::Difference: return "difference";
        case BlendMode::Exclusion: return "exclusion";
    }
    return "normal";
}

bool blendModeFromString(const char* text, BlendMode& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (s == "normal" || s == "正常") { out = BlendMode::Normal; return true; }
    if (s == "screen" || s == "滤色") { out = BlendMode::Screen; return true; }
    if (s == "overlay" || s == "叠加") { out = BlendMode::Overlay; return true; }
    if (s == "darken" || s == "变暗") { out = BlendMode::Darken; return true; }
    if (s == "hard_light" || s == "hardlight" || s == "hard-light" || s == "强光") {
        out = BlendMode::HardLight;
        return true;
    }
    if (s == "multiply" || s == "正片叠底") { out = BlendMode::Multiply; return true; }
    if (s == "lighten" || s == "变亮") { out = BlendMode::Lighten; return true; }
    if (s == "color_dodge" || s == "colordodge" || s == "color-dodge" || s == "颜色减淡") {
        out = BlendMode::ColorDodge;
        return true;
    }
    if (s == "color_burn" || s == "colorburn" || s == "color-burn" || s == "颜色加深") {
        out = BlendMode::ColorBurn;
        return true;
    }
    if (s == "linear_burn" || s == "linearburn" || s == "linear-burn" || s == "线性加深") {
        out = BlendMode::LinearBurn;
        return true;
    }
    if (s == "linear_dodge" || s == "lineardodge" || s == "linear-dodge" ||
        s == "linear_dodge_add" || s == "线性减淡") {
        out = BlendMode::LinearDodge;
        return true;
    }
    if (s == "soft_light" || s == "softlight" || s == "soft-light" || s == "柔光") {
        out = BlendMode::SoftLight;
        return true;
    }
    if (s == "linear_light" || s == "linearlight" || s == "linear-light" || s == "线性光") {
        out = BlendMode::LinearLight;
        return true;
    }
    if (s == "pin_light" || s == "pinlight" || s == "pin-light" || s == "点光") {
        out = BlendMode::PinLight;
        return true;
    }
    if (s == "difference" || s == "差值") { out = BlendMode::Difference; return true; }
    if (s == "exclusion" || s == "排除") { out = BlendMode::Exclusion; return true; }
    return false;
}

bool blendModeFromInt(int value, BlendMode& out) {
    switch (value) {
        case 0: out = BlendMode::Normal; return true;
        case 1: out = BlendMode::Screen; return true;
        case 2: out = BlendMode::Overlay; return true;
        case 3: out = BlendMode::Darken; return true;
        case 4: out = BlendMode::HardLight; return true;
        case 5: out = BlendMode::Multiply; return true;
        case 6: out = BlendMode::Lighten; return true;
        case 7: out = BlendMode::ColorDodge; return true;
        case 8: out = BlendMode::ColorBurn; return true;
        case 9: out = BlendMode::LinearBurn; return true;
        case 10: out = BlendMode::LinearDodge; return true;
        case 11: out = BlendMode::SoftLight; return true;
        case 12: out = BlendMode::LinearLight; return true;
        case 13: out = BlendMode::PinLight; return true;
        case 14: out = BlendMode::Difference; return true;
        case 15: out = BlendMode::Exclusion; return true;
        default: return false;
    }
}

double blendChannel(BlendMode mode, double base, double top) {
    // 输入钳制（防御）
    base = base < 0.0 ? 0.0 : (base > 1.0 ? 1.0 : base);
    top = top < 0.0 ? 0.0 : (top > 1.0 ? 1.0 : top);
    switch (mode) {
        case BlendMode::Normal:
            return top;
        case BlendMode::Screen:
            // 滤色：result = 1 - (1-A)(1-B)
            return 1.0 - (1.0 - base) * (1.0 - top);
        case BlendMode::Overlay:
            // 叠加：底图暗 → 正片叠底（2AB）；底图亮 → 滤色近似（1-2(1-A)(1-B)）
            // 经典 Photoshop 公式（分段点 0.5）
            return base < 0.5 ? 2.0 * base * top
                              : 1.0 - 2.0 * (1.0 - base) * (1.0 - top);
        case BlendMode::Darken:
            // 变暗：result = min(A, B)
            return base < top ? base : top;
        case BlendMode::HardLight:
            // 强光：Overlay 的变体（交换 A/B 角色，以上层为判定依据）
            return top < 0.5 ? 2.0 * top * base
                             : 1.0 - 2.0 * (1.0 - top) * (1.0 - base);
        // ---- B1-1 追加：均采用 Photoshop / W3C Compositing 标准公式 ----
        case BlendMode::Multiply:
            // 正片叠底：result = A × B（只会变暗）
            return base * top;
        case BlendMode::Lighten:
            // 变亮：result = max(A, B)
            return base > top ? base : top;
        case BlendMode::ColorDodge:
            // 颜色减淡：result = A / (1 - B)。B = 1 时为纯白（除零保护）。
            if (top >= 1.0) {
                return 1.0;
            }
            {
                const double r = base / (1.0 - top);
                return r > 1.0 ? 1.0 : r;
            }
        case BlendMode::ColorBurn:
            // 颜色加深：result = 1 - (1 - A) / B。B = 0 时为纯黑（除零保护）。
            if (top <= 0.0) {
                return 0.0;
            }
            {
                const double r = 1.0 - (1.0 - base) / top;
                return r < 0.0 ? 0.0 : r;
            }
        case BlendMode::LinearBurn:
            // 线性加深：result = A + B - 1（下限 0）
            {
                const double r = base + top - 1.0;
                return r < 0.0 ? 0.0 : r;
            }
        case BlendMode::LinearDodge:
            // 线性减淡（相加）：result = A + B（上限 1）
            {
                const double r = base + top;
                return r > 1.0 ? 1.0 : r;
            }
        case BlendMode::SoftLight:
            // 柔光：W3C Compositing 标准式（与 Photoshop 一致）
            //   B <= 0.5: A - (1 - 2B) · A · (1 - A)
            //   B >  0.5: A + (2B - 1) · (D(A) - A)
            //   D(A) = ((16A - 12)·A + 4)·A   (A <= 0.25)
            //        = sqrt(A)                (A >  0.25)
            if (top <= 0.5) {
                return base - (1.0 - 2.0 * top) * base * (1.0 - base);
            }
            {
                const double d = (base <= 0.25)
                                     ? ((16.0 * base - 12.0) * base + 4.0) * base
                                     : std::sqrt(base);
                const double r = base + (2.0 * top - 1.0) * (d - base);
                return r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
            }
        case BlendMode::LinearLight:
            // 线性光：result = A + 2B - 1
            {
                const double r = base + 2.0 * top - 1.0;
                return r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
            }
        case BlendMode::PinLight:
            // 点光：B < 0.5 取 min(A, 2B)；否则取 max(A, 2B - 1)
            if (top < 0.5) {
                const double v = 2.0 * top;
                return base < v ? base : v;
            }
            {
                const double v = 2.0 * top - 1.0;
                return base > v ? base : v;
            }
        case BlendMode::Difference:
            // 差值：result = |A - B|
            return base > top ? base - top : top - base;
        case BlendMode::Exclusion:
            // 排除：result = A + B - 2AB（差值的低对比变体）
            return base + top - 2.0 * base * top;
    }
    return top;
}

bool blendFrameRgba(std::uint8_t* base, const std::uint8_t* top, int width, int height,
                    BlendMode mode, double opacity) {
    if (base == nullptr || top == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (opacity <= 0.0) {
        return true;  // 不透明度 0：无操作（零改动成功）
    }
    if (opacity > 1.0) opacity = 1.0;
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < pixelCount; ++i) {
        const std::size_t idx = i * 4;
        const double topAlpha =
            static_cast<double>(top[idx + 3]) / 255.0 * opacity;
        if (topAlpha <= 0.0) {
            continue;  // 全透明：跳过
        }
        if (mode == BlendMode::Normal) {
            // 标准 Alpha 混合（最常用路径；避免经过公式函数减少浮点开销）
            for (int c = 0; c < 3; ++c) {
                const double b = base[idx + static_cast<std::size_t>(c)];
                const double t = top[idx + static_cast<std::size_t>(c)];
                const double mixed = t * topAlpha + b * (1.0 - topAlpha);
                base[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, mixed + 0.5)));
            }
        } else {
            for (int c = 0; c < 3; ++c) {
                const double b =
                    static_cast<double>(base[idx + static_cast<std::size_t>(c)]) / 255.0;
                const double t =
                    static_cast<double>(top[idx + static_cast<std::size_t>(c)]) / 255.0;
                const double blended = blendChannel(mode, b, t);
                // 混合公式结果按 alpha 权重与底层合成
                const double result = blended * topAlpha + b * (1.0 - topAlpha);
                base[idx + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                    std::min(255.0, std::max(0.0, result * 255.0 + 0.5)));
            }
        }
        // 合成后 base 视为不透明（与既有混合路径语义一致）
        base[idx + 3] = 255;
    }
    return true;
}

const char* blendModePurpose() {
    return "画中画混合模式（16 种，对标 Photoshop/AE）：图形行业标准公式，"
           "用于多层轨道合成的画中画效果";
}

}  // namespace core
}  // namespace nairoclip
