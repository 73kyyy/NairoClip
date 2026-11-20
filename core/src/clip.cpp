#include "nairoclip/core/clip.h"

#include <cmath>

namespace nairoclip {
namespace core {

void ClipTransform::clamp() {
    // Preserve neutral values for non-finite inputs. Ordinary finite values
    // outside the documented domain are clamped to the nearest legal bound.
    if (!std::isfinite(scale)) scale = 1.0;
    if (scale < 0.05) scale = 0.05;
    if (scale > 10.0) scale = 10.0;

    if (!std::isfinite(posX)) posX = 0.0;
    if (posX < -10.0) posX = -10.0;
    if (posX > 10.0) posX = 10.0;

    if (!std::isfinite(posY)) posY = 0.0;
    if (posY < -10.0) posY = -10.0;
    if (posY > 10.0) posY = 10.0;

    if (!std::isfinite(rotation)) rotation = 0.0;
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