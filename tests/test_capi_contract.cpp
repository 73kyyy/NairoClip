// C ABI 契约入口测试：nc_apply_command / nc_contract_revision
// 只验证跨语言边界行为（解析、拒绝语义、内存释放），
// 真实编辑链路由 test_contract_dispatch.cpp 覆盖。
#include "test_framework.h"

#include "nairoclip/core/capi.h"
#include "nairoclip/core/json.h"

#include <string>

using namespace nairoclip::core;

namespace {

/// 调用 nc_apply_command 并把结果 JSON 解析出来（调用方负责释放原始字符串）。
bool callApply(NcEditor* ed, const std::string& request, JsonValue* out) {
    char* raw = nc_apply_command(ed, request.c_str());
    if (raw == nullptr) return false;
    JsonParseError err;
    const std::string text(raw);
    nc_free_string(raw);
    return parseJson(text, *out, err);
}

}  // namespace

NC_TEST(CapiContract, NullArgsReturnNull) {
    NC_CHECK(nc_apply_command(nullptr, "{}") == nullptr);
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    NC_CHECK(nc_apply_command(ed, nullptr) == nullptr);
    nc_editor_destroy(ed);
}

NC_TEST(CapiContract, MalformedJsonReturnsStructuredError) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    JsonValue j;
    NC_CHECK(callApply(ed, "{ not json", &j));
    NC_CHECK_EQ(j.get("status").asString(), std::string("rejected"));
    NC_CHECK_EQ(j.get("error").get("code").asString(),
                std::string("NC-PROJECT-INVALID_PAYLOAD"));
    nc_editor_destroy(ed);
}

NC_TEST(CapiContract, MissingEnvelopeFieldsRejected) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    JsonValue j;
    NC_CHECK(callApply(ed, "{\"schemaVersion\":1}", &j));
    NC_CHECK_EQ(j.get("status").asString(), std::string("rejected"));
    // 信封 JSON 本身能解析，缺的是必填字段 → 落到信封校验而不是解析错误。
    NC_CHECK_EQ(j.get("error").get("code").asString(),
                std::string("NC-PROJECT-INVALID_COMMAND_ID"));
    nc_editor_destroy(ed);
}

NC_TEST(CapiContract, InvalidCommandTypeRejected) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    const std::string req =
        "{\"schemaVersion\":1,\"commandId\":\"c1\",\"projectId\":1,"
        "\"expectedRevision\":0,\"commandType\":\"BAD TYPE\","
        "\"origin\":\"user\",\"payload\":{}}";
    JsonValue j;
    NC_CHECK(callApply(ed, req, &j));
    NC_CHECK_EQ(j.get("status").asString(), std::string("rejected"));
    NC_CHECK_EQ(j.get("error").get("code").asString(),
                std::string("NC-PROJECT-INVALID_COMMAND_TYPE"));
    nc_editor_destroy(ed);
}

NC_TEST(CapiContract, UnsupportedTypeRejected) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    const std::string req =
        "{\"schemaVersion\":1,\"commandId\":\"c2\",\"projectId\":1,"
        "\"expectedRevision\":0,\"commandType\":\"clip.teleport\","
        "\"origin\":\"user\",\"payload\":{\"sequenceId\":1,\"trackId\":1,\"clipId\":1}}";
    JsonValue j;
    NC_CHECK(callApply(ed, req, &j));
    NC_CHECK_EQ(j.get("status").asString(), std::string("rejected"));
    NC_CHECK_EQ(j.get("error").get("code").asString(),
                std::string("NC-TIMELINE-UNSUPPORTED_COMMAND"));
    nc_editor_destroy(ed);
}

NC_TEST(CapiContract, NonexistentIdsRejectedNotCrash) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    const std::string req =
        "{\"schemaVersion\":1,\"commandId\":\"c3\",\"projectId\":1,"
        "\"expectedRevision\":0,\"commandType\":\"clip.remove\","
        "\"origin\":\"user\",\"payload\":{\"sequenceId\":999,\"trackId\":999,\"clipId\":999}}";
    JsonValue j;
    NC_CHECK(callApply(ed, req, &j));
    NC_CHECK_EQ(j.get("status").asString(), std::string("rejected"));
    // 不存在应得到 NOT_FOUND 语义，而不是崩溃或空指针。
    NC_CHECK_EQ(j.get("error").get("code").asString(),
                std::string("NC-TIMELINE-NOT_FOUND"));
    nc_editor_destroy(ed);
}

NC_TEST(CapiContract, RevisionStartsAtZero) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    NC_CHECK_EQ(nc_contract_revision(ed), std::uint64_t(0));
    NC_CHECK_EQ(nc_contract_revision(nullptr), std::uint64_t(0));
    nc_editor_destroy(ed);
}

NC_TEST(CapiContract, UnknownOriginRejected) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    const std::string req =
        "{\"schemaVersion\":1,\"commandId\":\"c4\",\"projectId\":1,"
        "\"expectedRevision\":0,\"commandType\":\"clip.remove\",\"origin\":\"hacker\","
        "\"payload\":{\"sequenceId\":1,\"trackId\":1,\"clipId\":1}}";
    JsonValue j;
    NC_CHECK(callApply(ed, req, &j));
    NC_CHECK_EQ(j.get("status").asString(), std::string("rejected"));
    NC_CHECK_EQ(j.get("error").get("code").asString(),
                std::string("NC-PROJECT-INVALID_PAYLOAD"));
    nc_editor_destroy(ed);
}
