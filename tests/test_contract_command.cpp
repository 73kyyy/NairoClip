// 契约层 · 命令信封测试（03 §4 / 07 §4）
#include "test_framework.h"

#include "nairoclip/core/contract/command.h"

#include <string>

using namespace nairoclip::core;
using namespace nairoclip::core::contract;

namespace {

CommandRequest makeValid() {
    CommandRequest r;
    r.schemaVersion = kSchemaVersion;
    r.commandId = "cmd-0001";
    r.projectId = 7;
    r.expectedRevision = 42;
    r.commandType = "clip.move";
    r.payload = JsonValue::object();
    r.origin = CommandOrigin::User;
    return r;
}

}  // namespace

NC_TEST(ContractCommand, ValidRequestPasses) {
    const CommandRequest r = makeValid();
    NC_CHECK(validateCommandRequest(r).isOk());
}

NC_TEST(ContractCommand, MissingCommandIdRejected) {
    CommandRequest r = makeValid();
    r.commandId.clear();
    const ErrorDetail e = validateCommandRequest(r);
    NC_CHECK_EQ(e.code, std::string("NC-PROJECT-INVALID_COMMAND_ID"));
    NC_CHECK_EQ(e.field, std::string("commandId"));
}

NC_TEST(ContractCommand, CommandIdWhitespaceRejected) {
    CommandRequest r = makeValid();
    r.commandId = "cmd 0001";  // 含空格，会破坏日志行
    NC_CHECK(!validateCommandRequest(r).isOk());
}

NC_TEST(ContractCommand, MissingProjectIdRejected) {
    CommandRequest r = makeValid();
    r.projectId = 0;
    const ErrorDetail e = validateCommandRequest(r);
    NC_CHECK_EQ(e.code, std::string("NC-PROJECT-INVALID_PROJECT_ID"));
}

NC_TEST(ContractCommand, BadCommandTypeRejected) {
    CommandRequest r = makeValid();
    r.commandType = "Clip Move";  // 大写 + 空格 + 无点
    NC_CHECK_EQ(validateCommandRequest(r).code, std::string("NC-PROJECT-INVALID_COMMAND_TYPE"));

    r.commandType = "clipmove";  // 无点
    NC_CHECK(!validateCommandRequest(r).isOk());

    r.commandType = "track.setState";
    NC_CHECK(validateCommandRequest(r).isOk());
}

NC_TEST(ContractCommand, PayloadMustBeObject) {
    CommandRequest r = makeValid();
    r.payload = JsonValue::integer(1);
    NC_CHECK_EQ(validateCommandRequest(r).code, std::string("NC-PROJECT-INVALID_PAYLOAD"));
}

NC_TEST(ContractCommand, OldSchemaRejected) {
    CommandRequest r = makeValid();
    r.schemaVersion = 0;
    NC_CHECK_EQ(validateCommandRequest(r).code, std::string("NC-PROJECT-VERSION_UNSUPPORTED"));
    r.schemaVersion = kSchemaVersion + 1;
    NC_CHECK_EQ(validateCommandRequest(r).code, std::string("NC-PROJECT-VERSION_UNSUPPORTED"));
}

NC_TEST(ContractCommand, BadTransactionIdRejected) {
    CommandRequest r = makeValid();
    r.transactionId = "tx\n1";
    NC_CHECK_EQ(validateCommandRequest(r).code, std::string("NC-PROJECT-INVALID_TRANSACTION_ID"));
}

NC_TEST(ContractCommand, RevisionConflictDetected) {
    NC_CHECK(checkRevision(42, 42).isOk());
    const ErrorDetail e = checkRevision(41, 42);
    NC_CHECK_EQ(e.code, std::string("NC-PROJECT-REVISION_CONFLICT"));
    NC_CHECK_EQ(e.field, std::string("expectedRevision"));
    NC_CHECK(e.diagnostic.find("expected=41") != std::string::npos);
}

NC_TEST(ContractCommand, OriginNameRoundTrip) {
    for (int i = 0; i <= static_cast<int>(CommandOrigin::System); ++i) {
        const CommandOrigin origin = static_cast<CommandOrigin>(i);
        CommandOrigin parsed = CommandOrigin::User;
        NC_CHECK(commandOriginFromName(commandOriginName(origin), &parsed));
        NC_CHECK(parsed == origin);
    }
    CommandOrigin parsed;
    NC_CHECK(!commandOriginFromName("god", &parsed));
}

NC_TEST(ContractCommand, JsonRoundTripPreservesAllFields) {
    CommandRequest r = makeValid();
    r.origin = CommandOrigin::Ai;
    r.transactionId = "tx-9";
    JsonValue payload = JsonValue::object();
    payload.set("targetTrackId", JsonValue::integer(3));
    payload.set("snap", JsonValue::boolean(true));
    r.payload = payload;

    const JsonValue encoded = commandRequestToJson(r);
    CommandRequest decoded;
    NC_CHECK(commandRequestFromJson(encoded, &decoded));
    NC_CHECK_EQ(decoded.schemaVersion, r.schemaVersion);
    NC_CHECK_EQ(decoded.commandId, r.commandId);
    NC_CHECK_EQ(decoded.projectId, r.projectId);
    NC_CHECK_EQ(decoded.expectedRevision, r.expectedRevision);
    NC_CHECK_EQ(decoded.commandType, r.commandType);
    NC_CHECK_EQ(decoded.transactionId, r.transactionId);
    NC_CHECK(decoded.origin == CommandOrigin::Ai);
    NC_CHECK(decoded.payload.isObject());
    NC_CHECK_EQ(decoded.payload.get("targetTrackId").asInteger(0), std::int64_t(3));
    NC_CHECK(decoded.payload.get("snap").asBool(false));
}

NC_TEST(ContractCommand, JsonDecodeRejectsUnknownOrigin) {
    CommandRequest r = makeValid();
    JsonValue encoded = commandRequestToJson(r);
    encoded.set("origin", JsonValue::string("hacker"));
    CommandRequest decoded;
    NC_CHECK(!commandRequestFromJson(encoded, &decoded));
}

NC_TEST(ContractCommand, JsonDecodeRejectsNonObject) {
    CommandRequest decoded;
    NC_CHECK(!commandRequestFromJson(JsonValue::integer(1), &decoded));
    NC_CHECK(!commandRequestFromJson(JsonValue::object(), nullptr));
}

NC_TEST(ContractCommand, ResultJsonCarriesError) {
    CommandResult res;
    res.status = CommandStatus::Rejected;
    res.commandId = "cmd-1";
    res.error = makeError(ErrorDomain::Timeline, "TRACK_LOCKED", "轨道已锁定", "trackId");
    const JsonValue j = commandResultToJson(res);
    NC_CHECK_EQ(j.get("status").asString(), std::string("rejected"));
    NC_CHECK(j.has("error"));
    NC_CHECK_EQ(j.get("error").get("code").asString(), std::string("NC-TIMELINE-TRACK_LOCKED"));
    NC_CHECK_EQ(j.get("error").get("field").asString(), std::string("trackId"));
}

NC_TEST(ContractCommand, ResultJsonOmitsErrorWhenOk) {
    CommandResult res;
    res.status = CommandStatus::Accepted;
    res.newRevision = 43;
    res.undoAvailable = true;
    const JsonValue j = commandResultToJson(res);
    NC_CHECK(!j.has("error"));
    NC_CHECK_EQ(j.get("newRevision").asInteger(0), std::int64_t(43));
    NC_CHECK(j.get("undoAvailable").asBool(false));
}
