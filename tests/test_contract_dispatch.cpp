// 契约层 · 命令分发器纵向切片测试
// 覆盖：信封 → revision 并发 → 真实编辑调用 → 事件发布 → 结果封装
#include "test_framework.h"

#include "nairoclip/core/contract/dispatch.h"

#include <string>

using namespace nairoclip::core;
using namespace nairoclip::core::contract;

namespace {

struct Fixture {
    Project project;
    TimelineEditor editor{project};
    EventBus bus;
    CommandDispatcher dispatcher{editor, bus};
    SequenceId seq;
    TrackId track;
    ClipId clip;
    int eventCount = 0;

    Fixture() {
        Sequence s;
        s.id = SequenceId{project.ids.next()};
        project.sequences.push_back(s);
        seq = s.id;
        editor.addTrack(seq, TrackType::Video, "V1", &track);

        Clip c;
        c.start = Rational::make(0);
        c.duration = Rational::make(2);
        c.sourceIn = Rational::make(0);
        ClipId out;
        editor.insertClip(seq, track, c, &out);
        clip = out;

        bus.subscribe(eventTypeTimelineChanged(),
                      [this](const EventEnvelope&) { ++eventCount; });
    }

    CommandRequest request(const std::string& type, const std::string& id) {
        CommandRequest r;
        r.commandId = id;
        r.projectId = 1;
        r.expectedRevision = 0;  // 默认不校验，个别用例显式设置
        r.commandType = type;
        r.origin = CommandOrigin::User;
        JsonValue p = JsonValue::object();
        p.set("sequenceId", JsonValue::integer(static_cast<std::int64_t>(seq.value)));
        p.set("trackId", JsonValue::integer(static_cast<std::int64_t>(track.value)));
        p.set("clipId", JsonValue::integer(static_cast<std::int64_t>(clip.value)));
        r.payload = p;
        return r;
    }

    void setTime(CommandRequest* r, const char* key, std::int64_t ticks, std::int64_t den) {
        JsonValue t = JsonValue::object();
        t.set("ticks", JsonValue::integer(ticks));
        JsonValue tb = JsonValue::object();
        tb.set("num", JsonValue::integer(1));
        tb.set("den", JsonValue::integer(den));
        t.set("timebase", tb);
        r->payload.set(key, t);
    }

    const Clip* findClip() const {
        return findClipIn(project, seq, track, clip);
    }

    static const Clip* findClipIn(const Project& p, SequenceId s, TrackId t, ClipId c) {
        // 必须写全限定名：类内 findClip() 会隐藏同名的自由函数。
        return ::nairoclip::core::findClip(const_cast<Project&>(p), s, t, c);
    }
};

}  // namespace

NC_TEST(ContractDispatch, MoveClipAccepted) {
    Fixture f;
    CommandRequest r = f.request("clip.move", "cmd-move-1");
    f.setTime(&r, "targetStart", 3, 1);  // 3 秒

    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(res.isAccepted());
    NC_CHECK_EQ(res.commandId, std::string("cmd-move-1"));
    NC_CHECK_EQ(res.changeSetId, std::string("cs-cmd-move-1"));
    NC_CHECK(res.undoAvailable);
    NC_CHECK(res.newRevision > 0);
    NC_CHECK_EQ(f.findClip()->start.toDouble(), 3.0);
}

NC_TEST(ContractDispatch, MovePublishesTimelineChanged) {
    Fixture f;
    CommandRequest r = f.request("clip.move", "cmd-move-2");
    f.setTime(&r, "targetStart", 5, 1);
    f.dispatcher.apply(r);
    NC_CHECK_EQ(f.eventCount, 1);
    const EventEnvelope& e = f.dispatcher.lastEvent();
    NC_CHECK_EQ(e.eventType, std::string(eventTypeTimelineChanged()));
    NC_CHECK_EQ(e.correlationId, std::string("cmd-move-2"));
    NC_CHECK_EQ(e.projectId, std::uint64_t(1));
    NC_CHECK_EQ(e.revision, f.dispatcher.currentRevision());
}

NC_TEST(ContractDispatch, InvalidEnvelopeRejectedWithoutSideEffect) {
    Fixture f;
    CommandRequest r = f.request("clip.move", "");  // 空 commandId
    f.setTime(&r, "targetStart", 3, 1);
    const std::uint64_t before = f.dispatcher.currentRevision();

    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(!res.isAccepted());
    NC_CHECK_EQ(res.error.code, std::string("NC-PROJECT-INVALID_COMMAND_ID"));
    NC_CHECK_EQ(f.dispatcher.currentRevision(), before);
    NC_CHECK_EQ(f.eventCount, 0);
}

NC_TEST(ContractDispatch, RevisionConflictRejected) {
    Fixture f;
    CommandRequest r = f.request("clip.move", "cmd-move-3");
    f.setTime(&r, "targetStart", 1, 1);
    r.expectedRevision = 999;  // 过期
    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(!res.isAccepted());
    NC_CHECK_EQ(res.error.code, std::string("NC-PROJECT-REVISION_CONFLICT"));
    NC_CHECK_EQ(f.eventCount, 0);
}

NC_TEST(ContractDispatch, MatchingRevisionAccepted) {
    Fixture f;
    CommandRequest r = f.request("clip.move", "cmd-move-4");
    f.setTime(&r, "targetStart", 1, 1);
    r.expectedRevision = f.dispatcher.currentRevision();
    NC_CHECK(f.dispatcher.apply(r).isAccepted());
}

NC_TEST(ContractDispatch, UnsupportedTypeRejectedExplicitly) {
    Fixture f;
    CommandRequest r = f.request("clip.teleport", "cmd-x");
    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(!res.isAccepted());
    NC_CHECK_EQ(res.error.code, std::string("NC-TIMELINE-UNSUPPORTED_COMMAND"));
    NC_CHECK_EQ(f.eventCount, 0);
}

NC_TEST(ContractDispatch, MissingArgsRejected) {
    Fixture f;
    CommandRequest r = f.request("clip.move", "cmd-move-5");
    // 不设置 targetStart
    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(!res.isAccepted());
    NC_CHECK_EQ(res.error.code, std::string("NC-TIMELINE-INVALID_ARGUMENT"));
    NC_CHECK_EQ(res.error.field, std::string("payload.targetStart"));
}

NC_TEST(ContractDispatch, BadTimebaseRejected) {
    Fixture f;
    CommandRequest r = f.request("clip.move", "cmd-move-6");
    f.setTime(&r, "targetStart", 5, 0);  // den=0 非法
    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(!res.isAccepted());
    NC_CHECK_EQ(res.error.code, std::string("NC-TIMELINE-INVALID_ARGUMENT"));
}

NC_TEST(ContractDispatch, SplitProducesTwoClips) {
    Fixture f;
    CommandRequest r = f.request("clip.split", "cmd-split-1");
    f.setTime(&r, "at", 1, 1);  // 在 1 秒处切分
    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(res.isAccepted());
    const Sequence& s = f.project.sequences[0];
    NC_CHECK_EQ(s.tracks[0].clips.size(), std::size_t(2));
}

NC_TEST(ContractDispatch, RemoveDropsClip) {
    Fixture f;
    CommandRequest r = f.request("clip.remove", "cmd-rm-1");
    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(res.isAccepted());
    NC_CHECK_EQ(f.project.sequences[0].tracks[0].clips.size(), std::size_t(0));
}

NC_TEST(ContractDispatch, MoveRejectedByLockedTrack) {
    Fixture f;
    f.editor.setTrackLocked(f.seq, f.track, true);
    CommandRequest r = f.request("clip.move", "cmd-move-7");
    f.setTime(&r, "targetStart", 2, 1);
    const CommandResult res = f.dispatcher.apply(r);
    NC_CHECK(!res.isAccepted());
    NC_CHECK_EQ(res.error.code, std::string("NC-TIMELINE-TRACK_LOCKED"));
    NC_CHECK_EQ(f.eventCount, 0);
}

NC_TEST(ContractDispatch, RevisionAdvancesPerSuccessfulCommand) {
    Fixture f;
    const std::uint64_t r0 = f.dispatcher.currentRevision();
    CommandRequest a = f.request("clip.move", "cmd-a");
    f.setTime(&a, "targetStart", 1, 1);
    f.dispatcher.apply(a);
    const std::uint64_t r1 = f.dispatcher.currentRevision();
    NC_CHECK(r1 > r0);

    CommandRequest b = f.request("clip.move", "cmd-b");
    f.setTime(&b, "targetStart", 2, 1);
    f.dispatcher.apply(b);
    NC_CHECK(f.dispatcher.currentRevision() > r1);
}

NC_TEST(ContractDispatch, SupportedTypeTable) {
    NC_CHECK(isSupportedCommandType("clip.move"));
    NC_CHECK(isSupportedCommandType("clip.split"));
    NC_CHECK(isSupportedCommandType("clip.remove"));
    NC_CHECK(!isSupportedCommandType("clip.teleport"));
    NC_CHECK_EQ(supportedCommandTypeCount(), std::size_t(3));
}
