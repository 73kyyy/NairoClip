#pragma once

#include "nairoclip/core/transition_types.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <vector>

namespace nairoclip {
namespace core {

/// 转场渲染：在 CPU 上对单帧 RGB 图像做两路输入混合。
/// 输入为归一化的「进度」t∈[0,1]（0 = 完全 A，1 = 完全 B）。
///
/// 支持的渲染类型：
///  - CrossDissolve / FadeToBlack / FadeFromBlack / FlashWhite：逐像素混合
///  - WipeLeft / WipeRight / SlideUp：空间扫描式（按像素位置）
///  - Zoom：径向缩放混合（简化：中心扩散）
///
/// 说明：该类是 CPU 参考实现（用于导出与预览的正确性），
/// 平台层可替换为 GPU（Metal/Vulkan）实现，接口语义保持一致。
class TransitionRenderer {
public:
    /// 渲染一帧混合结果。
    /// - a、b：输入帧（RGBA8 交错，尺寸必须相同）
    /// - out：输出帧（RGBA8，尺寸与输入相同）
    /// - progress：0..1 转场进度（会被钳制）
    /// - 若帧尺寸不匹配或为空，输出保持 out 内容不变并返回 false。
    static bool renderFrame(TransitionType type, const std::uint8_t* a, const std::uint8_t* b,
                            std::uint8_t* out, int width, int height, double progress);

    /// 计算转场进度：当前时间在 [start, start+duration) 中的比例（钳制 0..1）。
    static double transitionProgress(const Time& current, const Time& start, const Time& duration);
};

}  // namespace core
}  // namespace nairoclip
