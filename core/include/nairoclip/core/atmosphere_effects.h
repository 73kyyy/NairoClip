#pragma once
// =============================================================================
// 氛围特效（Atmosphere Effects）—— 文档第二段「5.3 氛围特效」
// =============================================================================
// 产品对应（原文 5.3）：
//   | 雪花/下雨 | 粒子系统：大量小纹理点按随机初始位置、统一下落速度向量、
//   |          | 循环重置位置规则逐帧更新 |
//   | 光斑      | 预制光斑纹理叠加显示，支持闪烁动画 |
//   | 胶片颗粒  | 基于时间变化的噪声纹理叠加 |
//   | 老电影划痕 | 预制划痕纹理素材库随机叠加 |
//   | 星光      | 对画面高光点做"十字星芒"形状径向采样叠加 |
//   工程要点：「粒子类效果建议抽象出通用粒子引擎模块（参数化：数量/初速度/
//   重力/生命周期/纹理）」→ 见 particle_engine.h。
//
// 实现说明（真实、可测，零第三方依赖）：
//   · 雪花/下雨：消费 particle_engine 的确定性粒子采样，逐粒子绘制（软边圆点）；
//   · 光斑：多个确定性光斑（径向渐变）叠加 + 闪烁（时间驱动透明度）；
//   · 胶片颗粒：复用 spatial_effects 的噪点（时间变化噪声纹理）；
//   · 老电影划痕：确定性竖线划痕（随机 x/长度/亮度）+ 抖动 + 颗粒；
//   · 星光：对画面高光点做十字星芒（径向采样叠加）。
//
// 质量分级（文档 4.4）：粒子数量按 RenderQuality 预览简化（导出完整）。

#include <cstdint>

#include "nairoclip/core/render_engine.h"

namespace nairoclip {
namespace core {

/// 氛围特效类型（与 EffectType 数值对齐；19..23）。
enum class AtmosphereEffectType {
    Snow = 19,       ///< 雪花
    Rain = 20,       ///< 下雨
    LightLeak = 21,  ///< 光斑
    OldFilm = 22,    ///< 老电影划痕
    Starburst = 23,  ///< 星光
};

/// 应用单个氛围特效（原地修改 RGB；alpha 不变）。
/// - effectType：19..23（AtmosphereEffectType 值）；其他值返回 true 且不改动；
/// - value：强度 0..1（0 = 关闭，零改动）；
/// - timeSeconds：时间（驱动粒子/闪烁；确定性——同一时刻同一结果）；
/// - quality：渲染质量分级（粒子数量按预览/导出分级）；
/// - 参数非法返回 false 且不改动缓冲。
bool applyAtmosphereEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                                 double value, double timeSeconds,
                                 const RenderQuality& quality = RenderQuality::exportQuality());

// ---- 单效果入口 ----

/// 雪花（粒子引擎；缓慢下落 + 飘移）。
bool applySnowRgba(std::uint8_t* pixels, int width, int height, double intensity,
                   double timeSeconds, const RenderQuality& quality);

/// 下雨（粒子引擎；快速下落 + 短拖尾）。
bool applyRainRgba(std::uint8_t* pixels, int width, int height, double intensity,
                   double timeSeconds, const RenderQuality& quality);

/// 光斑（确定性光斑叠加 + 闪烁动画）。
bool applyLightLeakRgba(std::uint8_t* pixels, int width, int height, double intensity,
                        double timeSeconds);

/// 老电影划痕（确定性竖线 + 画面抖动 + 颗粒）。
bool applyOldFilmRgba(std::uint8_t* pixels, int width, int height, double intensity,
                      double timeSeconds);

/// 星光（高光点十字星芒径向采样叠加）。
bool applyStarburstRgba(std::uint8_t* pixels, int width, int height, double intensity);

/// 说明文案（供 UI/日志）。
const char* atmosphereEffectPurpose();

}  // namespace core
}  // namespace nairoclip
