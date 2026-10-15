#pragma once

#include "nairoclip/core/keyframe.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

/// 速度曲线控制点：源素材内某时间点对应的速度。
/// speed 的合法范围为 [0.1x, 10x]；easing 控制到下一控制点的过渡。
struct SpeedPoint {
    Time sourceTime;
    Rational speed = Rational::make(1);
    Easing easing;
};

/// 速度曲线：源进度 → 播放速度 的分段曲线。
/// 语义：时间轴时长 = ∫ (1 / speed(s)) ds（对源区间积分）。
/// 空曲线表示恒速（fallback 速度，默认 1x）。
class SpeedCurve {
public:
    /// 添加控制点（同时刻替换）；速度越界返回 InvalidArgument。
    Result addPoint(SpeedPoint point);
    bool removePoint(const Time& sourceTime);
    void clear();

    bool empty() const { return points_.empty(); }
    std::size_t size() const { return points_.size(); }
    const std::vector<SpeedPoint>& points() const { return points_; }

    /// 恒速曲线的快捷构造。
    static SpeedCurve constant(const Rational& speed);

    /// 采样源时间点的速度（double，配合缓动插值）。
    /// 空曲线返回 fallback；区间外钳制到端点速度。
    double speedAt(const Time& sourceTime,
                   const Rational& fallback = Rational::make(1)) const;

    /// 计算源区间 [sourceIn, sourceOut] 映射到时间轴的时长（数值积分）。
    /// sourceOut <= sourceIn 返回 0。结果量化到微秒（保证跨平台确定性）。
    Time timelineDuration(const Time& sourceIn, const Time& sourceOut,
                          const Rational& fallback = Rational::make(1)) const;

    bool invariantsHold() const;

    /// 速度合法范围常量。
    static Rational minSpeed();
    static Rational maxSpeed();

private:
    std::vector<SpeedPoint> points_;
};

// =============================================================================
// 速度曲线预设（文档 Part 4-A 1.1「曲线预设」清单，**强制名称**）
// =============================================================================
//   自定义 / 蒙太奇 / 英雄时刻 / 子弹时间 / 跳跃 / 闪进 / 闪出
// 说明：预设为「生成曲线」的工厂（不含自定义项——自定义由用户点自行构造）。
//   · 曲线覆盖源区间 [0, duration]（调用方传入素材可用时长以定标）；
//   · 每个预设的参数（速度峰值/位置/个数）为文档语义的合理实现，
//     UI 只展示名称与曲线形状，不暴露内部参数。

enum class SpeedCurvePreset {
    Custom = 0,      ///< 自定义（空曲线 = 恒速；由用户自行添加控制点）
    Montage = 1,     ///< 蒙太奇（多次快慢交替；平均略快）
    HeroMoment = 2,  ///< 英雄时刻（前后正常 + 中段极慢，突出高光）
    BulletTime = 3,  ///< 子弹时间（中段极致慢速，接近定格）
    Jump = 4,        ///< 跳跃（周期性快跳：快-慢-快）
    FlashIn = 5,     ///< 闪进（片头极快，迅速进入正片）
    FlashOut = 6,    ///< 闪出（片尾极快收束）
};

/// 预设名称（"custom"/"montage"/...；序列化与 C ABI 使用）。
const char* speedCurvePresetName(SpeedCurvePreset preset);
/// 预设中文展示名（文档逐字："蒙太奇"/"英雄时刻"/...）。
const char* speedCurvePresetDisplayName(SpeedCurvePreset preset);
/// 从字符串解析（大小写不敏感；支持中文名；非法返回 false）。
bool speedCurvePresetFromString(const char* text, SpeedCurvePreset& out);
/// 从整数解析（0..6；非法返回 false）。
bool speedCurvePresetFromInt(int value, SpeedCurvePreset& out);

/// 生成预设曲线（sourceDuration = 曲线覆盖的源区间长度；<= 0 返回空曲线）。
SpeedCurve buildSpeedCurvePreset(SpeedCurvePreset preset, const Time& sourceDuration);

}  // namespace core
}  // namespace nairoclip
