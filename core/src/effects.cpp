#include "nairoclip/core/effects.h"

#include <algorithm>

namespace nairoclip {
namespace core {

EffectSpec effectSpec(EffectType type) {
    switch (type) {
        case EffectType::None: return EffectSpec{0.0, 0.0, 0.0, "无"};
        case EffectType::Brightness: return EffectSpec{-1.0, 1.0, 0.0, "亮度"};
        case EffectType::Contrast: return EffectSpec{0.0, 3.0, 1.0, "对比度"};
        case EffectType::Saturation: return EffectSpec{0.0, 3.0, 1.0, "饱和度"};
        case EffectType::Temperature: return EffectSpec{-1.0, 1.0, 0.0, "色温"};
        case EffectType::Tint: return EffectSpec{-1.0, 1.0, 0.0, "色调"};
        case EffectType::Grayscale: return EffectSpec{0.0, 1.0, 0.0, "灰度"};
        case EffectType::Sepia: return EffectSpec{0.0, 1.0, 0.0, "复古"};
        case EffectType::Vignette: return EffectSpec{0.0, 1.0, 0.0, "暗角"};
        case EffectType::GaussianBlur: return EffectSpec{0.0, 100.0, 0.0, "高斯模糊"};
        case EffectType::Sharpen: return EffectSpec{0.0, 5.0, 0.0, "锐化"};
        case EffectType::Grain: return EffectSpec{0.0, 1.0, 0.0, "噪点"};
        case EffectType::Fade: return EffectSpec{0.0, 1.0, 0.0, "褪色"};
    }
    return EffectSpec{};
}

const char* effectName(EffectType type) {
    return effectSpec(type).name;
}

Result EffectStack::add(Effect effect) {
    if (effect.type == EffectType::None) {
        return Result::fail(ErrorCode::InvalidArgument, "特效类型不能为空");
    }
    if (!effect.valid()) {
        return Result::fail(ErrorCode::InvalidArgument, "特效参数超出合法范围");
    }
    for (auto& e : effects_) {
        if (e.id == effect.id) {
            e = effect;
            return Result::ok();
        }
    }
    effects_.push_back(effect);
    return Result::ok();
}

bool EffectStack::remove(EffectId id) {
    const auto it = std::find_if(effects_.begin(), effects_.end(),
                                 [id](const Effect& e) { return e.id == id; });
    if (it == effects_.end()) {
        return false;
    }
    effects_.erase(it);
    return true;
}

bool EffectStack::moveUp(EffectId id) {
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].id == id && i > 0) {
            std::swap(effects_[i], effects_[i - 1]);
            return true;
        }
    }
    return false;
}

bool EffectStack::moveDown(EffectId id) {
    for (std::size_t i = 0; i < effects_.size(); ++i) {
        if (effects_[i].id == id && i + 1 < effects_.size()) {
            std::swap(effects_[i], effects_[i + 1]);
            return true;
        }
    }
    return false;
}

void EffectStack::clear() { effects_.clear(); }

Effect* EffectStack::find(EffectId id) {
    for (auto& e : effects_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const Effect* EffectStack::find(EffectId id) const {
    for (const auto& e : effects_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

bool EffectStack::invariantsHold() const {
    for (const auto& e : effects_) {
        if (!e.valid()) {
            return false;
        }
    }
    return true;
}

void ColorTransform::apply(double& r, double& g, double& b) const {
    // 1) 亮度：加法
    r += brightness;
    g += brightness;
    b += brightness;
    // 2) 对比度：围绕 0.5 缩放
    r = (r - 0.5) * contrast + 0.5;
    g = (g - 0.5) * contrast + 0.5;
    b = (b - 0.5) * contrast + 0.5;
    // 3) 色温：R/B 通道偏移
    r += temperature * 0.1;
    b -= temperature * 0.1;
    // 4) 色调：G/M 轴
    g += tint * 0.1;
    // 5) 饱和度：向灰度插值
    const double luma = 0.299 * r + 0.587 * g + 0.114 * b;
    r = luma + (r - luma) * saturation;
    g = luma + (g - luma) * saturation;
    b = luma + (b - luma) * saturation;
    // 6) 灰度：向 luma 插值
    if (grayscale > 0.0) {
        const double gray = 0.299 * r + 0.587 * g + 0.114 * b;
        r += (gray - r) * grayscale;
        g += (gray - g) * grayscale;
        b += (gray - b) * grayscale;
    }
    // 7) 复古
    if (sepia > 0.0) {
        const double sr = 0.393 * r + 0.769 * g + 0.189 * b;
        const double sg = 0.349 * r + 0.686 * g + 0.168 * b;
        const double sb = 0.272 * r + 0.534 * g + 0.131 * b;
        r += (sr - r) * sepia;
        g += (sg - g) * sepia;
        b += (sb - b) * sepia;
    }
    // 8) 褪色：向 0.5 灰雾提升
    if (fade > 0.0) {
        constexpr double kFadeLift = 0.05;
        r += (0.5 - r) * fade * kFadeLift * 2.0;
        g += (0.5 - g) * fade * kFadeLift * 2.0;
        b += (0.5 - b) * fade * kFadeLift * 2.0;
    }
    // 钳制
    r = r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
    g = g < 0.0 ? 0.0 : (g > 1.0 ? 1.0 : g);
    b = b < 0.0 ? 0.0 : (b > 1.0 ? 1.0 : b);
}

void applyColorTransformToRgba(std::uint8_t* pixels, std::size_t pixelCount,
                               const ColorTransform& transform) {
    for (std::size_t i = 0; i < pixelCount; ++i) {
        double r = static_cast<double>(pixels[i * 4]) / 255.0;
        double g = static_cast<double>(pixels[i * 4 + 1]) / 255.0;
        double b = static_cast<double>(pixels[i * 4 + 2]) / 255.0;
        transform.apply(r, g, b);
        pixels[i * 4] = static_cast<std::uint8_t>(r * 255.0 + 0.5);
        pixels[i * 4 + 1] = static_cast<std::uint8_t>(g * 255.0 + 0.5);
        pixels[i * 4 + 2] = static_cast<std::uint8_t>(b * 255.0 + 0.5);
        // alpha 保持不变
    }
}

ColorTransform evaluateColorTransform(const EffectStack& stack) {
    ColorTransform t;
    for (const auto& e : stack.effects()) {
        if (!e.enabled) {
            continue;
        }
        switch (e.type) {
            case EffectType::Brightness: t.brightness += e.value; break;
            case EffectType::Contrast: t.contrast *= e.value; break;
            case EffectType::Saturation: t.saturation *= e.value; break;
            case EffectType::Temperature: t.temperature += e.value; break;
            case EffectType::Tint: t.tint += e.value; break;
            case EffectType::Grayscale:
                t.grayscale = std::max(t.grayscale, e.value);
                break;
            case EffectType::Sepia: t.sepia = std::max(t.sepia, e.value); break;
            case EffectType::Fade: t.fade = std::max(t.fade, e.value); break;
            case EffectType::Vignette:
            case EffectType::GaussianBlur:
            case EffectType::Sharpen:
            case EffectType::Grain:
            case EffectType::None:
                break;  // 空间效果由 applyEffectStackToFrame 统一处理
        }
    }
    return t;
}

bool applyEffectStackToFrame(std::uint8_t* pixels, int width, int height,
                             const EffectStack& stack, std::int64_t frameIndex) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const std::size_t pixelCount =
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    // 1) 颜色变换（逐像素，无邻域；多特效折叠求值）
    const ColorTransform transform = evaluateColorTransform(stack);
    if (!transform.isIdentity()) {
        applyColorTransformToRgba(pixels, pixelCount, transform);
    }
    // 2) 空间效果（按栈序；需要邻域访问或时变种子）
    for (const auto& e : stack.effects()) {
        if (!e.enabled) {
            continue;
        }
        const int typeValue = static_cast<int>(e.type);
        if (typeValue < 8 || typeValue > 11) {
            continue;  // 颜色类已在上面处理
        }
        if (!applySpatialEffectToRgba(pixels, width, height, typeValue, e.value, frameIndex)) {
            return false;
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
