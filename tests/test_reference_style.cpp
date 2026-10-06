#include "test_framework.h"
#include "nairoclip/core/reference_style.h"
#include <cmath>
using namespace nairoclip::core;
namespace {
ShotBoundary shot(double startSec, double endSec) {
    ShotBoundary s;
    s.start = Rational::make(static_cast<std::int64_t>(startSec * 1000), 1000);
    s.end = Rational::make(static_cast<std::int64_t>(endSec * 1000), 1000);
    return s;
}
bool nearly(double a, double b, double eps = 1e-6) {
    const double d = a - b;
    return (d < 0 ? -d : d) < eps;
}
}  // namespace
NC_TEST(ReferenceStyle, AnalyzeRhythmBasic) {
    // 4 个镜头：0.5s / 1s / 2s / 1.5s，总 5s
    std::vector<ShotBoundary> shots = {shot(0, 0.5), shot(0.5, 1.5), shot(1.5, 3.5), shot(3.5, 5)};
    const RhythmProfile p = analyzeRhythm(shots);
    NC_CHECK_EQ(p.shotCount, std::size_t(4));
    NC_CHECK(nearly(p.totalDuration.toDouble(), 5.0, 1e-3));
    NC_CHECK(nearly(p.averageShotDuration.toDouble(), 1.25, 1e-3));
    NC_CHECK(nearly(p.shortestShot.toDouble(), 0.5, 1e-3));
    NC_CHECK(nearly(p.longestShot.toDouble(), 2.0, 1e-3));
    // 快切占比：0.5s 一个 < 1s → 25%
    NC_CHECK(nearly(p.fastCutRatio, 0.25, 1e-6));
    // 密度：4 镜 / 5s → 8 镜/10s
    NC_CHECK(nearly(p.cutsPer10s, 8.0, 1e-3));
    NC_CHECK(p.valid());
}
NC_TEST(ReferenceStyle, AnalyzeRhythmSkipsInvalid) {
    std::vector<ShotBoundary> shots = {shot(0, 1), shot(2, 2), shot(3, 5)};  // 第二个 end==start 非法
    const RhythmProfile p = analyzeRhythm(shots);
    NC_CHECK_EQ(p.shotCount, std::size_t(2));
}
NC_TEST(ReferenceStyle, AnalyzeRhythmEmpty) {
    const RhythmProfile p = analyzeRhythm({});
    NC_CHECK_EQ(p.shotCount, std::size_t(0));
    NC_CHECK(!p.valid());
}
NC_TEST(ReferenceStyle, AnalyzeTransitions) {
    const auto p = analyzeTransitions({3, 1, 0, 0, 0, 0, 0, 0}, Rational::make(1, 2), 6, 10);
    NC_CHECK_EQ(p.typeCounts.size(), std::size_t(8));
    NC_CHECK_EQ(p.typeCounts[0], std::uint32_t(3));
    NC_CHECK(nearly(p.hardCutRatio, 0.6, 1e-6));
    NC_CHECK(nearly(p.averageDuration.toDouble(), 0.5, 1e-6));
}
NC_TEST(ReferenceStyle, AnalyzeColorWarmth) {
    const ColorProfile warm = analyzeColor(0.8, 0.5, 0.3, 0.6, 0.5);
    NC_CHECK(nearly(warm.warmth, 0.5, 1e-6));
    const ColorProfile cool = analyzeColor(0.3, 0.5, 0.8, 0.6, 0.5);
    NC_CHECK(nearly(cool.warmth, -0.5, 1e-6));
    // 钳制
    const ColorProfile clamped = analyzeColor(2.0, -1.0, 0.5, 2.0, -1.0);
    NC_CHECK(nearly(clamped.avgR, 1.0, 1e-9));
    NC_CHECK(nearly(clamped.avgG, 0.0, 1e-9));
    NC_CHECK(nearly(clamped.avgSaturation, 1.0, 1e-9));
}
NC_TEST(ReferenceStyle, AnalyzeSubtitles) {
    const auto p = analyzeSubtitles({4, 6, 8, 10}, 4, Rational::make(10));
    NC_CHECK(p.present);
    NC_CHECK(nearly(p.averageCharsPerCue, 7.0, 1e-6));
    NC_CHECK(nearly(p.cuesPer10s, 4.0, 1e-6));
}
NC_TEST(ReferenceStyle, AnalyzeSubtitlesNone) {
    const auto p = analyzeSubtitles({}, 0, Rational::make(10));
    NC_CHECK(!p.present);
    NC_CHECK(nearly(p.averageCharsPerCue, 0.0));
}
NC_TEST(ReferenceStyle, SuggestCutsCoversShots) {
    // 目标：4 镜，中位数 1.25s
    std::vector<ShotBoundary> shots = {shot(0, 0.5), shot(0.5, 1.5), shot(1.5, 3.5), shot(3.5, 5)};
    const RhythmProfile target = analyzeRhythm(shots);
    // 素材：单段 10s
    std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(10)}};
    const auto cuts = suggestCuts(target, material);
    NC_CHECK_EQ(cuts.size(), std::size_t(4));  // 4 镜
    // 切割从素材 0 开始依次推进
    NC_CHECK(cuts[0].start == Rational::make(0));
    NC_CHECK(cuts[1].start > cuts[0].start);
    // 总建议时长 ≤ 素材
    Time total;
    for (const auto& c : cuts) {
        total = total + c.duration;
    }
    NC_CHECK(total <= Rational::make(10));
}
NC_TEST(ReferenceStyle, SuggestCutsMultipleMaterials) {
    std::vector<ShotBoundary> shots = {shot(0, 1), shot(1, 2), shot(2, 3)};
    const RhythmProfile target = analyzeRhythm(shots);
    std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(2, 1)},
                                             {AssetId{2}, Rational::make(2, 1)}};
    const auto cuts = suggestCuts(target, material);
    NC_CHECK_EQ(cuts.size(), std::size_t(3));
    // 交替使用素材
    NC_CHECK(cuts[0].asset == AssetId{1});
    NC_CHECK(cuts[1].asset == AssetId{2});
}
NC_TEST(ReferenceStyle, SuggestCutsExhaustsMaterial) {
    std::vector<ShotBoundary> shots = {shot(0, 1), shot(1, 2), shot(2, 3), shot(3, 4), shot(4, 5)};
    const RhythmProfile target = analyzeRhythm(shots);
    // 素材只有 1s，远不足以覆盖 5 镜（每镜 1s 典型时长）
    std::vector<MaterialSegment> material = {{AssetId{1}, Rational::make(1)}};
    const auto cuts = suggestCuts(target, material);
    NC_CHECK(cuts.size() == std::size_t(1));  // 只切出一段
    NC_CHECK(cuts[0].duration == Rational::make(1));
}
NC_TEST(ReferenceStyle, SuggestCutsEmptyInputs) {
    const auto none1 = suggestCuts(RhythmProfile{}, {{AssetId{1}, Rational::make(10)}});
    NC_CHECK(none1.empty());
    const RhythmProfile target = analyzeRhythm({shot(0, 1), shot(1, 2)});
    const auto none2 = suggestCuts(target, {});
    NC_CHECK(none2.empty());
}
NC_TEST(ReferenceStyle, SuggestColorEffectsWarm) {
    const ColorProfile profile = analyzeColor(0.8, 0.5, 0.3, 0.8, 0.8);
    const SuggestedColorEffects effects = suggestColorEffects(profile);
    // 饱和度 0.8 → 1 + 0.3*0.5 = 1.15
    NC_CHECK(nearly(effects.saturation, 1.15, 1e-6));
    // 对比度 0.8 → 1 + 0.3*0.5 = 1.15
    NC_CHECK(nearly(effects.contrast, 1.15, 1e-6));
    // 色温 = 0.5 * 0.6 = 0.3
    NC_CHECK(nearly(effects.temperature, 0.3, 1e-6));
    NC_CHECK(nearly(effects.brightness, 0.0));
    NC_CHECK(!effects.neutral());
}
NC_TEST(ReferenceStyle, SuggestColorEffectsNeutralProfile) {
    const ColorProfile profile = analyzeColor(0.5, 0.5, 0.5, 0.5, 0.5);
    const SuggestedColorEffects effects = suggestColorEffects(profile);
    NC_CHECK(effects.neutral());
}
NC_TEST(ReferenceStyle, SuggestColorEffectsClamped) {
    // 极端画像
    const ColorProfile extreme = analyzeColor(1.0, 0.0, 0.0, 1.0, 1.0);
    const SuggestedColorEffects effects = suggestColorEffects(extreme);
    NC_CHECK(effects.saturation >= 0.0 && effects.saturation <= 3.0);
    NC_CHECK(effects.contrast >= 0.0 && effects.contrast <= 3.0);
    NC_CHECK(effects.temperature >= -1.0 && effects.temperature <= 1.0);
}
NC_TEST(ReferenceStyle, DescribeRhythmFast) {
    // 10 镜 / 10s → 每 10 秒 10 刀 → 快
    std::vector<ShotBoundary> shots;
    for (int i = 0; i < 10; ++i) {
        shots.push_back(shot(i * 1.0, (i + 1) * 1.0));
    }
    const RhythmProfile p = analyzeRhythm(shots);
    const std::string desc = describeRhythm(p);
    NC_CHECK(desc.find("快") != std::string::npos);
    NC_CHECK(desc.find("1.0 秒/镜头") != std::string::npos);
}
NC_TEST(ReferenceStyle, DescribeRhythmSlow) {
    // 2 镜 / 10s → 每 10 秒 2 刀 → 舒缓
    std::vector<ShotBoundary> shots = {shot(0, 5), shot(5, 10)};
    const RhythmProfile p = analyzeRhythm(shots);
    const std::string desc = describeRhythm(p);
    NC_CHECK(desc.find("舒缓") != std::string::npos);
}
NC_TEST(ReferenceStyle, DescribeRhythmInvalid) {
    const std::string desc = describeRhythm(RhythmProfile{});
    NC_CHECK(desc.find("无法识别") != std::string::npos);
}
