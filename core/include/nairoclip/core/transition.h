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

/// 转场分类名（文档 Part 4-B 6.1 九个分类 Tab；供 UI 分组展示）。
/// 返回静态字符串；未知类型返回空串。
const char* transitionCategory(TransitionType type);

/// 方向选择器适用性（文档 6.3：仅推拉/擦除/百叶窗类转场显示方向选择器）。
bool transitionSupportsDirection(TransitionType type);

/// 时长范围校验（文档 6.2：0.1s ~ 2.0s）。
bool transitionDurationValid(const Time& duration);
}  // namespace core
}  // namespace nairoclip
