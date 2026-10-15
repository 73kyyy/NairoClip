// =============================================================================
// 人物特效 + 画面特效补充测试（文档 Part 4-B 5.2 预设清单：
//   画面特效：闪光 / 马赛克；人物特效：抖动 / 放大 / 边缘检测 / 瘦身 / 发光轮廓）
// =============================================================================
#include "test_framework.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/person_effects.h"
#include "nairoclip/core/screen_effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

constexpr int kW = 64;
constexpr int kH = 48;

/// 生成测试图（左半黑右半白 + 中间渐变；便于检测位移/压缩/边缘）。
std::vector<std::uint8_t> makeTestImage() {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(kW) * kH * 4, 255);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            std::uint8_t* px = pixels.data() + (static_cast<std::size_t>(y) * kW +
                                                static_cast<std::size_t>(x)) * 4;
            const std::uint8_t v = x < kW / 2 ? 40 : 220;
            px[0] = v;
            px[1] = static_cast<std::uint8_t>(x * 255 / kW);
            px[2] = static_cast<std::uint8_t>(y * 255 / kH);
            px[3] = 255;
        }
    }
    return pixels;
}

/// 平均亮度差（检测效果是否真实改变了画面）。
double meanAbsDiff(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i += 4) {
        sum += std::abs(static_cast<int>(a[i]) - static_cast<int>(b[i]));
        ++count;
    }
    return count > 0 ? sum / static_cast<double>(count) : 0.0;
}

/// 生成 Alpha 蒙版（中央圆形 = 人像区域）。
std::vector<std::uint8_t> makeCircleAlpha() {
    std::vector<std::uint8_t> alpha(static_cast<std::size_t>(kW) * kH, 0);
    const double cx = kW * 0.5;
    const double cy = kH * 0.5;
    const double r = std::min(kW, kH) * 0.3;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const double dx = x - cx;
            const double dy = y - cy;
            if (dx * dx + dy * dy < r * r) {
                alpha[static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)] = 255;
            }
        }
    }
    return alpha;
}

}  // namespace

// ---------------------------------------------------------------------------
// 闪光（文档「画面特效 → 闪光」）
// ---------------------------------------------------------------------------

NC_TEST(PersonEffects, FlashBrightensDuringPulse) {
    const std::vector<std::uint8_t> original = makeTestImage();
    // 脉冲峰值时刻（t = 0.0，phase = 0 → pulse = 1）
    std::vector<std::uint8_t> bright = original;
    NC_CHECK(applyFlashRgba(bright.data(), kW, kH, 1.0, 0.0));
    // 画面应整体变亮
    double sumBefore = 0.0;
    double sumAfter = 0.0;
    for (std::size_t i = 0; i < original.size(); i += 4) {
        sumBefore += original[i];
        sumAfter += bright[i];
    }
    NC_CHECK(sumAfter > sumBefore * 1.15);
    // 周期外时刻（t = 0.19，phase = 0.95 → pulse = 0）零改动
    std::vector<std::uint8_t> quiet = original;
    NC_CHECK(applyFlashRgba(quiet.data(), kW, kH, 1.0, 0.19));
    NC_CHECK_EQ(meanAbsDiff(quiet, original), 0.0);
    // 强度 0 零改动
    std::vector<std::uint8_t> zero = original;
    NC_CHECK(applyFlashRgba(zero.data(), kW, kH, 0.0, 0.0));
    NC_CHECK_EQ(meanAbsDiff(zero, original), 0.0);
}

// ---------------------------------------------------------------------------
// 马赛克（文档「画面特效 → 马赛克」）
// ---------------------------------------------------------------------------

NC_TEST(PersonEffects, MosaicPixelates) {
    const std::vector<std::uint8_t> original = makeTestImage();
    std::vector<std::uint8_t> mosaic = original;
    NC_CHECK(applyMosaicRgba(mosaic.data(), kW, kH, 0.8));
    // 画面被改变
    NC_CHECK(meanAbsDiff(mosaic, original) > 1.0);
    // 块内像素一致（像素化特征）：前 2 行内相邻像素（同块）应相同
    const std::uint8_t* px0 = mosaic.data();
    const std::uint8_t* px1 = mosaic.data() + 4;  // x+1（同块，块尺寸 ≥ 2）
    NC_CHECK_EQ(px0[0], px1[0]);
    // 块值应为该块原像素的均值（而非 0/黑块）：左上角块的源值分布 40..~46
    // （测试图左半为 40，第 0 行的绿色通道为 0..255 渐变）
    // 校验：块值应接近源块均值（容差 ±2），排除「退化为 0」的实现缺陷
    {
        const int blockSize = std::max(2, static_cast<int>(std::lround(kW * 0.0625 * 0.8)));
        double sumR = 0.0;
        int count = 0;
        for (int y = 0; y < std::min(kH, blockSize); ++y) {
            for (int x = 0; x < std::min(kW, blockSize); ++x) {
                sumR += original[(static_cast<std::size_t>(y) * kW +
                                  static_cast<std::size_t>(x)) * 4];
                ++count;
            }
        }
        const double expected = sumR / std::max(1, count);
        NC_CHECK(std::abs(static_cast<double>(px0[0]) - expected) <= 2.0);
    }
    // 强度 0 零改动
    std::vector<std::uint8_t> zero = original;
    NC_CHECK(applyMosaicRgba(zero.data(), kW, kH, 0.0));
    NC_CHECK_EQ(meanAbsDiff(zero, original), 0.0);
}

// ---------------------------------------------------------------------------
// 抖动（文档「人物特效 → 抖动」）
// ---------------------------------------------------------------------------

NC_TEST(PersonEffects, JitterShiftsFrame) {
    const std::vector<std::uint8_t> original = makeTestImage();
    // 找一个使位移非零的时刻（时间噪声驱动）
    bool changed = false;
    for (int step = 0; step < 20 && !changed; ++step) {
        std::vector<std::uint8_t> jittered = original;
        NC_CHECK(applyJitterRgba(jittered.data(), kW, kH, 1.0, step * 0.1));
        if (meanAbsDiff(jittered, original) > 0.5) {
            changed = true;
        }
    }
    NC_CHECK(changed);  // 至少某些时刻产生真实位移
    // 确定性：同一时刻两次 → 相同结果
    std::vector<std::uint8_t> a = original;
    std::vector<std::uint8_t> b = original;
    NC_CHECK(applyJitterRgba(a.data(), kW, kH, 1.0, 3.7));
    NC_CHECK(applyJitterRgba(b.data(), kW, kH, 1.0, 3.7));
    NC_CHECK_EQ(meanAbsDiff(a, b), 0.0);
    // 强度 0 零改动
    std::vector<std::uint8_t> zero = original;
    NC_CHECK(applyJitterRgba(zero.data(), kW, kH, 0.0, 3.7));
    NC_CHECK_EQ(meanAbsDiff(zero, original), 0.0);
}

// ---------------------------------------------------------------------------
// 放大（文档「人物特效 → 放大」）
// ---------------------------------------------------------------------------

NC_TEST(PersonEffects, ZoomInMagnifiesCenter) {
    const std::vector<std::uint8_t> original = makeTestImage();
    std::vector<std::uint8_t> zoomed = original;
    NC_CHECK(applyZoomInRgba(zoomed.data(), kW, kH, 0.8));
    // 画面被改变（中心放大 → 边界被拉伸）
    NC_CHECK(meanAbsDiff(zoomed, original) > 1.0);
    // 边缘像素应来自更靠内的源（左边缘变亮：源 x > 0 处的亮度）
    // 原左边缘 x=0 是 40（暗），放大后应变得比原来亮
    NC_CHECK(zoomed[0] >= original[0]);
    // 强度 0 零改动
    std::vector<std::uint8_t> zero = original;
    NC_CHECK(applyZoomInRgba(zero.data(), kW, kH, 0.0));
    NC_CHECK_EQ(meanAbsDiff(zero, original), 0.0);
}

// ---------------------------------------------------------------------------
// 边缘检测（文档「人物特效 → 边缘检测」）
// ---------------------------------------------------------------------------

NC_TEST(PersonEffects, EdgeDetectFindsEdges) {
    const std::vector<std::uint8_t> original = makeTestImage();
    std::vector<std::uint8_t> edges = original;
    NC_CHECK(applyEdgeDetectRgba(edges.data(), kW, kH, 1.0));
    // 亮度突变处（x = kW/2 黑→白边界）应有高边缘响应
    const int boundaryX = kW / 2;
    const int midY = kH / 2;
    std::uint8_t maxEdge = 0;
    for (int dx = -2; dx <= 2; ++dx) {
        const std::size_t idx =
            (static_cast<std::size_t>(midY) * kW + static_cast<std::size_t>(boundaryX + dx)) * 4;
        maxEdge = std::max(maxEdge, edges[idx]);
    }
    // 平坦区（x = 5）边缘响应应低
    const std::size_t flatIdx = (static_cast<std::size_t>(midY) * kW + 5) * 4;
    NC_CHECK(maxEdge > edges[flatIdx] + 40);
    // 强度 0 零改动
    std::vector<std::uint8_t> zero = original;
    NC_CHECK(applyEdgeDetectRgba(zero.data(), kW, kH, 0.0));
    NC_CHECK_EQ(meanAbsDiff(zero, original), 0.0);
}

// ---------------------------------------------------------------------------
// 瘦身 / 发光轮廓（依赖抠像 Alpha；文档 5.2）
// ---------------------------------------------------------------------------

NC_TEST(PersonEffects, SlimRequiresAlpha) {
    // 用高对比棋盘图（形变才可见；渐变图在形变区颜色相近，差异过小）
    std::vector<std::uint8_t> original(static_cast<std::size_t>(kW) * kH * 4, 255);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            std::uint8_t* px = original.data() +
                               (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 4;
            const std::uint8_t v = ((x / 4 + y / 4) % 2) ? 240 : 20;
            px[0] = v;
            px[1] = v;
            px[2] = v;
        }
    }
    // 无 Alpha → 零改动（不伪造人像区域）
    std::vector<std::uint8_t> noAlpha = original;
    NC_CHECK(applySlimRgba(noAlpha.data(), kW, kH, 1.0, nullptr));
    NC_CHECK_EQ(meanAbsDiff(noAlpha, original), 0.0);
    // 有 Alpha → 真实形变（人像区域内像素明显变化）
    const std::vector<std::uint8_t> alpha = makeCircleAlpha();
    std::vector<std::uint8_t> slimmed = original;
    NC_CHECK(applySlimRgba(slimmed.data(), kW, kH, 1.0, alpha.data()));
    NC_CHECK(meanAbsDiff(slimmed, original) > 0.5);
}

NC_TEST(PersonEffects, GlowOutlineRequiresAlpha) {
    const std::vector<std::uint8_t> original = makeTestImage();
    // 无 Alpha → 零改动
    std::vector<std::uint8_t> noAlpha = original;
    NC_CHECK(applyGlowOutlineRgba(noAlpha.data(), kW, kH, 1.0, nullptr,
                                  RenderQuality::exportQuality()));
    NC_CHECK_EQ(meanAbsDiff(noAlpha, original), 0.0);
    // 有 Alpha → 边缘处出现发光（比原图更亮）
    const std::vector<std::uint8_t> alpha = makeCircleAlpha();
    std::vector<std::uint8_t> glowing = original;
    NC_CHECK(applyGlowOutlineRgba(glowing.data(), kW, kH, 1.0, alpha.data(),
                                  RenderQuality::exportQuality()));
    double sumBefore = 0.0;
    double sumAfter = 0.0;
    for (std::size_t i = 0; i < original.size(); i += 4) {
        sumBefore += original[i];
        sumAfter += glowing[i];
    }
    NC_CHECK(sumAfter > sumBefore);  // 发光叠加使整体变亮
}

// ---------------------------------------------------------------------------
// 分发与特效栈接入（文档 Part 4-B：EffectType 29..35）
// ---------------------------------------------------------------------------

NC_TEST(PersonEffects, DispatcherRouting) {
    const std::vector<std::uint8_t> original = makeTestImage();
    // 错误类型：不改动且返回 true（由其他模块处理）
    std::vector<std::uint8_t> untouched = original;
    NC_CHECK(applyPersonEffectToRgba(untouched.data(), kW, kH, 96, 1.0, 0.0));
    NC_CHECK_EQ(meanAbsDiff(untouched, original), 0.0);
    // 合法类型（抖动，选非零位移时刻）
    bool changed = false;
    for (int step = 0; step < 20 && !changed; ++step) {
        std::vector<std::uint8_t> routed = original;
        NC_CHECK(applyPersonEffectToRgba(routed.data(), kW, kH, 31, 1.0, step * 0.1));
        if (meanAbsDiff(routed, original) > 0.5) {
            changed = true;
        }
    }
    NC_CHECK(changed);
}

NC_TEST(PersonEffects, EffectStackAppliesFlashAndEffects) {
    // 通过特效栈路径验证新特效真实生效（导出/预览共用入口）
    const std::vector<std::uint8_t> original = makeTestImage();
    std::vector<std::uint8_t> frame = original;
    EffectStack stack;
    Effect flash;
    flash.id = EffectId{1};
    flash.type = EffectType::Flash;
    flash.value = 1.0;
    NC_CHECK(stack.add(flash).isOk());
    // timeSeconds = 0.0 → 闪光峰值；frameIndex 对应 t = 0（renderAt 约定 30fps）
    NC_CHECK(applyEffectStackToFrame(frame.data(), kW, kH, stack, 0, RenderQuality::exportQuality(),
                                     0.0));
    NC_CHECK(meanAbsDiff(frame, original) > 2.0);
    // EffectType::Mosaic 亦可通过栈路径生效
    std::vector<std::uint8_t> frame2 = original;
    EffectStack stack2;
    Effect mosaic;
    mosaic.id = EffectId{2};
    mosaic.type = EffectType::Mosaic;
    mosaic.value = 0.8;
    NC_CHECK(stack2.add(mosaic).isOk());
    NC_CHECK(applyEffectStackToFrame(frame2.data(), kW, kH, stack2, 0,
                                     RenderQuality::exportQuality(), 0.0));
    NC_CHECK(meanAbsDiff(frame2, original) > 1.0);
}

NC_TEST(PersonEffects, EffectSpecNamesMatchDocument) {
    // 文档 Part 4-B 5.2 预设清单逐字核对（画面特效/人物特效新增项）
    NC_CHECK_EQ(std::string(effectSpec(EffectType::Flash).name), std::string("闪光"));
    NC_CHECK_EQ(std::string(effectSpec(EffectType::Mosaic).name), std::string("马赛克"));
    NC_CHECK_EQ(std::string(effectSpec(EffectType::Jitter).name), std::string("抖动"));
    NC_CHECK_EQ(std::string(effectSpec(EffectType::ZoomIn).name), std::string("放大"));
    NC_CHECK_EQ(std::string(effectSpec(EffectType::EdgeDetect).name), std::string("边缘检测"));
    NC_CHECK_EQ(std::string(effectSpec(EffectType::Slim).name), std::string("瘦身"));
    NC_CHECK_EQ(std::string(effectSpec(EffectType::GlowOutline).name), std::string("发光轮廓"));
}
