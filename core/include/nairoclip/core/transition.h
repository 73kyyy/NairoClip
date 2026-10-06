#pragma once

#include "nairoclip/core/track.h"
#include "nairoclip/core/transition_types.h"

#include <vector>

namespace nairoclip {
namespace core {

/// 转场校验：时长 > 0、两端片段均存在且相邻同名轨道内顺序正确、
/// 转场区间不与他转场重叠（由调用方提供的现存转场列表检查）。
Result validateTransition(const Track& track, const Transition& transition,
                          const std::vector<Transition>& existing = {});

/// 转场展示名称。
const char* transitionName(TransitionType type);

}  // namespace core
}  // namespace nairoclip
