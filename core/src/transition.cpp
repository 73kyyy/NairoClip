#include "nairoclip/core/transition.h"

namespace nairoclip {
namespace core {

const char* transitionName(TransitionType type) {
    switch (type) {
        case TransitionType::CrossDissolve:
            return "交叉溶解";
        case TransitionType::FadeToBlack:
            return "淡入黑场";
        case TransitionType::FadeFromBlack:
            return "从黑场淡入";
        case TransitionType::FlashWhite:
            return "闪白";
        case TransitionType::WipeLeft:
            return "左向擦除";
        case TransitionType::WipeRight:
            return "右向擦除";
        case TransitionType::SlideUp:
            return "上滑";
        case TransitionType::Zoom:
            return "缩放";
    }
    return "未知转场";
}

Result validateTransition(const Track& track, const Transition& transition,
                          const std::vector<Transition>& existing) {
    if (!transition.duration.isPositive()) {
        return Result::fail(ErrorCode::InvalidArgument, "转场时长必须大于 0");
    }
    const Clip* from = track.findClip(transition.fromClip);
    const Clip* to = track.findClip(transition.toClip);
    if (from == nullptr || to == nullptr) {
        return Result::fail(ErrorCode::NotFound, "转场引用的片段不存在");
    }
    if (!(from->start < to->start)) {
        return Result::fail(ErrorCode::InvalidArgument, "转场两端片段顺序不正确");
    }
    const Time available = to->start - from->end();
    if (available.isNegative()) {
        return Result::fail(ErrorCode::Overlap, "两端片段在时间上已重叠，无法再插入转场");
    }
    if (available.isPositive() && transition.duration > available) {
        return Result::fail(ErrorCode::OutOfRange, "转场时长超出两片段的时间间隙");
    }
    for (const auto& other : existing) {
        if (other.id == transition.id) {
            continue;
        }
        if (other.fromClip == transition.fromClip || other.fromClip == transition.toClip ||
            other.toClip == transition.fromClip || other.toClip == transition.toClip) {
            return Result::fail(ErrorCode::InvalidArgument, "片段已被其他转场占用");
        }
    }
    return Result::ok();
}

}  // namespace core
}  // namespace nairoclip
