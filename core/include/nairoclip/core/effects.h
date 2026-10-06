#pragma once

#include "nairoclip/core/types.h"

#include <cmath>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 特效类型（内置基础集；参数经 effectParams 校验）。
enum class EffectType {
    None = 0,
    Brightness,      // 亮度  [-1, 1]
    Contrast,        // 对比度 [0, 3]，1 = 原始
    Saturation,      // 饱和度 [0, 3]，1 = 原始
    Temperature,     // 色温   [-1, 1]，负=冷，正=暖
    Tint,            // 色调   [-1, 1]，负=绿，正=品红
    Grayscale,       // 灰度（0 = 关，1 = 全灰）
    Sepia,          // 复古（0 = 关，1 = 全复古）
    Vignette,       // 暗角强度 [0, 1]
    GaussianBlur,   // 高斯模糊半径（像素）[0, 100]
    Sharpen,        // 锐化强度 [0, 5]
    Grain,          // 噪点强度 [0, 1]
    Fade,           // 褪色（胶片感）[0, 1]
};

/// 特效参数范围描述。
struct EffectSpec {
    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.0;
    const char* name = "";
};

/// 获取特效规格。
EffectSpec effectSpec(EffectType type);

/// 特效展示名。
const char* effectName(EffectType type);

/// 单个特效实例。
struct Effect {
    EffectId id;
    EffectType type = EffectType::None;
    double value = 0.0;
    bool enabled = true;

    bool valid() const {
        const EffectSpec spec = effectSpec(type);
        return value >= spec.minValue && value <= spec.maxValue;
    }
};

/// 特效栈（按顺序应用，顺序影响结果——如模糊+锐化）。
class EffectStack {
public:
    /// 添加特效；同 ID 替换。
    Result add(Effect effect);
    /// 删除特效。
    bool remove(EffectId id);
    /// 上移/下移（调整应用顺序）。
    bool moveUp(EffectId id);
    bool moveDown(EffectId id);
    void clear();

    bool empty() const { return effects_.empty(); }
    std::size_t size() const { return effects_.size(); }
    const std::vector<Effect>& effects() const { return effects_; }

    Effect* find(EffectId id);
    const Effect* find(EffectId id) const;

    /// 校验：所有特效参数合法。
    bool invariantsHold() const;

private:
    std::vector<Effect> effects_;
};

/// 把特效栈求值为一个像素变换函数（供导出器与预览消耗）。
/// 返回：对归一化 RGB [0,1] 的变换（多特效串联求值，简化模型；
/// 模糊/锐化等空间效果由渲染器单独处理，这里被跳过）。
struct ColorTransform {
    double brightness = 0.0;   // 累加
    double contrast = 1.0;     // 累乘
    double saturation = 1.0;   // 累乘
    double temperature = 0.0;  // 累加
    double tint = 0.0;         // 累加
    double grayscale = 0.0;    // 取最大
    double sepia = 0.0;        // 取最大
    double fade = 0.0;         // 取最大

    /// 对单个 RGB 像素应用变换（输入/输出 [0,1]）。
    void apply(double& r, double& g, double& b) const;
};

/// 求值特效栈的颜色变换（忽略空间效果与禁用的特效）。
ColorTransform evaluateColorTransform(const EffectStack& stack);

/// 对 RGBA8 像素缓冲应用颜色变换（原地修改 RGB，保留 alpha）。
/// pixels：交错 RGBA8，长度必须是 4 的倍数（stride 处理由调用方负责跨行裁剪）。
void applyColorTransformToRgba(std::uint8_t* pixels, std::size_t pixelCount,
                               const ColorTransform& transform);

}  // namespace core
}  // namespace nairoclip
