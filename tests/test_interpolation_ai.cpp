// =============================================================================
// 光流补帧 AI 测试（文档 Part 4-A 1.7：RIFE 真实推理）
// =============================================================================
// 覆盖：
//   1. 目标帧率 / 插入帧数计算 / timestep 分布（文档 1.7 参数）；
//   2. 真实推理（需 ORT + RIFE 模型；缺失跳过）；
//   3. 失败路径（未加载/尺寸非法/模型缺失）。
#include "test_framework.h"
#include "nairoclip/core/interpolation_ai.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

bool rifeModelAvailable() {
    if (!aiEngineAvailable()) {
        return false;
    }
    FILE* f = std::fopen("/workspace/deps/rife47.onnx", "rb");
    if (f == nullptr) {
        return false;
    }
    std::fclose(f);
    return true;
}

/// 合成渐变帧（左暗右亮；用于验证插值真实性）。
std::vector<std::uint8_t> makeGradientFrame(int w, int h, double factor) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t* px = pixels.data() +
                               (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
            const std::uint8_t v = static_cast<std::uint8_t>(
                std::clamp(static_cast<double>(x) / std::max(1, w - 1) * 255.0 * factor, 0.0,
                           255.0));
            px[0] = v;
            px[1] = static_cast<std::uint8_t>(v / 2);
            px[2] = static_cast<std::uint8_t>(255 - v);
        }
    }
    return pixels;
}

double meanAbsDiff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i += 4) {
        sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 参数计算（文档 1.7）
// ---------------------------------------------------------------------------

NC_TEST(InterpolationAI, TargetNamesAndInsertCount) {
    NC_CHECK_EQ(std::string(interpolationTargetName(InterpolationTarget::Fps60)),
                std::string("60"));
    NC_CHECK_EQ(std::string(interpolationTargetName(InterpolationTarget::Fps120)),
                std::string("120"));
    NC_CHECK_EQ(std::string(interpolationTargetName(InterpolationTarget::Fps240)),
                std::string("240"));
    InterpolationTarget target;
    NC_CHECK(interpolationTargetFromInt(60, target) && target == InterpolationTarget::Fps60);
    NC_CHECK(interpolationTargetFromInt(120, target) && target == InterpolationTarget::Fps120);
    NC_CHECK(interpolationTargetFromInt(240, target) && target == InterpolationTarget::Fps240);
    NC_CHECK(!interpolationTargetFromInt(30, target));
    // 插入帧数：源 30fps → 60fps 插 1 帧；→ 120fps 插 3 帧；→ 240fps 插 7 帧
    NC_CHECK_EQ(interpolationInsertCount(30, InterpolationTarget::Fps60), 1);
    NC_CHECK_EQ(interpolationInsertCount(30, InterpolationTarget::Fps120), 3);
    NC_CHECK_EQ(interpolationInsertCount(30, InterpolationTarget::Fps240), 7);
    // 源 60fps → 60fps 无需插值
    NC_CHECK_EQ(interpolationInsertCount(60, InterpolationTarget::Fps60), 0);
    // 目标低于源 → 0
    NC_CHECK_EQ(interpolationInsertCount(120, InterpolationTarget::Fps60), 0);
    // 非法源帧率
    NC_CHECK_EQ(interpolationInsertCount(0, InterpolationTarget::Fps60), 0);
}

NC_TEST(InterpolationAI, TimestepDistribution) {
    // 均匀分布（平滑度 50）：等间距且位于 (0,1) 开区间
    const std::vector<double> uniform = interpolationTimesteps(3, 50.0);
    NC_CHECK_EQ(uniform.size(), std::size_t(3));
    NC_CHECK(uniform[0] > 0.0 && uniform[0] < 1.0);
    NC_CHECK(uniform[1] > uniform[0]);
    NC_CHECK(uniform[2] > uniform[1]);
    // 均匀间距（0.25 / 0.5 / 0.75）
    NC_CHECK(std::abs(uniform[0] - 0.25) < 1e-9);
    NC_CHECK(std::abs(uniform[1] - 0.5) < 1e-9);
    NC_CHECK(std::abs(uniform[2] - 0.75) < 1e-9);
    // 单帧：0.5
    const std::vector<double> single = interpolationTimesteps(1, 50.0);
    NC_CHECK_EQ(single.size(), std::size_t(1));
    NC_CHECK(std::abs(single[0] - 0.5) < 1e-9);
    // 零插入 → 空
    NC_CHECK_EQ(interpolationTimesteps(0, 50.0).size(), std::size_t(0));
    // 高平滑度：中间密集（与均匀不同）
    const std::vector<double> smooth = interpolationTimesteps(3, 100.0);
    NC_CHECK_EQ(smooth.size(), std::size_t(3));
    bool differs = false;
    for (std::size_t i = 0; i < 3 && !differs; ++i) {
        if (std::abs(smooth[i] - uniform[i]) > 1e-6) differs = true;
    }
    NC_CHECK(differs);
    // 单调性保持
    NC_CHECK(smooth[0] < smooth[1] && smooth[1] < smooth[2]);
}

// ---------------------------------------------------------------------------
// 2. 真实推理（需 ORT + RIFE；缺失跳过）
// ---------------------------------------------------------------------------

NC_TEST(InterpolationAI, RealInterpolation) {
    if (!rifeModelAvailable()) return;
    InterpolationEngine engine;
    std::string failure;
    NC_CHECK(engine.load("/workspace/deps/rife47.onnx", &failure));
    NC_CHECK(engine.loaded());
    // 两帧：亮度 0.7 与 1.0（中间帧应介于两者之间）
    const int W = 128;
    const int H = 128;
    const std::vector<std::uint8_t> frame0 = makeGradientFrame(W, H, 0.7);
    const std::vector<std::uint8_t> frame1 = makeGradientFrame(W, H, 1.0);
    std::vector<std::uint8_t> middle;
    NC_CHECK(engine.interpolate(frame0.data(), frame1.data(), W, H, 0.5,
                                InterpolationMode::Standard, middle, &failure));
    NC_CHECK_EQ(middle.size(), std::size_t(W * H * 4));
    // 插值帧应与两帧都不同（真实插值，非简单拷贝）
    const double diff0 = meanAbsDiff(middle, frame0);
    const double diff1 = meanAbsDiff(middle, frame1);
    NC_CHECK(diff0 > 0.5);
    NC_CHECK(diff1 > 0.5);
    // timestep = 0 → 接近 frame0；timestep = 1 → 接近 frame1
    std::vector<std::uint8_t> atZero;
    std::vector<std::uint8_t> atOne;
    NC_CHECK(engine.interpolate(frame0.data(), frame1.data(), W, H, 0.0,
                                InterpolationMode::Standard, atZero, &failure));
    NC_CHECK(engine.interpolate(frame0.data(), frame1.data(), W, H, 1.0,
                                InterpolationMode::Standard, atOne, &failure));
    NC_CHECK(meanAbsDiff(atZero, frame0) < meanAbsDiff(atZero, frame1));
    NC_CHECK(meanAbsDiff(atOne, frame1) < meanAbsDiff(atOne, frame0));
}

NC_TEST(InterpolationAI, HighPrecisionMode) {
    if (!rifeModelAvailable()) return;
    InterpolationEngine engine;
    std::string failure;
    NC_CHECK(engine.load("/workspace/deps/rife47.onnx", &failure));
    const int W = 64;
    const int H = 64;
    const std::vector<std::uint8_t> frame0 = makeGradientFrame(W, H, 0.7);
    const std::vector<std::uint8_t> frame1 = makeGradientFrame(W, H, 1.0);
    std::vector<std::uint8_t> standard;
    std::vector<std::uint8_t> highPrecision;
    NC_CHECK(engine.interpolate(frame0.data(), frame1.data(), W, H, 0.5,
                                InterpolationMode::Standard, standard, &failure));
    NC_CHECK(engine.interpolate(frame0.data(), frame1.data(), W, H, 0.5,
                                InterpolationMode::HighPrecision, highPrecision, &failure));
    // 两种模式均产出有效帧（尺寸一致）
    NC_CHECK_EQ(standard.size(), std::size_t(W * H * 4));
    NC_CHECK_EQ(highPrecision.size(), std::size_t(W * H * 4));
    // 高精度模式经 2x 推理再降采样 → 结果应与标准模式不同（更精细）
    bool differs = false;
    for (std::size_t i = 0; i < standard.size() && !differs; i += 4) {
        if (standard[i] != highPrecision[i]) differs = true;
    }
    NC_CHECK(differs);
}

NC_TEST(InterpolationAI, Deterministic) {
    if (!rifeModelAvailable()) return;
    InterpolationEngine engine;
    std::string failure;
    NC_CHECK(engine.load("/workspace/deps/rife47.onnx", &failure));
    const int W = 64;
    const int H = 64;
    const std::vector<std::uint8_t> frame0 = makeGradientFrame(W, H, 0.7);
    const std::vector<std::uint8_t> frame1 = makeGradientFrame(W, H, 1.0);
    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> second;
    NC_CHECK(engine.interpolate(frame0.data(), frame1.data(), W, H, 0.5,
                                InterpolationMode::Standard, first, &failure));
    NC_CHECK(engine.interpolate(frame0.data(), frame1.data(), W, H, 0.5,
                                InterpolationMode::Standard, second, &failure));
    NC_CHECK_EQ(first.size(), second.size());
    bool identical = true;
    for (std::size_t i = 0; i < first.size() && identical; ++i) {
        if (first[i] != second[i]) identical = false;
    }
    NC_CHECK(identical);  // 同一输入 → 同一输出
}

NC_TEST(InterpolationAI, FailurePaths) {
    if (!rifeModelAvailable()) return;
    InterpolationEngine engine;
    std::string failure;
    std::vector<std::uint8_t> out;
    const std::vector<std::uint8_t> frame(64 * 64 * 4, 128);
    // 未加载 → 失败
    NC_CHECK(!engine.interpolate(frame.data(), frame.data(), 64, 64, 0.5,
                                 InterpolationMode::Standard, out, &failure));
    NC_CHECK(!failure.empty());
    // 不存在的模型
    NC_CHECK(!engine.load("/tmp/no_rife_model.onnx", &failure));
    // 加载后非法尺寸
    NC_CHECK(engine.load("/workspace/deps/rife47.onnx", &failure));
    NC_CHECK(!engine.interpolate(nullptr, frame.data(), 64, 64, 0.5,
                                 InterpolationMode::Standard, out, &failure));
    NC_CHECK(!engine.interpolate(frame.data(), frame.data(), 0, 64, 0.5,
                                 InterpolationMode::Standard, out, &failure));
}

NC_TEST(InterpolationAI, MultiInsertSequence) {
    // 文档 1.7 端到端语义：30fps 源 → 60fps 目标，每对帧插 1 帧
    if (!rifeModelAvailable()) return;
    InterpolationEngine engine;
    std::string failure;
    NC_CHECK(engine.load("/workspace/deps/rife47.onnx", &failure));
    const int W = 64;
    const int H = 64;
    const std::vector<std::uint8_t> frameA = makeGradientFrame(W, H, 0.5);
    const std::vector<std::uint8_t> frameB = makeGradientFrame(W, H, 1.0);
    const int insertCount = interpolationInsertCount(30, InterpolationTarget::Fps60);
    NC_CHECK_EQ(insertCount, 1);
    const std::vector<double> steps = interpolationTimesteps(insertCount, 70.0);
    NC_CHECK_EQ(steps.size(), std::size_t(1));
    std::vector<std::uint8_t> inserted;
    NC_CHECK(engine.interpolate(frameA.data(), frameB.data(), W, H, steps[0],
                                InterpolationMode::Standard, inserted, &failure));
    NC_CHECK_EQ(inserted.size(), std::size_t(W * H * 4));
    // 插入帧亮度应介于两帧之间（插值特性）
    const double diffA = meanAbsDiff(inserted, frameA);
    const double diffB = meanAbsDiff(inserted, frameB);
    NC_CHECK(diffA > 0.5 && diffB > 0.5);
}
