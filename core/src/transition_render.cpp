#include "nairoclip/core/transition_render.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

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

        // ===================================================================
        // 文档 Part 4-B 6.1 新增预设（8..33）
        // 实现取向：单帧像素运算 + 时间进度驱动（与既有 8 种同一框架）。
        // 确定性：同一 (a, b, t) → 同一输出（可测）。
        // ===================================================================

        case TransitionType::Cut:
            // 硬切：t < 0.5 显示 A，否则显示 B（无混合）
            if (t < 0.5) {
                std::memcpy(out, a,
                            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
            } else {
                std::memcpy(out, b,
                            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
            }
            return true;

        case TransitionType::ColorDissolve: {
            // 色彩叠化：溶解 + 色偏（前半偏暖、后半偏冷）
            const double warm = 1.0 - std::fabs(2.0 * t - 1.0);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    const double phase = (t < 0.5 ? 1.0 : -1.0) * warm;
                    for (int c = 0; c < 3; ++c) {
                        const double va = a[index + static_cast<std::size_t>(c)];
                        const double vb = b[index + static_cast<std::size_t>(c)];
                        double mixed = va * (1.0 - t) + vb * t;
                        if (c == 0) mixed += phase * 40.0;       // 暖偏 R
                        if (c == 2) mixed -= phase * 40.0;       // 冷偏 B
                        out[index + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(clamp01(mixed / 255.0) * 255.0 + 0.5);
                    }
                    out[index + 3] = static_cast<std::uint8_t>(
                        static_cast<double>(a[index + 3]) * (1.0 - t) +
                        static_cast<double>(b[index + 3]) * t + 0.5);
                }
            }
            return true;
        }

        case TransitionType::BlurDissolve: {
            // 模糊叠化：中段模糊最强（模糊 A 与 B 的混合）
            const double blurAmount = 1.0 - std::fabs(2.0 * t - 1.0);  // 中段最大
            const int radius = std::max(1, static_cast<int>(blurAmount * 6.0));
            std::vector<std::uint8_t> blurred(
                static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
            // 先按 t 混合，再对结果做盒式模糊（强度随进度）
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            if (blurAmount > 0.02) {
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        int sum[3] = {0, 0, 0};
                        int count = 0;
                        for (int dy = -radius; dy <= radius; ++dy) {
                            const int yy = std::max(0, std::min(height - 1, y + dy));
                            for (int dx = -radius; dx <= radius; ++dx) {
                                const int xx = std::max(0, std::min(width - 1, x + dx));
                                const std::uint8_t* px =
                                    out + (static_cast<std::size_t>(yy) *
                                               static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(xx)) *
                                              4;
                                sum[0] += px[0];
                                sum[1] += px[1];
                                sum[2] += px[2];
                                ++count;
                            }
                        }
                        std::uint8_t* dst =
                            blurred.data() +
                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                                4;
                        dst[0] = static_cast<std::uint8_t>(sum[0] / count);
                        dst[1] = static_cast<std::uint8_t>(sum[1] / count);
                        dst[2] = static_cast<std::uint8_t>(sum[2] / count);
                        dst[3] = 255;
                    }
                }
                std::memcpy(out, blurred.data(),
                            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
            }
            return true;
        }

        case TransitionType::ZoomOut:
            // 拉远：从外圈向内收（反向径向）
            blendPixels(a, b, out, width, height, [t, width, height](int x, int y) {
                const double cx = static_cast<double>(width) / 2.0;
                const double cy = static_cast<double>(height) / 2.0;
                const double dx = static_cast<double>(x) - cx;
                const double dy = static_cast<double>(y) - cy;
                const double maxDist = std::sqrt(cx * cx + cy * cy);
                const double dist = std::sqrt(dx * dx + dy * dy) / maxDist;
                return dist > 1.0 - t ? 1.0 : 0.0;
            });
            return true;

        case TransitionType::Rotate:
            // 旋转：旋涡式（角度随半径与进度）
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double cx = static_cast<double>(width) / 2.0;
                    const double cy = static_cast<double>(height) / 2.0;
                    const double dx = static_cast<double>(x) - cx;
                    const double dy = static_cast<double>(y) - cy;
                    const double maxDist = std::sqrt(cx * cx + cy * cy);
                    const double dist = std::sqrt(dx * dx + dy * dy) / std::max(1.0, maxDist);
                    const double angle = t * 3.14159 * 2.0 * (1.0 - dist);
                    const double cs = std::cos(angle);
                    const double sn = std::sin(angle);
                    double u = (dx * cs - dy * sn) + cx;
                    double v = (dx * sn + dy * cs) + cy;
                    const int sx = std::max(0, std::min(width - 1, static_cast<int>(u)));
                    const int sy = std::max(0, std::min(height - 1, static_cast<int>(v)));
                    const std::size_t dstIdx =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    const std::size_t srcIdxA =
                        (static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(sx)) *
                        4;
                    const std::size_t srcIdxB =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    for (int c = 0; c < 4; ++c) {
                        const double va = a[srcIdxA + static_cast<std::size_t>(c)];
                        const double vb = b[srcIdxB + static_cast<std::size_t>(c)];
                        out[dstIdx + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(va * (1.0 - t) + vb * t + 0.5);
                    }
                }
            }
            return true;

        case TransitionType::WhipPan:
            // 甩镜：水平方向性扫描 + 前段快速移动（加速曲线）
            blendPixels(a, b, out, width, height, [t, width](int x, int) {
                // 加速：前 70% 完成大部分扫描
                const double eased = t < 1.0 ? 1.0 - std::pow(1.0 - t, 3.0) : 1.0;
                const double boundary = static_cast<double>(width) * eased;
                return static_cast<double>(x) < boundary ? 1.0 : 0.0;
            });
            return true;

        case TransitionType::ZoomPunch: {
            // 缩放冲击：中段脉冲式缩放（画面放大再回归）
            const double punch = std::sin(t * 3.14159);  // 0 → 1 → 0
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            if (punch > 0.02) {
                std::vector<std::uint8_t> src(
                    out, out + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                   4);
                const double scale = 1.0 + punch * 0.15;
                for (int y = 0; y < height; ++y) {
                    for (int x = 0; x < width; ++x) {
                        const double u = (x - width * 0.5) / scale + width * 0.5;
                        const double v = (y - height * 0.5) / scale + height * 0.5;
                        const int sx = std::max(0, std::min(width - 1, static_cast<int>(u)));
                        const int sy = std::max(0, std::min(height - 1, static_cast<int>(v)));
                        const std::size_t dstIdx =
                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            4;
                        const std::size_t srcIdx =
                            (static_cast<std::size_t>(sy) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(sx)) *
                            4;
                        for (int c = 0; c < 4; ++c) {
                            out[dstIdx + static_cast<std::size_t>(c)] =
                                src[srcIdx + static_cast<std::size_t>(c)];
                        }
                    }
                }
            }
            return true;
        }

        case TransitionType::Ripple:
            // 波纹：正弦扰动扫描边界（水平方向）
            blendPixels(a, b, out, width, height, [t, width, height](int x, int y) {
                const double wave =
                    0.08 * std::sin(static_cast<double>(y) / std::max(1, height) * 3.14159 * 8.0);
                const double boundary = static_cast<double>(width) * (t + wave);
                return static_cast<double>(x) < boundary ? 1.0 : 0.0;
            });
            return true;

        case TransitionType::Liquify:
            // 液化：径向坐标扰动（边界随半径起伏）
            blendPixels(a, b, out, width, height, [t, width, height](int x, int y) {
                const double cx = static_cast<double>(width) / 2.0;
                const double cy = static_cast<double>(height) / 2.0;
                const double dx = static_cast<double>(x) - cx;
                const double dy = static_cast<double>(y) - cy;
                const double maxDist = std::sqrt(cx * cx + cy * cy);
                const double dist = std::sqrt(dx * dx + dy * dy) / std::max(1.0, maxDist);
                const double wobble = 0.06 * std::sin(dist * 12.0);
                return t + wobble > dist ? 1.0 : 0.0;
            });
            return true;

        case TransitionType::Fisheye:
            // 鱼眼：桶形畸变强度随进度（居中放大径向采样）
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double cx = static_cast<double>(width) / 2.0;
                    const double cy = static_cast<double>(height) / 2.0;
                    const double nx = (x - cx) / cx;
                    const double ny = (y - cy) / cy;
                    const double r2 = nx * nx + ny * ny;
                    const double distortion = 1.0 + t * 0.5 * r2;
                    const int sx = static_cast<int>(nx * distortion * cx + cx);
                    const int sy = static_cast<int>(ny * distortion * cy + cy);
                    const int clampedX = std::max(0, std::min(width - 1, sx));
                    const int clampedY = std::max(0, std::min(height - 1, sy));
                    const std::size_t dstIdx =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    const std::size_t srcIdxA =
                        (static_cast<std::size_t>(clampedY) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(clampedX)) *
                        4;
                    const std::size_t srcIdxB =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    for (int c = 0; c < 4; ++c) {
                        const double va = a[srcIdxA + static_cast<std::size_t>(c)];
                        const double vb = b[srcIdxB + static_cast<std::size_t>(c)];
                        out[dstIdx + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(va * (1.0 - t) + vb * t + 0.5);
                    }
                }
            }
            return true;

        case TransitionType::GlitchWarp:
            // 故障扭曲：分行块位移（每行按进度出现随机水平偏移）
            for (int y = 0; y < height; ++y) {
                const double rowNoise = std::fmod(std::sin(y * 12.9898 + t * 78.233) * 43758.5453,
                                                  1.0);
                const int shift = static_cast<int>((rowNoise - 0.5) * 24.0 * t);
                for (int x = 0; x < width; ++x) {
                    const int sxA = std::max(0, std::min(width - 1, x - shift));
                    const std::size_t dstIdx =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    const std::size_t srcIdxA =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(sxA)) *
                        4;
                    const std::size_t srcIdxB = dstIdx;
                    for (int c = 0; c < 4; ++c) {
                        const double va = a[srcIdxA + static_cast<std::size_t>(c)];
                        const double vb = b[srcIdxB + static_cast<std::size_t>(c)];
                        out[dstIdx + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(va * (1.0 - t) + vb * t + 0.5);
                    }
                }
            }
            return true;

        case TransitionType::RgbSplit: {
            // RGB分离：A 的三通道各自水平错位后过渡到 B
            const int shift = static_cast<int>(t * 12.0);
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t dstIdx =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    for (int c = 0; c < 3; ++c) {
                        const int offset = (c - 1) * shift;
                        const int sx = std::max(0, std::min(width - 1, x + offset));
                        const std::size_t srcIdxA =
                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(sx)) *
                            4;
                        const double va = a[srcIdxA + static_cast<std::size_t>(c)];
                        const double vb = b[dstIdx + static_cast<std::size_t>(c)];
                        out[dstIdx + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(va * (1.0 - t) + vb * t + 0.5);
                    }
                    out[dstIdx + 3] = 255;
                }
            }
            return true;
        }

        case TransitionType::SignalNoise: {
            // 信号干扰：扫描带随机亮度扰动
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            const int bandCenter = static_cast<int>(t * height);
            const int bandHalf = std::max(2, height / 12);
            for (int y = std::max(0, bandCenter - bandHalf);
                 y < std::min(height, bandCenter + bandHalf); ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    const double noise =
                        std::fmod(std::sin(x * 12.9898 + y * 78.233) * 43758.5453, 1.0);
                    for (int c = 0; c < 3; ++c) {
                        const double base = out[index + static_cast<std::size_t>(c)];
                        out[index + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                            clamp01((base * (1.0 - noise * 0.65) + 255.0 * noise * 0.65) / 255.0) *
                                255.0 +
                            0.5);
                    }
                }
            }
            return true;
        }

        case TransitionType::DigitalNoise: {
            // 数字噪点：随机块置换（按进度）
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            const int blockSize = 8;
            for (int by = 0; by < height; by += blockSize) {
                for (int bx = 0; bx < width; bx += blockSize) {
                    const double noise = std::fmod(
                        std::sin(bx * 12.9898 + by * 78.233 + t * 37.719) * 43758.5453, 1.0);
                    if (noise > t) {
                        continue;
                    }
                    // 块内随机偏移拷贝
                    const int shiftX = static_cast<int>((noise - 0.5) * blockSize * 2.0);
                    for (int y = by; y < std::min(height, by + blockSize); ++y) {
                        for (int x = bx; x < std::min(width, bx + blockSize); ++x) {
                            const int sx = std::max(0, std::min(width - 1, x + shiftX));
                            const std::size_t dstIdx =
                                (static_cast<std::size_t>(y) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)) *
                                4;
                            const std::size_t srcIdx =
                                (static_cast<std::size_t>(y) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(sx)) *
                                4;
                            for (int c = 0; c < 3; ++c) {
                                out[dstIdx + static_cast<std::size_t>(c)] =
                                    b[srcIdx + static_cast<std::size_t>(c)];
                            }
                        }
                    }
                }
            }
            return true;
        }

        case TransitionType::LightSweep: {
            // 光晕扫过：斜向高光带从左上扫到右下
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double diag =
                        (static_cast<double>(x) / std::max(1, width) +
                         static_cast<double>(y) / std::max(1, height)) /
                        2.0;
                    const double sweep = 1.0 - std::min(
                                                   1.0, std::fabs(diag - t) / 0.18);
                    if (sweep <= 0.0) {
                        continue;
                    }
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    for (int c = 0; c < 3; ++c) {
                        const double base = out[index + static_cast<std::size_t>(c)];
                        out[index + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                            clamp01((base + 255.0 * sweep * 0.8) / 255.0) * 255.0 + 0.5);
                    }
                }
            }
            return true;
        }

        case TransitionType::LensFlare: {
            // 镜头光斑：圆形光晕（半径随进度扩散 + 亮度衰减）
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            const double cx = static_cast<double>(width) * 0.5;
            const double cy = static_cast<double>(height) * 0.5;
            const double maxDist = std::sqrt(cx * cx + cy * cy);
            const double flareRadius = t * maxDist;
            const double flareWidth = maxDist * 0.25;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const double dx = static_cast<double>(x) - cx;
                    const double dy = static_cast<double>(y) - cy;
                    const double dist = std::sqrt(dx * dx + dy * dy);
                    const double ring = 1.0 - std::min(1.0, std::fabs(dist - flareRadius) / flareWidth);
                    if (ring <= 0.0) {
                        continue;
                    }
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    for (int c = 0; c < 3; ++c) {
                        const double base = out[index + static_cast<std::size_t>(c)];
                        out[index + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                            clamp01((base + 255.0 * ring * 0.7) / 255.0) * 255.0 + 0.5);
                    }
                }
            }
            return true;
        }

        case TransitionType::ExposureFlicker: {
            // 曝光闪烁：亮度脉冲（中段闪亮）叠加溶解
            const double flicker = 1.0 - std::fabs(2.0 * t - 1.0);
            const double boost = flicker * 0.6;
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    for (int c = 0; c < 3; ++c) {
                        const double va = a[index + static_cast<std::size_t>(c)];
                        const double vb = b[index + static_cast<std::size_t>(c)];
                        const double mixed = va * (1.0 - t) + vb * t;
                        out[index + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(
                            clamp01((mixed + (255.0 - mixed) * boost) / 255.0) * 255.0 + 0.5);
                    }
                    out[index + 3] = t < 0.5 ? a[index + 3] : b[index + 3];
                }
            }
            return true;
        }

        case TransitionType::CircleWipe:
            // 圆形擦除：中心圆扩散
            blendPixels(a, b, out, width, height, [t, width, height](int x, int y) {
                const double cx = static_cast<double>(width) / 2.0;
                const double cy = static_cast<double>(height) / 2.0;
                const double dx = static_cast<double>(x) - cx;
                const double dy = static_cast<double>(y) - cy;
                const double maxDist = std::sqrt(cx * cx + cy * cy);
                const double dist = std::sqrt(dx * dx + dy * dy) / maxDist;
                return dist < t ? 1.0 : 0.0;
            });
            return true;

        case TransitionType::Blinds:
            // 百叶窗：水平条带交错翻转
            blendPixels(a, b, out, width, height, [t, height](int, int y) {
                const int band = 8;
                const int bandIndex = y / band;
                const double phase = static_cast<double>(bandIndex % 2) * 0.15;
                return t + phase > 1.0 ? 1.0 : (t > phase + 0.5 ? 1.0 : (t > phase ? 1.0 : 0.0));
            });
            return true;

        case TransitionType::MosaicSplit: {
            // 马赛克分割：网格块按随机顺序渐显
            const int blockSize = 16;
            for (int by = 0; by < height; by += blockSize) {
                for (int bx = 0; bx < width; bx += blockSize) {
                    const double noise = std::fmod(
                        std::sin(bx * 12.9898 + by * 78.233) * 43758.5453, 1.0);
                    const std::size_t reveal = noise < t ? 1 : 0;
                    for (int y = by; y < std::min(height, by + blockSize); ++y) {
                        for (int x = bx; x < std::min(width, bx + blockSize); ++x) {
                            const std::size_t index =
                                (static_cast<std::size_t>(y) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x)) *
                                4;
                            const std::uint8_t* src = reveal ? (b + index) : (a + index);
                            for (int c = 0; c < 4; ++c) {
                                out[index + static_cast<std::size_t>(c)] =
                                    src[static_cast<std::size_t>(c)];
                            }
                        }
                    }
                }
            }
            return true;
        }

        case TransitionType::Pixelate: {
            // 像素化：块尺寸随进度（先变大再回到正常）
            const double pixelate = 1.0 - std::fabs(2.0 * t - 1.0);
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            if (pixelate > 0.02) {
                const int blockSize = std::max(2, static_cast<int>(pixelate * 24.0));
                std::vector<std::uint8_t> src(
                    out, out + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                                   4);
                for (int by = 0; by < height; by += blockSize) {
                    for (int bx = 0; bx < width; bx += blockSize) {
                        int sum[3] = {0, 0, 0};
                        int count = 0;
                        for (int y = by; y < std::min(height, by + blockSize); ++y) {
                            for (int x = bx; x < std::min(width, bx + blockSize); ++x) {
                                const std::uint8_t* px =
                                    src.data() + (static_cast<std::size_t>(y) *
                                                      static_cast<std::size_t>(width) +
                                                  static_cast<std::size_t>(x)) *
                                                     4;
                                sum[0] += px[0];
                                sum[1] += px[1];
                                sum[2] += px[2];
                                ++count;
                            }
                        }
                        if (count == 0) continue;
                        for (int y = by; y < std::min(height, by + blockSize); ++y) {
                            for (int x = bx; x < std::min(width, bx + blockSize); ++x) {
                                std::uint8_t* px =
                                    out + (static_cast<std::size_t>(y) *
                                               static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(x)) *
                                              4;
                                px[0] = static_cast<std::uint8_t>(sum[0] / count);
                                px[1] = static_cast<std::uint8_t>(sum[1] / count);
                                px[2] = static_cast<std::uint8_t>(sum[2] / count);
                            }
                        }
                    }
                }
            }
            return true;
        }

        case TransitionType::ParticleDissolve:
            // 粒子消散：散点噪声溶解（每像素随机阈值）
            blendPixels(a, b, out, width, height, [t](int x, int y) {
                const double noise =
                    std::fmod(std::sin(x * 12.9898 + y * 78.233) * 43758.5453, 1.0);
                return noise < t ? 1.0 : 0.0;
            });
            return true;

        case TransitionType::SmokeTransition:
            // 烟雾过渡：柔和噪波扫描（低频扰动边界）
            blendPixels(a, b, out, width, height, [t, width, height](int x, int y) {
                const double fx =
                    std::sin(static_cast<double>(x) / std::max(1, width) * 3.14159 * 3.0) * 0.1;
                const double fy =
                    std::sin(static_cast<double>(y) / std::max(1, height) * 3.14159 * 2.0) * 0.1;
                const double boundary = t + fx + fy;
                const double local =
                    static_cast<double>(x) / std::max(1, width) < boundary ? 1.0 : 0.0;
                return local;
            });
            return true;

        case TransitionType::SparkleGather:
            // 光点聚散：亮点网格按进度加亮聚合
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            for (int y = 0; y < height; y += 8) {
                for (int x = 0; x < width; x += 8) {
                    const double noise =
                        std::fmod(std::sin(x * 12.9898 + y * 78.233) * 43758.5453, 1.0);
                    const double sparkle = std::max(0.0, 1.0 - std::fabs(noise - t) * 6.0);
                    if (sparkle <= 0.0) {
                        continue;
                    }
                    for (int dy = 0; dy < 3 && y + dy < height; ++dy) {
                        for (int dx = 0; dx < 3 && x + dx < width; ++dx) {
                            const std::size_t index =
                                (static_cast<std::size_t>(y + dy) *
                                     static_cast<std::size_t>(width) +
                                 static_cast<std::size_t>(x + dx)) *
                                4;
                            for (int c = 0; c < 3; ++c) {
                                const double base = out[index + static_cast<std::size_t>(c)];
                                out[index + static_cast<std::size_t>(c)] =
                                    static_cast<std::uint8_t>(
                                        clamp01((base + 255.0 * sparkle) / 255.0) * 255.0 + 0.5);
                            }
                        }
                    }
                }
            }
            return true;

        case TransitionType::FilmFlicker: {
            // 胶片闪烁：亮度抖动 + 颗粒
            const double flicker =
                0.92 + 0.08 * std::sin(t * 3.14159 * 12.0);  // 高频抖动
            for (int y = 0; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    const std::size_t index =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                        4;
                    const double grain =
                        std::fmod(std::sin(x * 12.9898 + y * 78.233 + t * 51.0) * 43758.5453, 1.0) -
                        0.5;
                    for (int c = 0; c < 3; ++c) {
                        const double va = a[index + static_cast<std::size_t>(c)];
                        const double vb = b[index + static_cast<std::size_t>(c)];
                        const double mixed = (va * (1.0 - t) + vb * t) * flicker + grain * 16.0;
                        out[index + static_cast<std::size_t>(c)] =
                            static_cast<std::uint8_t>(clamp01(mixed / 255.0) * 255.0 + 0.5);
                    }
                    out[index + 3] = t < 0.5 ? a[index + 3] : b[index + 3];
                }
            }
            return true;
        }

        case TransitionType::OldFilmScratch: {
            // 老电影划痕转场：竖直划痕线随机移动 + 溶解
            blendPixels(a, b, out, width, height, [t](int, int) { return t; });
            const int scratchCount = 5;
            for (int i = 0; i < scratchCount; ++i) {
                const double noise =
                    std::fmod(std::sin(i * 12.9898 + t * 78.233) * 43758.5453, 1.0);
                const int scratchX = static_cast<int>(noise * width);
                const int scratchWidth = 1 + static_cast<int>(noise * 2.0);
                for (int y = 0; y < height; ++y) {
                    for (int dx = 0; dx < scratchWidth; ++dx) {
                        const int x = std::min(width - 1, scratchX + dx);
                        const std::size_t index =
                            (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                             static_cast<std::size_t>(x)) *
                            4;
                        for (int c = 0; c < 3; ++c) {
                            out[index + static_cast<std::size_t>(c)] = 235;
                        }
                    }
                }
            }
            return true;
        }
    }
    return false;
}

}  // namespace core
}  // namespace nairoclip
