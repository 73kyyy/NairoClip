#include "nairoclip/core/contract/command.h"

#include <cstring>

namespace nairoclip {
namespace core {
namespace contract {
namespace {

struct OriginInfo {
    CommandOrigin origin;
    const char* name;
};

const OriginInfo kOrigins[] = {
    {CommandOrigin::User, "user"},     {CommandOrigin::Undo, "undo"},
    {CommandOrigin::Redo, "redo"},     {CommandOrigin::Ai, "ai"},
    {CommandOrigin::Plugin, "plugin"}, {CommandOrigin::Import, "import"},
    {CommandOrigin::Sync, "sync"},     {CommandOrigin::System, "system"},
};

constexpr std::size_t kOriginCount = sizeof(kOrigins) / sizeof(kOrigins[0]);

/// commandType 的稳定格式：字母/数字/下划线，段间用 '.'，必须含至少一个 '.'。
/// 例子见 07 §4 原文："clip.move"、"clip.split"、"track.setState"。
/// 注意 track.setState 是 camelCase —— 段内**必须允许大写**，
/// 否则规范给的示例自己都过不了校验（初版曾只放小写，被单测抓出）。
/// 仍禁止空格与其它字符，避免「后端按字符串模糊匹配」的隐患（03 §6）。
bool isValidCommandType(const std::string& type) {
    if (type.empty() || type.size() > 64) return false;
    bool hasDot = false;
    for (char c : type) {
        if (c == '.') {
            hasDot = true;
            continue;
        }
        const bool alpha = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool digit = c >= '0' && c <= '9';
        const bool underscore = c == '_';
        if (!alpha && !digit && !underscore) return false;
    }
    // 首字符必须是字母，且必须以字母/数字结尾（禁止 ".foo" / "foo."）。
    const char first = type[0];
    const char last = type[type.size() - 1];
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) return false;
    if (last == '.') return false;
    return hasDot;
}

/// commandId 只允许可打印 ASCII 且不含空白，长度 1..128。
/// 目的：它会被写进日志与事件 correlationId，含换行会破坏日志行完整性。
bool isValidCommandId(const std::string& id) {
    if (id.empty() || id.size() > 128) return false;
    for (unsigned char c : id) {
        if (c < 0x21 || c > 0x7E) return false;
    }
    return true;
}

}  // namespace

const char* commandOriginName(CommandOrigin origin) {
    for (const OriginInfo& o : kOrigins) {
        if (o.origin == origin) return o.name;
    }
    return kOrigins[0].name;
}

bool commandOriginFromName(const std::string& name, CommandOrigin* out) {
    for (const OriginInfo& o : kOrigins) {
        if (name == o.name) {
            if (out != nullptr) *out = o.origin;
            return true;
        }
    }
    return false;
}

const char* commandStatusName(CommandStatus status) {
    switch (status) {
        case CommandStatus::Accepted:
            return "accepted";
        case CommandStatus::Rejected:
            return "rejected";
        case CommandStatus::PartiallyApplied:
            return "partially_applied";
    }
    return "rejected";
}

ErrorDetail validateCommandRequest(const CommandRequest& request) {
    if (request.schemaVersion < kMinReadableSchemaVersion) {
        return makeError(ErrorDomain::Project, "VERSION_UNSUPPORTED",
                         "命令契约版本过旧，无法解析", "schemaVersion");
    }
    if (request.schemaVersion > kSchemaVersion) {
        return makeError(ErrorDomain::Project, "VERSION_UNSUPPORTED",
                         "命令契约版本高于当前引擎支持范围", "schemaVersion");
    }
    if (!isValidCommandId(request.commandId)) {
        return makeError(ErrorDomain::Project, "INVALID_COMMAND_ID",
                         "commandId 必须为 1..128 个可打印 ASCII 字符（不含空白）",
                         "commandId");
    }
    if (request.projectId == 0) {
        return makeError(ErrorDomain::Project, "INVALID_PROJECT_ID",
                         "命令必须显式指定 projectId，禁止隐式操作当前工程", "projectId");
    }
    if (!isValidCommandType(request.commandType)) {
        return makeError(ErrorDomain::Project, "INVALID_COMMAND_TYPE",
                         "commandType 必须形如 \"clip.move\"（小写字母/数字/_，至少含一个点）",
                         "commandType");
    }
    if (!request.payload.isObject()) {
        return makeError(ErrorDomain::Project, "INVALID_PAYLOAD",
                         "payload 必须是 JSON 对象", "payload");
    }
    // transactionId 允许为空；非空时格式与 commandId 一致。
    if (!request.transactionId.empty() && !isValidCommandId(request.transactionId)) {
        return makeError(ErrorDomain::Project, "INVALID_TRANSACTION_ID",
                         "transactionId 格式非法", "transactionId");
    }
    return ErrorDetail{};
}

ErrorDetail checkRevision(std::uint64_t expectedRevision, std::uint64_t currentRevision) {
    if (expectedRevision == currentRevision) return ErrorDetail{};
    ErrorDetail e = makeError(ErrorDomain::Project, "REVISION_CONFLICT",
                              "工程已被其他操作修改，请刷新后重试", "expectedRevision");
    e.diagnostic = "expected=" + std::to_string(expectedRevision) +
                   " current=" + std::to_string(currentRevision);
    return e;
}

JsonValue commandRequestToJson(const CommandRequest& request) {
    JsonValue o = JsonValue::object();
    o.set("schemaVersion", JsonValue::integer(request.schemaVersion));
    o.set("commandId", JsonValue::string(request.commandId));
    o.set("projectId", JsonValue::integer(static_cast<std::int64_t>(request.projectId)));
    o.set("expectedRevision",
          JsonValue::integer(static_cast<std::int64_t>(request.expectedRevision)));
    o.set("commandType", JsonValue::string(request.commandType));
    o.set("origin", JsonValue::string(commandOriginName(request.origin)));
    if (!request.transactionId.empty()) {
        o.set("transactionId", JsonValue::string(request.transactionId));
    }
    o.set("payload", request.payload);
    return o;
}

bool commandRequestFromJson(const JsonValue& value, CommandRequest* out) {
    if (out == nullptr || !value.isObject()) return false;
    CommandRequest r;
    r.schemaVersion = static_cast<std::int32_t>(
        value.get("schemaVersion").asInteger(kSchemaVersion));
    r.commandId = value.get("commandId").asString();
    r.projectId = static_cast<std::uint64_t>(value.get("projectId").asInteger(0));
    r.expectedRevision =
        static_cast<std::uint64_t>(value.get("expectedRevision").asInteger(0));
    r.commandType = value.get("commandType").asString();
    r.payload = value.get("payload");
    r.origin = CommandOrigin::User;
    const std::string originName = value.get("origin").asString();
    if (!originName.empty() && !commandOriginFromName(originName, &r.origin)) {
        // 未知 origin 不静默接受：来源决定审计语义，猜错比拒绝更危险。
        return false;
    }
    if (value.has("transactionId")) {
        r.transactionId = value.get("transactionId").asString();
    }
    *out = r;
    return true;
}

JsonValue commandResultToJson(const CommandResult& result) {
    JsonValue o = JsonValue::object();
    o.set("status", JsonValue::string(commandStatusName(result.status)));
    o.set("commandId", JsonValue::string(result.commandId));
    o.set("newRevision", JsonValue::integer(static_cast<std::int64_t>(result.newRevision)));
    o.set("changeSetId", JsonValue::string(result.changeSetId));
    o.set("undoAvailable", JsonValue::boolean(result.undoAvailable));
    if (!result.error.isOk()) {
        JsonValue e = JsonValue::object();
        e.set("code", JsonValue::string(result.error.code));
        e.set("message", JsonValue::string(result.error.message));
        if (!result.error.field.empty()) {
            e.set("field", JsonValue::string(result.error.field));
        }
        e.set("recoverable", JsonValue::boolean(result.error.recoverable));
        if (!result.error.diagnostic.empty()) {
            e.set("diagnostic", JsonValue::string(result.error.diagnostic));
        }
        o.set("error", e);
    }
    return o;
}

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
