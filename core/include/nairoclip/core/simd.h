#pragma once

/* =============================================================================
   SIMD 加速核心循环（P4）
   -----------------------------------------------------------------------------
   只优化**热点且可向量化**的三处（见 PERF_XFORM_PLAN.md §P4）：
     1. RGBA 通道交换（去交织）
     2. 调色（亮度/对比度/饱和度）
     3. 整型混合 lerp（>>8）

   **不做全面 SIMD 化** —— 那会让代码难以维护，且大部分循环不是瓶颈。

   设计原则（两条，都很重要）：
     · 用 `#if defined(__ARM_NEON)` 守卫，**保留标量回退路径**。
       x86 CI、无 NEON 的老设备走的仍是标量实现。
     · 标量与 NEON 必须 **bit-exact 一致**。这是硬性要求：
       预览与导出可能走不同路径（一个在 ARM 设备、一个在 x86 CI），
       结果不一致就是「预览对、导出偏色」这类最难查的 bug。
       因此 NEON 路径刻意用与标量**相同的运算顺序与舍入方式**，
       不为了快一丁点而改用近似算法。
   ============================================================================= */

#include <cstddef>
#include <cstdint>

namespace nairoclip {
namespace core {

/// RGBA → BGRA 原地交换通道（R 与 B 互换），逐像素。
/// 用途：Android Bitmap 是 ARGB_8888/ABGR，与 core 的 RGBA 约定不同。
void rgbaToBgraInPlace(std::uint8_t* pixels, std::size_t pixelCount);

/// 整型 lerp：out = (a * (256 - w) + b * w) >> 8，w ∈ [0, 256]。
/// 用于 LUT 强度混合、透明度合成等。逐通道 8bit，无浮点。
void lerpRgbaInPlace(std::uint8_t* dst, const std::uint8_t* src, std::size_t pixelCount,
                     int weight256);

/// 该构建是否启用了 NEON（供测试断言与诊断展示）。
bool simdHasNeon();

/// 是否走的是 16 字节对齐的快路径（诊断用）。
bool simdIsAligned16(const void* p);

}  // namespace core
}  // namespace nairoclip
