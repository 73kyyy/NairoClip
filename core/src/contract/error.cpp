#include "nairoclip/core/contract/error.h"

#include <cstring>

namespace nairoclip {
namespace core {
namespace contract {
namespace {

struct DomainInfo {
    ErrorDomain domain;
    const char* prefix;
};

const DomainInfo kDomains[] = {
    {ErrorDomain::Project, "NC-PROJECT"},
    {ErrorDomain::Timeline, "NC-TIMELINE"},
    {ErrorDomain::Media, "NC-MEDIA"},
    {ErrorDomain::Render, "NC-RENDER"},
    {ErrorDomain::Audio, "NC-AUDIO"},
    {ErrorDomain::Export, "NC-EXPORT"},
    {ErrorDomain::Auth, "NC-AUTH"},
    {ErrorDomain::Service, "NC-SERVICE"},
};

constexpr std::size_t kDomainCount = sizeof(kDomains) / sizeof(kDomains[0]);

/// 可恢复（可重试）的稳定码后缀集合。
/// 判定依据：这类错误是「外部条件暂时不满足」或「用户取消」，
/// 换时间/换设备/重试可能成功；而参数非法、轨道锁定这类重试也没用。
bool isRecoverableSuffix(const std::string& suffix) {
    static const char* kRecoverable[] = {
        "CANCELLED",
        "NETWORK",
        "RATE_LIMITED",
        "REMOTE_FAILURE",
        "OUT_OF_MEMORY",
        "CONTEXT_LOST",
        "BACKEND_UNAVAILABLE",
        "SOURCE_MISSING",
        "PERMISSION_REVOKED",
        "NO_SPACE",
        "DEVICE_UNAVAILABLE",
        "UNDERRUN",
        "ENCODER_FAILED",
        "TOKEN_EXPIRED",
    };
    for (const char* c : kRecoverable) {
        if (suffix == c) return true;
    }
    return false;
}

/// 拆分 "NC-DOMAIN-SUFFIX" → suffix（最后一个域前缀之后的部分）。
std::string suffixOf(const std::string& code) {
    for (const DomainInfo& d : kDomains) {
        const std::size_t n = std::strlen(d.prefix);
        if (code.size() > n + 1 && code.compare(0, n, d.prefix) == 0 && code[n] == '-') {
            return code.substr(n + 1);
        }
    }
    return std::string();
}

}  // namespace

const char* domainPrefix(ErrorDomain domain) {
    for (const DomainInfo& d : kDomains) {
        if (d.domain == domain) return d.prefix;
    }
    return kDomains[0].prefix;
}

bool domainFromCode(const std::string& code, ErrorDomain* out) {
    for (const DomainInfo& d : kDomains) {
        const std::size_t n = std::strlen(d.prefix);
        if (code.size() > n + 1 && code.compare(0, n, d.prefix) == 0 && code[n] == '-') {
            if (out != nullptr) *out = d.domain;
            return true;
        }
    }
    return false;
}

ErrorDetail makeError(ErrorDomain domain, const std::string& suffix, std::string message,
                      std::string field, bool recoverable) {
    ErrorDetail e;
    e.domain = domain;
    e.code = std::string(domainPrefix(domain)) + "-" + suffix;
    e.message = std::move(message);
    e.field = std::move(field);
    // 调用方没显式指定时，按码表推断，避免「CANCELLED 被标成不可恢复」。
    e.recoverable = recoverable || isRecoverableSuffix(suffix);
    return e;
}

ErrorDetail mapCoreResult(const Result& result, ErrorDomain fallbackDomain) {
    if (result.isOk()) return ErrorDetail{};

    switch (result.code) {
        case ErrorCode::Ok:
            return ErrorDetail{};
        case ErrorCode::InvalidArgument:
            return makeError(fallbackDomain, "INVALID_ARGUMENT", result.message);
        case ErrorCode::NotFound:
            return makeError(fallbackDomain, "NOT_FOUND", result.message);
        case ErrorCode::Overlap:
            // 重叠是时间轴专有语义，无论调用方在哪个域，都应落在 TIMELINE。
            return makeError(ErrorDomain::Timeline, "CLIP_OVERLAP", result.message);
        case ErrorCode::OutOfRange:
            return makeError(ErrorDomain::Timeline, "INVALID_RANGE", result.message);
        case ErrorCode::Locked:
            return makeError(ErrorDomain::Timeline, "TRACK_LOCKED", result.message);
        case ErrorCode::Unsupported:
            return makeError(fallbackDomain, "UNSUPPORTED", result.message);
        case ErrorCode::Internal:
            return makeError(fallbackDomain, "INTERNAL", result.message);
        case ErrorCode::Cancelled:
            return makeError(fallbackDomain, "CANCELLED", result.message, std::string(), true);
    }
    return makeError(fallbackDomain, "INTERNAL", result.message);
}

bool isRecoverableCode(const std::string& code) {
    if (!isKnownCode(code)) return false;
    return isRecoverableSuffix(suffixOf(code));
}

bool isKnownCode(const std::string& code) {
    ErrorDomain domain;
    if (!domainFromCode(code, &domain)) return false;
    // 必须带非空后缀，且不允许多余的域前缀嵌套。
    const std::string suffix = suffixOf(code);
    return !suffix.empty() && suffix.find("NC-") == std::string::npos;
}

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
