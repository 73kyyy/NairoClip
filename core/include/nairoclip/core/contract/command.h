#pragma once
// ============================================================================
// 契约层 · 命令信封
// ----------------------------------------------------------------------------
// 依据 03 §4 与 07 §4：命令必须带
//   schemaVersion / commandId / projectId / expectedRevision /
//   commandType / payload / origin / transactionId
//
// 与核心层 Command 的关系：
//   本结构是**跨层信封**，不替代 core::Command（apply/revert）。
//   信封经校验后由门面映射到具体 Command 子类执行。
//   这样 332 个既有 C ABI 与既有命令类都不用改。
// ============================================================================

#include "nairoclip/core/contract/error.h"
#include "nairoclip/core/contract/version.h"
#include "nairoclip/core/json.h"

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {
namespace contract {

/// 命令来源（03 §4 origin 枚举）。
enum class CommandOrigin {
    User = 0,
    Undo,
    Redo,
    Ai,
    Plugin,
    Import,
    Sync,
    System,
};

const char* commandOriginName(CommandOrigin origin);
bool commandOriginFromName(const std::string& name, CommandOrigin* out);

/// 命令执行状态（03 §4 result）。
enum class CommandStatus {
    Accepted = 0,
    Rejected,
    PartiallyApplied,
};

const char* commandStatusName(CommandStatus status);

/// 命令请求信封。
struct CommandRequest {
    std::int32_t schemaVersion = kSchemaVersion;
    std::string commandId;              ///< 一次提交唯一，用于日志关联与幂等去重
    std::uint64_t projectId = 0;        ///< 明确作用域，禁止隐式「当前工程」
    std::uint64_t expectedRevision = 0; ///< 乐观并发控制；0 = 不校验（仅限只读/初始化）
    std::string commandType;            ///< 稳定枚举，如 "clip.move"
    JsonValue payload;                  ///< 版本化参数对象
    CommandOrigin origin = CommandOrigin::User;
    std::string transactionId;          ///< 可选：把多条命令合成一个原子事务
};

/// 命令结果。
struct CommandResult {
    CommandStatus status = CommandStatus::Rejected;
    std::string commandId;
    std::uint64_t newRevision = 0;
    std::string changeSetId;
    bool undoAvailable = false;
    ErrorDetail error;

    bool isAccepted() const { return status == CommandStatus::Accepted; }
};

/// 校验请求信封本身（不执行业务语义）。
/// 失败时返回非空 reason，并给出可直接回传的错误码。
ErrorDetail validateCommandRequest(const CommandRequest& request);

/// 乐观并发：比较期望 revision 与当前 revision。
/// 不一致返回 NC-PROJECT-REVISION_CONFLICT，并带 field="expectedRevision"。
ErrorDetail checkRevision(std::uint64_t expectedRevision, std::uint64_t currentRevision);

/// 序列化 / 反序列化（跨语言统一格式，07 §4 payload 版本化）。
JsonValue commandRequestToJson(const CommandRequest& request);
bool commandRequestFromJson(const JsonValue& value, CommandRequest* out);

JsonValue commandResultToJson(const CommandResult& result);

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
