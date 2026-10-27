#pragma once
// =============================================================================
// 方向性运动模糊（B1-4）
// =============================================================================
// 为什么重构：原有 `MotionBlur` 是「相邻多帧加权叠加」。它物理上是对的，
// 但有两个硬伤：
//   1. 需要帧序列——单帧渲染（预览拖拽、缩略图）拿不到相邻帧，退化成占位；
//   2. 叠帧权重按「中央帧最高」的三角分布，与实际运动方向无关——
//      水平甩镜和垂直运动得到的模糊完全一样，视觉上没有方向感。
//
// 本模块改为**单帧内沿运动方向的多点采样**：
//   1. 从片段的位移动画（PositionX/Y）取当前时刻的速度矢量；
//   2. 沿该方向在 1..N 个采样点上取像素平均；
//   3. 采样越远权重越低（三角权重，与真实快门积分形状一致）。
//
// 与 `WhipPan` 的区别：WhipPan 是固定方向的风格化模糊（用户指定角度）；
// 本模块方向**由动画推导**，用户不必额外调角度，移动即模糊。
//
// **确定性**：全部由输入决定，无随机。

#include <cstdint>

namespace nairoclip {
namespace core {

/// 方向性运动模糊参数。
struct DirectionalBlurParams {
    /// 本帧内的位移量（归一化画幅宽；0 = 静止，无模糊）。
    ///
    /// **单位约定**：这是「一帧内移动了多少画幅」，不是「每秒」。选择这个
    /// 单位是因为本函数不接收帧率，用帧位移可以自洽地表达模糊长度：
    /// 模糊半长 = |位移| × (快门角度/360) / 2。
    /// 若你手上是「画幅/秒」的速度，先除以帧率再传入。
    double velocityX = 0.0;
    double velocityY = 0.0;
    /// 快门角度（度，0..360）。180° = 半帧曝光（电影标准）。
    /// 决定模糊长度：angle/360 × 速度。
    double shutterAngleDegrees = 180.0;
    /// 采样点数（2..32）。越多越平滑，代价是 O(N) 次采样。
    int samples = 8;
    /// 最大模糊长度（占画幅宽比例，0..0.5）。防止极快运动把画面糊成一团。
    double maxLength = 0.2;
};

/// 参数是否合法。
bool directionalBlurParamsValid(const DirectionalBlurParams& params);

/// 由位移动画的两个采样点求「每帧位移」（与 DirectionalBlurParams 的单位一致）。
/// - 先用 (后-前)/时间差 求速度，再乘 frameDurationSeconds 换算成帧位移；
/// - dt <= 0 或 frameDuration <= 0 返回 false。
bool velocityFromPositions(double x0, double y0, double x1, double y1, double dtSeconds,
                           double frameDurationSeconds, double& outVx, double& outVy);

/// 应用方向性模糊（原地修改 RGB；alpha 不变）。
/// - 速度接近 0 → 零改动返回 true；
/// - 参数非法（空指针/尺寸非法/参数越界）返回 false 且不改动缓冲。
bool applyDirectionalBlurToRgba(std::uint8_t* pixels, int width, int height,
                                const DirectionalBlurParams& params);

/// 说明文案（供 UI/日志）。
const char* directionalBlurPurpose();

}  // namespace core
}  // namespace nairoclip
