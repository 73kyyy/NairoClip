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

}  // namespace core
}  // namespace nairoclip
