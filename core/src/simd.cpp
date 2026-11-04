#include "nairoclip/core/simd.h"

#include <algorithm>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define NC_HAS_NEON 1
#else
#define NC_HAS_NEON 0
#endif

namespace nairoclip {
namespace core {

bool simdHasNeon() { return NC_HAS_NEON != 0; }

bool simdIsAligned16(const void* p) {
    return (reinterpret_cast<std::uintptr_t>(p) & 0xF) == 0;
}

/* ---------------------------------------------------------------- RGBA ↔ BGRA */
void rgbaToBgraInPlace(std::uint8_t* pixels, std::size_t pixelCount) {
    if (pixels == nullptr || pixelCount == 0) return;

#if NC_HAS_NEON
    /* vld4q_u8 把交错 RGBA 拆成 4 个通道向量，交换 R/B，再 vst4q_u8 交织回去。
       一次处理 16 像素（4 个 q 寄存器 × 16 字节 = 64 字节）。
       alpha 保持不变。 */
    std::size_t i = 0;
    for (; i + 16 <= pixelCount; i += 16) {
        uint8x16x4_t v = vld4q_u8(pixels + i * 4);
        uint8x16x4_t o;
        o.val[0] = v.val[2];   // B 放到 R 位
        o.val[1] = v.val[1];   // G 不变
        o.val[2] = v.val[0];   // R 放到 B 位
        o.val[3] = v.val[3];   // A 不变
        vst4q_u8(pixels + i * 4, o);
    }
    for (; i < pixelCount; ++i) {
        std::swap(pixels[i * 4], pixels[i * 4 + 2]);
    }
#else
    for (std::size_t i = 0; i < pixelCount; ++i) {
        std::swap(pixels[i * 4], pixels[i * 4 + 2]);
    }
#endif
}

/* ---------------------------------------------------------------- 整型 lerp */
void lerpRgbaInPlace(std::uint8_t* dst, const std::uint8_t* src, std::size_t pixelCount,
                     int weight256) {
    if (dst == nullptr || src == nullptr || pixelCount == 0) return;
    const int w = std::clamp(weight256, 0, 256);
    if (w == 0) return;
    if (w == 256) {
        std::copy(src, src + pixelCount * 4, dst);
        return;
    }
    const int iw = 256 - w;

#if NC_HAS_NEON
    /* 用 u16 中间结果：8bit × 8bit = 16bit，不会溢出。
       vmovl_u8 把 8bit 扩到 16bit，vmull_n_u16 乘常数，
       vshrn_n_u16 带舍入右移回 8bit。
       舍入方式与标量的 `>> 8` **完全一致**（都是截断，不加 0.5），
       这是 bit-exact 的关键 —— 若这里用 vrshrn（带舍入）而标量用截断，
       两者就会差 1。 */
    const uint16x8_t vw = vdupq_n_u16(static_cast<std::uint16_t>(w));
    const uint16x8_t viw = vdupq_n_u16(static_cast<std::uint16_t>(iw));

    std::size_t i = 0;
    for (; i + 8 <= pixelCount * 4; i += 8) {
        const uint8x8_t d8 = vld1_u8(dst + i);
        const uint8x8_t s8 = vld1_u8(src + i);
        const uint16x8_t d16 = vmovl_u8(d8);
        const uint16x8_t s16 = vmovl_u8(s8);
        // (d * iw + s * w) >> 8 —— 分两步乘加，避免 16bit 溢出
        const uint16x8_t acc = vaddq_u16(vmulq_u16(d16, viw), vmulq_u16(s16, vw));
        vst1_u8(dst + i, vshrn_n_u16(acc, 8));
    }
    for (; i < pixelCount * 4; ++i) {
        dst[i] = static_cast<std::uint8_t>((static_cast<int>(dst[i]) * iw +
                                            static_cast<int>(src[i]) * w) >> 8);
    }
#else
    for (std::size_t i = 0; i < pixelCount * 4; ++i) {
        dst[i] = static_cast<std::uint8_t>((static_cast<int>(dst[i]) * iw +
                                            static_cast<int>(src[i]) * w) >> 8);
    }
#endif
}

}  // namespace core
}  // namespace nairoclip
