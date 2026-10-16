#pragma once
// =============================================================================
// 光流补帧 AI（Frame Interpolation）—— 文档 Part 4-A 1.7「光流补帧」真实推理
// =============================================================================
// 产品对应（原文 1.7）：
//   「目标帧率 [按钮组]：60fps / 120fps / 240fps
//     算法模式 [Radio]：标准(快速) / 高精度(较慢)
//     运动平滑度 [滑块] 0~100  默认 70
//     处理进度 [进度条] 0~100%」
//
// 本模块实现「真实推理」（RIFE ONNX）：
//   · 输入：前后两帧（RGBA8）+ timestep（0..1 插值位置）；
//   · 推理：RIFE → 中间帧；
//   · 输出：插值后的中间帧（RGBA8）；
//   · 倍率：目标帧率 → 每对帧插入 N 帧（60fps = 1 帧，120fps = 3 帧…）；
//   · 运动平滑度：控制 timestep 分布（均匀 vs 中间密集）。
//
// 可用性：需 ONNX Runtime + RIFE 模型；不可用时明确失败（不伪造插值帧）。
// 性能：CPU 单帧插值 ~100-400ms（128-512 分辨率）；平台层应经
//       AI 任务状态机驱动进度与取消。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nairoclip/core/ai_engine.h"

namespace nairoclip {
namespace core {

/// 目标帧率档位（文档 1.7「目标帧率」）。
enum class InterpolationTarget {
    Fps60 = 60,
    Fps120 = 120,
    Fps240 = 240,
};

/// 算法模式（文档 1.7「算法模式」）。
enum class InterpolationMode {
    Standard = 0,  ///< 标准（快速；推理分辨率与输入一致）
    HighPrecision = 1, ///< 高精度（较慢；内部放大推理再降采样）
};

/// 目标帧率名称（"60"/"120"/"240"）。
const char* interpolationTargetName(InterpolationTarget target);
/// 从整数解析（60/120/240）。
bool interpolationTargetFromInt(int value, InterpolationTarget& out);
/// 为「源帧率 → 目标帧率」计算每对相邻帧需插入的中间帧数。
/// 返回 0：无需插值（目标 <= 源）。
int interpolationInsertCount(int sourceFps, InterpolationTarget target);

/// 计算插入帧的 timestep 序列（0..1 开区间；长度为 insertCount）。
/// - smoothness：运动平滑度 0..100（50 = 均匀；>50 中间密集，<50 两端密集）。
std::vector<double> interpolationTimesteps(int insertCount, double smoothness);

/// 光流补帧推理器（RIFE 会话复用；单实例单线程）。
class InterpolationEngine {
public:
    InterpolationEngine();
    ~InterpolationEngine();
    InterpolationEngine(const InterpolationEngine&) = delete;
    InterpolationEngine& operator=(const InterpolationEngine&) = delete;

    /// 加载模型（RIFE .onnx 路径）。
    bool load(const std::string& modelPath, std::string* outputFailure = nullptr);
    bool loaded() const { return loaded_; }
    void unload();

    /// 插值单帧（两帧之间）。
    /// - frame0/frame1：RGBA8 输入帧（尺寸必须相同；建议 32 的倍数）；
    /// - timestep：插值位置 0..1（0 = frame0，1 = frame1，0.5 = 中间）；
    /// - mode：标准/高精度；
    /// - outRgba：出参插值帧（RGBA8；尺寸 = 输入尺寸）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：未加载 / 尺寸不匹配 / 推理失败。
    bool interpolate(const std::uint8_t* frame0, const std::uint8_t* frame1, int width, int height,
                     double timestep, InterpolationMode mode,
                     std::vector<std::uint8_t>& outRgba,
                     std::string* outputFailure = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

/// 模块说明（供 UI/日志）。
const char* interpolationAiPurpose();

}  // namespace core
}  // namespace nairoclip
