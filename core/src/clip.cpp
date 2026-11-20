#include "nairoclip/core/clip.h"
#include "nairoclip/core/transform_limits.h"

#include <cmath>

namespace nairoclip {
namespace core {

void ClipTransform::clamp() {
    // Preserve neutral values for non-finite inputs. Ordinary finite values
    // outside the documented domain are clamped to the nearest legal bound.
    if (!std::isfinite(scale)) scale = 1.0;
    if (scale < transform_limits::kScaleMin) scale = transform_limits::kScaleMin;
    if (scale > transform_limits::kScaleMax) scale = transform_limits::kScaleMax;

    if (!std::isfinite(posX)) posX = 0.0;
    if (posX < transform_limits::kPositionMin) posX = transform_limits::kPositionMin;
    if (posX > transform_limits::kPositionMax) posX = transform_limits::kPositionMax;

    if (!std::isfinite(posY)) posY = 0.0;
    if (posY < transform_limits::kPositionMin) posY = transform_limits::kPositionMin;
    if (posY > transform_limits::kPositionMax) posY = transform_limits::kPositionMax;

    if (!std::isfinite(rotation)) rotation = 0.0;
    if (rotation < transform_limits::kRotationMinDegrees) rotation = transform_limits::kRotationMinDegrees;
    if (rotation > transform_limits::kRotationMaxDegrees) rotation = transform_limits::kRotationMaxDegrees;

    if (anchor < transform_limits::kAnchorMin || anchor > transform_limits::kAnchorMax) anchor = transform_limits::kAnchorCenter;
}

bool Clip::valid() const {
    return duration.isPositive() && speed.isPositive() && !start.isNegative() &&
           !sourceIn.isNegative();
}

}  // namespace core
}  // namespace nairoclip