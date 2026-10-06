#include "test_framework.h"
#include "nairoclip/core/transition_render.h"
#include <vector>
using namespace nairoclip::core;
namespace {
std::vector<std::uint8_t> solid(int w, int h, std::uint8_t gray) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * h * 4);
    for (std::size_t i = 0; i < pixels.size(); i += 4) {
        pixels[i] = gray;
        pixels[i + 1] = gray;
        pixels[i + 2] = gray;
        pixels[i + 3] = 255;
    }
    return pixels;
}
}  // namespace
NC_TEST(TransitionRender, CrossDissolveMidpoint) {
    const auto a = solid(4, 4, 0);
    const auto b = solid(4, 4, 200);
    std::vector<std::uint8_t> out(a.size(), 0);
    NC_CHECK(TransitionRenderer::renderFrame(TransitionType::CrossDissolve, a.data(), b.data(),
                                             out.data(), 4, 4, 0.5));
    // 中点混合：0 * 0.5 + 200 * 0.5 = 100
    NC_CHECK(out[0] >= 99 && out[0] <= 101);
    NC_CHECK_EQ(out[3], std::uint8_t(255));  // alpha 保留
}
NC_TEST(TransitionRender, CrossDissolveEndpoints) {
    const auto a = solid(2, 2, 10);
    const auto b = solid(2, 2, 250);
    std::vector<std::uint8_t> out(a.size(), 0);
    // t=0 → 全 A
    NC_CHECK(TransitionRenderer::renderFrame(TransitionType::CrossDissolve, a.data(), b.data(),
                                             out.data(), 2, 2, 0.0));
    NC_CHECK_EQ(out[0], std::uint8_t(10));
    // t=1 → 全 B
    NC_CHECK(TransitionRenderer::renderFrame(TransitionType::CrossDissolve, a.data(), b.data(),
                                             out.data(), 2, 2, 1.0));
    NC_CHECK_EQ(out[0], std::uint8_t(250));
}
NC_TEST(TransitionRender, ProgressClamped) {
    const auto a = solid(2, 2, 100);
    const auto b = solid(2, 2, 200);
    std::vector<std::uint8_t> out(a.size(), 0);
    // progress 超界应被钳制
    TransitionRenderer::renderFrame(TransitionType::CrossDissolve, a.data(), b.data(), out.data(),
                                    2, 2, 2.0);
    NC_CHECK_EQ(out[0], std::uint8_t(200));
    TransitionRenderer::renderFrame(TransitionType::CrossDissolve, a.data(), b.data(), out.data(),
                                    2, 2, -1.0);
    NC_CHECK_EQ(out[0], std::uint8_t(100));
}
NC_TEST(TransitionRender, RejectsNull) {
    std::vector<std::uint8_t> out(64, 0);
    NC_CHECK(!TransitionRenderer::renderFrame(TransitionType::CrossDissolve, nullptr, nullptr,
                                              out.data(), 4, 4, 0.5));
}
NC_TEST(TransitionRender, FadeToBlackMiddles) {
    const auto a = solid(2, 2, 200);
    const auto b = solid(2, 2, 100);
    std::vector<std::uint8_t> out(a.size(), 0);
    // t=0.25 → 淡出中（A 半暗）：200 * 0.5 = 100
    TransitionRenderer::renderFrame(TransitionType::FadeToBlack, a.data(), b.data(), out.data(), 2,
                                    2, 0.25);
    NC_CHECK(out[0] >= 99 && out[0] <= 101);
    // t=0.75 → 从黑进场（B 半亮）：100 * 0.5 = 50
    TransitionRenderer::renderFrame(TransitionType::FadeToBlack, a.data(), b.data(), out.data(), 2,
                                    2, 0.75);
    NC_CHECK(out[0] >= 49 && out[0] <= 51);
}
NC_TEST(TransitionRender, FlashWhitePeaksAtMiddle) {
    const auto a = solid(2, 2, 0);
    const auto b = solid(2, 2, 0);
    std::vector<std::uint8_t> out(a.size(), 0);
    TransitionRenderer::renderFrame(TransitionType::FlashWhite, a.data(), b.data(), out.data(), 2, 2,
                                    0.5);
    // 中点全白
    NC_CHECK_EQ(out[0], std::uint8_t(255));
}
NC_TEST(TransitionRender, WipeLeftSpacial) {
    const auto a = solid(4, 2, 0);
    const auto b = solid(4, 2, 200);
    std::vector<std::uint8_t> out(a.size(), 0);
    // t=0.5 → 一半 A 一半 B
    TransitionRenderer::renderFrame(TransitionType::WipeLeft, a.data(), b.data(), out.data(), 4, 2,
                                    0.5);
    // 第 0 行：x=0 处应为 B（boundary=2, x<2 → A？再检查语义）
    // WipeLeft: boundary = width*(1-t) = 2；x < 2 → w=0（A），x >= 2 → w=1（B）
    NC_CHECK_EQ(out[0], std::uint8_t(0));      // x=0 → A
    NC_CHECK_EQ(out[4 * 3], std::uint8_t(200));  // x=3 → B
}
NC_TEST(TransitionRender, ProgressHelper) {
    NC_CHECK(std::abs(TransitionRenderer::transitionProgress(Rational::make(1), Rational::make(0),
                                                             Rational::make(2)) -
                      0.5) < 1e-9);
    // 超时钳制
    NC_CHECK(std::abs(TransitionRenderer::transitionProgress(Rational::make(5), Rational::make(0),
                                                             Rational::make(2)) -
                      1.0) < 1e-9);
    // 零时长
    NC_CHECK(std::abs(TransitionRenderer::transitionProgress(Rational::make(1), Rational::make(0),
                                                             Time{}) -
                      1.0) < 1e-9);
}
