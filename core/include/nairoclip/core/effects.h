#pragma once

#include "nairoclip/core/atmosphere_effects.h"
#include "nairoclip/core/motion_effects.h"
#include "nairoclip/core/render_engine.h"
#include "nairoclip/core/screen_effects.h"
#include "nairoclip/core/spatial_effects.h"

#include "nairoclip/core/types.h"

#include <cmath>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 特效类型（内置基础集；参数经 effectParams 校验）。
/// 特效类型（画面特效 13..18 见 screen_effects.h）。
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
    // ---- 画面特效（文档第二段 5.1；实现见 screen_effects.h）----
    Glitch = 13,      // 故障（RGB 通道偏移 + 色块替换）[0, 1]
    SplitScreen = 14, // 分屏（2x2 宫格，每格完整帧缩小版）[0, 1]
    Mirror = 15,      // 镜像（左右翻转）[0, 1]
    RadialBlur = 16,  // 旋转模糊（绕中心多角度采样）[0, 1]
    Scanlines = 17,   // 扫描线（周期暗条纹）[0, 1]
    RgbSplit = 18,    // RGB 分离（三通道位移）[0, 1]
    // ---- 氛围特效（文档第二段 5.3；实现见 atmosphere_effects.h）----
    Snow = 19,       // 雪花（粒子引擎）[0, 1]
    Rain = 20,       // 下雨（粒子引擎）[0, 1]
    LightLeak = 21,  // 光斑（叠加 + 闪烁）[0, 1]
    OldFilm = 22,    // 老电影划痕（划痕 + 抖动 + 颗粒）[0, 1]
    Starburst = 23,  // 星光（高光十字星芒）[0, 1]
    // ---- 动感特效（文档第二段 5.4；实现见 motion_effects.h）----
    ZoomPunch = 24,    // 缩放冲击（时间函数脉冲）[0, 1]
    WhipPan = 25,      // 甩镜（方向性模糊）[0, 1]
    BeatFlicker = 26,  // 变速闪烁（节拍驱动）[0, 1]
    BeatRhythm = 27,   // 节奏律动（节拍驱动）[0, 1]
    MotionBlur = 28,   // 运动模糊（多帧叠加；导出管线帧序列层处理）[0, 1]
    // ---- 画面特效补充（文档 Part 4-B 5.2「画面特效」预设清单余项）----
    Flash = 29,        // 闪光（整帧亮度瞬间提升）[0, 1]
    Mosaic = 30,       // 马赛克（像素化块替换）[0, 1]
    // ---- 人物特效（文档 Part 4-B 5.2「人物特效」预设清单）----
    Jitter = 31,       // 抖动（整帧随机小幅度位移，时间噪声驱动）[0, 1]
    ZoomIn = 32,       // 放大（画面中心区域局部放大采样）[0, 1]
    EdgeDetect = 33,   // 边缘检测（Sobel 边缘提取）[0, 1]
    Slim = 34,         // 瘦身（人像区域横向压缩；依赖抠像 Alpha，无 Alpha 时零改动）[0, 1]
    GlowOutline = 35,  // 发光轮廓（抠像 Alpha 边缘高斯模糊 + 发光叠加）[0, 1]
    // ---- 震动/摇晃（B1-5；实现见 shake_effect.h）----
    /// 震动：value 为强度（0..1 = 是否启用及倍率）。
    /// 完整参数（轨迹/振幅/频率/种子/衰减）走 ShakeParams，不进 Effect.value
    /// ——Effect 是单值字段，塞不进四维参数，硬塞会让序列化变成猜谜。
    Shake = 36,
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

    /// 是否为单位变换（无任何调整；跳过像素处理用）。
    bool isIdentity() const {
        return brightness == 0.0 && contrast == 1.0 && saturation == 1.0 &&
               temperature == 0.0 && tint == 0.0 && grayscale == 0.0 && sepia == 0.0 &&
               fade == 0.0;
    }
};

/// 求值特效栈的颜色变换（忽略空间效果与禁用的特效）。
ColorTransform evaluateColorTransform(const EffectStack& stack);

/// 对 RGBA8 像素缓冲应用颜色变换（原地修改 RGB，保留 alpha）。
/// pixels：交错 RGBA8，长度必须是 4 的倍数（stride 处理由调用方负责跨行裁剪）。
void applyColorTransformToRgba(std::uint8_t* pixels, std::size_t pixelCount,
                               const ColorTransform& transform);

/// 应用整个特效栈到一帧（颜色变换 + 空间效果 + 画面特效 + 氛围特效 + 动感特效；
/// 导出与预览共用，消除两处各自解释栈语义的风险）。
/// - 顺序：先颜色变换（evaluate → 逐像素），再按栈序应用：
///   空间效果（8..11）、画面特效（13..18）、氛围特效（19..23）、动感特效（24..27）；
/// - frameIndex：时变种子（噪点/故障/RGB 分离/氛围特效）；
/// - quality：渲染质量分级（旋转模糊/甩镜采样数、粒子数量按预览/导出分级；
///   默认导出质量 = 完整精度；文档 4.4）；
/// - timeSeconds：当前帧时间（氛围特效的粒子/闪烁、动感特效的时间函数与
///   节拍窗口使用；默认由 frameIndex 推算 —— 调用方应传入真实时间）；
/// - beats：节拍时间戳数组（升序；变速闪烁/节奏律动使用；可为空）；
/// - 参数非法返回 false 且不改动；空栈返回 true。
bool applyEffectStackToFrame(std::uint8_t* pixels, int width, int height,
                             const EffectStack& stack, std::int64_t frameIndex = 0,
                             const RenderQuality& quality = RenderQuality::exportQuality(),
                             double timeSeconds = -1.0,
                             const std::vector<Time>& beats = {});

}  // namespace core
}  // namespace nairoclip
