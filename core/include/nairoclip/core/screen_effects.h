#pragma once
// =============================================================================
// 画面特效（Screen Effects）—— 文档第二段「5.1 画面特效」六种：
//   故障（Glitch）/ 分屏 / 镜像 / 旋转模糊 / 扫描线 / RGB 分离
// =============================================================================
// 产品对应（原文 5.1）：
//   | 故障（Glitch） | RGB通道独立像素偏移+随机色块替换 |
//   | 分屏          | 同一帧纹理多次采样+坐标变换后拼接显示 |
//   | 镜像          | UV坐标沿中轴翻转采样 |
//   | 旋转模糊      | 以画面中心为原点，多个旋转角度采样加权平均 |
//   | 扫描线        | 按Y坐标周期性降低亮度/叠加半透明条纹 |
//   | RGB分离       | R/G/B三通道分别做微小像素位移后重新合成 |
//   工程要点：「均为单帧内像素处理，不需帧间状态，适合优先 batch 开发」。
//
// 实现说明（真实、可测，零第三方依赖；CPU 参考实现 = 正确性基准）：
//   · 全部为确定性像素运算（同一输入 + 同一 frameIndex → 同一输出）；
//   · 故障/RGB 分离的随机性来自按 (行号, frameIndex, 通道) 派生的确定性伪随机
//     （murmur3 雪崩混合；与 spatial_effects 的噪点同源技术）；
//   · 旋转模糊使用固定采样数（导出 24 次；预览按 RenderQuality 简化）；
//   · 分屏模式：2x2 四宫格（每格取源帧不同区域，坐标变换后拼接）。
//
// 与 EffectStack 的集成：
//   · 六种画面特效的 EffectType 取值 13..18（紧跟既有 12 种）；
//   · 本模块提供 applyScreenEffectToRgba 分发（导出/预览共用）；
//   · 参数含义（value 0..1 强度；部分效果按 intensity 映射到幅度）。

#include <cstdint>

#include "nairoclip/core/render_engine.h"

namespace nairoclip {
namespace core {

/// 画面特效类型（与 EffectType 数值对齐；13..18）。
enum class ScreenEffectType {
    Glitch = 13,        ///< 故障（RGB 通道偏移 + 色块替换）
    SplitScreen = 14,   ///< 分屏（2x2 宫格）
    Mirror = 15,        ///< 镜像（左右翻转）
    RadialBlur = 16,    ///< 旋转模糊（绕中心多角度采样平均）
    Scanlines = 17,     ///< 扫描线（周期性暗条纹）
    RgbSplit = 18,      ///< RGB 分离（三通道位移）
};

/// 应用单个画面特效（原地修改 RGB；alpha 不变）。
/// - effectType：13..18（ScreenEffectType 值）；其他值返回 true 且不改动
///   （由其他模块处理）；
/// - value：强度 0..1（0 = 关闭，零改动）；
/// - frameIndex：时变种子（故障/RGB 分离；同一帧 → 同一结果，可测）；
/// - quality：渲染质量分级（旋转模糊采样数按预览/导出分级）；
/// - 参数非法（缓冲区不足/尺寸非法）返回 false 且不改动缓冲。
bool applyScreenEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                             double value, std::int64_t frameIndex,
                             const RenderQuality& quality = RenderQuality::exportQuality());

// ---- 单效果入口（供测试与 GPU 后端参数对齐）----

/// 故障：按行随机 RGB 通道水平偏移 + 随机色块替换。
bool applyGlitchRgba(std::uint8_t* pixels, int width, int height, double intensity,
                     std::int64_t frameIndex);

/// 分屏：源帧 2x2 宫格拼接（每格展示源帧对应象限）。
/// 实现为「坐标变换后采样」——输出 (x,y) 映射到源帧 (x', y')。
bool applySplitScreenRgba(std::uint8_t* pixels, int width, int height, double intensity);

/// 镜像：左右翻转（UV 沿中轴翻转）。
bool applyMirrorRgba(std::uint8_t* pixels, int width, int height, double intensity);

/// 旋转模糊：绕画面中心多个旋转角度采样加权平均。
/// - sampleCount：完整精度采样数（quality 会按预览/导出分级缩放）。
bool applyRadialBlurRgba(std::uint8_t* pixels, int width, int height, double intensity,
                         const RenderQuality& quality);

/// 扫描线：按 Y 坐标周期性降低亮度。
bool applyScanlinesRgba(std::uint8_t* pixels, int width, int height, double intensity);

/// RGB 分离：R/G/B 三通道分别做水平像素位移后重新合成。
bool applyRgbSplitRgba(std::uint8_t* pixels, int width, int height, double intensity,
                       std::int64_t frameIndex);

/// 说明文案（供 UI/日志）。
const char* screenEffectPurpose();

}  // namespace core
}  // namespace nairoclip
