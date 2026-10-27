#pragma once
// =============================================================================
// 震动/摇晃特效（Shake）—— B1-5，从 depth_ai 的视差轨迹抽取为独立特效
// =============================================================================
// 产品场景：「超燃剪辑」里的冲击震动——踩点瞬间画面猛地一抖再快速衰减归位。
//
// 与既有 `Jitter` 的区别：
//   · Jitter 是**纯随机**抖动（每帧独立噪声，无方向、无衰减），适合持续的手持感；
//   · Shake 是**时间函数 + 衰减包络**的位移（正弦/圆周/随机三选一），
//     适合「一次冲击 → 迅速平息」的节拍动作。
//
// **确定性是本模块的第一约束**：
//   同参数、同一时间点，任意次渲染必须得到**逐像素一致**的结果。
//   否则预览与导出不一致、三端（iOS/Android/桌面）成片不同、问题无法复现。
//   因此全部随机性都来自 `seed` 的纯函数哈希，不用 std::random_device / rand()。
//
// 实现说明（真实、可测，零第三方依赖）：
//   · 位移量为亚像素，采样用双线性插值；
//   · 边缘以镜像方式填充，避免黑边；
//   · alpha 通道原样保留（位移不改变透明度分布的形状）。

#include <cstdint>

namespace nairoclip {
namespace core {

/// 震动轨迹类型。
enum class ShakeTrajectory {
    Horizontal = 0,  ///< 水平正弦（左右晃，最常见）
    Vertical = 1,    ///< 垂直正弦（上下颠）
    Circular = 2,    ///< 圆周（绕中心摇）
    Random = 3,      ///< 伪随机（每个时间步一次性偏移，冲击感最强）
};

/// 震动参数。
///
/// 默认值对应「踩点轻震」——开箱即可用，不必先调参。
struct ShakeParams {
    /// 轨迹类型。
    ShakeTrajectory trajectory = ShakeTrajectory::Horizontal;
    /// 振幅（占画面短边的比例，0..0.2）。0.02 = 短边的 2%。
    /// 上限 0.2 是经验值：再大就露出画面外的空白，观感变成「跑偏」而非「震动」。
    double amplitude = 0.02;
    /// 频率（Hz，0..20）。0 = 完全不振荡（只保留衰减包络的初始位移）。
    double frequency = 8.0;
    /// 随机种子（确定性）。同一 seed 永远得到同一条轨迹。
    std::uint32_t seed = 0;
    /// 衰减时长（秒，0..10）。0 = 不衰减（持续震动）；
    /// > 0 时振幅在此时长内按指数衰减到 ~0 —— 用于「冲击后迅速平息」。
    double decaySeconds = 0.0;
    /// 相位偏移（弧度）。用于多条轨道错开，避免所有震动完全同步。
    double phase = 0.0;
};

/// 该参数是否合法（供 UI 校验与命令拒绝）。
bool shakeParamsValid(const ShakeParams& params);

/// 计算某一时刻的位移（像素；相对于画面中心，返回 dx, dy）。
///
/// 拆出来单独暴露的目的：位移是纯计算，可以脱离像素缓冲做穷举测试
/// （例如断言「同 seed 同 t 输出完全相同」「衰减后位移单调趋零」）。
void shakeOffsetAt(const ShakeParams& params, double timeSeconds, int width, int height,
                   double& outDx, double& outDy);

/// 对整帧应用震动（原地修改 RGB；alpha 不变）。
/// - intensity：额外强度乘数 0..1（供 Effect.value 直接驱动；0 = 零改动）；
/// - 参数非法（空指针/尺寸非法/参数越界）返回 false 且不改动缓冲。
bool applyShakeToRgba(std::uint8_t* pixels, int width, int height, const ShakeParams& params,
                      double timeSeconds, double intensity = 1.0);

}  // namespace core
}  // namespace nairoclip
