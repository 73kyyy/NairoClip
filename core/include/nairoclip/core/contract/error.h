#pragma once
// ============================================================================
// 契约层 · 错误模型
// ----------------------------------------------------------------------------
// 依据 07 §7「错误码体系」：八域前缀 + 稳定码 + 用户可读消息 + 开发诊断字段。
//
// 与核心层 ErrorCode 的关系：
//   · 核心层 ErrorCode 只有 9 个通用值（types.h），无法表达「哪个域、哪个字段」；
//   · 本层把它映射成 NC-<DOMAIN>-<CODE>，并补充 field / recoverable / diagnostic；
//   · 旧 ErrorCode 保留不动（不破坏 332 个 C ABI），契约层只是**叠加**。
// ============================================================================

#include "nairoclip/core/contract/version.h"
#include "nairoclip/core/types.h"

#include <string>

namespace nairoclip {
namespace core {
namespace contract {

/// 八个错误域（07 §7）。
enum class ErrorDomain {
    Project = 0,
    Timeline,
    Media,
    Render,
    Audio,
    Export,
    Auth,
    Service,
};

/// 域前缀，例如 "NC-TIMELINE"。返回值指向静态存储，不拥有所有权。
const char* domainPrefix(ErrorDomain domain);

/// 从完整错误码（"NC-MEDIA-SOURCE_MISSING"）解析出域。
bool domainFromCode(const std::string& code, ErrorDomain* out);

/// 结构化错误。code 为空表示成功。
struct ErrorDetail {
    ErrorDomain domain = ErrorDomain::Project;
    std::string code;     ///< 完整稳定码，如 NC-TIMELINE-TRACK_LOCKED
    std::string message;  ///< 用户可读消息
    std::string field;    ///< 字段级定位，如 "payload.targetTrackId"（可为空）
    bool recoverable = false;
    std::string diagnostic;  ///< 开发诊断，禁止写入密钥/隐私

    bool isOk() const { return code.empty(); }
};

/// 拼装错误：约定校验类错误一律 not recoverable，资源类错误由调用方指定。
ErrorDetail makeError(ErrorDomain domain, const std::string& suffix, std::string message,
                      std::string field = std::string(), bool recoverable = false);

/// 核心层 Result → 契约错误。
/// 核心层没有域信息，因此由调用方给出 fallbackDomain（本次调用属于哪个域）。
ErrorDetail mapCoreResult(const Result& result, ErrorDomain fallbackDomain);

/// 判断某个稳定码是否属于「可重试/可恢复」类别。
bool isRecoverableCode(const std::string& code);

/// 稳定码是否属于已知的八个域之一（用于拒绝乱造的码）。
bool isKnownCode(const std::string& code);

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
