#pragma once

#include <cstdint>

namespace nairoclip {
namespace core {

// =============================================================================
// 画幅适配（Frame Fit）—— 源画面放入目标画布的几何与像素处理
// =============================================================================
// 场景（对应 UI/需求）：
//   - 导出预设 9:16 / 16:9 / 1:1 与素材比例不一致；
//   - 「智能重构图：横转竖 / 竖转横」需要一个确定的适配策略；
//   - 混比例素材（手机竖拍 + 相机横拍）拼剪时避免拉伸变形或左上角错位。
//
// 三种专业模式：
//   Fit  —— 等比内缩，完整保留画面，余量填充背景（信箱/邮筒黑边），不裁切内容；
//   Fill —— 等比放大铺满画布，超出部分居中裁切，无黑边（会裁掉部分画面）；
//   Crop —— 不缩放，取源画面中心区域 1:1 居中放置（像素级精确，用于等比例裁切）。
//
// 说明：本模块为纯计算（零第三方依赖），供导出执行器 / 预览 / 平台层复用。

/// 画幅适配模式。
enum class FrameFit {
    Fit = 0,   ///< 等比内缩居中，余量填充背景（默认，最安全）
    Fill = 1,  ///< 等比铺满并居中裁切（无黑边）
    Crop = 2,  ///< 1:1 居中裁切（不缩放）
};

/// 画幅适配几何计算结果。
struct FrameFitRect {
    /// 源取用区域（Fit/Fill 为整幅；Crop 为居中裁切区域）。
    int srcX = 0;
    int srcY = 0;
    int srcW = 0;
    int srcH = 0;
    /// 目标绘制区域（Fit 为内缩后矩形；Fill 允许负偏移表示溢出裁切）。
    int dstX = 0;
    int dstY = 0;
    int dstW = 0;
    int dstH = 0;
    /// 实际采用的缩放系数（Crop 恒为 1.0）。
    double scale = 1.0;
};

/// 计算画幅适配几何。任一尺寸非正时返回 false（out 不被修改）。
bool computeFrameFitRect(FrameFit mode, int srcW, int srcH, int dstW, int dstH,
                         FrameFitRect& out);

/// 把源 RGBA8 图像按 mode 渲染到目标缓冲。
/// - dst 必须已分配 dstW × dstH × 4 字节；函数整体写入（含背景填充）；
/// - Fit/Fill 采用双线性采样；Crop 为逐像素精确拷贝（无插值损失）；
/// - srcStride 为源每行字节数（0 表示按 srcW × 4 计算）；
/// - backgroundRgba 为背景色（0xRRGGBBAA；默认不透明黑 = 信箱黑边）；
/// - 参数非法（空指针 / 非正尺寸 / stride 过小）返回 false 且不触碰 dst。
bool renderFrameFit(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                    std::uint8_t* dst, int dstW, int dstH, FrameFit mode,
                    std::uint32_t backgroundRgba = 0x000000FF);

/// 模式名称（"fit" / "fill" / "crop"；未知值返回 "fit"）。
const char* frameFitName(FrameFit mode);

/// 从整数解析（0=fit 1=fill 2=crop；非法返回 false）。
bool frameFitFromInt(int value, FrameFit& out);

/// 从字符串解析（"fit"/"fill"/"crop"，大小写不敏感；非法返回 false）。
bool frameFitFromString(const char* text, FrameFit& out);

}  // namespace core
}  // namespace nairoclip
