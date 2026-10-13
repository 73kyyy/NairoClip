#pragma once
// =============================================================================
// 空间特效（Spatial Effects）—— 文档第 2 部分「✨特效 → 画面特效」中需要
// 邻域像素访问的效果：暗角 / 高斯模糊 / 锐化 / 噪点（噪点为逐像素扰动但按
// 画面坐标固定种子，语义上归"空间层"——与颜色变换分离，见下）
// =============================================================================
// 背景（此前的真实缺陷，本模块修复）：
//   effects.h 的 EffectType 声明了 Vignette / GaussianBlur / Sharpen / Grain
//   四种特效，但 evaluateColorTransform 把它们全部跳过（注释称"由渲染器单独
//   处理"），而导出/预览渲染器从未实现 → 用户添加这 4 种特效后画面毫无变化
//   （假功能）。本模块提供真实实现，并接入导出与预览管线。
//
// 实现说明（真实、可测，零第三方依赖）：
//   · 暗角（Vignette）：以画面中心为原点的径向衰减（平滑余弦曲线），
//     逐像素乘性变暗；参数 = 强度 [0,1]（0 = 关闭）；
//   · 高斯模糊（GaussianBlur）：可分离两趟卷积（水平 + 垂直），
//     半径 r 由参数 [0,100] 决定；sigma = max(0.5, r/3)，核半径 = ceil(3σ)；
//     边界用边缘钳制（clamp）；double 中间缓冲保证精度；
//   · 锐化（Sharpen）：非锐化掩模「原始 + 强度 ×（原始 - 模糊）」，
//     强度 [0,5] 直接作为混合权重，避免过度过冲；
//   · 噪点（Grain）：逐像素叠加确定性伪随机噪声（LCG 按 (x,y,帧序号) 派生，
//     同一输入 → 同一输出，可测）；强度参数 [0,1] 映射到噪声幅值 ±32 灰度。
//
// 应用顺序（与 EffectStack 顺序一致）：
//   颜色变换（ColorTransform，逐像素无邻域）先行，空间效果随后应用；
//   即 evaluate → applyColorTransformToRgba → applySpatialEffectToRgba。
//   （空间效果在颜色变换之后：符合"调色后加颗粒/暗角"的行业惯例。）
//
// 性能说明（文档 4.4 预览/导出质量分级）：
//   · 本模块为 CPU 参考实现（正确性基准）；GPU 后端（render_engine.h 抽象）
//     后续接入同一 EffectStack 语义；
//   · 高斯模糊为可分离两趟卷积 O(w·h·σ)，导出场景可接受；
//     预览路径由调用方控制半径上限（见 render_engine.h 的 RenderQuality）。

#include <cstdint>

namespace nairoclip {
namespace core {

/// 对 RGBA8 缓冲应用单个空间效果（原地修改 RGB，alpha 不变）。
/// - effectType 取值与 effects.h 的 EffectType 数值对齐：
///     8 = Vignette，9 = GaussianBlur，10 = Sharpen，11 = Grain；
/// - value：特效强度（参数范围见 effects.h 的 effectSpec）；
/// - 颜色类 type（0..7）返回 true 且不改动（由 ColorTransform 处理）；
/// - 参数非法（缓冲区不足/尺寸非法）返回 false 且不改动缓冲。
bool applySpatialEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                              double value, std::int64_t frameIndex);

/// 应用高斯模糊（可分离两趟卷积；radiusPixels 为用户半径，内部 sigma = r/3）。
/// 供本模块内部与 render_engine 复用；参数非法返回 false。
bool applyGaussianBlurRgba(std::uint8_t* pixels, int width, int height, double radiusPixels);

/// 应用暗角（强度 0..1）。
bool applyVignetteRgba(std::uint8_t* pixels, int width, int height, double strength);

/// 应用锐化（强度 0..5）。
bool applySharpenRgba(std::uint8_t* pixels, int width, int height, double strength);

/// 应用噪点（强度 0..1；frameIndex 参与种子）。
bool applyGrainRgba(std::uint8_t* pixels, int width, int height, double strength,
                    std::int64_t frameIndex);

}  // namespace core
}  // namespace nairoclip
