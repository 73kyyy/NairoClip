#include "nairoclip/core/proxy.h"

#include "nairoclip/core/media_io.h"
#include "nairoclip/core/media_writer.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

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

/// 双线性缩放 RGBA8（源 → 目标）。目标缓冲由调用方分配。
/// 采用与 frame_fit 相同的像素中心映射，保证两处缩放结果一致。
void scaleRgbaBilinear(const std::uint8_t* src, int srcW, int srcH, int srcStride,
                       std::uint8_t* dst, int dstW, int dstH) {
    const int stride = srcStride > 0 ? srcStride : srcW * 4;
    const double stepX = static_cast<double>(srcW) / static_cast<double>(dstW);
    const double stepY = static_cast<double>(srcH) / static_cast<double>(dstH);
    for (int y = 0; y < dstH; ++y) {
        double sy = (static_cast<double>(y) + 0.5) * stepY - 0.5;
        sy = std::max(0.0, std::min(static_cast<double>(srcH - 1), sy));
        const int iy = static_cast<int>(std::floor(sy));
        const int iyNext = std::min(srcH - 1, iy + 1);
        const float fy = static_cast<float>(sy - iy);
        const std::uint8_t* rowTop =
            src + static_cast<std::size_t>(iy) * static_cast<std::size_t>(stride);
        const std::uint8_t* rowBottom =
            src + static_cast<std::size_t>(iyNext) * static_cast<std::size_t>(stride);
        std::uint8_t* dstRow =
            dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) * 4;
        for (int x = 0; x < dstW; ++x) {
            double sx = (static_cast<double>(x) + 0.5) * stepX - 0.5;
            sx = std::max(0.0, std::min(static_cast<double>(srcW - 1), sx));
            const int ix = static_cast<int>(std::floor(sx));
            const int ixNext = std::min(srcW - 1, ix + 1);
            const float fx = static_cast<float>(sx - ix);
            const std::uint8_t* p00 = rowTop + static_cast<std::size_t>(ix) * 4;
            const std::uint8_t* p10 = rowTop + static_cast<std::size_t>(ixNext) * 4;
            const std::uint8_t* p01 = rowBottom + static_cast<std::size_t>(ix) * 4;
            const std::uint8_t* p11 = rowBottom + static_cast<std::size_t>(ixNext) * 4;
            const float w00 = (1.0f - fx) * (1.0f - fy);
            const float w10 = fx * (1.0f - fy);
            const float w01 = (1.0f - fx) * fy;
            const float w11 = fx * fy;
            std::uint8_t* out = dstRow + static_cast<std::size_t>(x) * 4;
            for (int ch = 0; ch < 4; ++ch) {
                const float value =
                    p00[ch] * w00 + p10[ch] * w10 + p01[ch] * w01 + p11[ch] * w11;
                out[ch] = static_cast<std::uint8_t>(value + 0.5f);
            }
        }
    }
}

}  // namespace

// =============================================================================
// ProxyRegistry
// =============================================================================

const ProxyRecord* ProxyRegistry::find(AssetId assetId) const {
    for (const auto& record : records) {
        if (record.asset == assetId) {
            return &record;
        }
    }
    return nullptr;
}

ProxyRecord* ProxyRegistry::find(AssetId assetId) {
    for (auto& record : records) {
        if (record.asset == assetId) {
            return &record;
        }
    }
    return nullptr;
}

void ProxyRegistry::upsert(ProxyRecord record) {
    for (auto& existing : records) {
        if (existing.asset == record.asset) {
            existing = std::move(record);
            return;
        }
    }
    records.push_back(std::move(record));
}

bool ProxyRegistry::remove(AssetId assetId) {
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (it->asset == assetId) {
            records.erase(it);
            return true;
        }
    }
    return false;
}

void ProxyRegistry::clear() { records.clear(); }

std::int64_t ProxyRegistry::totalSizeBytes() const {
    std::int64_t total = 0;
    for (const auto& record : records) {
        if (record.status == ProxyStatus::Ready) {
            total += record.sizeBytes;
        }
    }
    return total;
}

std::size_t ProxyRegistry::readyCount() const {
    std::size_t count = 0;
    for (const auto& record : records) {
        if (record.status == ProxyStatus::Ready) {
            ++count;
        }
    }
    return count;
}

std::size_t ProxyRegistry::generatingCount() const {
    std::size_t count = 0;
    for (const auto& record : records) {
        if (record.status == ProxyStatus::Generating) {
            ++count;
        }
    }
    return count;
}

// =============================================================================
// 尺寸与导入检查
// =============================================================================

void proxyTargetSize(int srcWidth, int srcHeight, ProxyQuality quality, int& outWidth,
                     int& outHeight) {
    if (srcWidth <= 0 || srcHeight <= 0) {
        outWidth = 0;
        outHeight = 0;
        return;
    }
    const int divisor = quality == ProxyQuality::Quarter ? 4 : 2;
    outWidth = std::max(1, (srcWidth + divisor / 2) / divisor);
    outHeight = std::max(1, (srcHeight + divisor / 2) / divisor);
}

bool isHighResolutionAsset(const MediaAsset& asset, const ProxySettings& settings) {
    if (asset.type != MediaType::Video) {
        return false;
    }
    if (asset.width <= 0 || asset.height <= 0) {
        return false;
    }
    const std::int64_t pixels =
        static_cast<std::int64_t>(asset.width) * static_cast<std::int64_t>(asset.height);
    return pixels >= settings.highResPixelThreshold;
}

ProxyImportCheck checkProxyOnImport(const MediaAsset& asset, const ProxySettings& settings,
                                    const ProxyRegistry* registry) {
    ProxyImportCheck check;
    if (settings.mode == ProxyMode::Never) {
        return check;  // 从不生成：无建议
    }
    if (!isHighResolutionAsset(asset, settings)) {
        return check;  // 非高分辨率：无需代理
    }
    // 已有可用代理：无需重复生成
    if (registry != nullptr) {
        const ProxyRecord* record = registry->find(asset.id);
        if (record != nullptr && record->usable()) {
            return check;
        }
    }
    proxyTargetSize(asset.width, asset.height, settings.quality, check.proxyWidth,
                    check.proxyHeight);
    check.suggestProxy = true;
    check.autoGenerate = settings.mode == ProxyMode::Auto;
    // 估算（供 UI 展示「预计生成耗时: 45秒」）：
    //  · 耗时：解码 + 缩放 + 编码三段成本，均随「源像素数」增长（解码与缩放
    //    受源分辨率支配，编码受代理分辨率支配）。经验吞吐取下限值以保证
    //    估算偏保守（宁可提示偏长，不可误导用户以为很快）：
    //      解码 ≈ 12 MP/s；缩放 ≈ 30 MP/s；编码 ≈ 25 MP/s
    //    （真机差异较大，UI 可在估算基础上按设备基准分二次校正）。
    //  · 体积：按目标短边与经验码率（1.5 Mbps @ 540p 量级，线性缩放）。
    const double durationSeconds = std::max(0.0, asset.duration.toDouble());
    const double sourcePixels =
        static_cast<double>(asset.width) * static_cast<double>(asset.height);
    const double proxyPixels =
        static_cast<double>(check.proxyWidth) * static_cast<double>(check.proxyHeight);
    const double decodeSeconds = durationSeconds * sourcePixels / (12.0 * 1000000.0);
    const double scaleSeconds = durationSeconds * sourcePixels / (30.0 * 1000000.0);
    const double encodeSeconds = durationSeconds * proxyPixels / (25.0 * 1000000.0);
    check.estimatedSeconds = decodeSeconds + scaleSeconds + encodeSeconds;
    const double shortSide = static_cast<double>(std::min(check.proxyWidth, check.proxyHeight));
    const double bitrateMbps = std::max(0.35, 1.5 * shortSide / 540.0);
    check.estimatedBytes =
        static_cast<std::int64_t>(durationSeconds * bitrateMbps * 1000000.0 / 8.0);
    check.reason = std::string("高分辨率素材（") + std::to_string(asset.width) + "x" +
                   std::to_string(asset.height) + "），生成代理可提升剪辑流畅度";
    if (settings.mode == ProxyMode::Auto) {
        check.reason += "（自动生成已开启）";
    }
    return check;
}

SourceSelection selectPreviewSource(const MediaAsset& asset, const ProxyRegistry& registry) {
    SourceSelection selection;
    const ProxyRecord* record = registry.find(asset.id);
    if (record != nullptr && record->usable()) {
        selection.path = record->path;
        selection.usingProxy = true;
        selection.width = record->width;
        selection.height = record->height;
        return selection;
    }
    selection.path = asset.path;
    selection.usingProxy = false;
    selection.width = asset.width;
    selection.height = asset.height;
    return selection;
}

// =============================================================================
// 转码驱动
// =============================================================================

ProxyGenerateResult generateProxyFrames(MediaReader& source, MediaWriter& writer,
                                        int targetWidth, int targetHeight,
                                        std::int64_t maxFrames,
                                        const std::function<bool(double)>& progress) {
    ProxyGenerateResult result;
    if (targetWidth <= 0 || targetHeight <= 0) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "代理目标尺寸非法");
        return result;
    }
    if (!source.isOpen()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "源素材未打开");
        return result;
    }
    if (!writer.isOpen()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "写入器未打开");
        return result;
    }
    const Time totalDuration = source.probe().duration;
    result.proxyWidth = targetWidth;
    result.proxyHeight = targetHeight;

    VideoFrame frame;
    std::vector<std::uint8_t> scaled;
    while (source.readVideoFrame(frame)) {
        if (frame.width <= 0 || frame.height <= 0 || frame.pixels.empty()) {
            continue;
        }
        // 进度（基于源时间戳；失败时静默——进度仅提示用）
        if (progress != nullptr && totalDuration.isPositive()) {
            const double p = frame.timestamp.toDouble() / totalDuration.toDouble();
            if (!progress(std::min(1.0, std::max(0.0, p)))) {
                result.cancelled = true;
                result.result = Result::fail(ErrorCode::Cancelled, "代理生成已取消");
                return result;
            }
        }
        OutputVideoFrame out;
        out.timestamp = frame.timestamp;
        out.width = targetWidth;
        out.height = targetHeight;
        if (frame.width == targetWidth && frame.height == targetHeight) {
            out.rgba = frame.pixels;  // 尺寸一致：直接复用
        } else {
            scaled.assign(static_cast<std::size_t>(targetWidth) *
                              static_cast<std::size_t>(targetHeight) * 4,
                          0);
            scaleRgbaBilinear(frame.pixels.data(), frame.width, frame.height, frame.stride,
                              scaled.data(), targetWidth, targetHeight);
            out.rgba = scaled;
        }
        const Result writeResult = writer.writeVideoFrame(out);
        if (!writeResult.isOk()) {
            result.result = writeResult;
            return result;
        }
        ++result.framesWritten;
        if (maxFrames > 0 && result.framesWritten >= maxFrames) {
            break;
        }
    }
    if (progress != nullptr) {
        progress(1.0);
    }
    result.result = Result::ok();
    return result;
}

// =============================================================================
// 名称与解析
// =============================================================================

const char* proxyQualityName(ProxyQuality quality) {
    switch (quality) {
        case ProxyQuality::Half:
            return "half";
        case ProxyQuality::Quarter:
            return "quarter";
        default:
            return "half";
    }
}

bool proxyQualityFromString(const char* text, ProxyQuality& out) {
    if (text == nullptr) {
        return false;
    }
    if (equalsIgnoreCase(text, "half")) {
        out = ProxyQuality::Half;
        return true;
    }
    if (equalsIgnoreCase(text, "quarter")) {
        out = ProxyQuality::Quarter;
        return true;
    }
    return false;
}

const char* proxyStatusName(ProxyStatus status) {
    switch (status) {
        case ProxyStatus::None:
            return "none";
        case ProxyStatus::Generating:
            return "generating";
        case ProxyStatus::Ready:
            return "ready";
        case ProxyStatus::Failed:
            return "failed";
        default:
            return "none";
    }
}

bool proxyStatusFromString(const char* text, ProxyStatus& out) {
    if (text == nullptr) {
        return false;
    }
    if (equalsIgnoreCase(text, "none")) {
        out = ProxyStatus::None;
        return true;
    }
    if (equalsIgnoreCase(text, "generating")) {
        out = ProxyStatus::Generating;
        return true;
    }
    if (equalsIgnoreCase(text, "ready")) {
        out = ProxyStatus::Ready;
        return true;
    }
    if (equalsIgnoreCase(text, "failed")) {
        out = ProxyStatus::Failed;
        return true;
    }
    return false;
}

}  // namespace core
}  // namespace nairoclip
