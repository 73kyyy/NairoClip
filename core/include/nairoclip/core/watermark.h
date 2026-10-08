#pragma once

#include "nairoclip/core/media_io.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 导出水印（Export Watermark）—— 免费版导出附加的可见标识
// =============================================================================
// 产品对应（文档第 6 部分 3.9「水印渲染规格」，逐项精确实现）：
//   · 位置：右下角，距边缘 16px（1080p 基准，按画布短边等比缩放）
//   · 尺寸：宽度 = 画面宽度 × 8%
//   · 不透明度：70%
//   · 内容：App Logo + 说明文字
//   · 颜色：白色文字 + 30% 黑色描边
//
// 渲染分工（与既有 ai_label / subtitle 架构一致）：
//   · Logo 标记：本模块用程序化光栅化绘制真实像素（零字体依赖、可测试）；
//   · 说明文字：输出为渲染项描述符（SubtitleRenderItem），由平台层用系统字体绘制。
//
// 合规：水印属于免费版导出的产品约束，由导出管线强制应用（文档 3.1/3.10）。

/// 水印渲染规格。
struct WatermarkSpec {
    /// 是否启用（导出管线按此开关逐帧应用；权益门控见 entitlement.h）。
    bool enabled = false;
    /// 说明文字（平台层绘制；文档文案「用剪辑软件制作」）。
    std::string caption = "用剪辑软件制作";
    /// 宽度占比（文档：画面宽度 × 8%）。
    double widthRatio = 0.08;
    /// 不透明度（文档：70%）。
    double opacity = 0.70;
    /// 描边不透明度（文档：30% 黑色描边，相对水印层）。
    double outlineOpacity = 0.30;
    /// 基准边距（px；文档：16px @ 1080p，按短边等比缩放）。
    int referenceMarginPx = 16;
    /// 基准短边（px；上者对应的画布短边）。
    int referenceShortSide = 1080;
};

/// 水印几何（针对具体输出尺寸计算；供渲染与平台层布局共用）。
struct WatermarkGeometry {
    /// 距画面右下边缘的实际边距（px）。
    int marginPx = 0;
    /// 水印块外接矩形（左上角坐标 + 尺寸）。
    int blockX = 0;
    int blockY = 0;
    int blockW = 0;  // = round(画面宽 × widthRatio)
    int blockH = 0;
    /// Logo 标记方形区域。
    int iconX = 0;
    int iconY = 0;
    int iconSize = 0;
    /// 说明文字区域（描述符坐标参考）。
    int textX = 0;
    int textY = 0;
    int textW = 0;
    int textH = 0;
};

/// 计算水印几何（含右下角定位与各子区域切分）。
/// 返回 false：参数非法（画面尺寸非正）。几何保证落在画面内（极端小画面时钳制）。
bool computeWatermarkGeometry(const WatermarkSpec& spec, int frameW, int frameH,
                              WatermarkGeometry& out);

/// 应用水印到一帧画面（交错 RGBA8）：
///  - 在 icon 区域光栅化程序化 Logo 标记（白色 70% + 30% 黑色描边）；
///  - 把说明文字追加到渲染项列表（items 可为空；平台层负责实际文字绘制）。
/// 返回 false：参数非法（空指针 / 尺寸非正；不改动任何像素）。
bool applyWatermark(std::uint8_t* frame, int frameW, int frameH, const WatermarkSpec& spec,
                    std::vector<SubtitleRenderItem>* items);

}  // namespace core
}  // namespace nairoclip
