#pragma once

#include "nairoclip/core/rational.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

namespace nairoclip {
namespace core {

/// 时间轴上的一个片段。
struct Clip {
    ClipId id;
    AssetId asset;    // 引用的素材（可为空，如文字片段）
    Time start;       // 时间轴起点
    Time duration;    // 时间轴时长
    Time sourceIn;    // 源素材入点
    Rational speed = Rational::make(1);  // 播放速度倍率（1 = 原速）
    bool enabled = true;

    /// 时间轴终点。
    Time end() const { return start + duration; }

    /// 消耗的源素材范围终点：sourceIn + duration * speed。
    Time sourceOut() const { return sourceIn + duration * speed; }

    /// [start, end) 是否包含时刻 t。
    bool contains(const Time& t) const { return start <= t && t < end(); }

    /// 片段参数是否合法：时长 > 0、速度 > 0、起点/入点不为负。
    bool valid() const;
};

}  // namespace core
}  // namespace nairoclip