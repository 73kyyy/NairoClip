#pragma once
// =============================================================================
// 动感特效（Motion Effects）—— 文档第二段「5.4 动感特效」+「5.5 运动模糊」
// =============================================================================
// 产品对应（原文 5.4）：
//   | 缩放冲击 | 时间函数驱动的画面周期性快速放大再恢复 |
//   | 甩镜      | 方向性模糊Shader实现，可与旋转模糊共享底层算法 |
//   | 变速闪烁  | 基于节拍检测结果触发画面亮度瞬间提升再恢复 |
//   | 节奏律动  | 基于节拍检测驱动画面局部元素周期性缩放动画 |
//   工程要点：「变速闪烁和节奏律动依赖已完成的音频节拍检测模块输出数据，
//   需设计特效系统读取节拍时间戳数组的接口」（节拍检测 M2.10 已完成）。
//
// 产品对应（原文 5.5 运动模糊）：
//   「读取快门角度参数，换算需叠加的相邻帧数量N；对N帧分别加权（中间帧权重最高，
//    两端递减）；GPU端做多张纹理的加权混合」+「默认模式纯GPU叠加无需模型」。
//
// 实现说明（真实、可测，零第三方依赖）：
//   · 缩放冲击：时间函数（确定性正弦脉冲）驱动的画面周期缩放；
//   · 甩镜：方向性模糊（沿指定方向的线性采样平均；与旋转模糊同为多采样）；
//   · 变速闪烁：按节拍时间戳触发的亮度脉冲（衰减窗口内提亮）；
//   · 节奏律动：按节拍驱动的局部区域缩放（确定性网格律动）；
//   · 运动模糊：多帧加权叠加（快门角度 → 叠帧数；中央帧权重最高）。
//
// 质量分级（文档 4.4）：采样数按 RenderQuality 预览简化（导出完整）。

#include <cstdint>
#include <vector>

#include "nairoclip/core/render_engine.h"
#include "nairoclip/core/time.h"

namespace nairoclip {
namespace core {

/// 动感特效类型（与 EffectType 数值对齐；24..28）。
enum class MotionEffectType {
    ZoomPunch = 24,    ///< 缩放冲击
    WhipPan = 25,      ///< 甩镜
    BeatFlicker = 26,  ///< 变速闪烁（节拍驱动）
    BeatRhythm = 27,   ///< 节奏律动（节拍驱动）
    MotionBlur = 28,   ///< 运动模糊（多帧叠加）
};

/// 应用单个动感特效（原地修改 RGB；alpha 不变）。
/// - effectType：24..28（MotionEffectType 值）；其他值返回 true 且不改动；
/// - value：强度 0..1（0 = 关闭，零改动）；
/// - timeSeconds：当前帧时间（驱动时间函数与节拍窗口）；
/// - beats：节拍时间戳数组（升序；节拍驱动效果使用；可为空 = 无节拍）；
/// - quality：渲染质量分级（采样数按预览/导出分级）；
/// - 参数非法返回 false 且不改动缓冲。
bool applyMotionEffectToRgba(std::uint8_t* pixels, int width, int height, int effectType,
                             double value, double timeSeconds,
                             const std::vector<Time>& beats,
                             const RenderQuality& quality = RenderQuality::exportQuality());

// ---- 单效果入口 ----

/// 缩放冲击（周期脉冲：快速放大再恢复）。
bool applyZoomPunchRgba(std::uint8_t* pixels, int width, int height, double intensity,
                        double timeSeconds);

/// 甩镜（方向性模糊；directionDegrees 指定方向，0 = 水平）。
bool applyWhipPanRgba(std::uint8_t* pixels, int width, int height, double intensity,
                      double timeSeconds, double directionDegrees,
                      const RenderQuality& quality);

/// 变速闪烁（节拍触发亮度脉冲）。
bool applyBeatFlickerRgba(std::uint8_t* pixels, int width, int height, double intensity,
                          double timeSeconds, const std::vector<Time>& beats);

/// 节奏律动（节拍驱动局部缩放律动）。
bool applyBeatRhythmRgba(std::uint8_t* pixels, int width, int height, double intensity,
                         double timeSeconds, const std::vector<Time>& beats);

/// 运动模糊（多帧加权叠加）。
/// - frames：相邻帧序列（按时间序；中央帧为基准；奇数长度推荐）；
/// - shutterAngleDegrees：快门角度（0..360；换算叠帧数）；
/// - 输出写入 out（尺寸与输入一致；frames 为空或参数非法返回 false）。
bool applyMotionBlurFrames(const std::vector<std::vector<std::uint8_t>>& frames, int width,
                           int height, double shutterAngleDegrees,
                           std::vector<std::uint8_t>& out);

/// 快门角度 → 叠帧数（文档 5.5：「读取快门角度参数，换算需叠加的相邻帧数量N」）。
/// - shutterAngleDegrees：0..360；返回 >= 1 的叠帧数（奇数为宜）。
int blurFrameCountForShutterAngle(double shutterAngleDegrees);

/// 说明文案（供 UI/日志）。
const char* motionEffectPurpose();

}  // namespace core
}  // namespace nairoclip
