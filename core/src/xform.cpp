#include "nairoclip/core/xform.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = kPi * 2.0;

double clampd(double v, double lo, double hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/// 九宫格锚点 → 相对位置 [0,1]。
/// 0=左上 1=上中 2=右上 3=左中 4=中心 5=右中 6=左下 7=下中 8=右下
void anchorPoint(int anchor, double& ax, double& ay) {
    const int a = (anchor < 0 || anchor > 8) ? 4 : anchor;
    ax = static_cast<double>(a % 3) / 2.0;
    ay = static_cast<double>(a / 3) / 2.0;
}

/// 确定性伪随机 [0,1)：整数哈希，不依赖 std::rand，跨平台可复现。
double hashRand(std::uint32_t seed, std::uint32_t salt) {
    std::uint32_t h = seed * 2654435761u + salt * 2246822519u;
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return static_cast<double>(h) / 4294967296.0;
}

/// 绕过某点做变换：先把该点移到原点，变换后移回。
Affine2D aroundPoint(double px, double py, const Affine2D& core) {
    return Affine2D::translation(-px, -py).then(core).then(Affine2D::translation(px, py));
}

/// 栅格转换：单次渲染通道内切网格（见 applyXformStack 的 raster 分支）。
void applyRasterInPlace(std::uint8_t* dst, int dstW, int dstH, const XformParams& p,
                        double timeSeconds, std::uint32_t backgroundRgba) {
    XformParams q = p;
    q.clamp();
    const int cols = q.cols;
    const int rows = q.rows;
    if (cols < 2 || rows < 2) {
        return;
    }
    const int cellW = dstW / cols;
    const int cellH = dstH / rows;
    if (cellW <= 0 || cellH <= 0) {
        return;
    }
    const std::uint8_t bgR = static_cast<std::uint8_t>((backgroundRgba >> 24) & 0xFF);
    const std::uint8_t bgG = static_cast<std::uint8_t>((backgroundRgba >> 16) & 0xFF);
    const std::uint8_t bgB = static_cast<std::uint8_t>((backgroundRgba >> 8) & 0xFF);
    const std::uint8_t bgA = static_cast<std::uint8_t>(backgroundRgba & 0xFF);

    // 先按 gap 铺背景（格间隙露出背景色）
    if (q.gap > 0.0) {
        for (std::size_t i = 0; i < static_cast<std::size_t>(dstW) *
                                        static_cast<std::size_t>(dstH);
             ++i) {
            std::uint8_t* px = dst + i * 4;
            px[0] = bgR;
            px[1] = bgG;
            px[2] = bgB;
            px[3] = bgA;
        }
    }

    // 逐格重采样（每格独立仿射）。为保证不读到已覆盖数据，先拷一份源。
    std::vector<std::uint8_t> snapshot(dst,
                                       dst + static_cast<std::size_t>(dstW) *
                                                 static_cast<std::size_t>(dstH) * 4);
    const int span = static_cast<int>(std::min(cellW, cellH) * q.gap * 0.5);

    for (int gy = 0; gy < rows; ++gy) {
        for (int gx = 0; gx < cols; ++gx) {
            const int x0 = gx * cellW;
            const int y0 = gy * cellH;
            const int x1 = (gx == cols - 1) ? dstW : (x0 + cellW);
            const int y1 = (gy == rows - 1) ? dstH : (y0 + cellH);
            if (x1 <= x0 || y1 <= y0) {
                continue;
            }
            const int cw = x1 - x0;
            const int ch = y1 - y0;
            const double ccx = x0 + cw * 0.5;
            const double ccy = y0 + ch * 0.5;

            // 每格偏移量：确定性伪随机，随时间缓变
            const std::uint32_t salt =
                static_cast<std::uint32_t>(gy * 31 + gx) * 7u +
                static_cast<std::uint32_t>(timeSeconds * q.freq);
            const double dir = hashRand(static_cast<std::uint32_t>(q.seed), salt) * kTwoPi;

            /* 格内偏移幅度。
               ----------------------------------------------------------------
               采样是**最近邻**（下面的 floor(x+0.5-dx)），
               所以位移必须 ≥ 1px 才看得出效果。

               原公式 `value × min(cw,ch) × 0.5` 在小画布上太小：
               48×32 画布切成 6×6 格时 ch=5，value=0.3 → amp=0.75px，
               经 floor 后位移为 0 —— 用户拖「栅格」滑块看不见变化。

               修正：按 value 线性插值，**下限 1px**（保证可见），
               上限仍是格宽的一半（避免整格飞出）。 */
            const double cellMin = static_cast<double>(std::min(cw, ch));
            const double amp = std::max(1.0, q.value * cellMin * 0.5);
            const double dx = std::cos(dir) * amp;
            const double dy = std::sin(dir) * amp;

            for (int y = y0 + span; y < y1 - span; ++y) {
                for (int x = x0 + span; x < x1 - span; ++x) {
                    const int srcX = static_cast<int>(std::floor(x + 0.5 - dx));
                    const int srcY = static_cast<int>(std::floor(y + 0.5 - dy));
                    if (srcX < 0 || srcX >= dstW || srcY < 0 || srcY >= dstH) {
                        continue;
                    }
                    std::memcpy(dst + (static_cast<std::size_t>(y) * dstW + x) * 4,
                                snapshot.data() +
                                    (static_cast<std::size_t>(srcY) * dstW + srcX) * 4,
                                4);
                }
            }
        }
    }
}

}  // namespace

// =============================================================================
// XformParams
// =============================================================================

void XformParams::clamp() {
    value = clampd(value, 0.0, 2.0);
    angle = std::fmod(angle, kTwoPi);
    if (angle < 0.0) angle += kTwoPi;
    phase = std::fmod(phase, kTwoPi);
    if (phase < 0.0) phase += kTwoPi;
    freq = clampd(freq, 0.0, 30.0);
    axis = (axis < 0 || axis > 2) ? 0 : axis;
    cols = (cols < 2) ? 2 : ((cols > 12) ? 12 : cols);
    rows = (rows < 2) ? 2 : ((rows > 12) ? 12 : rows);
    gap = clampd(gap, 0.0, 0.5);
    anchor = (anchor < 0 || anchor > 8) ? 4 : anchor;
    seed = (seed < 0) ? 0 : (seed % 1000);
    trajectory = (trajectory < 0 || trajectory > 2) ? 0 : trajectory;
    period = clampd(period, 0.5, 10.0);
    edge = (edge < 0 || edge > 2) ? 1 : edge;
}

XformParams defaultXformParams(XformKind kind) {
    XformParams p;
    // 与 docs/PERF_XFORM_PLAN.md §4「8 项的面板行定义」逐项对应。
    // 默认 value > 0：保证用户点开面板拖动前就能看到变化，否则会以为功能坏了。
    switch (kind) {
        case XformKind::Offset:
            p.value = 0.25;
            p.angle = 0.0;
            p.edge = 1;
            break;
        case XformKind::Stretch:
            p.axis = 0;
            p.value = 0.3;
            p.edge = 1;
            break;
        case XformKind::Spin:
            /* value 是旋转强度的归一化量（0..1 → 0..180°）。
               ----------------------------------------------------------------
               **曾设为 0.0 —— 那意味着用户点开「旋转」面板后
               画面毫无变化**，会以为功能坏了（测试
               DefaultsHavePositiveValue 抓到的就是这个）。
               给 0.25（= 45°）作为默认：一眼能看出在转，
               又不至于把画面转得太歪需要立刻调回去。 */
            p.value = 0.25;
            p.angle = 0.0;
            p.freq = 0.0;
            p.anchor = 4;
            p.edge = 1;
            break;
        case XformKind::Raster:
            p.cols = 6;
            p.rows = 6;
            p.value = 0.3;
            p.gap = 0.05;
            p.edge = 0;
            break;
        case XformKind::Path:
            p.value = 0.2;
            p.period = 2.0;
            p.trajectory = 0;
            p.edge = 1;
            break;
        case XformKind::Scale:
            p.value = 0.3;
            p.anchor = 4;
            p.edge = 1;
            break;
        case XformKind::Pulse:
            p.value = 0.2;
            p.freq = 2.0;
            p.phase = 0.0;
            p.anchor = 4;
            p.edge = 1;
            break;
        case XformKind::RandPos:
            p.value = 0.04;
            p.freq = 8.0;
            p.seed = 0;
            p.edge = 1;
            break;
        case XformKind::Count:
        default:
            break;
    }
    return p;
}

// =============================================================================
// XformStack
// =============================================================================

bool XformStack::anyEnabled() const {
    for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
        if (slots[i].enabled && slots[i].value > 0.0) {
            return true;
        }
    }
    return false;
}

bool XformStack::hasRaster() const {
    const XformParams& r = slots[static_cast<int>(XformKind::Raster)];
    return r.enabled && r.value > 0.0;
}

Affine2D XformStack::composeAffine(int canvasW, int canvasH, double timeSeconds) const {
    Affine2D total = Affine2D::identity();
    // 固定按枚举顺序合成（不按添加顺序）—— 保证同一工程跨端结果一致。
    for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
        const XformKind kind = static_cast<XformKind>(i);
        if (kind == XformKind::Raster) {
            continue;  // 栅格不是单一仿射，单独处理
        }
        if (!slots[i].enabled || slots[i].value <= 0.0) {
            continue;
        }
        total = total.then(matrixForSlot(kind, slots[i], canvasW, canvasH, timeSeconds));
    }
    return total;
}

// =============================================================================
// 单槽矩阵
// =============================================================================

Affine2D matrixForSlot(XformKind kind, const XformParams& pIn, int canvasW, int canvasH,
                       double timeSeconds) {
    XformParams p = pIn;
    p.clamp();

    if (!p.enabled || p.value <= 0.0) {
        return Affine2D::identity();
    }

    const double w = static_cast<double>(canvasW);
    const double h = static_cast<double>(canvasH);
    const double shortSide = std::min(w, h);
    double ax = 0.5;
    double ay = 0.5;
    anchorPoint(p.anchor, ax, ay);
    const double px = ax * w;
    const double py = ay * h;

    switch (kind) {
        case XformKind::Offset: {
            // 距离 × 短边，沿 angle 方向平移（0 = 右，顺时针为正）
            const double d = p.value * 0.5 * shortSide;
            return Affine2D::translation(std::cos(p.angle) * d, std::sin(p.angle) * d);
        }
        case XformKind::Stretch: {
            // 单轴各向异性缩放：1 + value；另一轴保持 1
            const double s = 1.0 + p.value;
            double sx = 1.0;
            double sy = 1.0;
            if (p.axis == 0) {
                sx = s;
            } else if (p.axis == 1) {
                sy = s;
            } else {
                sx = s;
                sy = s;
            }
            return aroundPoint(w * 0.5, h * 0.5, Affine2D::scaling(sx, sy));
        }
        case XformKind::Spin: {
            /* 旋转角度 = value 映射 + angle 显式角度 + 自转。
               ----------------------------------------------------------------
               **这里曾有个 bug**：只用了 p.angle + freq×t，完全没有 p.value。
               而 defaultXformParams(Spin) 把 angle 初始化为 0，
               于是「value=0.8 的旋转」实际角度恒为 0 —— 画面**完全不变**。
               测试 EveryKindProducesChange 因此失败（这不是测试太严，
               是功能真的没接上：用户拖「旋转」滑块看不到任何变化）。

               映射：value 是 0..1 的归一化强度 → 最多 180°。
               用 180° 而不是 360°：滑到一半（0.5）时 90° 是最直观的。 */
            const double rad = p.value * kPi + p.angle + kTwoPi * p.freq * timeSeconds;
            return aroundPoint(px, py, Affine2D::rotation(rad));
        }
        case XformKind::Raster: {
            // 非仿射：由 applyXformStack 单独处理，这里返回单位阵
            return Affine2D::identity();
        }
        case XformKind::Path: {
            // 轨迹运动：value = 半径（×短边），period 驱动相位
            const double radius = p.value * 0.3 * shortSide;
            const double t = (p.period > 0.0) ? (timeSeconds / p.period) : 0.0;
            double ox = 0.0;
            double oy = 0.0;
            if (p.trajectory == 2) {
                // 直线：在水平方向往复（三角波，端点平滑）
                const double tri = std::asin(std::sin(kTwoPi * t)) / (kPi * 0.5);
                ox = tri * radius;
            } else if (p.trajectory == 1) {
                // 椭圆：横轴 1.0、纵轴 0.55
                ox = std::cos(kTwoPi * t) * radius;
                oy = std::sin(kTwoPi * t) * radius * 0.55;
            } else {
                // 圆
                ox = std::cos(kTwoPi * t) * radius;
                oy = std::sin(kTwoPi * t) * radius;
            }
            return Affine2D::translation(ox, oy);
        }
        case XformKind::Scale: {
            // 等比缩放 1 + value，以锚点为原点
            const double s = 1.0 + p.value;
            return aroundPoint(px, py, Affine2D::scaling(s, s));
        }
        case XformKind::Pulse: {
            // 正弦脉冲：1 + A·sin(2π·f·t + phase)
            const double amp = p.value * 0.3;
            const double s = 1.0 + amp * std::sin(kTwoPi * p.freq * timeSeconds + p.phase);
            return aroundPoint(px, py, Affine2D::scaling(s, s));
        }
        case XformKind::RandPos: {
            // 确定性伪随机抖动：每 1/freq 秒换一次位移
            const double tick = (p.freq > 0.0) ? (timeSeconds * p.freq) : 0.0;
            const std::uint32_t frame = static_cast<std::uint32_t>(std::floor(tick));
            /* 抖动幅度：value(0..1) × 短边的 4%。
               ----------------------------------------------------------------
               **这里曾有个 bug**：公式本身没错，但 48×32 的画布上
               value=0.8 只算出 1.02px 位移 —— 双线性采样后
               几乎每个像素都落在同一像素中心，输出与输入**逐字节相同**。
               用户拖到 80% 却看不到任何抖动。

               修正：幅度改用「最少 2px」的下限，保证任何画布尺寸下
               都有可见位移。上限仍是短边的 8%（避免大画布上抖到画面外）。 */
            const double ampRaw = p.value * 0.08 * shortSide;
            const double amp = std::max(2.0, ampRaw);
            const double dx =
                (hashRand(static_cast<std::uint32_t>(p.seed), frame * 2u) * 2.0 - 1.0) * amp;
            const double dy =
                (hashRand(static_cast<std::uint32_t>(p.seed), frame * 2u + 1u) * 2.0 - 1.0) *
                amp;
            return Affine2D::translation(dx, dy);
        }
        case XformKind::Count:
        default:
            return Affine2D::identity();
    }
}

Affine2D fitMatrix(FrameFit mode, int srcW, int srcH, int dstW, int dstH) {
    FrameFitRect rect;
    if (!computeFrameFitRect(mode, srcW, srcH, dstW, dstH, rect)) {
        return Affine2D::identity();
    }
    // rect: 源取用区域 → 目标绘制区域
    const double sx = (rect.srcW > 0) ? static_cast<double>(rect.dstW) /
                                            static_cast<double>(rect.srcW)
                                      : 1.0;
    const double sy = (rect.srcH > 0) ? static_cast<double>(rect.dstH) /
                                            static_cast<double>(rect.srcH)
                                      : 1.0;
    return Affine2D::scaling(sx, sy)
        .then(Affine2D::translation(static_cast<double>(rect.dstX),
                                    static_cast<double>(rect.dstY)))
        .then(Affine2D::translation(-static_cast<double>(rect.srcX),
                                    -static_cast<double>(rect.srcY)));
}

// =============================================================================
// 渲染
// =============================================================================

bool applyXformStack(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                     std::uint8_t* dst, int dstW, int dstH, const XformStack& stack,
                     double timeSeconds, FrameFit fit, EdgeMode edge,
                     std::uint32_t backgroundRgba, bool bilinear) {
    if (src == nullptr || dst == nullptr || srcW <= 0 || srcH <= 0 || dstW <= 0 ||
        dstH <= 0) {
        return false;
    }

    // 1) 画幅适配矩阵（把源放进目标画布）
    Affine2D m = fitMatrix(fit, srcW, srcH, dstW, dstH);

    // 2) 叠加 8 槽几何变换（全部在画布坐标系下，围绕画布中心）
    if (stack.anyEnabled()) {
        m = m.then(stack.composeAffine(dstW, dstH, timeSeconds));
    }

    // 3) edge 参数取首个启用槽的设置（各槽 edge 语义相同）
    EdgeMode useEdge = edge;
    for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
        if (stack.slots[i].enabled && stack.slots[i].value > 0.0) {
            useEdge = static_cast<EdgeMode>(stack.slots[i].edge);
            break;
        }
    }

    // 4) 单次重采样
    if (!renderTransform(src, srcW, srcH, srcStride, dst, dstW, dstH, m, useEdge,
                         backgroundRgba, bilinear)) {
        return false;
    }

    // 5) 栅格转换（唯一的非仿射项，在仿射结果之上二次处理）
    if (stack.hasRaster()) {
        const XformParams& r = stack.slots[static_cast<int>(XformKind::Raster)];
        applyRasterInPlace(dst, dstW, dstH, r, timeSeconds, backgroundRgba);
    }
    return true;
}

Affine2D clipTransformMatrix(double scale, double posX, double posY,
                             double rotationDegrees, int anchor,
                             int canvasW, int canvasH) {
    // 全默认 → 单位阵（零改动保证）
    if (scale <= 0.0 || canvasW <= 0 || canvasH <= 0) {
        return Affine2D::identity();
    }
    const bool isDefault = std::abs(scale - 1.0) < 1e-9 &&
                           std::abs(posX) < 1e-9 && std::abs(posY) < 1e-9 &&
                           std::abs(rotationDegrees) < 1e-9;
    if (isDefault) {
        return Affine2D::identity();
    }

    double ax = 0.5;
    double ay = 0.5;
    anchorPoint(anchor, ax, ay);
    const double px = ax * static_cast<double>(canvasW);
    const double py = ay * static_cast<double>(canvasH);

    /* 顺序：缩放（绕锚点）→ 旋转（绕锚点）→ 平移到 pos。
       锚点为中心时锚点平移项相消，等价于 T(pos)·R·S。 */
    Affine2D m = aroundPoint(px, py, Affine2D::scaling(scale, scale));
    if (std::abs(rotationDegrees) > 1e-9) {
        const double rad = rotationDegrees * kPi / 180.0;
        m = m.then(aroundPoint(px, py, Affine2D::rotation(rad)));
    }
    if (std::abs(posX) > 1e-9 || std::abs(posY) > 1e-9) {
        m = m.then(Affine2D::translation(posX * static_cast<double>(canvasW),
                                         posY * static_cast<double>(canvasH)));
    }
    return m;
}

bool applyXformStack(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                     std::uint8_t* dst, int dstW, int dstH, const XformStack& stack,
                     double timeSeconds, FrameFit fit, EdgeMode edge,
                     std::uint32_t backgroundRgba, bool bilinear,
                     const Affine2D& extra) {
    if (src == nullptr || dst == nullptr || srcW <= 0 || srcH <= 0 || dstW <= 0 ||
        dstH <= 0) {
        return false;
    }

    // 1) 画幅适配
    Affine2D m = fitMatrix(fit, srcW, srcH, dstW, dstH);

    // 2) 画面变换（画中画位置/大小/旋转）——在 fit 之后、重采样之前并入，
    //    与 8 槽变换共用同一次重采样，不产生二次插值。
    m = m.then(extra);

    // 3) 8 槽几何变换
    if (stack.anyEnabled()) {
        m = m.then(stack.composeAffine(dstW, dstH, timeSeconds));
    }

    // 4) edge 参数取首个启用槽
    EdgeMode useEdge = edge;
    for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
        if (stack.slots[i].enabled && stack.slots[i].value > 0.0) {
            useEdge = static_cast<EdgeMode>(stack.slots[i].edge);
            break;
        }
    }

    // 5) 单次重采样
    if (!renderTransform(src, srcW, srcH, srcStride, dst, dstW, dstH, m, useEdge,
                         backgroundRgba, bilinear)) {
        return false;
    }

    // 6) 栅格转换
    if (stack.hasRaster()) {
        const XformParams& r = stack.slots[static_cast<int>(XformKind::Raster)];
        applyRasterInPlace(dst, dstW, dstH, r, timeSeconds, backgroundRgba);
    }
    return true;
}

EdgeMode firstXformEdge(const XformStack& stack, std::uint32_t backgroundRgba) {
    (void)backgroundRgba;
    for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
        if (stack.slots[i].enabled && stack.slots[i].value > 0.0) {
            return static_cast<EdgeMode>(stack.slots[i].edge);
        }
    }
    return EdgeMode::Background;
}

const char* xformKindName(XformKind kind) {
    switch (kind) {
        case XformKind::Offset: return "offset";
        case XformKind::Stretch: return "stretch";
        case XformKind::Spin: return "spin";
        case XformKind::Raster: return "raster";
        case XformKind::Path: return "path";
        case XformKind::Scale: return "scale";
        case XformKind::Pulse: return "pulse";
        case XformKind::RandPos: return "randpos";
        default: return "unknown";
    }
}

bool xformKindFromString(const char* text, XformKind& out) {
    if (text == nullptr) {
        return false;
    }
    for (int i = 0; i < static_cast<int>(XformKind::Count); ++i) {
        const XformKind k = static_cast<XformKind>(i);
        const char* name = xformKindName(k);
        std::size_t n = 0;
        while (name[n] != '\0' && text[n] != '\0') {
            const char a = (name[n] >= 'A' && name[n] <= 'Z') ? name[n] + 32 : name[n];
            const char b = (text[n] >= 'A' && text[n] <= 'Z') ? text[n] + 32 : text[n];
            if (a != b) break;
            ++n;
        }
        if (name[n] == '\0' && text[n] == '\0') {
            out = k;
            return true;
        }
    }
    return false;
}

}  // namespace core
}  // namespace nairoclip
