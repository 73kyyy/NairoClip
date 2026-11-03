#include "nairoclip/core/clip.h"

namespace nairoclip {
namespace core {

void ClipTransform::clamp() {
    // 与 docs/PIP_GESTURE_PLAN.md §4.1 的范围一致；越界静默钳制不报错
    if (scale < 0.05) scale = 0.05;
    if (scale > 10.0) scale = 10.0;
    if (posX < -10.0) posX = -10.0;
    if (posX > 10.0) posX = 10.0;
    if (posY < -10.0) posY = -10.0;
    if (posY > 10.0) posY = 10.0;
    if (rotation < -3600.0) rotation = -3600.0;
    if (rotation > 3600.0) rotation = 3600.0;
    if (anchor < 0 || anchor > 8) anchor = 4;
}

bool Clip::valid() const {
    return duration.isPositive() && speed.isPositive() && !start.isNegative() &&
           !sourceIn.isNegative();
}

}  // namespace core
}  // namespace nairoclip