#pragma once

#include "nairoclip/core/keyframe.h"
#include "nairoclip/core/rational.h"

#include <cstddef>

namespace nairoclip {
namespace core {

/// 片段可动画的属性通道。
enum class ClipProperty {
    Opacity,    // 透明度 [0,1]，默认 1
    Scale,      // 缩放倍数 [0.01, 100]，默认 1
    PositionX,  // 归一化 x 偏移 [-10,10]，默认 0（单位：画面宽占比）
    PositionY,  // 归一化 y 偏移 [-10,10]，默认 0
    Rotation,   // 旋转角度 [-3600,3600] 度，默认 0
    Volume,     // 音量 [0,4]，默认 1
};

/// 通道属性定义（默认值 + 合法范围）。
struct PropertySpec {
    double defaultValue = 0.0;
    double minValue = 0.0;
    double maxValue = 0.0;
};

/// 获取通道规格。
PropertySpec propertySpec(ClipProperty property);

/// 通道展示名（如「透明度」）。
const char* propertyName(ClipProperty property);

/// 一组动画通道。
/// 约定：采样时间使用「片段本地时间」（从片段起点开始的偏移）。
class ClipAnimation {
public:
    /// 通道是否存在关键帧。
    bool hasCurve(ClipProperty property) const;

    /// 获取（必要时创建）通道，用于插入关键帧。
    KeyframeTrack& curve(ClipProperty property);
    const KeyframeTrack* findCurve(ClipProperty property) const;

    /// 删除通道。
    bool removeCurve(ClipProperty property);
    void clear();
    bool empty() const;
    std::size_t curveCount() const;

    /// 采样：通道无曲线时返回默认值；有曲线时按关键帧插值后
    /// 钳制到合法范围。localTime 为片段本地时间。
    double sample(ClipProperty property, const Time& localTime) const;

    /// 校验所有通道不变量（排序 + 合法值域）。
    bool invariantsHold() const;

private:
    KeyframeTrack opacity_;
    KeyframeTrack scale_;
    KeyframeTrack positionX_;
    KeyframeTrack positionY_;
    KeyframeTrack rotation_;
    KeyframeTrack volume_;
};

}  // namespace core
}  // namespace nairoclip
