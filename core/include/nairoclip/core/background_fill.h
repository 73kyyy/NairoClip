#pragma once
// =============================================================================
// 背景填充（Background Fill）—— 文档 Part 4-C 第 11 节「背景」
// =============================================================================
// 产品对应（原文 11.1，三 Tab）：
//   · 颜色：预设色板网格 + 自定义取色器（色盘）；
//   · 模糊：模糊度单一滑块（0~100），将素材本身放大模糊后铺满空白区域作为背景；
//   · 画布样式：预设纹理缩略图网格（木纹/纸张/渐变等静态图案）。
//
// 关键边界（文档 11.2）：
//   · 背景为**单片段级别**（不同片段素材尺寸不同、留白情况不同）；
//   · 三个 Tab **互斥**（同一片段同一时刻只能生效一种填充方式）；
//   · 背景**不处理素材本身内容**（抠像换背景属于「智能抠像」，职责不同）；
//   · 仅为「素材之外的空白区域」填充（Fit 模式的信箱/邮筒条）。
//
// 实现说明：
//   · 颜色/模糊为真实像素运算（无第三方依赖）；
//   · 画布样式为**程序化纹理**（木纹/纸张/渐变等 16 种；确定性可测），
//     后续可替换为预制纹理素材（接口不变）；
//   · 三角色语义与「序列背景色」（frameFit 的信箱色）区分：本模块仅在
//     片段设置了背景填充时覆盖该片段的留白区域。

#include <cstdint>
#include <vector>

#include "nairoclip/core/render_engine.h"

namespace nairoclip {
namespace core {

/// 背景填充模式（文档 11.1 三 Tab；互斥）。
enum class BackgroundMode {
    None = 0,   ///< 未设置（留白 = 序列背景色）
    Color = 1,  ///< 颜色
    Blur = 2,   ///< 模糊（素材放大模糊）
    Style = 3,  ///< 画布样式（预制纹理）
};

/// 深度图应用效果模式（文档 Part 4-C 13.2；互斥）。
enum class DepthEffectMode {
    None = 0,       ///< 未设置
    Bokeh = 1,      ///< 景深虚化
    Fog = 2,        ///< 雾化
    ColorBands = 3, ///< 色彩分层
    Parallax = 4,   ///< 视差3D
};

/// 深度效果名称（"none"/"bokeh"/"fog"/"color_bands"/"parallax"）。
const char* depthEffectModeName(DepthEffectMode mode);
/// 从整数解析（0..4）。
bool depthEffectModeFromInt(int value, DepthEffectMode& out);

/// 模式名称（"none"/"color"/"blur"/"style"）。
const char* backgroundModeName(BackgroundMode mode);
/// 从字符串解析（大小写不敏感；支持中文「颜色」/「模糊」/「画布样式」）。
bool backgroundModeFromString(const char* text, BackgroundMode& out);
/// 从整数解析（0..3）。
bool backgroundModeFromInt(int value, BackgroundMode& out);

/// 画布样式预设（文档 11.1「木纹/纸张/渐变等」；数量由设计团队确定——此处 16 种）。
enum class CanvasStyle {
    None = 0,
    Wood = 1,        ///< 木纹
    Paper = 2,       ///< 纸张
    Linen = 3,       ///< 亚麻
    Concrete = 4,    ///< 水泥
    Marble = 5,      ///< 大理石
    GradientWarm = 6,///< 暖色渐变
    GradientCool = 7,///< 冷色渐变
    GradientMono = 8,///< 灰阶渐变
    Carbon = 9,      ///< 碳纤维
    Grid = 10,       ///< 网格
    Dots = 11,       ///< 圆点
    Bricks = 12,     ///< 砖块
    Fabric = 13,     ///< 织物
    Sand = 14,       ///< 沙粒
    Neon = 15,       ///< 霓虹
};

/// 画布样式名称（"wood"/"paper"/...）。
const char* canvasStyleName(CanvasStyle style);
/// 画布样式中文展示名（"木纹"/"纸张"/...）。
const char* canvasStyleDisplayName(CanvasStyle style);
/// 从整数解析（0..15）。
bool canvasStyleFromInt(int value, CanvasStyle& out);

/// 用背景填充画布的空白区域（原地修改；保留素材区域不变）。
/// - canvas：目标画布 RGBA8（宽 × 高）；
/// - source：已渲染到画布的素材像素（同尺寸；用于模糊模式的基底与
///   「素材区域」判定）——可为空（颜色/样式模式不需要）；
/// - sourceMask：素材区域蒙版（非 0 = 素材像素，保持原样；0 = 空白区域，
///   用背景填充）——**必须**提供（长度 = 宽 × 高）；
/// - mode：填充模式（None 时零改动）；
/// - colorRgba：颜色模式填充色；
/// - intensity：模糊度 0..100（模糊模式）/ 样式编号（样式模式）；
/// - quality：渲染质量分级（模糊采样与样式图案密度按质量分级）；
/// 返回 false：参数非法（缓冲区不足/尺寸非法）。
bool applyBackgroundFill(std::uint8_t* canvas, const std::uint8_t* source,
                         const std::uint8_t* sourceMask, int width, int height,
                         BackgroundMode mode, std::uint32_t colorRgba, double intensity,
                         const RenderQuality& quality = RenderQuality::exportQuality());

/// 生成程序化画布样式纹理（确定性；供预览缩略图与填充基底复用）。
/// - rgba 出参尺寸 = width × height × 4；
bool generateCanvasStyle(CanvasStyle style, int width, int height, std::vector<std::uint8_t>& rgba);

/// 模块说明文案（供 UI/日志）。
const char* backgroundFillPurpose();

}  // namespace core
}  // namespace nairoclip
