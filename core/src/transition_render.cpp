#include "nairoclip/core/transition_render.h"

#include <cmath>

namespace nairoclip {
namespace core {

namespace {

double clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

/// 逐像素混合：out = a * (1 - w) + b * w（w 可逐像素变化）。
template <typename WeightFn>
void blendPixels(const std::uint8_t* a, const std::uint8_t* b, std::uint8_t* out, int width,
                 int height, WeightFn weightAt) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) *
                4;
            const double w = clamp01(weightAt(x, y));
            for (int c = 0; c < 4; ++c) {
                const double va = static_cast<double>(a[index + static_cast<std::size_t>(c)]);
                const double vb = static_cast<double>(b[index + static_cast<std::size_t>(c)]);
                const double mixed = va * (1.0 - w) + vb * w;
                out[index + static_cast<std::size_t>(c)] =
                    static_cast<std::uint8_t>(mixed + 0.5);
            }
        }
    }
}

}  // namespace

double TransitionRenderer::transitionProgress(const Time& current, const Time& start,
                                              const Time& duration) {
    if (!duration.isPositive()) {
        return 1.0;
    }
    const double elapsed = (current - start).toDouble();
    return clamp01(elapsed / duration.toDouble());
}

bool TransitionRenderer::renderFrame(TransitionType type, const std::uint8_t* a,
                                     const std::uint8_t* b, std::uint8_t* out, int width,
                                     int height, double progress) {
    if (a == nullptr || b == nullptr || out == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const double t = clamp01(progress);
    switch (type) {
        case TransitionType::CrossDissolve:
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            return true;

        case TransitionType::FadeToBlack:
            // 前半淡出到黑，后半从黑淡入
            if (t < 0.5) {
                const double w = t * 2.0;  // 0→1 走向黑
                blendPixels(a, a, out, width, height, [w](int, int) { return 0.0; });
                // 手动应用衰减（到黑）
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const std::size_t index =
                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            4;
                        for (int c = 0; c < 3; ++c) {
                            const double va = static_cast<double>(a[index + static_cast<std::size_t>(c)]);
                            out[index + static_cast<std::size_t>(c)] =
                                static_cast<std::uint8_t>(va * (1.0 - w) + 0.5);
                        }
                        out[index + 3] = a[index + 3];
                    }
                }
            } else {
                const double w = (t - 0.5) * 2.0;  // 0→1 从黑进来
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const std::size_t index =
                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            4;
                        for (int c = 0; c < 3; ++c) {
                            const double vb = static_cast<double>(b[index + static_cast<std::size_t>(c)]);
                            out[index + static_cast<std::size_t>(c)] =
                                static_cast<std::uint8_t>(vb * w + 0.5);
                        }
                        out[index + 3] = b[index + 3];
                    }
                }
            }
            return true;

        case TransitionType::FadeFromBlack:
            // 直接从黑淡入到 b
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    for (int c = 0; c < 3; ++c) {
                        const double v = static_cast<double>(b[index + static_cast<std::size_t>(c)]);
                        out[index + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(v * t + 0.5);
                    }
                    out[index + 3] = b[index + 3];
                }
            }
            return true;

        case TransitionType::FlashWhite:
            // 中点最白：a → 白 → b
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    // 闪白强度 = 1 - |2t - 1|
                    const double flash = 1.0 - std::fabs(2.0 * t - 1.0);
                    const double baseW = t < 0.5 ? 0.0 : 1.0;
                    for (int c = 0; c < 3; ++c) {
                        const double va = static_cast<double>(a[index + static_cast<std::size_t>(c)]);
                        const double vb = static_cast<double>(b[index + static_cast<std::size_t>(c)]);
                        const double base = va * (1.0 - baseW) + vb * baseW;
                        const double mixed = base * (1.0 - flash) + 255.0 * flash;
                        out[index + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(mixed + 0.5);
                    }
                    out[index + 3] = t < 0.5 ? a[index + 3] : b[index + 3];
                }
            }
            return true;

        case TransitionType::WipeLeft:
            // 扫描线从右向左：w = 1 当 x < width * (1 - t)
            blendPixels(a, b, out, width, height, [t, width](int x, int) {
                const double boundary = static_cast<double>(width) * (1.0 - t);
                return static_cast<double>(x) < boundary ? 0.0 : 1.0;
            });
            return true;

        case TransitionType::WipeRight:
            blendPixels(a, b, out, width, height, [t, width](int x, int) {
                const double boundary = static_cast<double>(width) * t;
                return static_cast<double>(x) < boundary ? 1.0 : 0.0;
            });
            return true;

        case TransitionType::SlideUp:
            blendPixels(a, b, out, width, height, [t, height](int, int y) {
                const double boundary = static_cast<double>(height) * (1.0 - t);
                return static_cast<double>(y) < boundary ? 0.0 : 1.0;
            });
            return true;

        case TransitionType::Zoom:
            // 径向扩散：从中心圆形展开
            blendPixels(a, b, out, width, height, [t, width, height](int x, int y) {
                const double cx = static_cast<double>(width) / 2.0;
                const double cy = static_cast<double>(height) / 2.0;
                const double dx = static_cast<double>(x) - cx;
                const double dy = static_cast<double>(y) - cy;
                const double maxDist = std::sqrt(cx * cx + cy * cy);
                const double dist = std::sqrt(dx * dx + dy * dy) / maxDist;
                // 圆内先切
                return dist < t ? 1.0 : 0.0;
            });
            return true;
    }
    return false;
}

}  // namespace core
}  // namespace nairoclip
