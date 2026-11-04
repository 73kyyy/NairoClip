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
#include <cmath>
#include <vector>

#include "nairoclip/core/render_engine.h"

namespace nairoclip {
namespace core {

/// 背景填充模式（互斥；四种只能选一个）。
///
/// **Blur 与 CopyBackground 的区别（最容易混淆的一对）**：
///   · Blur           —— 取**当前片段自己**的画面，放大模糊铺满
///   · CopyBackground —— 取**当前帧下方所有轨道合成后的完整画面**
///                       作背景（可以是多层叠加结果）
///
/// 典型场景：竖屏素材放进横屏工程，且下方还有画中画。
///   Blur 只会看到竖屏那一层的模糊版（画中画不会出现在背景里）；
///   CopyBackground 会把「竖屏 + 画中画」的合成画面一起做背景。
enum class BackgroundMode {
    None = 0,   ///< 未设置（留白 = 序列背景色）
    Color = 1,  ///< 颜色（可随机生成）
    Blur = 2,   ///< 模糊（本片段素材放大模糊）
    Style = 3,  ///< 画布样式（预制纹理）
    CopyBackground = 4,  ///< **新增**：复制下方合成画面作背景（§4.8）
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

/// 模式名称（"none"/"color"/"blur"/"style"/"copy_background"）。
const char* backgroundModeName(BackgroundMode mode);
/// 从字符串解析（大小写不敏感；中文支持「颜色」/「模糊」/「画布样式」/「复制背景」）。
bool backgroundModeFromString(const char* text, BackgroundMode& out);
/// 从整数解析（0..4）。越界返回 false，out 不变。
bool backgroundModeFromInt(int value, BackgroundMode& out);

/// 背景填充的可调参数（§4.8 表格）。
///
/// 为什么单独一个结构而不是塞进 BackgroundMode：模式是**互斥枚举**
/// （只能选一个），参数是**连续量**（可打关键帧）。混在一起会让
/// 序列化变成「用不到的模式也存一堆参数」。
///
/// 这些参数只对 Blur / CopyBackground 有意义；Color/Style 用不到
/// （Color 只需 rgb，Style 只需 styleId）。
struct BackgroundFillParams {
    /// 快照整体不透明度 0..1。
    double opacity = 1.0;
    /// 背景虚化半径（像素）；0 = 不模糊。
    /// 默认 12：竖屏进横屏最常用的「不抢主体视线」档位。
    double blurPx = 12.0;
    /// 放大倍率 0.5..2.0（默认 1.1）。
    /// >1 是为了避免边缘露出 —— 模糊会让边缘透出底色。
    double scale = 1.1;
    /// 亮度偏移 -1..1。
    double brightness = 0.0;
    /// 饱和度倍率 0..2（1 = 不变）。
    double saturation = 1.0;
    /// 边缘羽化 0..1（0 = 硬边）。
    double feather = 0.0;

    /// 是否全部为默认值（用于「无效果」快速判断，跳过整条渲染）。
    bool isDefault() const {
        return opacity >= 0.999 && blurPx <= 0.001 && std::abs(scale - 1.0) <= 0.001 &&
               std::abs(brightness) <= 0.001 && std::abs(saturation - 1.0) <= 0.001 &&
               feather <= 0.001;
    }
};

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
