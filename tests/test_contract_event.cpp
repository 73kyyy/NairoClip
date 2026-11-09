// 契约层 · 事件总线测试（03 §5 / 07 §6）
#include "test_framework.h"

#include "nairoclip/core/contract/event.h"

#include <string>
#include <vector>

using namespace nairoclip::core;
using namespace nairoclip::core::contract;

namespace {

EventEnvelope makeEvent(const std::string& type, std::uint64_t revision,
                        const std::string& id = "evt-1") {
    EventEnvelope e;
    e.schemaVersion = kSchemaVersion;
    e.eventId = id;
    e.eventType = type;
    e.projectId = 3;
    e.revision = revision;
    e.occurredAtMs = 1700000000000LL;
    e.correlationId = "corr-1";
    e.payload = JsonValue::object();
    return e;
}

}  // namespace

NC_TEST(ContractEvent, SubscribeAllReceives) {
    EventBus bus;
    int count = 0;
    const std::uint64_t id = bus.subscribe("", [&count](const EventEnvelope&) { ++count; });
    NC_CHECK(id != 0);
    NC_CHECK_EQ(bus.publish(makeEvent(eventTypeTimelineChanged(), 1)), std::size_t(1));
    NC_CHECK_EQ(count, 1);
}

NC_TEST(ContractEvent, FilterByType) {
    EventBus bus;
    int timeline = 0;
    int errors = 0;
    bus.subscribe(eventTypeTimelineChanged(), [&timeline](const EventEnvelope&) { ++timeline; });
    bus.subscribe(eventTypeEngineError(), [&errors](const EventEnvelope&) { ++errors; });

    bus.publish(makeEvent(eventTypeTimelineChanged(), 1, "e1"));
    bus.publish(makeEvent(eventTypeTimelineChanged(), 2, "e2"));
    bus.publish(makeEvent(eventTypeEngineError(), 3, "e3"));
    NC_CHECK_EQ(timeline, 2);
    NC_CHECK_EQ(errors, 1);
}

NC_TEST(ContractEvent, UnsubscribeStopsDelivery) {
    EventBus bus;
    int count = 0;
    const std::uint64_t id = bus.subscribe("", [&count](const EventEnvelope&) { ++count; });
    bus.publish(makeEvent(eventTypeTimelineChanged(), 1));
    NC_CHECK_EQ(count, 1);
    NC_CHECK(bus.unsubscribe(id));
    NC_CHECK(!bus.unsubscribe(id));  // 重复退订不崩溃
    bus.publish(makeEvent(eventTypeTimelineChanged(), 2));
    NC_CHECK_EQ(count, 1);
    NC_CHECK_EQ(bus.subscriberCount(), std::size_t(0));
}

NC_TEST(ContractEvent, InvalidEventNotDelivered) {
    EventBus bus;
    int count = 0;
    bus.subscribe("", [&count](const EventEnvelope&) { ++count; });

    EventEnvelope bad = makeEvent("not.a.type", 1);
    NC_CHECK_EQ(bus.publish(bad), std::size_t(0));
    NC_CHECK_EQ(count, 0);

    EventEnvelope noProject = makeEvent(eventTypeTimelineChanged(), 2);
    noProject.projectId = 0;
    NC_CHECK_EQ(bus.publish(noProject), std::size_t(0));
    NC_CHECK_EQ(count, 0);
}

NC_TEST(ContractEvent, EnvelopeValidationRules) {
    NC_CHECK(validateEventEnvelope(makeEvent(eventTypePreviewReady(), 1)).empty());

    EventEnvelope e = makeEvent(eventTypePreviewReady(), 1);
    e.eventId.clear();
    NC_CHECK(!validateEventEnvelope(e).empty());

    e = makeEvent(eventTypePreviewReady(), 1);
    e.eventType = "";
    NC_CHECK(!validateEventEnvelope(e).empty());
}

NC_TEST(ContractEvent, RevisionGapDetected) {
    EventBus bus;
    bus.subscribe("", [](const EventEnvelope&) {});
    NC_CHECK(!bus.detectRevisionGap(1));  // 首个事件不算跳跃
    bus.publish(makeEvent(eventTypeTimelineChanged(), 1));
    NC_CHECK_EQ(bus.lastRevision(), std::uint64_t(1));
    NC_CHECK(!bus.detectRevisionGap(2));
    NC_CHECK(bus.detectRevisionGap(3));  // 1 → 3 跳过了 2
}

NC_TEST(ContractEvent, DuplicateRevisionIsNotGap) {
    EventBus bus;
    bus.publish(makeEvent(eventTypeTimelineChanged(), 5, "e1"));
    // 重复投递（同一 revision）由 UI 幂等处理，不属于丢事件。
    NC_CHECK(!bus.detectRevisionGap(5));
    NC_CHECK(!bus.detectRevisionGap(6));
}

NC_TEST(ContractEvent, ResetTrackingClearsGapBaseline) {
    EventBus bus;
    bus.publish(makeEvent(eventTypeTimelineChanged(), 10));
    NC_CHECK(bus.detectRevisionGap(20));
    bus.resetRevisionTracking();
    NC_CHECK(!bus.detectRevisionGap(20));  // 重置后视为首个事件
}

NC_TEST(ContractEvent, CoalescingKeepsLatestValue) {
    EventBus bus;
    bus.publishCoalesced(makeEvent(eventTypePlaybackPositionChanged(), 1, "a"));
    bus.publishCoalesced(makeEvent(eventTypePlaybackPositionChanged(), 2, "b"));
    bus.publishCoalesced(makeEvent(eventTypePlaybackPositionChanged(), 3, "c"));
    const std::vector<EventEnvelope> out = bus.drainCoalesced();
    NC_CHECK_EQ(out.size(), std::size_t(1));
    NC_CHECK_EQ(out[0].eventId, std::string("c"));
    NC_CHECK_EQ(out[0].revision, std::uint64_t(3));
}

NC_TEST(ContractEvent, CoalescingPreservesFirstSeenOrder) {
    EventBus bus;
    bus.publishCoalesced(makeEvent(eventTypeExportProgressChanged(), 1, "x1"));
    bus.publishCoalesced(makeEvent(eventTypePlaybackPositionChanged(), 2, "y1"));
    bus.publishCoalesced(makeEvent(eventTypeExportProgressChanged(), 3, "x2"));
    const std::vector<EventEnvelope> out = bus.drainCoalesced();
    NC_CHECK_EQ(out.size(), std::size_t(2));
    NC_CHECK_EQ(out[0].eventType, std::string(eventTypeExportProgressChanged()));
    NC_CHECK_EQ(out[0].eventId, std::string("x2"));
    NC_CHECK_EQ(out[1].eventType, std::string(eventTypePlaybackPositionChanged()));
}

NC_TEST(ContractEvent, DrainTwiceIsEmpty) {
    EventBus bus;
    bus.publishCoalesced(makeEvent(eventTypeTaskProgress(), 1));
    NC_CHECK_EQ(bus.drainCoalesced().size(), std::size_t(1));
    NC_CHECK_EQ(bus.drainCoalesced().size(), std::size_t(0));
}

NC_TEST(ContractEvent, UnknownTypeNotInWhitelist) {
    NC_CHECK(isKnownEventType(eventTypeTimelineChanged()));
    NC_CHECK(isKnownEventType("export.progress_changed"));
    NC_CHECK(!isKnownEventType("bogus"));
    NC_CHECK(!isKnownEventType(""));
}
