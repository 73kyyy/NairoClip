#pragma once
// ============================================================================
// 契约层 · 版本常量与跨语言结构体头
// ----------------------------------------------------------------------------
// 依据：
//   · 02 §4「结构体必须带 size/version 或使用明确版本化消息」
//   · 07 §4「schemaVersion：命令契约版本」
//   · 07 §9「Project schemaVersion 与子 schema 版本分离管理」
//
// 为什么单独一个头：版本号被 command/event/task 三个契约共用，
// 分散定义必然出现「命令是 1、事件是 2」的不一致。
// ============================================================================

#include <cstdint>

namespace nairoclip {
namespace core {
namespace contract {

/// 当前契约 schema 版本。任何不兼容变更必须递增，并补迁移函数。
inline constexpr std::int32_t kSchemaVersion = 1;

/// 最低可读版本。低于此值一律拒绝（NC-PROJECT-VERSION_UNSUPPORTED）。
inline constexpr std::int32_t kMinReadableSchemaVersion = 1;

/// 跨语言结构体统一头。C ABI 侧结构体首字段必须是它，
/// 便于未来追加字段时按 size 判断调用方版本。
struct StructHeader {
    std::uint32_t size = 0;
    std::uint32_t version = 0;
};

inline StructHeader makeHeader(std::uint32_t size,
                              std::uint32_t version = static_cast<std::uint32_t>(kSchemaVersion)) {
    StructHeader h;
    h.size = size;
    h.version = version;
    return h;
}

/// 头是否可用：至少能容纳头本身，且版本不低于最低可读版本。
inline bool headerCompatible(const StructHeader& h) {
    return h.size >= sizeof(StructHeader) &&
           h.version >= static_cast<std::uint32_t>(kMinReadableSchemaVersion);
}

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
