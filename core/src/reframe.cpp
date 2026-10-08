#include "nairoclip/core/reframe.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "nairoclip/core/frame_fit.h"

namespace nairoclip {
namespace core {

namespace {

/// 亮度（BT.601）。
double luminance(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
}

/// 读取像素（stride 感知）。
void readPixel(const std::uint8_t* src, int stride, int x, int y, int& r, int& g, int& b) {
    const std::uint8_t* px =
        src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
        static_cast<std::size_t>(x) * 4;
    r = px[0];
    g = px[1];
    b = px[2];
}

}  // namespace

bool computeFrameSaliency(const std::uint8_t* src, int width, int height, int stride,
                          const std::uint8_t* prevSrc, int prevStride,
                          const ReframeConfig& config, const Time& timestamp,
                          FrameSaliency& out) {
    if (src == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    const int rowStride = stride > 0 ? stride : width * 4;
    if (rowStride < width * 4) {
        return false;
    }
    // 采样步长（性能：大图按 4 像素步进；显著性对分辨率不敏感）
    const int step = width > 640 ? 4 : 2;
    const int prevRowStride = prevStride > 0 ? prevStride : width * 4;

    // ---- 1) 全图均值（用于色彩对比项）----
    double sumR = 0.0, sumG = 0.0, sumB = 0.0;
    int count = 0;
    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            int r, g, b;
            readPixel(src, rowStride, x, y, r, g, b);
            sumR += r;
            sumG += g;
            sumB += b;
            ++count;
        }
    }
    if (count == 0) {
        return false;
    }
    const double meanR = sumR / count / 255.0;
    const double meanG = sumG / count / 255.0;
    const double meanB = sumB / count / 255.0;

    // ---- 2) 逐采样点显著性 ----
    double weightSum = 0.0;
    double weightedX = 0.0;
    double weightedY = 0.0;
    double totalSaliency = 0.0;
    for (int y = 0; y < height; y += step) {
        for (int x = 0; x < width; x += step) {
            int r, g, b;
            readPixel(src, rowStride, x, y, r, g, b);
            const double lum = luminance(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
                                         static_cast<std::uint8_t>(b));
            // 2a) 亮度梯度（与右/下邻居差分）
            double gradient = 0.0;
            if (x + step < width) {
                int r2, g2, b2;
                readPixel(src, rowStride, x + step, y, r2, g2, b2);
                gradient += std::fabs(
                    lum - luminance(static_cast<std::uint8_t>(r2), static_cast<std::uint8_t>(g2),
                                    static_cast<std::uint8_t>(b2)));
            }
            if (y + step < height) {
                int r2, g2, b2;
                readPixel(src, rowStride, x, y + step, r2, g2, b2);
                gradient += std::fabs(
                    lum - luminance(static_cast<std::uint8_t>(r2), static_cast<std::uint8_t>(g2),
                                    static_cast<std::uint8_t>(b2)));
            }
            // 2b) 色彩对比（与全图均值的偏离）
            const double colorContrast =
                (std::fabs(r / 255.0 - meanR) + std::fabs(g / 255.0 - meanG) +
                 std::fabs(b / 255.0 - meanB)) / 3.0;
            // 2c) 运动能量（与前一帧差分；可选）
            double motion = 0.0;
            if (prevSrc != nullptr) {
                const std::uint8_t* prevPx =
                    prevSrc + static_cast<std::size_t>(y) * static_cast<std::size_t>(prevRowStride) +
                    static_cast<std::size_t>(x) * 4;
                const double prevLum = luminance(prevPx[0], prevPx[1], prevPx[2]);
                motion = std::fabs(lum - prevLum);
            }
            // 2d) 构图先验（中心 + 上部）
            const double nx = (x + 0.5) / width;   // 0..1
            const double ny = (y + 0.5) / height;  // 0..1
            const double dx = nx - 0.5;
            const double dy = ny - 0.5;
            const double centerPrior = std::exp(-(dx * dx + dy * dy) / (2.0 * 0.23 * 0.23));
            const double upperPrior = std::exp(-(ny - 0.32) * (ny - 0.32) / (2.0 * 0.28 * 0.28));
            const double prior = 1.0 + config.centerPriorWeight * centerPrior +
                                 config.upperPriorWeight * upperPrior;
            const double saliency = (gradient * 2.0 + colorContrast * 0.8 + motion * 2.5) * prior;
            weightSum += saliency;
            weightedX += saliency * nx;
            weightedY += saliency * ny;
            totalSaliency += saliency;
        }
    }
    if (totalSaliency <= 0.0) {
        out.timestamp = timestamp;
        out.centerX = 0.5;
        out.centerY = 0.5;
        out.energy = 0.0;
        out.hasSubject = false;
        return true;
    }
    const double sampleCount = static_cast<double>(count);
    out.timestamp = timestamp;
    out.centerX = weightSum > 0.0 ? weightedX / weightSum : 0.5;
    out.centerY = weightSum > 0.0 ? weightedY / weightSum : 0.5;
    // 能量归一化：平均显著性 × 放大系数（经验映射，供阈值判定）
    out.energy = std::min(1.0, totalSaliency / sampleCount * 12.0);
    out.hasSubject = out.energy >= config.minEnergy;
    // 钳制到 [0,1]
    out.centerX = std::min(1.0, std::max(0.0, out.centerX));
    out.centerY = std::min(1.0, std::max(0.0, out.centerY));
    return true;
}

ReframeTrack planReframe(const std::vector<FrameSaliency>& saliency, int srcWidth, int srcHeight,
                         int dstWidth, int dstHeight, const ReframeConfig& config) {
    ReframeTrack track;
    track.srcWidth = srcWidth;
    track.srcHeight = srcHeight;
    track.trackingSensitivity = config.trackingSensitivity;
    if (saliency.empty() || srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
        track.note = "无有效输入（显著性序列为空或尺寸非法）";
        return track;
    }

    // ---- 1) 原始主体位置（低能量帧沿用前一位置；首帧无主体则居中）----
    std::vector<double> rawX(static_cast<std::size_t>(saliency.size()), 0.5);
    std::vector<double> rawY(static_cast<std::size_t>(saliency.size()), 0.5);
    double lastX = 0.5;
    double lastY = 0.5;
    bool sawSubject = false;
    for (std::size_t i = 0; i < saliency.size(); ++i) {
        if (saliency[i].hasSubject) {
            lastX = saliency[i].centerX;
            lastY = saliency[i].centerY;
            sawSubject = true;
        }
        rawX[i] = lastX;
        rawY[i] = lastY;
    }
    if (!sawSubject) {
        track.note = "全程未检测到明确主体，退化为居中裁剪（可提高跟踪灵敏度或调整构图先验）";
    }

    // ---- 2) 裁剪窗口尺寸（源内按目标比例的最大矩形；决定中心的活动范围）----
    // 窗口必须完整落在源内：中心范围受窗口半宽/半高约束。
    const double srcAspect = static_cast<double>(srcWidth) / srcHeight;
    const double dstAspect = static_cast<double>(dstWidth) / dstHeight;
    double windowW = 0.0;
    double windowH = 0.0;
    if (dstAspect >= srcAspect) {
        // 目标更宽：以宽度为准（窗口宽 = 源宽）
        windowW = static_cast<double>(srcWidth);
        windowH = windowW / dstAspect;
    } else {
        // 目标更高：以高度为准
        windowH = static_cast<double>(srcHeight);
        windowW = windowH * dstAspect;
    }
    const double halfW = windowW / 2.0 / srcWidth;   // 归一化半宽
    const double halfH = windowH / 2.0 / srcHeight;  // 归一化半高
    const double minCX = halfW;
    const double maxCX = 1.0 - halfW;
    const double minCY = halfH;
    const double maxCY = 1.0 - halfH;

    // ---- 3) 时间平滑（滑动平均，窗口由跟踪灵敏度映射）----
    // 帧间隔估计（用相邻时间戳中位数）
    double frameInterval = 1.0 / 30.0;
    if (saliency.size() >= 2) {
        std::vector<double> intervals;
        intervals.reserve(saliency.size() - 1);
        for (std::size_t i = 1; i < saliency.size(); ++i) {
            const double dt = (saliency[i].timestamp - saliency[i - 1].timestamp).toDouble();
            if (dt > 0.0) {
                intervals.push_back(dt);
            }
        }
        if (!intervals.empty()) {
            std::sort(intervals.begin(), intervals.end());
            frameInterval = intervals[intervals.size() / 2];
        }
    }
    if (frameInterval <= 0.0) {
        frameInterval = 1.0 / 30.0;
    }
    const double smoothingSeconds = config.smoothingSeconds();
    int halfWindow = static_cast<int>(smoothingSeconds / frameInterval / 2.0 + 0.5);
    halfWindow = std::max(0, std::min(halfWindow, 60));

    std::vector<double> smoothX(static_cast<std::size_t>(saliency.size()), 0.5);
    std::vector<double> smoothY(static_cast<std::size_t>(saliency.size()), 0.5);
    for (std::size_t i = 0; i < saliency.size(); ++i) {
        double sumX = 0.0;
        double sumY = 0.0;
        int n = 0;
        const std::size_t lo = i > static_cast<std::size_t>(halfWindow) ? i - halfWindow : 0;
        const std::size_t hi = std::min(saliency.size() - 1,
                                        i + static_cast<std::size_t>(halfWindow));
        for (std::size_t k = lo; k <= hi; ++k) {
            sumX += rawX[k];
            sumY += rawY[k];
            ++n;
        }
        smoothX[i] = n > 0 ? sumX / n : rawX[i];
        smoothY[i] = n > 0 ? sumY / n : rawY[i];
    }

    // ---- 4) 速度限制 + 边界钳制 ----
    const double maxStep = config.maxPanSpeedPerSecond * frameInterval;  // 每帧最大位移（归一化）
    std::vector<double> finalX(static_cast<std::size_t>(saliency.size()), 0.5);
    std::vector<double> finalY(static_cast<std::size_t>(saliency.size()), 0.5);
    double curX = std::min(maxCX, std::max(minCX, smoothX[0]));
    double curY = std::min(maxCY, std::max(minCY, smoothY[0]));
    for (std::size_t i = 0; i < saliency.size(); ++i) {
        double targetX = std::min(maxCX, std::max(minCX, smoothX[i]));
        double targetY = std::min(maxCY, std::max(minCY, smoothY[i]));
        // 速度限制（逐帧限幅）
        const double dx = targetX - curX;
        const double dy = targetY - curY;
        if (maxStep > 0.0) {
            if (std::fabs(dx) > maxStep) {
                targetX = curX + (dx > 0.0 ? maxStep : -maxStep);
            }
            if (std::fabs(dy) > maxStep) {
                targetY = curY + (dy > 0.0 ? maxStep : -maxStep);
            }
        }
        // 二次边界钳制（速度限制后仍须在范围内）
        curX = std::min(maxCX, std::max(minCX, targetX));
        curY = std::min(maxCY, std::max(minCY, targetY));
        finalX[i] = curX;
        finalY[i] = curY;
    }

    // ---- 5) 输出关键帧 ----
    track.keyframes.reserve(saliency.size());
    for (std::size_t i = 0; i < saliency.size(); ++i) {
        ReframeKeyframe kf;
        kf.time = saliency[i].timestamp;
        kf.centerX = finalX[i];
        kf.centerY = finalY[i];
        track.keyframes.push_back(kf);
    }
    return track;
}

bool reframeAt(const ReframeTrack& track, const Time& t, ReframeKeyframe& out) {
    if (!track.valid()) {
        return false;
    }
    const auto& keys = track.keyframes;
    if (t <= keys.front().time) {
        out = keys.front();
        return true;
    }
    if (t >= keys.back().time) {
        out = keys.back();
        return true;
    }
    // 线性插值（keyframes 按时间升序）
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (t <= keys[i].time) {
            const Time span = keys[i].time - keys[i - 1].time;
            const double ratio =
                span.isPositive() ? (t - keys[i - 1].time).toDouble() / span.toDouble() : 0.0;
            out.time = t;
            out.centerX = keys[i - 1].centerX + (keys[i].centerX - keys[i - 1].centerX) * ratio;
            out.centerY = keys[i - 1].centerY + (keys[i].centerY - keys[i - 1].centerY) * ratio;
            return true;
        }
    }
    out = keys.back();
    return true;
}

bool cropWindowAt(const ReframeTrack& track, const Time& t, int dstWidth, int dstHeight,
                  CropWindow& out) {
    if (!track.valid() || dstWidth <= 0 || dstHeight <= 0) {
        return false;
    }
    ReframeKeyframe kf;
    if (!reframeAt(track, t, kf)) {
        return false;
    }
    // 窗口尺寸：源内按目标比例的最大矩形
    const double srcAspect = static_cast<double>(track.srcWidth) / track.srcHeight;
    const double dstAspect = static_cast<double>(dstWidth) / dstHeight;
    int windowW = 0;
    int windowH = 0;
    if (dstAspect >= srcAspect) {
        windowW = track.srcWidth;
        windowH = static_cast<int>(static_cast<double>(windowW) / dstAspect + 0.5);
    } else {
        windowH = track.srcHeight;
        windowW = static_cast<int>(static_cast<double>(windowH) * dstAspect + 0.5);
    }
    windowW = std::max(1, std::min(windowW, track.srcWidth));
    windowH = std::max(1, std::min(windowH, track.srcHeight));
    // 中心 → 左上角（并钳制在源内）
    int x = static_cast<int>(kf.centerX * track.srcWidth - windowW / 2.0 + 0.5);
    int y = static_cast<int>(kf.centerY * track.srcHeight - windowH / 2.0 + 0.5);
    x = std::max(0, std::min(x, track.srcWidth - windowW));
    y = std::max(0, std::min(y, track.srcHeight - windowH));
    out.x = x;
    out.y = y;
    out.width = windowW;
    out.height = windowH;
    return true;
}

bool reframeRenderFrameAt(const ReframeTrack& track, const Time& t, const std::uint8_t* src,
                          int srcWidth, int srcHeight, int srcStride, std::uint8_t* dst,
                          int dstWidth, int dstHeight) {
    if (src == nullptr || dst == nullptr) {
        return false;
    }
    if (srcWidth != track.srcWidth || srcHeight != track.srcHeight) {
        return false;  // 源尺寸与轨迹不匹配（防御）
    }
    CropWindow window;
    if (!cropWindowAt(track, t, dstWidth, dstHeight, window)) {
        return false;
    }
    const int rowStride = srcStride > 0 ? srcStride : srcWidth * 4;
    if (rowStride < srcWidth * 4) {
        return false;
    }
    // 复用 frame_fit 的双线性缩放：把裁剪子图缩放到目标尺寸。
    // 做法：先把子图逐行拷到紧凑缓冲（去掉 stride 与偏移），再调用 renderFrameFit。
    std::vector<std::uint8_t> cropped(static_cast<std::size_t>(window.width) *
                                          static_cast<std::size_t>(window.height) * 4,
                                      0);
    for (int row = 0; row < window.height; ++row) {
        const std::uint8_t* srcRow =
            src + static_cast<std::size_t>(window.y + row) * static_cast<std::size_t>(rowStride) +
            static_cast<std::size_t>(window.x) * 4;
        std::uint8_t* dstRow = cropped.data() + static_cast<std::size_t>(row) *
                                                    static_cast<std::size_t>(window.width) * 4;
        std::memcpy(dstRow, srcRow, static_cast<std::size_t>(window.width) * 4);
    }
    // 子图与目标比例一致 → Fit 即为精确缩放（无黑边）
    return renderFrameFit(cropped.data(), window.width, window.height, window.width * 4, dst,
                          dstWidth, dstHeight, FrameFit::Fit, 0x000000FF);
}

ReframeDirection reframeDirection(int srcWidth, int srcHeight, int dstWidth, int dstHeight) {
    if (srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0) {
        return ReframeDirection::SameOrientation;
    }
    const double srcAspect = static_cast<double>(srcWidth) / srcHeight;
    const double dstAspect = static_cast<double>(dstWidth) / dstHeight;
    // 比例差异 < 5% 视为同方向
    if (std::fabs(srcAspect - dstAspect) / std::max(srcAspect, dstAspect) < 0.05) {
        return ReframeDirection::SameOrientation;
    }
    const bool srcLandscape = srcAspect > 1.0;
    const bool dstLandscape = dstAspect > 1.0;
    if (srcLandscape && !dstLandscape) {
        return ReframeDirection::LandscapeToPortrait;
    }
    if (!srcLandscape && dstLandscape) {
        return ReframeDirection::PortraitToLandscape;
    }
    // 同为横或同为竖但比例差异大（如 4:3 → 16:9）：按目标更宽/更高判定
    if (dstAspect > srcAspect) {
        return ReframeDirection::LandscapeToPortrait;  // 目标更宽：横向空间增多
    }
    return ReframeDirection::PortraitToLandscape;
}

const char* reframeDirectionName(ReframeDirection direction) {
    switch (direction) {
        case ReframeDirection::LandscapeToPortrait:
            return "横转竖";
        case ReframeDirection::PortraitToLandscape:
            return "竖转横";
        case ReframeDirection::SameOrientation:
        default:
            return "同方向";
    }
}

}  // namespace core
}  // namespace nairoclip
