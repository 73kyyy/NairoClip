// 契约层 · 错误模型测试（07 §7）
#include "test_framework.h"

#include "nairoclip/core/contract/error.h"

#include <string>

using namespace nairoclip::core;
using namespace nairoclip::core::contract;

namespace {

ErrorDomain domainOf(const std::string& code) {
    ErrorDomain d = ErrorDomain::Project;
    NC_CHECK(domainFromCode(code, &d));
    return d;
}

}  // namespace

NC_TEST(ContractError, DomainPrefixIsStable) {
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Project)), std::string("NC-PROJECT"));
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Timeline)), std::string("NC-TIMELINE"));
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Media)), std::string("NC-MEDIA"));
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Render)), std::string("NC-RENDER"));
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Audio)), std::string("NC-AUDIO"));
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Export)), std::string("NC-EXPORT"));
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Auth)), std::string("NC-AUTH"));
    NC_CHECK_EQ(std::string(domainPrefix(ErrorDomain::Service)), std::string("NC-SERVICE"));
}

NC_TEST(ContractError, MakeErrorBuildsFullCode) {
    const ErrorDetail e = makeError(ErrorDomain::Timeline, "TRACK_LOCKED", "轨道已锁定", "trackId");
    NC_CHECK_EQ(e.code, std::string("NC-TIMELINE-TRACK_LOCKED"));
    NC_CHECK_EQ(e.message, std::string("轨道已锁定"));
    NC_CHECK_EQ(e.field, std::string("trackId"));
    NC_CHECK(!e.isOk());
    NC_CHECK(!e.recoverable);  // 锁定重试无用
}

NC_TEST(ContractError, DefaultErrorIsOk) {
    const ErrorDetail e;
    NC_CHECK(e.isOk());
}

NC_TEST(ContractError, DomainRoundTrip) {
    NC_CHECK(domainOf("NC-PROJECT-CORRUPT") == ErrorDomain::Project);
    NC_CHECK(domainOf("NC-MEDIA-SOURCE_MISSING") == ErrorDomain::Media);
    NC_CHECK(domainOf("NC-EXPORT-NO_SPACE") == ErrorDomain::Export);
    NC_CHECK(domainOf("NC-SERVICE-RATE_LIMITED") == ErrorDomain::Service);
}

NC_TEST(ContractError, RejectsUnknownDomain) {
    ErrorDomain d;
    NC_CHECK(!domainFromCode("NC-UNKNOWN-X", &d));
    NC_CHECK(!domainFromCode("", &d));
    NC_CHECK(!domainFromCode("NC-", &d));
}

NC_TEST(ContractError, KnownCodeRequiresSuffix) {
    NC_CHECK(isKnownCode("NC-TIMELINE-TRACK_LOCKED"));
    NC_CHECK(!isKnownCode("NC-TIMELINE-"));
    NC_CHECK(!isKnownCode("TRACK_LOCKED"));
    NC_CHECK(!isKnownCode("NC-TIMELINE-NC-MEDIA-X"));  // 嵌套前缀视为非法
}

NC_TEST(ContractError, RecoverableClassification) {
    NC_CHECK(isRecoverableCode("NC-EXPORT-NO_SPACE"));
    NC_CHECK(isRecoverableCode("NC-SERVICE-NETWORK"));
    NC_CHECK(isRecoverableCode("NC-MEDIA-SOURCE_MISSING"));
    NC_CHECK(!isRecoverableCode("NC-TIMELINE-TRACK_LOCKED"));
    NC_CHECK(!isRecoverableCode("NC-PROJECT-CORRUPT"));
}

NC_TEST(ContractError, MapCoreResultLocked) {
    const Result r = Result::fail(ErrorCode::Locked, "轨道已锁定");
    const ErrorDetail e = mapCoreResult(r, ErrorDomain::Timeline);
    NC_CHECK_EQ(e.code, std::string("NC-TIMELINE-TRACK_LOCKED"));
    NC_CHECK_EQ(e.message, std::string("轨道已锁定"));
}

NC_TEST(ContractError, MapCoreResultOverlapAlwaysTimeline) {
    // 即便调用方声明自己在 MEDIA 域，重叠语义也必须落到 TIMELINE。
    const Result r = Result::fail(ErrorCode::Overlap, "与已有片段重叠");
    const ErrorDetail e = mapCoreResult(r, ErrorDomain::Media);
    NC_CHECK_EQ(e.code, std::string("NC-TIMELINE-CLIP_OVERLAP"));
}

NC_TEST(ContractError, MapCoreResultCancelledIsRecoverable) {
    const Result r = Result::fail(ErrorCode::Cancelled, "用户取消");
    const ErrorDetail e = mapCoreResult(r, ErrorDomain::Export);
    NC_CHECK_EQ(e.code, std::string("NC-EXPORT-CANCELLED"));
    NC_CHECK(e.recoverable);
}

NC_TEST(ContractError, MapCoreResultOk) {
    const ErrorDetail e = mapCoreResult(Result::ok(), ErrorDomain::Project);
    NC_CHECK(e.isOk());
}

NC_TEST(ContractError, MapCoreResultInvalidArgumentUsesFallbackDomain) {
    const Result r = Result::fail(ErrorCode::InvalidArgument, "参数非法");
    NC_CHECK_EQ(mapCoreResult(r, ErrorDomain::Render).code,
                std::string("NC-RENDER-INVALID_ARGUMENT"));
    NC_CHECK_EQ(mapCoreResult(r, ErrorDomain::Audio).code,
                std::string("NC-AUDIO-INVALID_ARGUMENT"));
}
