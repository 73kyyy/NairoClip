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
    return false;
}

bool blendModeFromInt(int value, BlendMode& out) {
    switch (value) {
        case 0: out = BlendMode::Normal; return true;
        case 1: out = BlendMode::Screen; return true;
        case 2: out = BlendMode::Overlay; return true;
        case 3: out = BlendMode::Darken; return true;
        case 4: out = BlendMode::HardLight; return true;
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
    return "画中画混合模式（正常/滤色/叠加/变暗/强光）：图形行业标准公式，"
           "用于多层轨道合成的画中画效果";
}

}  // namespace core
}  // namespace nairoclip
