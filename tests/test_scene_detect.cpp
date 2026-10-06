#include "test_framework.h"
#include "nairoclip/core/scene_detect.h"
#include <cmath>
using namespace nairoclip::core;
namespace {
FrameFeature frame(double sec, double r, double g, double b, double diff) {
    FrameFeature f;
    f.timestamp = Rational::make(static_cast<std::int64_t>(sec * 1000), 1000);
    f.avgR = r;
    f.avgG = g;
    f.avgB = b;
    f.histogramDiff = diff;
    return f;
}
}  // namespace
NC_TEST(SceneDetect, EmptyInputReturnsEmpty) {
    const auto shots = detectShots({}, Rational::make(10));
    NC_CHECK(shots.empty());
}
NC_TEST(SceneDetect, SingleShotNoCuts) {
    // 5 帧，无切换 → 1 个镜头
    std::vector<FrameFeature> frames = {frame(0.0, 0.5, 0.5, 0.5, 0.0),
                                        frame(0.5, 0.5, 0.5, 0.5, 0.01),
                                        frame(1.0, 0.5, 0.5, 0.5, 0.02),
                                        frame(1.5, 0.5, 0.5, 0.5, 0.01),
                                        frame(2.0, 0.5, 0.5, 0.5, 0.0)};
    const auto shots = detectShots(frames, Rational::make(3));
    NC_CHECK_EQ(shots.size(), std::size_t(1));
    NC_CHECK(shots[0].start == Rational::make(0));
    NC_CHECK(shots[0].end == Rational::make(3));
    NC_CHECK(std::abs(shots[0].brightness - 0.5) < 1e-6);
}
NC_TEST(SceneDetect, DetectsOneCut) {
    // t=1.0 处有大差异 → 2 镜
    std::vector<FrameFeature> frames = {frame(0.0, 0.2, 0.2, 0.2, 0.0),
                                        frame(0.5, 0.2, 0.2, 0.2, 0.01),
                                        frame(1.0, 0.8, 0.8, 0.8, 0.9),
                                        frame(1.5, 0.8, 0.8, 0.8, 0.01),
                                        frame(2.0, 0.8, 0.8, 0.8, 0.0)};
    const auto shots = detectShots(frames, Rational::make(3));
    NC_CHECK_EQ(shots.size(), std::size_t(2));
    NC_CHECK(shots[0].start == Rational::make(0));
    NC_CHECK(shots[0].end == Rational::make(1));
    NC_CHECK(shots[1].start == Rational::make(1));
    NC_CHECK(shots[1].end == Rational::make(3));
    // 亮度不同
    NC_CHECK(shots[0].brightness < shots[1].brightness);
}
NC_TEST(SceneDetect, MinDurationSuppressesFlicker) {
    // t=0.1 与 t=0.2 连续两刀（间隔 0.1s < minShot 0.25s）→ 第二刀被抑制
    std::vector<FrameFeature> frames = {frame(0.0, 0.2, 0.2, 0.2, 0.0),
                                        frame(0.05, 0.2, 0.2, 0.2, 0.01),
                                        frame(0.1, 0.9, 0.9, 0.9, 0.9),   // 第一刀
                                        frame(0.15, 0.9, 0.9, 0.9, 0.0),
                                        frame(0.2, 0.2, 0.2, 0.2, 0.9),   // 闪烁（被抑制）
                                        frame(0.25, 0.2, 0.2, 0.2, 0.0),
                                        frame(1.0, 0.2, 0.2, 0.2, 0.0)};
    SceneDetectConfig config;
    config.minShotDurationSeconds = 0.25;
    const auto shots = detectShots(frames, Rational::make(2), config);
    // 只有一刀有效 → 2 镜
    NC_CHECK_EQ(shots.size(), std::size_t(2));
}
NC_TEST(SceneDetect, MotionScoreFromMaxDiff) {
    // 差异均低于阈值（0.35）→ 单镜；motionScore = 段内最大差异 0.3
    std::vector<FrameFeature> frames = {frame(0.0, 0.5, 0.5, 0.5, 0.1),
                                        frame(0.5, 0.5, 0.5, 0.5, 0.3),
                                        frame(1.0, 0.5, 0.5, 0.5, 0.2)};
    const auto shots = detectShots(frames, Rational::make(2));
    NC_CHECK_EQ(shots.size(), std::size_t(1));
    // motionScore 应反映段内最大差异 0.3
    NC_CHECK(std::abs(shots[0].motionScore - 0.3) < 1e-6);
}
