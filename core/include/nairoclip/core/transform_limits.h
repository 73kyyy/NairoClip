#pragma once

namespace nairoclip {
namespace core {
namespace transform_limits {

constexpr double kScaleMin = 0.05;
constexpr double kScaleMax = 10.0;
constexpr double kPositionMin = -10.0;
constexpr double kPositionMax = 10.0;
constexpr double kRotationMinDegrees = -3600.0;
constexpr double kRotationMaxDegrees = 3600.0;
constexpr int kAnchorMin = 0;
constexpr int kAnchorMax = 8;
constexpr int kAnchorCenter = 4;

}  // namespace transform_limits
}  // namespace core
}  // namespace nairoclip
