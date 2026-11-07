// 参考视频分析器（AI 剪辑的「理解」层）
// 合成帧序列测试：分析部分是纯函数，不需要真实视频。

#include "test_framework.h"
#include "nairoclip/core/ref_style_analyzer.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

FrameSample mk(double t, double luma, double diff, double cx = 0.5, double cy = 0.5,
               double conc = 0.15) {
    FrameSample s;
    s.timeSeconds = t;
    s.width = 1920;
    s.height = 1080;
    s.meanR = luma;
    s.meanG = luma;
    s.meanB = luma;
    s.luma = luma;
    s.saturation = 0.3;
    s.contrast = 0.2;
    s.diff = diff;
    s.diffCentroidX = cx;
    s.diffCentroidY = cy;
    s.diffConcentration = conc;
    s.detailEnergy = 0.10;
    return s;
}

std::vector<FrameSample> baseSequence(double luma = 0.40) {
    std::vector<FrameSample> v;
    for (int i = 0; i < 20; ++i) v.push_back(mk(i * 0.1, luma, i == 0 ? 0.0 : 0.01));
    return v;
}

}  // namespace

NC_TEST(RefAnalyzer, DetectsHardCut) {
    auto v = baseSequence();
    v[10] = mk(1.0, 0.72, 0.55);
    for (int i = 11; i < 20; ++i) v[i] = mk(i * 0.1, 0.72, 0.01);
    RefStyleProfile p;
    analyzeFrameSamples(v, p);
    NC_CHECK(p.transitions.size() == 1);
    if (!p.transitions.empty()) {
        NC_CHECK(p.transitions[0].kind == RefTransitionKind::HardCut);
        NC_CHECK(p.transitions[0].confidence > 0.5);
    }
    NC_CHECK(p.shotCount == 2);
}

NC_TEST(RefAnalyzer, DetectsDissolve) {
    auto v = baseSequence();
    for (int i = 10; i <= 14; ++i) v[i] = mk(i * 0.1, 0.40 + 0.04 * (i - 9), 0.22);
    v[15] = mk(1.5, 0.62, 0.01);
    RefStyleProfile p;
    analyzeFrameSamples(v, p);
    NC_CHECK(p.transitions.size() == 1);
    if (!p.transitions.empty()) {
        NC_CHECK(p.transitions[0].kind == RefTransitionKind::Dissolve);
        NC_CHECK(p.transitions[0].durationSeconds >= 0.3);
    }
}

NC_TEST(RefAnalyzer, DetectsFlash) {
    auto v = baseSequence(0.30);
    v[10] = mk(1.0, 0.95, 0.45);
    v[11] = mk(1.1, 0.32, 0.20);
    RefStyleProfile p;
    analyzeFrameSamples(v, p);
    NC_CHECK(p.transitions.size() == 1);
    if (!p.transitions.empty()) NC_CHECK(p.transitions[0].kind == RefTransitionKind::Flash);
}

NC_TEST(RefAnalyzer, DetectsWipeByConcentration) {
    auto v = baseSequence();
    for (int i = 9; i <= 11; ++i) v[i] = mk(i * 0.1, 0.45, 0.26, 0.5, 0.5, 0.85);
    v[12] = mk(1.2, 0.45, 0.01);
    RefStyleProfile p;
    analyzeFrameSamples(v, p);
    NC_CHECK(p.transitions.size() == 1);
    if (!p.transitions.empty()) NC_CHECK(p.transitions[0].kind == RefTransitionKind::Wipe);
}

NC_TEST(RefAnalyzer, EstimatesShake) {
    std::vector<FrameSample> v;
    for (int i = 0; i < 40; ++i) {
        v.push_back(mk(i * 0.1, 0.45, 0.08, (i % 2 == 0) ? 0.44 : 0.56));
    }
    RefStyleProfile p;
    analyzeFrameSamples(v, p);
    NC_CHECK(p.shakeAmplitude > 0.01);
    NC_CHECK(p.shakeAmplitude <= 0.2);
    NC_CHECK(p.shakeFrequencyHz > 1.0);
    NC_CHECK(p.shakeConfidence > 0.3);
    NC_CHECK(p.transitions.empty());
}

NC_TEST(RefAnalyzer, IgnoresStaticFootageForShake) {
    std::vector<FrameSample> v;
    for (int i = 0; i < 40; ++i) {
        v.push_back(mk(i * 0.1, 0.45, 0.001, (i % 2 == 0) ? 0.499 : 0.501));
    }
    RefStyleProfile p;
    analyzeFrameSamples(v, p);
    NC_CHECK(p.shakeAmplitude < 0.01);
    NC_CHECK(p.shakeConfidence < 0.3);
}

NC_TEST(RefAnalyzer, EmptyIsSafe) {
    RefStyleProfile p;
    analyzeFrameSamples({}, p);
    NC_CHECK(p.shotCount == 0);
    NC_CHECK(p.transitions.empty());
    NC_CHECK(p.totalSeconds == 0.0);
}

NC_TEST(RefAnalyzer, JsonHasFrontendFields) {
    auto v = baseSequence();
    v[10] = mk(1.0, 0.72, 0.55);
    for (int i = 11; i < 20; ++i) v[i] = mk(i * 0.1, 0.72, 0.01);
    RefStyleProfile p;
    analyzeFrameSamples(v, p);
    const std::string j = p.toJson();
    for (const char* key : {"\"shot_count\"", "\"total_seconds\"", "\"avg_r\"",
                            "\"shake_amplitude\"", "\"shake_frequency_hz\"",
                            "\"motion_intensity\"", "\"shots\"", "\"transitions\"",
                            "\"confidence\""}) {
        NC_CHECK(j.find(key) != std::string::npos);
    }
    NC_CHECK(j.front() == '{');
    NC_CHECK(j.back() == '}');
}
