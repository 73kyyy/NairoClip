#include "nairoclip/core/thumbnail.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

bool downscaleRgba(const std::uint8_t* src, int srcWidth, int srcHeight, int* dstWidth,
                   int* dstHeight, std::vector<std::uint8_t>& dst) {
    if (src == nullptr || srcWidth <= 0 || srcHeight <= 0 || dstWidth == nullptr ||
        dstHeight == nullptr) {
        return false;
    }
    const int targetW = *dstWidth;
    const int targetH = *dstHeight;
    if (targetW <= 0 || targetH <= 0) {
        return false;
    }
    if (targetW >= srcWidth && targetH >= srcHeight) {
        return false;  // 无需放大
    }
    dst.assign(static_cast<std::size_t>(targetW) * static_cast<std::size_t>(targetH) * 4, 0);
    // 最近邻采样（速度优先；质量由上层决定是否用双线性）
    for (int y = 0; y < targetH; ++y) {
        const int srcY = std::min(srcHeight - 1, y * srcHeight / targetH);
        for (int x = 0; x < targetW; ++x) {
            const int srcX = std::min(srcWidth - 1, x * srcWidth / targetW);
            const std::uint8_t* srcPx =
                src + (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(srcWidth) +
                       static_cast<std::size_t>(srcX)) * 4;
            std::uint8_t* dstPx =
                dst.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(targetW) +
                              static_cast<std::size_t>(x)) * 4;
            dstPx[0] = srcPx[0];
            dstPx[1] = srcPx[1];
            dstPx[2] = srcPx[2];
            dstPx[3] = srcPx[3];
        }
    }
    return true;
}

std::vector<Thumbnail> generateThumbnails(MediaReader& reader, const std::string& assetPath,
                                          const Time& duration, const ThumbnailConfig& config) {
    std::vector<Thumbnail> result;
    if (!duration.isPositive() || config.count <= 0) {
        return result;
    }
    if (!reader.open(assetPath).isOk()) {
        return result;
    }

    const int count = config.count;
    const double durationSec = duration.toDouble();
    for (int i = 0; i < count; ++i) {
        double t = 0.0;
        if (config.centered) {
            t = durationSec * (static_cast<double>(i) + 0.5) / static_cast<double>(count);
        } else {
            t = durationSec * static_cast<double>(i) / static_cast<double>(std::max(1, count - 1));
        }
        const Time sampleTime = Rational::make(static_cast<std::int64_t>(t * 1000.0 + 0.5), 1000);
        if (!reader.seek(sampleTime).isOk()) {
            continue;
        }
        VideoFrame frame;
        if (!reader.readVideoFrame(frame)) {
            continue;
        }
        Thumbnail thumb;
        thumb.sourceTime = sampleTime;
        // 计算缩放目标（保持宽高比，限制在 maxWidth/maxHeight 内）
        int targetW = frame.width;
        int targetH = frame.height;
        if (config.maxWidth > 0 && targetW > config.maxWidth) {
            targetH = std::max(1, targetH * config.maxWidth / targetW);
            targetW = config.maxWidth;
        }
        if (config.maxHeight > 0 && targetH > config.maxHeight) {
            targetW = std::max(1, targetW * config.maxHeight / targetH);
            targetH = config.maxHeight;
        }
        if (targetW < frame.width || targetH < frame.height) {
            int w = targetW;
            int h = targetH;
            std::vector<std::uint8_t> scaled;
            if (downscaleRgba(frame.pixels.data(), frame.width, frame.height, &w, &h, scaled)) {
                thumb.width = w;
                thumb.height = h;
                thumb.rgba = std::move(scaled);
            } else {
                thumb.width = frame.width;
                thumb.height = frame.height;
                thumb.rgba = frame.pixels;
            }
        } else {
            thumb.width = frame.width;
            thumb.height = frame.height;
            thumb.rgba = frame.pixels;
        }
        result.push_back(std::move(thumb));
    }
    reader.close();
    return result;
}

}  // namespace core
}  // namespace nairoclip
