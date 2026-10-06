#include "nairoclip/core/clip.h"

namespace nairoclip {
namespace core {

bool Clip::valid() const {
    return duration.isPositive() && speed.isPositive() && !start.isNegative() &&
           !sourceIn.isNegative();
}

}  // namespace core
}  // namespace nairoclip