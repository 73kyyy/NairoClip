#pragma once
// =============================================================================
// 几何变换特效（Transform Effects）—— 「转换」面板 8 项
// =============================================================================
// 设计要点（务必先读）：
//
// 1) 8 项里 7 项是仿射变换。**绝不能各自读写一遍像素** —— 那样 7 个效果就是
//    7 次重采样，1080p 下每帧多出上百 MB 带宽，且每次双线性插值都糊一层。
//    正确做法：先把 7 个矩阵合成 1 个，再交给 renderFrameTransformed **采样一次**。
//    这是 AE 的 Collapse Transformations。
//
// 2) 8 槽固定数组而非单槽：用户加「旋转 + 偏移」是常见操作，单槽会强制互斥。
//
// 3) 合成顺序**按枚举值从小到大**，不按添加顺序 —— 否则同一工程在不同端
//    可能算出不同结果。
//
// 4) value <= 0 的槽**必须零改动**（逐字节等于输入）。这是可测的硬约束。
//
// 5) 栅格转换不是单一仿射（格间不连续），单独走网格路径。

#include <cstdint>

#include "nairoclip/core/frame_fit.h"

namespace nairoclip {
namespace core {

// =============================================================================
// 8 项几何变换
// =============================================================================
/// 与「转换」面板的 8 个未接入项一一对应（编辑器前端按同序排列）。
enum class XformKind : int {
    Offset = 0,   ///< 偏移：按方向平移
    Stretch = 1,  ///< 拉伸轴：单轴各向异性缩放
    Spin = 2,     ///< 旋转：绕锚点旋转（可随时间累加）
    Raster = 3,   ///< 栅格转换：切网格，每格独立偏移（非单一仿射）
    Path = 4,     ///< 沿路径移动：椭圆/圆/直线轨迹运动
    Scale = 5,    ///< 缩放辅助：等比缩放 + 镜像延展补边
    Pulse = 6,    ///< 脉冲大小：时间正弦驱动的周期缩放
    RandPos = 7,  ///< 随机位移：确定性伪随机抖动
    Count = 8,
};

/// 单个变换槽的参数。
/// 说明：为什么不塞进 Effect —— Effect 是单值结构 `{id,type,value,enabled}`，
/// 放不下 axis/cols/rows/gap，硬塞会让序列化变成猜谜。与 ShakeParams 同理。
struct XformParams {
    bool enabled = false;
    double value = 0.0;   ///< 主强度 [0,1]，0 = 关闭。UI 上各效果叫法不同：
                          ///< Offset=距离 / Stretch=倍率 / Spin=不变 / Scale=比例
                          ///< Raster=偏移强度 / Path=半径 / Pulse=峰值 / RandPos=幅度
    double angle = 0.0;   ///< 方向/固定角度（弧度）。Offset=位移方向，Spin=基础角度
    double freq = 2.0;    ///< 频率：Spin=自转 rev/s；Pulse=Hz；RandPos=每秒变化次数
    double phase = 0.0;   ///< 相位偏移（弧度）。Pulse 波形起始相位
    int axis = 0;         ///< Stretch：0=X 1=Y 2=对角
    int cols = 6;         ///< Raster：列数 [2,12]
    int rows = 6;         ///< Raster：行数 [2,12]
    double gap = 0.0;     ///< Raster：格间隙（相对格边长 [0,0.5]）
    int anchor = 4;       ///< Scale/Spin 锚点：0..8 九宫格，4=中心
    int seed = 0;         ///< RandPos：随机种子
    int trajectory = 0;   ///< Path：0=圆 1=椭圆 2=直线
    double period = 2.0;  ///< Path：周期（秒）
    int edge = 1;         ///< 边缘模式：0=背景 1=镜像 2=拉伸（默认镜像，无黑边）

    /// 钳制到合法范围（越界不报错，钳制 —— 与现有 Effect.value 处理一致）。
    void clamp();
};

/// 单槽默认值（面板首次打开、以及「⟲ 重置」都返回这里）。
/// 与 XformParams 的成员初值保持一致，改一处必须同步另一处。
XformParams defaultXformParams(XformKind kind);


/// 8 槽容器（挂在 Clip 上）。
struct XformStack {
    XformParams slots[static_cast<int>(XformKind::Count)];

    /// 是否有任一槽启用。
    bool anyEnabled() const;

    /// 按 kind 合并为单个仿射矩阵（画布坐标系，围绕画布中心）。
    /// - 只合并 enabled 且 value > 0 的仿射类槽（Raster 不参与，见下）；
    /// - timeSeconds 驱动时变效果（Path/Pulse/RandPos/Spin.speed）；
    /// - canvasW/canvasH 用于把相对量换算成像素（比例基于短边）。
    Affine2D composeAffine(int canvasW, int canvasH, double timeSeconds) const;

    /// 是否有启用的 Raster 槽（需单独走网格路径）。
    bool hasRaster() const;
};

// =============================================================================
// 渲 染
// =============================================================================

/// 把 8 项变换应用到源图，输出到 dst（单次重采样）。
///
/// - 若没有任何槽启用，本函数退化为一次普通画幅适配（等价 renderFrameFit）；
/// - 7 个仿射类合并为 1 个矩阵 → 双线性只做一次；
/// - Raster 若启用，在仿射结果之上再做网格切分（二次处理，但仅此一项）；
/// - edge 决定画面露出部分如何填充（背景 / 镜像 / 拉伸）；
/// - bilinear=false 走最近邻（预览快速档），结果与 true 不同但几何一致。
///
/// 参数非法（空指针 / 非正尺寸 / stride 过小）返回 false 且不触碰 dst。
bool applyXformStack(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                     std::uint8_t* dst, int dstW, int dstH, const XformStack& stack,
                     double timeSeconds, FrameFit fit, EdgeMode edge,
                     std::uint32_t backgroundRgba, bool bilinear);

/// 单槽矩阵计算（供单测与调试；正常渲染走 applyXformStack）。
/// Raster 返回单位阵（它不是单一仿射，由 applyXformStack 单独处理）。
Affine2D matrixForSlot(XformKind kind, const XformParams& p, int canvasW, int canvasH,
                       double timeSeconds);

/// 由源尺寸按画幅适配策略生成 src→dst 矩阵（与 renderFrameFit 几何一致）。
Affine2D fitMatrix(FrameFit mode, int srcW, int srcH, int dstW, int dstH);

/// kind 名称（"offset" / "stretch" / ... ；未知返回 "unknown"）。供日志与测试。
const char* xformKindName(XformKind kind);

/// 由字符串解析 kind（大小写不敏感；未知返回 false）。
bool xformKindFromString(const char* text, XformKind& out);

/// 取首个启用槽的边缘模式（各槽 edge 语义相同；无启用槽返回 Background）。
/// 渲染路径共用，避免预览/导出各写一份判断。
EdgeMode firstXformEdge(const XformStack& stack, std::uint32_t backgroundRgba);

}  // namespace core
}  // namespace nairoclip
