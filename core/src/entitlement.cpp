#include "nairoclip/core/entitlement.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

bool Entitlement::proActiveAt(std::int64_t nowUnix) const {
    if (tier != MembershipTier::Pro) {
        return false;
    }
    if (lifetime) {
        return true;  // 永久买断：不受时间影响（文档 3.2）
    }
    if (expiresAtUnix <= 0) {
        return true;  // 无到期信息：以服务端校验为准
    }
    return nowUnix < expiresAtUnix;
}

int Entitlement::maxTracks() const {
    return tier == MembershipTier::Pro ? 0 : kFreeMaxTracks;
}

int Entitlement::maxExportQueue() const {
    return tier == MembershipTier::Pro ? 5 : kFreeMaxExportQueue;
}

int Entitlement::maxExportShortSide() const {
    return tier == MembershipTier::Pro ? 0 : kFreeMaxExportShortSide;
}

bool Entitlement::watermarkRequired() const {
    return tier != MembershipTier::Pro;
}

int Entitlement::subtitleMinutesPerMonth() const {
    return tier == MembershipTier::Pro ? 0 : kFreeSubtitleMinutesPerMonth;
}

int Entitlement::aiGeneratePerMonth() const {
    return tier == MembershipTier::Pro ? 0 : kFreeAiGenerationsPerMonth;
}

bool Entitlement::frameInterpolation() const {
    return tier == MembershipTier::Pro;
}

bool Entitlement::highPrecisionMatting() const {
    return tier == MembershipTier::Pro;
}

bool Entitlement::canAddTrack(int currentTrackCount) const {
    if (currentTrackCount < 0) {
        return false;
    }
    const int limit = maxTracks();
    return limit == 0 || currentTrackCount < limit;
}

bool clampSizeToShortSide(int width, int height, int maxShortSide, int& outWidth,
                          int& outHeight) {
    outWidth = width;
    outHeight = height;
    if (width <= 0 || height <= 0 || maxShortSide <= 0) {
        return false;
    }
    const int shortSide = std::min(width, height);
    if (shortSide <= maxShortSide) {
        return false;  // 无需缩小（也绝不放大）
    }
    const double scale = static_cast<double>(maxShortSide) / static_cast<double>(shortSide);
    outWidth = std::max(1, static_cast<int>(std::lround(width * scale)));
    outHeight = std::max(1, static_cast<int>(std::lround(height * scale)));
    return true;
}

ExportPolicy makeExportPolicy(const Entitlement& entitlement, std::int64_t nowUnix) {
    ExportPolicy policy;
    if (entitlement.proActiveAt(nowUnix)) {
        // Pro 生效：不限制、无水印。
        policy.maxExportShortSide = 0;
        policy.watermark.enabled = false;
        return policy;
    }
    // 免费/过期/信息不足：fail-closed（降级 + 水印）。
    policy.maxExportShortSide = kFreeMaxExportShortSide;
    policy.watermark = WatermarkSpec{};
    policy.watermark.enabled = true;
    return policy;
}

}  // namespace core
}  // namespace nairoclip
