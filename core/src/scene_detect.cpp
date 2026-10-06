#include "nairoclip/core/scene_detect.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

double luma(double r, double g, double b) {
    return 0.299 * r + 0.587 * g + 0.114 * b;
}

}  // namespace

std::vector<ShotBoundary> detectShots(const std::vector<FrameFeature>& features,
                                      const Time& endTime, const SceneDetectConfig& config) {
    std::vector<ShotBoundary> shots;
    if (features.empty()) {
        return shots;
    }
    const double minShot = std::max(0.05, config.minShotDurationSeconds);

    // 找切换点：直方图差异超阈值处
    std::vector<std::size_t> cutIndices;
    for (std::size_t i = 1; i < features.size(); ++i) {
        if (features[i].histogramDiff >= config.threshold) {
            // 防闪烁：与前一个切换点间隔足够
            if (cutIndices.empty()) {
                cutIndices.push_back(i);
            } else {
                const double gap = (features[i].timestamp - features[cutIndices.back()].timestamp)
                                       .toDouble();
                if (gap >= minShot) {
                    cutIndices.push_back(i);
                }
            }
        }
    }

    // 组装镜头段
    std::size_t startIndex = 0;
    auto makeShot = [&](std::size_t fromIndex, const Time& shotEnd, std::size_t toIndex) {
        ShotBoundary shot;
        shot.start = features[fromIndex].timestamp;
        shot.end = shotEnd;
        // 亮度：均值；运动：段内最大直方图差异
        double sumLuma = 0.0;
        double maxDiff = 0.0;
        std::size_t count = 0;
        for (std::size_t i = fromIndex; i < toIndex && i < features.size(); ++i) {
            sumLuma += luma(features[i].avgR, features[i].avgG, features[i].avgB);
            maxDiff = std::max(maxDiff, features[i].histogramDiff);
            ++count;
        }
        shot.brightness = count > 0 ? sumLuma / static_cast<double>(count) : 0.5;
        shot.motionScore = std::min(1.0, maxDiff);
        shots.push_back(shot);
    };

    for (const std::size_t cut : cutIndices) {
        // 每个切换点结束上一镜
        if (cut > startIndex) {
            makeShot(startIndex, features[cut].timestamp, cut);
            startIndex = cut;
        }
    }
    // 最后一镜：从最后切换点到素材末尾
    if (startIndex < features.size()) {
        const Time lastEnd = endTime > features.back().timestamp ? endTime
                                                                 : features.back().timestamp;
        makeShot(startIndex, lastEnd, features.size());
    }
    return shots;
}

}  // namespace core
}  // namespace nairoclip
