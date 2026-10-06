#pragma once

#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

/// 缓动类型：控制「当前关键帧 → 下一关键帧」区间的插值曲线。
enum class EasingType {
    Linear,     // 线性
    EaseIn,     // 缓入（慢→快）
    EaseOut,    // 缓出（快→慢）
    EaseInOut,  // 缓入缓出
    Stepped,    // 阶跃（保持前值，到下一帧直接跳变）
    Bezier,     // 自定义三次贝塞尔（同 CSS cubic-bezier 语义）
};

/// 缓动参数。Bezier 时使用四个控制点坐标（控制点 x 会被钳制到 [0,1]，
/// 保证求解单调；控制点 y 允许超出 [0,1]，用于「过冲」回弹效果）。
struct Easing {
    EasingType type = EasingType::Linear;
    double c1x = 0.0;
    double c1y = 0.0;
    double c2x = 1.0;
    double c2y = 1.0;

    static Easing linear();
    static Easing easeIn();
    static Easing easeOut();
    static Easing easeInOut();
    static Easing stepped();
    static Easing bezier(double c1x, double c1y, double c2x, double c2y);

    /// 将归一化进度 u∈[0,1] 映射为缓动进度。u<=0 返回 0，u>=1 返回 1。
    double map(double u) const;
};

/// 单个关键帧：时刻 + 值 + 离开时的缓动。
struct Keyframe {
    Time time;
    double value = 0.0;
    Easing easing;
};

/// 关键帧轨道（单通道，如「透明度」「缩放」「X 位移」）。
/// 不变量：按 time 严格升序，时刻非负。
class KeyframeTrack {
public:
    /// 插入关键帧；同时刻已存在时替换（返回 ok）。
    Result insert(Keyframe keyframe);
    /// 删除指定时刻的关键帧；不存在返回 false。
    bool remove(const Time& at);
    void clear();

    bool empty() const { return keyframes_.empty(); }
    std::size_t size() const { return keyframes_.size(); }
    const std::vector<Keyframe>& keyframes() const { return keyframes_; }

    /// 采样：空轨道返回 fallback；区间外钳制到端点值；区间内按左侧
    /// 关键帧的缓动插值。
    double sample(const Time& t, double fallback = 0.0) const;

    bool invariantsHold() const;

private:
    std::vector<Keyframe> keyframes_;
};

}  // namespace core
}  // namespace nairoclip
