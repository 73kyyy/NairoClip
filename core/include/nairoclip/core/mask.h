#pragma once

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {

// =============================================================================
// 蒙版（Mask）—— 文档第 2 部分「✂️剪辑 → 蒙版：线性/镜面/圆形/矩形（羽化/反转）」
// =============================================================================
// 产品对应：
//   · 线性蒙版：一条可旋转的分界线，一侧可见、一侧隐藏（羽化过渡）；
//   · 镜面蒙版：以中心线为轴的带状窗口（两侧羽化）；
//   · 圆形蒙版：椭圆区域（可调大小）；
//   · 矩形蒙版：可旋转/圆角矩形区域；
//   · 羽化：边界过渡软度；反转：可见区与隐藏区互换。
//
// 语义：蒙版作用于片段的图层可见性——
//   · Transparent 模式：蒙版外 alpha=0（多层合成时透出下层，前景抠像/画中画基础）；
//   · Background 模式：蒙版外填充背景色（单层导出时的黑边/纯色底不透明输出）。
// 全部为确定性像素运算（零第三方依赖），预览与导出共用同一实现。

/// 蒙版形状（保持稳定枚举值供 UI 映射与序列化）。
enum class MaskShape {
    None = 0,       ///< 未使用蒙版（全可见）
    Linear = 1,     ///< 线性（单侧 + 羽化过渡）
    Mirror = 2,     ///< 镜面（中心带状窗口，两侧羽化）
    Circle = 3,     ///< 圆形/椭圆
    Rectangle = 4,  ///< 矩形（可旋转/圆角）
};

/// 蒙版外的填充策略。
enum class MaskBlendMode {
    /// 蒙版外 alpha=0（透出下层；用于多层合成）。
    Transparent = 0,
    /// 蒙版外用指定背景色填充（不透明；用于单层导出）。
    Background = 1,
};

/// 蒙版定义（归一化尺寸参数；坐标为**像素**输入到评估函数）。
struct Mask {
    MaskShape shape = MaskShape::None;
    /// 中心位置（归一化 0..1）。
    double centerX = 0.5;
    double centerY = 0.5;
    /// 旋转角（度）。
    ///  - Linear/Mirror：分界线的法线方向；
    ///  - Rectangle：矩形旋转角。
    double rotationDegrees = 0.0;
    /// 尺寸（归一化；含义随形状）：
    ///  - Rectangle：半宽（相对画面宽度）/ 半高（相对画面高度）；
    ///  - Circle：半轴（相对宽度/高度）；
    ///  - Mirror：width 未使用，height = 半带高（相对画面高度）；
    ///  - Linear：width 未使用，height = 分界线偏移（相对画面高度，0 = 过中心）。
    double width = 0.5;
    double height = 0.5;
    /// 羽化（0..0.5；相对画面短边的过渡带半宽比例）。
    double feather = 0.0;
    /// 矩形圆角半径（相对画面短边；仅 Rectangle 使用）。
    double cornerRadius = 0.0;
    /// 反转（可见区与隐藏区互换）。
    bool invert = false;

    /// 参数是否合法（供命令校验与反序列化防御）。
    bool valid() const {
        return feather >= 0.0 && feather <= 0.5 && cornerRadius >= 0.0 &&
               cornerRadius <= 0.5;
    }
};

/// 计算某像素点的蒙版覆盖率（0..1；1 = 完全可见）。
/// - x/y：像素坐标（可含小数）；
/// - width/height：画面像素尺寸；
/// - 返回 < 0 表示参数非法（形状非法/尺寸非正/羽化越界）；
/// - shape == None 返回 1.0（全可见）。
double maskCoverageAt(const Mask& mask, double x, double y, int width, int height);

/// 采样估计整体覆盖率（0..1；供 UI 展示「蒙版覆盖 X%」）。
/// - step：采样步长（像素；>=1，越大越快越粗）。
/// - 参数非法返回 < 0。
double maskCoverageRatio(const Mask& mask, int width, int height, int step = 2);

/// 对 RGBA8 缓冲应用蒙版。
/// - mode == Transparent：蒙版外 alpha 按覆盖率缩放（1 - 覆盖率）；
/// - mode == Background：蒙版外按覆盖率混入背景色（0xRRGGBBAA），alpha 保持不透明；
/// - 参数非法返回 false 且不改动缓冲；全覆盖（无遮蔽）时零改动返回 true。
bool applyMaskToFrame(std::uint8_t* pixels, int width, int height, const Mask& mask,
                      MaskBlendMode mode = MaskBlendMode::Transparent,
                      std::uint32_t backgroundRgba = 0x000000FF);

/// 形状名称（"none" / "linear" / "mirror" / "circle" / "rectangle"）。
const char* maskShapeName(MaskShape shape);
/// 从字符串解析（大小写不敏感；支持 "rect" / "oval" 等别名；非法返回 false）。
bool maskShapeFromString(const char* text, MaskShape& out);
/// 从整数解析（0..4；非法返回 false）。
bool maskShapeFromInt(int value, MaskShape& out);

}  // namespace core
}  // namespace nairoclip
