#include "nairoclip/core/mask.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.141592653589793;

bool equalsIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/// 羽化过渡带半宽（像素；相对画面短边）。
double featherHalfPixels(double feather, int width, int height) {
    const double shortSide = static_cast<double>(std::min(width, height));
    return feather * shortSide;
}

/// 平滑过渡：distance 为「距边界的有符号像素距离」（>0 = 内侧）。
/// 返回 0..1（边界处 0.5）。
double smoothStep(double distance, double halfWidth) {
    if (halfWidth <= 0.0) {
        return distance >= 0.0 ? 1.0 : 0.0;
    }
    if (distance <= -halfWidth) {
        return 0.0;
    }
    if (distance >= halfWidth) {
        return 1.0;
    }
    const double t = (distance + halfWidth) / (2.0 * halfWidth);
    return t * t * (3.0 - 2.0 * t);
}

/// 圆角矩形 SDF（内为正）：局部坐标 (lx,ly)，半尺寸 (halfW,halfH)，圆角 r。
double roundedRectSdf(double lx, double ly, double halfW, double halfH, double radius) {
    const double r = std::min(radius, std::min(halfW, halfH));
    const double qx = std::fabs(lx) - (halfW - r);
    const double qy = std::fabs(ly) - (halfH - r);
    const double outside = std::sqrt(std::max(qx, 0.0) * std::max(qx, 0.0) +
                                     std::max(qy, 0.0) * std::max(qy, 0.0));
    return -(outside + std::min(std::max(qx, qy), 0.0) - r);
}

}  // namespace

double maskCoverageAt(const Mask& mask, double x, double y, int width, int height) {
    if (width <= 0 || height <= 0) {
        return -1.0;
    }
    if (!mask.valid()) {
        return -1.0;
    }
    if (mask.shape == MaskShape::None) {
        return 1.0;
    }
    // 形状参数合法性（0 值视为无效形状）
    if (mask.width <= 0.0 && mask.shape == MaskShape::Rectangle) {
        return -1.0;
    }
    if (mask.width <= 0.0 && mask.shape == MaskShape::Circle) {
        return -1.0;
    }

    const double cx = mask.centerX * static_cast<double>(width);
    const double cy = mask.centerY * static_cast<double>(height);
    const double angle = mask.rotationDegrees * kPi / 180.0;
    const double cosA = std::cos(angle);
    const double sinA = std::sin(angle);
    const double dx = x - cx;
    const double dy = y - cy;
    // 旋转到局部坐标（法线方向为 +x）
    const double lx = dx * cosA + dy * sinA;
    const double ly = -dx * sinA + dy * cosA;
    const double halfFeather = featherHalfPixels(mask.feather, width, height);

    double weight = 1.0;
    switch (mask.shape) {
        case MaskShape::Linear: {
            // 水平分界线（默认上半可见、下半遮蔽）：
            //  - 位于 centerY + height×画面高（height = 0 时过中心）；
            //  - rotationDegrees 可旋转该分界线（带动可见侧一起旋转）。
            const double offset = mask.height * static_cast<double>(height);
            // 距分界线的有符号距离（>0 = 上方/可见侧）
            const double distance = -(ly - offset);
            weight = smoothStep(distance, halfFeather);
            break;
        }
        case MaskShape::Mirror: {
            // 中心带状窗口：|ly| <= 半带高（height = 半带高，相对画面高度）
            const double halfBand = mask.height * static_cast<double>(height);
            const double distance = halfBand - std::fabs(ly);
            weight = smoothStep(distance, halfFeather);
            break;
        }
        case MaskShape::Circle: {
            // 椭圆：width = 半轴（相对宽度），height = 半轴（相对高度）
            const double radiusX = std::max(1e-6, mask.width * static_cast<double>(width));
            const double radiusY = std::max(1e-6, mask.height * static_cast<double>(height));
            const double normalized =
                std::sqrt((lx / radiusX) * (lx / radiusX) + (ly / radiusY) * (ly / radiusY));
            // 近似像素距离（取两轴平均半径）
            const double averageRadius = (radiusX + radiusY) * 0.5;
            const double distance = (1.0 - normalized) * averageRadius;
            weight = smoothStep(distance, halfFeather);
            break;
        }
        case MaskShape::Rectangle: {
            const double halfW = std::max(1e-6, mask.width * static_cast<double>(width));
            const double halfH = std::max(1e-6, mask.height * static_cast<double>(height));
            const double radius =
                mask.cornerRadius * static_cast<double>(std::min(width, height));
            const double distance = roundedRectSdf(lx, ly, halfW, halfH, radius);
            weight = smoothStep(distance, halfFeather);
            break;
        }
        case MaskShape::Star: {
            // 星形（文档 5.8：「星形用极坐标角度分段函数」）：
            // 5 角星；对每个像素取极坐标 (r, θ)，星形边界半径
            //   R(θ) = inner + (outer - inner) × tri(5θ)（三角波，角尖/谷交替）
            // 其中 outer = width × 画面宽（外接半径），inner = outer × 0.45。
            const double outer =
                std::max(1e-6, mask.width * static_cast<double>(width));
            const double inner = outer * 0.45;  // 5 角星典型内/外半径比
            const double r = std::sqrt(lx * lx + ly * ly);
            double theta = std::atan2(ly, lx);
            if (theta < 0.0) theta += 2.0 * kPi;
            // 5 次对称三角波：0（角尖）→1（谷）→0...
            const double phase = std::fmod(theta * 5.0 / (2.0 * kPi), 1.0);
            const double tri = phase < 0.5 ? (phase * 2.0) : (2.0 - phase * 2.0);
            const double boundary = outer - (outer - inner) * tri;
            const double distance = boundary - r;
            weight = smoothStep(distance, halfFeather);
            break;
        }
        case MaskShape::Heart: {
            // 爱心（文档 5.8：「爱心用现成心形线数学参数方程」）：
            // 经典心形隐式方程 (x² + y² − 1)³ − x²y³ = 0 的归一化形式；
            // 取 width 为半宽（相对画面宽），按短边等比缩放。
            const double scale =
                std::max(1e-6, mask.width * static_cast<double>(width));
            // 归一化到心形坐标系（y 轴向上；画面 y 向下 → 取负）
            const double nx = lx / scale;
            const double ny = -ly / scale * 1.15;  // 心形高宽比约 0.85
            // 隐式方程值 f = (x²+y²−1)³ − x²y³；f < 0 在内部
            const double a = nx * nx + ny * ny - 1.0;
            const double f = a * a * a - nx * nx * ny * ny * ny;
            // 近似像素距离：内部为负 → 距离 = -f 的缩放（f 的梯度近似 1/scale）
            const double distance = -f * scale * 0.9;
            weight = smoothStep(distance, halfFeather);
            break;
        }
        case MaskShape::None:
        default:
            return 1.0;
    }
    if (mask.invert) {
        weight = 1.0 - weight;
    }
    return std::max(0.0, std::min(1.0, weight));
}

double maskCoverageRatio(const Mask& mask, int width, int height, int step) {
    if (width <= 0 || height <= 0 || step < 1) {
        return -1.0;
    }
    if (!mask.valid()) {
        return -1.0;
    }
    if (mask.shape == MaskShape::None) {
        return 1.0;
    }
    double sum = 0.0;
    double count = 0.0;
    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            const double weight =
                maskCoverageAt(mask, static_cast<double>(x) + 0.5,
                               static_cast<double>(y) + 0.5, width, height);
            if (weight < 0.0) {
                return -1.0;
            }
            sum += weight;
            count += 1.0;
        }
    }
    return count > 0.0 ? sum / count : -1.0;
}

bool applyMaskToFrame(std::uint8_t* pixels, int width, int height, const Mask& mask,
                      MaskBlendMode mode, std::uint32_t backgroundRgba) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (!mask.valid()) {
        return false;
    }
    if (mask.shape == MaskShape::None) {
        return true;  // 无蒙版：零改动
    }
    const std::uint8_t bgR = static_cast<std::uint8_t>((backgroundRgba >> 24) & 0xFF);
    const std::uint8_t bgG = static_cast<std::uint8_t>((backgroundRgba >> 16) & 0xFF);
    const std::uint8_t bgB = static_cast<std::uint8_t>((backgroundRgba >> 8) & 0xFF);
    const std::uint8_t bgA = static_cast<std::uint8_t>(backgroundRgba & 0xFF);

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double weight =
                maskCoverageAt(mask, static_cast<double>(x) + 0.5,
                               static_cast<double>(y) + 0.5, width, height);
            if (weight < 0.0) {
                return false;  // 形状参数非法（理论上前置校验已拦截）
            }
            if (weight >= 1.0) {
                continue;  // 快路径：完全可见
            }
            const std::size_t index =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                 static_cast<std::size_t>(x)) * 4;
            std::uint8_t* px = pixels + index;
            if (mode == MaskBlendMode::Transparent) {
                // 蒙版外 alpha 缩放（保持 RGB，便于与下层混合）
                const double alpha = static_cast<double>(px[3]) * weight;
                px[3] = static_cast<std::uint8_t>(std::max(0.0, std::min(255.0, alpha + 0.5)));
            } else {
                // 背景模式：按 (1 - weight) 混入背景色；alpha 不透明
                const double mix = 1.0 - weight;
                px[0] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, px[0] * weight + bgR * mix + 0.5)));
                px[1] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, px[1] * weight + bgG * mix + 0.5)));
                px[2] = static_cast<std::uint8_t>(
                    std::max(0.0, std::min(255.0, px[2] * weight + bgB * mix + 0.5)));
                px[3] = bgA;
            }
        }
    }
    return true;
}

const char* maskShapeName(MaskShape shape) {
    switch (shape) {
        case MaskShape::None:
            return "none";
        case MaskShape::Linear:
            return "linear";
        case MaskShape::Mirror:
            return "mirror";
        case MaskShape::Circle:
            return "circle";
        case MaskShape::Rectangle:
            return "rectangle";
        case MaskShape::Star:
            return "star";
        case MaskShape::Heart:
            return "heart";
        default:
            return "none";
    }
}

bool maskShapeFromString(const char* text, MaskShape& out) {
    if (text == nullptr) {
        return false;
    }
    if (equalsIgnoreCase(text, "none")) {
        out = MaskShape::None;
        return true;
    }
    if (equalsIgnoreCase(text, "linear")) {
        out = MaskShape::Linear;
        return true;
    }
    if (equalsIgnoreCase(text, "mirror")) {
        out = MaskShape::Mirror;
        return true;
    }
    if (equalsIgnoreCase(text, "circle")) {
        out = MaskShape::Circle;
        return true;
    }
    if (equalsIgnoreCase(text, "rectangle") || equalsIgnoreCase(text, "rect")) {
        out = MaskShape::Rectangle;
        return true;
    }
    if (equalsIgnoreCase(text, "star") || equalsIgnoreCase(text, "星形")) {
        out = MaskShape::Star;
        return true;
    }
    if (equalsIgnoreCase(text, "heart") || equalsIgnoreCase(text, "爱心")) {
        out = MaskShape::Heart;
        return true;
    }
    return false;
}

bool maskShapeFromInt(int value, MaskShape& out) {
    switch (value) {
        case 0:
            out = MaskShape::None;
            return true;
        case 1:
            out = MaskShape::Linear;
            return true;
        case 2:
            out = MaskShape::Mirror;
            return true;
        case 3:
            out = MaskShape::Circle;
            return true;
        case 4:
            out = MaskShape::Rectangle;
            return true;
        case 5:
            out = MaskShape::Star;
            return true;
        case 6:
            out = MaskShape::Heart;
            return true;
        default:
            return false;
    }
}

}  // namespace core
}  // namespace nairoclip
