#pragma once
// =============================================================================
// 画中画混合模式（Blend Modes）—— 文档第二段「5.9 画中画混合模式」
// =============================================================================
// 产品对应（原文 5.9）：
//   | 正常         | 标准Alpha混合 |
//   | 滤色（Screen）| result = 1 - (1-A)×(1-B) |
//   | 叠加（Overlay）| 经典Photoshop混合公式，按底图亮度分段应用正片叠底或滤色 |
//   | 变暗（Darken）| result = min(A, B) |
//   | 强光（Hard Light）| Overlay公式变体，交换AB层级关系 |
//   工程要点：「均为图形行业标准公式，一次性实现后即可复用」。
//
// 语义约定（与全项目多层合成一致）：
//   · 下层（背景）= 序列/底层轨已渲染的像素（dst）；
//   · 上层（画中画）= 当前片段像素（src）；
//   · 混合结果 = 按模式公式计算，再按上层 alpha 与标准 alpha 混合合成
//     （即：mode(A,B) 只在 alpha 权重内生效，保持边缘/透明度语义正确）。
//
// 实现说明（真实、可测，零第三方依赖）：
//   · 全部为图形行业标准公式（Photoshop 混合公式，公开领域）；
//   · 逐像素确定性运算（同一输入 → 同一输出）；
//   · 实现为纯函数（不依赖 EffectStack），供导出/预览多层合成路径直接调用。

#include <cstdint>
#include <string>

#include "nairoclip/core/render_engine.h"

namespace nairoclip {
namespace core {

/// 画中画混合模式（数值稳定，供序列化与 C ABI 映射）。
enum class BlendMode {
    Normal = 0,     ///< 正常（标准 Alpha 混合）
    Screen = 1,     ///< 滤色
    Overlay = 2,    ///< 叠加
    Darken = 3,     ///< 变暗
    HardLight = 4,  ///< 强光
};

/// 模式名称（"normal" / "screen" / "overlay" / "darken" / "hard_light"）。
const char* blendModeName(BlendMode mode);
/// 从字符串解析（大小写不敏感；支持 "hardlight" / "hard-light" 别名）。
bool blendModeFromString(const char* text, BlendMode& out);
/// 从整数解析（0..4；非法返回 false）。
bool blendModeFromInt(int value, BlendMode& out);

/// 计算单通道混合值（输入/输出 0..1；行业标准公式）。
/// - base：下层（背景）通道值；
/// - top：上层（画中画）通道值；
/// - Normal 返回 top（标准 Alpha 混合由调用方处理 alpha 权重）。
double blendChannel(BlendMode mode, double base, double top);

/// 对整帧应用混合：把 top 层（画中画）按 mode 混合到 base 上（原地修改 base）。
/// - base/top：交错 RGBA8，尺寸必须一致（width × height）；
/// - opacity：额外不透明度乘数（0..1；默认 1 = 完全按 alpha）；
/// - 说明：合成权重 = top.alpha / 255 × opacity；
///   mode 公式作用于 RGB，之后按权重线性合成（保持半透明边缘正确）。
/// - 参数非法（空指针/尺寸非法/尺寸不一致）返回 false 且不改动 base。
bool blendFrameRgba(std::uint8_t* base, const std::uint8_t* top, int width, int height,
                    BlendMode mode, double opacity = 1.0);

/// 说明文案（供 UI/日志）。
const char* blendModePurpose();

}  // namespace core
}  // namespace nairoclip
