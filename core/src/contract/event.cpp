#include "nairoclip/core/contract/event.h"

#include <cstring>

namespace nairoclip {
namespace core {
namespace contract {
namespace {

const char* const kEventTypes[] = {
    "project.changed",   "timeline.changed", "playback.position_changed",
    "preview.ready",     "media.imported",   "export.progress_changed",
    "engine.error",      "task.progress",
};

constexpr std::size_t kEventTypeCount = sizeof(kEventTypes) / sizeof(kEventTypes[0]);

bool isPrintableId(const std::string& id, std::size_t maxLen) {
    if (id.empty() || id.size() > maxLen) return false;
    for (unsigned char c : id) {
        if (c < 0x21 || c > 0x7E) return false;
    }
    return true;
}

}  // namespace

const char* eventTypeProjectChanged() { return kEventTypes[0]; }
const char* eventTypeTimelineChanged() { return kEventTypes[1]; }
const char* eventTypePlaybackPositionChanged() { return kEventTypes[2]; }
const char* eventTypePreviewReady() { return kEventTypes[3]; }
const char* eventTypeMediaImported() { return kEventTypes[4]; }
const char* eventTypeExportProgressChanged() { return kEventTypes[5]; }
const char* eventTypeEngineError() { return kEventTypes[6]; }
const char* eventTypeTaskProgress() { return kEventTypes[7]; }

bool isKnownEventType(const std::string& type) {
    for (std::size_t i = 0; i < kEventTypeCount; ++i) {
        if (type == kEventTypes[i]) return true;
    }
    return false;
}

bool EventEnvelope::isValid() const { return validateEventEnvelope(*this).empty(); }

std::string validateEventEnvelope(const EventEnvelope& envelope) {
    if (envelope.schemaVersion < kMinReadableSchemaVersion) return "schemaVersion 过旧";
    if (!isPrintableId(envelope.eventId, 128)) return "eventId 非法（1..128 可打印 ASCII）";
    if (!isKnownEventType(envelope.eventType)) return "eventType 不在白名单：" + envelope.eventType;
    if (envelope.projectId == 0) return "projectId 必须非 0";
    if (!envelope.correlationId.empty() && !isPrintableId(envelope.correlationId, 128)) {
        return "correlationId 非法";
    }
    // 事件里不得夹带二进制/指针；JSON 载荷天然满足，这里只拦超大对象。
    const std::string encoded = toJsonString(envelope.payload);
    if (encoded.size() > 1024 * 1024) return "payload 超过 1MB 上限";
    return std::string();
}

std::uint64_t EventBus::subscribe(std::string filterType, Handler handler) {
    if (!handler) return 0;
    Subscription s;
    s.id = nextSubscriptionId_++;
    s.filter = std::move(filterType);
    s.handler = std::move(handler);
    subscribers_.push_back(std::move(s));
    return subscribers_.back().id;
}

bool EventBus::unsubscribe(std::uint64_t subscriptionId) {
    for (std::size_t i = 0; i < subscribers_.size(); ++i) {
        if (subscribers_[i].id == subscriptionId) {
            subscribers_.erase(subscribers_.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

std::size_t EventBus::publish(const EventEnvelope& envelope) {
    if (!validateEventEnvelope(envelope).empty()) {
        // 非法事件不得进入总线：宁可丢弃并让调用方在源头报错，
        // 也不能把半成品事件推给 UI 造成状态漂移。
        return 0;
    }
    if (!hasRevision_ || envelope.revision > lastRevision_) {
        lastRevision_ = envelope.revision;
        hasRevision_ = true;
    }
    std::size_t delivered = 0;
    // 先复制一份订阅表？不：本类是单线程契约，但允许处理器内部退订。
    // 用索引遍历并在删除时回退一步，避免迭代器失效导致崩溃。
    for (std::size_t i = 0; i < subscribers_.size();) {
        const Subscription& s = subscribers_[i];
        const bool match = s.filter.empty() || s.filter == envelope.eventType;
        if (match && s.handler) {
            s.handler(envelope);
            ++delivered;
        }
        ++i;
    }
    return delivered;
}

void EventBus::publishCoalesced(const EventEnvelope& envelope) {
    if (!validateEventEnvelope(envelope).empty()) return;
    if (coalesced_.find(envelope.eventType) == coalesced_.end()) {
        coalescedOrder_.push_back(envelope.eventType);
    }
    coalesced_[envelope.eventType] = envelope;
}

std::vector<EventEnvelope> EventBus::drainCoalesced() {
    std::vector<EventEnvelope> out;
    out.reserve(coalescedOrder_.size());
    for (const std::string& type : coalescedOrder_) {
        auto it = coalesced_.find(type);
        if (it != coalesced_.end()) out.push_back(it->second);
    }
    coalesced_.clear();
    coalescedOrder_.clear();
    return out;
}

bool EventBus::detectRevisionGap(std::uint64_t incomingRevision) const {
    if (!hasRevision_) return false;  // 首个事件不可能是跳跃
    // 相等 = 重复投递（UI 需幂等），不算跳跃；
    // 只有「跳过至少一个 revision」才是丢事件。
    return incomingRevision > lastRevision_ + 1;
}

void EventBus::resetRevisionTracking() {
    lastRevision_ = 0;
    hasRevision_ = false;
}

void EventBus::clearSubscribers() {
    subscribers_.clear();
    coalesced_.clear();
    coalescedOrder_.clear();
}

}  // namespace contract
}  // namespace core
}  // namespace nairoclip
