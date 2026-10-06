#pragma once

#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <string>

namespace nairoclip {
namespace core {

/// 转场类型。
/// 说明：转场作用于「时间上相邻的两个片段」之间，占用前一片段尾部与
/// 后一片段头部的重叠区间（交叉溶解等），或占用两片段之间的空隙
/// （黑场/闪白等）。
enum class TransitionType {
    CrossDissolve,  // 交叉溶解
    FadeToBlack,    // 淡入黑场
    FadeFromBlack,  // 从黑场淡入
    FlashWhite,     // 闪白
    WipeLeft,       // 左向擦除
    WipeRight,      // 右向擦除
    SlideUp,        // 上滑
    Zoom,           // 缩放
};

/// 转场参数（纯数据类型，不依赖 Track/Clip，供 Track 存储与校验共用）。
struct Transition {
    TransitionId id;
    TransitionType type = TransitionType::CrossDissolve;
    Time duration = Rational::make(1, 2);  // 转场时长，必须 > 0
    /// 所属轨道内的片段对：从前片段 A 到后片段 B。
    ClipId fromClip;
    ClipId toClip;
    std::string name;  // 展示名（如「交叉溶解」）
};

}  // namespace core
}  // namespace nairoclip
