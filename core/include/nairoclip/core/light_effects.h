#pragma once
// =============================================================================
// 光效（B1-3）：Bloom（泛光）+ 镜头光晕（Lens Flare）
// =============================================================================
// 与既有光效的分工：
//   · `LightLeak`/`Starburst`（atmosphere_effects）：风格化的叠加光斑；
//   · `GlowOutline`（person_effects）：依赖抠像 Alpha，只在有人像时可用；
//   · 本模块：**基于画面亮度真实提取**的物理感光效，不需要 Alpha、不依赖素材。
//
// Bloom 原理（三步，与影视后期一致）：
//   1. 高光提取：亮度超过 threshold 的像素才参与（其余置黑）；
//   2. 高斯模糊：让高光向外扩散（半径决定扩散范围）；
//   3. 叠加回原图：按 intensity 加权相加。
// 为什么不能省掉"高光提取"直接模糊整帧：那会把暗部也提亮，
// 画面整体发灰、对比度塌陷——看起来像"镜头脏了"而不是"光在发亮"。
//
// 镜头光晕原理：把光源位置投影到画面，沿"画面中心 → 光源"的轴
// 放置若干鬼影（ghost），每个鬼影是带色偏的圆盘，按距离衰减。
// 色温参数控制色偏方向（暖 → 橙金，冷 → 青蓝）。

#include <cstdint>

namespace nairoclip {
namespace core {

/// Bloom（泛光）参数。
struct BloomParams {
    /// 高光阈值 0..1：亮度高于它的像素才发光。
    /// 0 = 全图参与（等于整帧变亮，通常不是想要的效果）；
    /// 默认 0.8 让只有真高光（灯、高光反射、白字）发光。
    double threshold = 0.8;
    /// 模糊半径（像素，0..64）。决定光扩散多远。
    double radiusPx = 12.0;
    /// 强度 0..1：叠加权重。
    double intensity = 0.6;
};

/// Bloom 参数是否合法。
bool bloomParamsValid(const BloomParams& params);

/// 应用 Bloom（原地修改 RGB；alpha 不变）。
/// - 强度 0 或半径 0 → 零改动并返回 true；
/// - 参数非法（空指针/尺寸非法/参数越界）返回 false 且不改动缓冲。
bool applyBloomToRgba(std::uint8_t* pixels, int width, int height, const BloomParams& params);

/// 镜头光晕参数。
struct LensFlareParams {
    /// 光源位置（归一化 0..1；(0.5,0.5) 为正中心）。
    double sourceX = 0.7;
    double sourceY = 0.3;
    /// 光源大小 0..1（相对画面短边）。0 = 无光源圆盘，仅有鬼影。
    double size = 0.15;
    /// 色温 -1..1：负 = 冷（青蓝），0 = 中性白，正 = 暖（橙金）。
    double temperature = 0.3;
    /// 可见度 0..1（整体强度）。
    double intensity = 0.5;
    /// 鬼影数量 0..6：沿「中心→光源」轴线反方向依次排布的圆盘数量。
    int ghostCount = 3;
};

/// 镜头光晕参数是否合法。
bool lensFlareParamsValid(const LensFlareParams& params);

/// 应用镜头光晕（原地修改 RGB；alpha 不变）。
bool applyLensFlareToRgba(std::uint8_t* pixels, int width, int height,
                          const LensFlareParams& params);

/// 说明文案（供 UI/日志）。
const char* lightEffectsPurpose();

}  // namespace core
}  // namespace nairoclip
