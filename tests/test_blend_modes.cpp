#include "test_framework.h"
#include "nairoclip/core/blend_modes.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 单通道比较（1e-9 容差——纯公式运算，无需放宽容差）。
void expectChannel(const char* label, BlendMode mode, double base, double top, double want) {
    const double got = blendChannel(mode, base, top);
    if (std::fabs(got - want) > 1e-9) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                         std::string(label) + " base=" + std::to_string(base) +
                                             " top=" + std::to_string(top) + " 期望 " +
                                             std::to_string(want) + " 实得 " + std::to_string(got));
    }
}

}  // namespace

// =============================================================================
// 混合模式（B1-1：5 → 16）
// =============================================================================
//
// 对拍依据：W3C Compositing and Blending Level 1 与 Photoshop 公开公式。
// 每组挑选「特征点」而不是随机值——随机值算出的期望值也是我写的公式，
// 那样只能证明代码等于自身，证明不了公式正确。

NC_TEST(BlendModes, CountAndLegacyValuesStable) {
    // 0..4 是历史值，C ABI 按整数映射，改值会让历史调用错位
    NC_CHECK_EQ(static_cast<int>(BlendMode::Normal), 0);
    NC_CHECK_EQ(static_cast<int>(BlendMode::Screen), 1);
    NC_CHECK_EQ(static_cast<int>(BlendMode::Overlay), 2);
    NC_CHECK_EQ(static_cast<int>(BlendMode::Darken), 3);
    NC_CHECK_EQ(static_cast<int>(BlendMode::HardLight), 4);
    NC_CHECK_EQ(static_cast<int>(kBlendModeCount), 16);
    // 新模式从 5 起连续编号
    NC_CHECK_EQ(static_cast<int>(BlendMode::Multiply), 5);
    NC_CHECK_EQ(static_cast<int>(BlendMode::Exclusion), 15);
}

NC_TEST(BlendModes, EveryModeHasNameRoundtrip) {
    // 16 种模式都必须有名字，且名字能解析回同一模式（序列化依赖）
    for (int v = 0; v < kBlendModeCount; ++v) {
        BlendMode mode;
        NC_CHECK(blendModeFromInt(v, mode));
        const char* name = blendModeName(mode);
        NC_CHECK(name != nullptr);
        BlendMode back;
        NC_CHECK(blendModeFromString(name, back));
        NC_CHECK(back == mode);
    }
    // 越界整数拒绝
    BlendMode dummy;
    NC_CHECK(!blendModeFromInt(-1, dummy));
    NC_CHECK(!blendModeFromInt(kBlendModeCount, dummy));
}

NC_TEST(BlendModes, Multiply) {
    // A×B
    expectChannel("multiply 全黑", BlendMode::Multiply, 0.0, 0.5, 0.0);
    expectChannel("multiply 白底", BlendMode::Multiply, 1.0, 0.4, 0.4);
    expectChannel("multiply 中值", BlendMode::Multiply, 0.5, 0.5, 0.25);
    // 只会变暗：结果不超过任一输入
    for (double a = 0.1; a < 1.0; a += 0.1) {
        for (double b = 0.1; b < 1.0; b += 0.1) {
            const double r = blendChannel(BlendMode::Multiply, a, b);
            NC_CHECK(r <= a + 1e-12);
            NC_CHECK(r <= b + 1e-12);
        }
    }
}

NC_TEST(BlendModes, Lighten) {
    expectChannel("lighten 取大", BlendMode::Lighten, 0.2, 0.7, 0.7);
    expectChannel("lighten 反向", BlendMode::Lighten, 0.7, 0.2, 0.7);
    expectChannel("lighten 相等", BlendMode::Lighten, 0.5, 0.5, 0.5);
}

NC_TEST(BlendModes, ColorDodge) {
    expectChannel("dodge 上层纯白", BlendMode::ColorDodge, 0.3, 1.0, 1.0);
    expectChannel("dodge 上层全黑", BlendMode::ColorDodge, 0.3, 0.0, 0.3);
    expectChannel("dodge 中值", BlendMode::ColorDodge, 0.5, 0.5, 1.0);
    // 0.25 / (1 - 0.5) = 0.5
    expectChannel("dodge 精确", BlendMode::ColorDodge, 0.25, 0.5, 0.5);
    // 超界钳制到 1
    expectChannel("dodge 钳制", BlendMode::ColorDodge, 0.9, 0.8, 1.0);
}

NC_TEST(BlendModes, ColorBurn) {
    expectChannel("burn 上层全黑", BlendMode::ColorBurn, 0.3, 0.0, 0.0);
    expectChannel("burn 上层纯白", BlendMode::ColorBurn, 0.3, 1.0, 0.3);
    // 1 - (1-0.5)/0.5 = 0
    expectChannel("burn 精确", BlendMode::ColorBurn, 0.5, 0.5, 0.0);
    // 1 - (1-0.75)/0.5 = 0.5
    expectChannel("burn 半值", BlendMode::ColorBurn, 0.75, 0.5, 0.5);
    // 负值钳制到 0
    expectChannel("burn 钳制", BlendMode::ColorBurn, 0.1, 0.9, 0.0);
}

NC_TEST(BlendModes, LinearBurnAndDodge) {
    // A + B - 1
    expectChannel("linear_burn 中值", BlendMode::LinearBurn, 0.8, 0.5, 0.3);
    expectChannel("linear_burn 下限", BlendMode::LinearBurn, 0.3, 0.3, 0.0);
    // A + B
    expectChannel("linear_dodge 中值", BlendMode::LinearDodge, 0.3, 0.4, 0.7);
    expectChannel("linear_dodge 上限", BlendMode::LinearDodge, 0.8, 0.5, 1.0);
}

NC_TEST(BlendModes, SoftLight) {
    // 上层 0.5 是不动点（软光不改变底图）
    expectChannel("softlight 中性", BlendMode::SoftLight, 0.4, 0.5, 0.4);
    expectChannel("softlight 中性2", BlendMode::SoftLight, 0.9, 0.5, 0.9);
    // B = 0：A - A(1-A) = A²
    expectChannel("softlight 全黑", BlendMode::SoftLight, 0.6, 0.0, 0.36);
    // B = 1：A + (D(A) - A)；A=0.64 → D=sqrt(0.64)=0.8 → 0.64+0.16=0.8
    expectChannel("softlight 纯白", BlendMode::SoftLight, 0.64, 1.0, 0.8);
    // A <= 0.25 走多项式分支：A=0.25 → D=((4-12)*0.25+4)*0.25 = (2-3+4)*0.25? 逐步算：
    // (16*0.25-12)=-8; (-8*0.25+4)=2; (2*0.25)=0.5 → D=0.5
    // B=1 → 0.25 + (0.5-0.25) = 0.5
    expectChannel("softlight 低区", BlendMode::SoftLight, 0.25, 1.0, 0.5);
    // 单调性：B 增大结果不减
    double prev = -1.0;
    for (double b = 0.0; b <= 1.0001; b += 0.05) {
        const double r = blendChannel(BlendMode::SoftLight, 0.4, b);
        NC_CHECK(r >= prev - 1e-12);
        prev = r;
    }
}

NC_TEST(BlendModes, LinearLight) {
    // A + 2B - 1
    expectChannel("linearlight 中性", BlendMode::LinearLight, 0.5, 0.5, 0.5);
    expectChannel("linearlight 提亮", BlendMode::LinearLight, 0.5, 0.75, 1.0);
    expectChannel("linearlight 压暗", BlendMode::LinearLight, 0.5, 0.25, 0.0);
    expectChannel("linearlight 中值", BlendMode::LinearLight, 0.4, 0.6, 0.6);
}

NC_TEST(BlendModes, PinLight) {
    // B < 0.5 → min(A, 2B)
    expectChannel("pinlight 暗", BlendMode::PinLight, 0.8, 0.2, 0.4);
    expectChannel("pinlight 暗2", BlendMode::PinLight, 0.3, 0.2, 0.3);
    // B >= 0.5 → max(A, 2B-1)
    expectChannel("pinlight 亮", BlendMode::PinLight, 0.2, 0.8, 0.6);
    expectChannel("pinlight 亮2", BlendMode::PinLight, 0.9, 0.8, 0.9);
}

NC_TEST(BlendModes, DifferenceAndExclusion) {
    expectChannel("difference 正向", BlendMode::Difference, 0.8, 0.3, 0.5);
    expectChannel("difference 反向", BlendMode::Difference, 0.3, 0.8, 0.5);
    expectChannel("difference 相同", BlendMode::Difference, 0.5, 0.5, 0.0);
    // A + B - 2AB：0.5,0.5 → 0.5
    expectChannel("exclusion 中值", BlendMode::Exclusion, 0.5, 0.5, 0.5);
    // 0.8 + 0.2 - 2*0.8*0.2 = 1.0 - 0.32 = 0.68
    expectChannel("exclusion 精确", BlendMode::Exclusion, 0.8, 0.2, 0.68);
    // 与黑色混合不改：A + 0 - 0 = A
    expectChannel("exclusion 黑", BlendMode::Exclusion, 0.37, 0.0, 0.37);
}

NC_TEST(BlendModes, AllModesStayInUnitRange) {
    // 所有模式对全域输入都必须产出 [0,1]，否则出图会溢出/回绕
    for (int v = 0; v < kBlendModeCount; ++v) {
        BlendMode mode;
        NC_CHECK(blendModeFromInt(v, mode));
        for (int i = 0; i <= 10; ++i) {
            for (int j = 0; j <= 10; ++j) {
                const double a = i / 10.0;
                const double b = j / 10.0;
                const double r = blendChannel(mode, a, b);
                NC_CHECK(r >= -1e-12 && r <= 1.0 + 1e-12);
            }
        }
    }
}

NC_TEST(BlendModes, InputsOutsideRangeAreClamped) {
    // 防御：越界输入先钳制，不应产生越界输出
    NC_CHECK(std::fabs(blendChannel(BlendMode::Multiply, -1.0, 0.5) - 0.0) < 1e-12);
    NC_CHECK(std::fabs(blendChannel(BlendMode::Multiply, 2.0, 0.5) - 0.5) < 1e-12);
    BlendMode m;
    NC_CHECK(blendModeFromString("multiply", m) && m == BlendMode::Multiply);
    NC_CHECK(blendModeFromString("正片叠底", m) && m == BlendMode::Multiply);
    NC_CHECK(blendModeFromString("ColorDodge", m) && m == BlendMode::ColorDodge);
    NC_CHECK(blendModeFromString("soft-light", m) && m == BlendMode::SoftLight);
    NC_CHECK(!blendModeFromString("不存在的模式", m));
    NC_CHECK(!blendModeFromString(nullptr, m));
}

NC_TEST(BlendModes, FrameBlendUsesNewMode) {
    // 2x1 像素：底图灰 128，上层纯白，正片叠底应保持不变（白 × A = A）
    std::vector<std::uint8_t> base = {128, 128, 128, 255, 64, 64, 64, 255};
    const std::vector<std::uint8_t> top = {255, 255, 255, 255, 255, 255, 255, 255};
    NC_CHECK(blendFrameRgba(base.data(), top.data(), 2, 1, BlendMode::Multiply, 1.0));
    NC_CHECK_EQ(static_cast<int>(base[0]), 128);
    NC_CHECK_EQ(static_cast<int>(base[4]), 64);

    // 上层全黑 → 正片叠底结果为 0
    const std::vector<std::uint8_t> black = {0, 0, 0, 255, 0, 0, 0, 255};
    std::vector<std::uint8_t> base2 = {128, 128, 128, 255, 64, 64, 64, 255};
    NC_CHECK(blendFrameRgba(base2.data(), black.data(), 2, 1, BlendMode::Multiply, 1.0));
    NC_CHECK_EQ(static_cast<int>(base2[0]), 0);
    NC_CHECK_EQ(static_cast<int>(base2[4]), 0);
}
