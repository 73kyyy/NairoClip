#pragma once
// ============================================================================
// 契约层 · 命令分发器（P1 端到端纵向切片）
// ----------------------------------------------------------------------------
// 职责：把 CommandRequest 信封翻译成对 TimelineEditor 的真实编辑调用，
//       并产出 CommandResult + 发布 timeline.changed 事件。
//
// 为什么单独一层而不是塞进 TimelineEditor：
//   · TimelineEditor 是「编辑语义」实现，不认识 JSON 信封与事件总线；
//   · 契约层负责「协议」：校验、revision 并发、事件、结果封装；
//   · 两者解耦后，既有 59 个门面方法保持零改动。
//
// 当前支持的命令（第一切片，按需扩展）：
//   clip.move    {sequenceId, trackId, clipId, targetStart{ticks,timebase{num,den}}}
//   clip.split   {sequenceId, trackId, clipId, at{...}}
//   clip.remove  {sequenceId, trackId, clipId}
// 其余类型返回 NC-TIMELINE-UNSUPPORTED_COMMAND（**不静默成功**）。
// ============================================================================

#include "nairoclip/core/contract/command.h"
#include "nairoclip/core/contract/event.h"
#include "nairoclip/core/editor.h"

#include <cstdint>

namespace nairoclip {
namespace core {
namespace contract {

class CommandDispatcher {
public:
    CommandDispatcher(TimelineEditor& editor, EventBus& bus);

    /// 执行一条命令。无论成功失败都返回结构化结果（不抛异常）。
    CommandResult apply(const CommandRequest& request);

    /// 当前工程 revision（直接取撤销栈版本：任何内容变化都会递增）。
    std::uint64_t currentRevision() const;

    /// 最近一次成功命令发布的事件（便于测试与上层订阅验证）。
    const EventEnvelope& lastEvent() const { return lastEvent_; }

private:
    TimelineEditor& editor_;
    EventBus& bus_;
    EventEnvelope lastEvent_;
};

/// 已支持的命令类型列表（用于能力查询与文档生成）。
bool isSupportedCommandType(const std::string& type);
std::size_t supportedCommandTypeCount();

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
