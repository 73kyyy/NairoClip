#pragma once
// =============================================================================
// 通用粒子引擎（Particle Engine）—— 文档第二段「5.3 氛围特效」
// =============================================================================
// 产品对应（原文 5.3 工程要点）：
//   「粒子类效果建议抽象出通用粒子引擎模块（参数化：数量/初速度/重力/
//     生命周期/纹理），便于后续扩展」。
//
// 设计（确定性、无状态、零第三方依赖）：
//   · 每个粒子的位置由「粒子索引 + 帧序号 + 参数」纯函数计算——
//     不需要状态机/上一帧缓存，任意帧可随机访问（导出、预览、跳帧都一致）；
//   · 伪随机源：murmur3 雪崩混合（与 spatial/screen 特效同源技术）；
//   · 粒子生命周期：t = fmod(帧时间 + 相位, 寿命) / 寿命；
//     位置 = 初始位置 + 初速度 × t × 寿命 + 0.5 × 重力 × t²；
//   · 循环重置：寿命结束回到初始位置（雪花循环下落、雨滴循环洗刷）。
//
// 与渲染后端的关系：
//   · 本模块为 CPU 参考实现（正确性基准）；GPU 后端（compute shader）按同一
//     参数语义实现（文档 4.4：预览/导出质量分级通过 sampleBudget 控制粒子数）。

#include <cstdint>
#include <string>

#include "nairoclip/core/render_engine.h"

namespace nairoclip {
namespace core {

/// 粒子引擎配置（参数化：数量/初速度/重力/生命周期）。
struct ParticleConfig {
    /// 粒子数量（完整精度下的数量；预览按 RenderQuality 缩放）。
    int count = 200;
    /// 初速度（归一化画面尺寸/秒；x 向右、y 向下为正）。
    double velocityX = 0.0;
    double velocityY = 0.25;
    /// 速度随机扰动范围（±该值；0 = 所有粒子同速）。
    double velocitySpread = 0.15;
    /// 重力（归一化尺寸/秒²；y 向下为正）。
    double gravityY = 0.0;
    /// 生命周期（秒；到期回到初始位置循环）。
    double lifetimeSeconds = 4.0;
    /// 粒子大小（归一化尺寸；相对画面短边）。
    double size = 0.006;
    /// 大小随机扰动范围（±该值）。
    double sizeSpread = 0.4;
    /// 初始位置确定性种子（不同种子 → 不同粒子布局）。
    std::uint64_t seed = 0x50415254ULL;  // "PART"

    bool valid() const {
        return count >= 1 && count <= 20000 && lifetimeSeconds > 0.0 && size > 0.0 &&
               size <= 0.5 && velocitySpread >= 0.0;
    }
};

/// 单个粒子的采样结果（归一化坐标；可越界——由调用方决定裁剪或环绕）。
struct ParticleSample {
    /// 位置（归一化；0..1 为画面内）。
    double x = 0.0;
    double y = 0.0;
    /// 尺寸（归一化；相对画面短边）。
    double size = 0.0;
    /// 生命周期进度（0 = 刚生成，1 = 即将重置）。
    double lifePhase = 0.0;
    /// 亮度/不透明度 [0, 1]（按生命周期淡入淡出）。
    double opacity = 1.0;
};

/// 采样第 index 个粒子在 frameIndex 帧（timeSeconds）的状态。
/// - 纯函数：同一 (index, timeSeconds, config) → 同一结果（确定性）；
/// - frameIndex 用作时变种子（雪花旋转/光斑闪烁等附加变化）；
/// - 参数非法返回 false（out 不变）。
bool sampleParticle(int index, double timeSeconds, const ParticleConfig& config,
                    ParticleSample& out);

/// 说明文案（供 UI/日志）。
const char* particleEnginePurpose();

}  // namespace core
}  // namespace nairoclip
