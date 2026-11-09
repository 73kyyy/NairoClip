#pragma once
// ============================================================================
// 契约层 · 事件模型
// ----------------------------------------------------------------------------
// 依据 03 §5 与 07 §6：事件必须有 envelope（schemaVersion / eventId /
// eventType / projectId / revision / occurredAt / correlationId / payload），
// 且「事件不是唯一持久化来源」——丢事件后必须能靠 snapshot 重建。
//
// 本类只解决「进程内分发 + revision 连续性判定 + 高频事件合并」，
// 不做跨进程网络传输（那是 SyncService 的职责）。
// ============================================================================

#include "nairoclip/core/contract/version.h"
#include "nairoclip/core/json.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {
namespace contract {

/// 事件类型白名单（07 §6 eventType 稳定枚举）。
const char* eventTypeProjectChanged();
const char* eventTypeTimelineChanged();
const char* eventTypePlaybackPositionChanged();
const char* eventTypePreviewReady();
const char* eventTypeMediaImported();
const char* eventTypeExportProgressChanged();
const char* eventTypeEngineError();
const char* eventTypeTaskProgress();

bool isKnownEventType(const std::string& type);

/// 事件信封。
struct EventEnvelope {
    std::int32_t schemaVersion = kSchemaVersion;
    std::string eventId;
    std::string eventType;
    std::uint64_t projectId = 0;
    std::uint64_t revision = 0;
    std::int64_t occurredAtMs = 0;
    std::string correlationId;  ///< 串起 UI→Editor API→桥接→核心的关联 ID
    JsonValue payload;

    bool isValid() const;
};

/// 校验信封（类型白名单、ID 非空、projectId 非 0）。
/// 失败时返回非空 reason。
std::string validateEventEnvelope(const EventEnvelope& envelope);

/// 进程内事件总线。
///
/// 线程约定：与 TimelineEditor 一致，**非线程安全**，只在 Engine 线程调用。
class EventBus {
public:
    using Handler = std::function<void(const EventEnvelope&)>;

    /// 订阅。filterType 为空 = 订阅全部；否则只收该类型。
    /// 返回订阅 ID，用于 unsubscribe。
    std::uint64_t subscribe(std::string filterType, Handler handler);

    /// 退订。ID 不存在返回 false（重复退订不应崩溃）。
    bool unsubscribe(std::uint64_t subscriptionId);

    std::size_t subscriberCount() const { return subscribers_.size(); }

    /// 立即递送给匹配的订阅者，返回实际投递次数。
    /// 会更新 revision 追踪（取最大值）。
    std::size_t publish(const EventEnvelope& envelope);

    /// 高频事件合并：同一 eventType 只保留最新一条。
    /// 适用播放头位置、导出进度这类「只关心最新值」的事件（03 §5）。
    /// **工程结构类事件不得走这里**——丢弃中间态会让 UI 与核心不一致。
    void publishCoalesced(const EventEnvelope& envelope);

    /// 取走并清空合并缓冲（按首次出现顺序）。
    std::vector<EventEnvelope> drainCoalesced();

    /// revision 是否跳跃（> lastRevision + 1）。
    /// 为 true 时调用方必须重新拉 snapshot（03 §5 / 07 §6）。
    bool detectRevisionGap(std::uint64_t incomingRevision) const;

    std::uint64_t lastRevision() const { return lastRevision_; }

    /// 进程恢复/切换工程时重置追踪，下一个事件不算跳跃。
    void resetRevisionTracking();

    /// 清空订阅者与合并缓冲（销毁前调用；不重置 revision 追踪）。
    void clearSubscribers();

private:
    struct Subscription {
        std::uint64_t id = 0;
        std::string filter;
        Handler handler;
    };

    std::vector<Subscription> subscribers_;
    std::uint64_t nextSubscriptionId_ = 1;

    std::map<std::string, EventEnvelope> coalesced_;
    std::vector<std::string> coalescedOrder_;

    std::uint64_t lastRevision_ = 0;
    bool hasRevision_ = false;
};

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
