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
        // 占用规则（专业剪辑语义：允许 A→B→C 转场链）：
        //  1) 同一对片段（任意顺序）已存在转场 → 拒绝（重复）；
        //  2) 同一片段作为两个转场的**起点** → 拒绝（一个片段只能有一个后继转场）；
        //  3) 同一片段作为两个转场的**终点** → 拒绝（一个片段只能有一个前驱转场）。
        // 注意：A→B 与 B→C 是合法的链式关系（B 是先者的终点、后者的起点），
        // 因此不能简单按「片段被任何转场引用」判占用。
        const bool samePair =
            (other.fromClip == transition.fromClip && other.toClip == transition.toClip) ||
            (other.fromClip == transition.toClip && other.toClip == transition.fromClip);
        if (samePair || other.fromClip == transition.fromClip ||
            other.toClip == transition.toClip) {
            return Result::fail(ErrorCode::InvalidArgument, "片段已被其他转场占用");
        }
    }
    return Result::ok();
}

}  // namespace core
}  // namespace nairoclip
