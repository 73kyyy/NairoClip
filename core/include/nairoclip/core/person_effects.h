#pragma once
// =============================================================================
// 人物特效（Person Effects）—— 文档 Part 4-B 5.2「人物特效」预设清单
// =============================================================================
// 产品对应（文档 Part 4-B 第 5.2 节「人物特效」大类，预设 5 项）：
//   · 抖动（整帧随机小幅度位移，时间噪声函数驱动）；
//   · 放大（画面中心区域局部放大采样）；
//   · 瘦身（人像区域横向压缩变形；依赖抠像 Alpha 蒙版）；
//   · 发光轮廓（抠像 Alpha 边缘高斯模糊 + 发光色叠加）；
//   · 边缘检测（Sobel 边缘检测）。
//
// 实现约束（与全项目一致）：
//   · 纯 CPU 像素运算（零第三方依赖；GPU 后端可复用同一参数语义）；
//   · 确定性：同一 (帧内容, 强度, 时间) → 同一输出（可测）；
//   · 强度 0 = 零改动（不产生伪影）；
//   · 「瘦身」「发光轮廓」依赖抠像 Alpha：**无 Alpha 时零改动**
//     （不伪造人物区域；文档 5.2 工程要点明确其排在智能抠像之后）。
//
// 与文档的对应说明：
//   · 文档 5.2 指出「瘦身」「发光轮廓」需复用智能抠像（MediaPipe）的 Alpha；
//     本模块提供 Alpha 接口，模型就绪后由平台层传入真实 Alpha，语义不变。

#include <cstdint>

#include "nairoclip/core/render_engine.h"

namespace nairoclip {
namespace core {

/// 人物特效类型（与 EffectType 数值对齐；31..35）。
enum class PersonEffectType {
    Jitter = 31,       ///< 抖动（整帧随机小位移；时间噪声驱动）
    ZoomIn = 32,       ///< 放大（中心区域局部放大采样）
    EdgeDetect = 33,   ///< 边缘检测（Sobel）
    Slim = 34,         ///< 瘦身（人像区域横向压缩；需 Alpha）
    GlowOutline = 35,  ///< 发光轮廓（Alpha 边缘发光；需 Alpha）
};

/// 应用单个人物特效（原地修改 RGB；alpha 通道不变）。
/// - effectType：31..35；其他值返回 true 且不改动（由其他模块处理）；
/// - value：强度 0..1（0 = 关闭，零改动）；
/// - timeSeconds：时变驱动（抖动；同一时刻 → 同一结果，可测）；
/// - alpha：可选抠像 Alpha（灰度 0..255；长度 = width × height）。
///   仅「瘦身」「发光轮廓」消费；nullptr 时这两项零改动；
/// - quality：渲染质量分级（邻域采样/模糊半径按质量分级）；
/// - 参数非法（缓冲区不足/尺寸非法）返回 false 且不改动缓冲。
bool applyPersonEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                             double value, double timeSeconds,
                             const std::uint8_t* alpha = nullptr,
                             const RenderQuality& quality = RenderQuality::exportQuality());

// ---- 单效果入口（供测试与 GPU 后端参数对齐）----

/// 抖动：整帧按 (dx, dy) 位移（时间噪声驱动；边缘以镜像填充）。
bool applyJitterRgba(std::uint8_t* pixels, int width, int height, double intensity,
                     double timeSeconds);

/// 放大：以画面中心为锚点放大（中心放大采样；边缘外像素取边界）。
bool applyZoomInRgba(std::uint8_t* pixels, int width, int height, double intensity);

/// 边缘检测：Sobel 梯度幅值 → 边缘图（按强度混入原图）。
/// 语义：intensity = 0 原图；1 完全为边缘图。
bool applyEdgeDetectRgba(std::uint8_t* pixels, int width, int height, double intensity);

/// 瘦身：以 Alpha 为形变权重，区域横向压缩（最大 50%）。
/// alpha 为空时返回 true 且零改动（不伪造人像区域）。
bool applySlimRgba(std::uint8_t* pixels, int width, int height, double intensity,
                   const std::uint8_t* alpha);

/// 发光轮廓：Alpha 边缘检测 + 模糊 + 发光色（暖白）叠加。
/// alpha 为空时返回 true 且零改动。
bool applyGlowOutlineRgba(std::uint8_t* pixels, int width, int height, double intensity,
                          const std::uint8_t* alpha, const RenderQuality& quality);

}  // namespace core
}  // namespace nairoclip
