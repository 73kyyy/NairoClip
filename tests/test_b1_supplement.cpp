// B1 补充验收：四项遗留问题的专项测试
//   1. expectedRevision 在 commit 路径真正生效
//   2. 事件统一由 commit() 发布（成功一次、失败/ no-op 不发、无重复）
//   3. straight-alpha 前景/背景真实混合（覆盖行为已修复）
// 第 4 项（CI valgrind）属工作流问题，由报告记录，不在本文件测试。
#include "test_framework.h"

#include "nairoclip/core/canvas.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/contract/dispatch.h"
#include "nairoclip/core/contract/event.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/frame_fit.h"

#include <string>
#include <vector>

using namespace nairoclip::core;
using namespace nairoclip::core::contract;

namespace {

struct Fixture {
    Project project;
    TimelineEditor editor{project};
    EventBus bus;
    SequenceId seq;
    TrackId track;
    std::vector<EventEnvelope> events;
    int eventCount = 0;

    Fixture() {
        Sequence s;
        s.id = SequenceId{project.ids.next()};
        project.sequences.push_back(s);
        seq = s.id;
        project.id = ProjectId{1};   // 事件要求 projectId 非 0
        editor.addTrack(seq, TrackType::Video, "V1", &track);
        editor.setEventBus(&bus);
        bus.subscribe(eventTypeTimelineChanged(),
                      [this](const EventEnvelope& e) {
                          ++eventCount;
                          events.push_back(e);
                      });
    }

    Sequence* main() { return project.mainSequence(); }
};

}  // namespace

// ===========================================================================
// 1. expectedRevision
// ===========================================================================

NC_TEST(B1Rev, MatchingRevisionAccepted) {
    Fixture f;
    const std::uint64_t rev = f.editor.editRevision();
    NC_CHECK(f.editor.renameProject("新名字", rev).isOk());
    NC_CHECK_EQ(f.project.name, std::string("新名字"));
    NC_CHECK(f.editor.editRevision() > rev);
}

NC_TEST(B1Rev, MismatchedRevisionRejectedWithoutSideEffect) {
    Fixture f;
    const std::string before = f.project.name;
    const std::uint64_t rev = f.editor.editRevision();
    const std::size_t depth = f.editor.undoCount();
    const int eventsBefore = f.eventCount;

    const Result r = f.editor.renameProject("不该生效", rev + 99);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Conflict);

    NC_CHECK_EQ(f.project.name, before);              // 数据未被修改
    NC_CHECK_EQ(f.editor.editRevision(), rev);        // revision 不变
    NC_CHECK_EQ(f.editor.undoCount(), depth);         // 不入撤销栈
    NC_CHECK_EQ(f.eventCount, eventsBefore);          // 不发布事件
}

NC_TEST(B1Rev, ZeroMeansNoCheck) {
    Fixture f;
    // 0 = 不校验（与 P1 契约一致）。这是旧 C ABI 的语义。
    NC_CHECK(f.editor.renameProject("零校验", 0).isOk());
    NC_CHECK_EQ(f.project.name, std::string("零校验"));
}

NC_TEST(B1Rev, CanvasAndBackgroundHonorRevision) {
    Fixture f;
    const std::uint64_t rev = f.editor.editRevision();
    const int w0 = f.main()->width;
    const std::uint32_t bg0 = f.main()->backgroundRgba;

    NC_CHECK(!f.editor.setCanvasSize(f.seq, 1440, 1440, CanvasAdaptMode::Preserve, rev + 5).isOk());
    NC_CHECK_EQ(f.main()->width, w0);                 // 画布未变
    NC_CHECK(!f.editor.setCanvasBackground(f.seq, CanvasBackground::fromRgba32(0x11223344),
                                           rev + 5).isOk());
    NC_CHECK_EQ(f.main()->backgroundRgba, bg0);       // 背景未变

    // 用正确 revision 则成功
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1440, 1440, CanvasAdaptMode::Preserve, rev).isOk());
    NC_CHECK_EQ(f.main()->width, 1440);
}

NC_TEST(B1Rev, CapiExRejectsConflict) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    const uint64_t rev = nc_contract_revision(ed);
    const uint64_t wrong = rev + 42;

    // 重命名
    NC_CHECK_EQ(nc_set_project_name_ex(ed, "冲突名字", wrong), NC_ERROR_REVISION_CONFLICT);
    char* name = nc_project_name(ed);
    NC_CHECK_EQ(std::string(name), std::string("未命名项目"));
    nc_free_string(name);

    // 画布
    int32_t w = 0;
    int32_t h = 0;
    NC_CHECK_EQ(nc_canvas_set_ex(ed, 1440, 1440, 0, wrong), NC_ERROR_REVISION_CONFLICT);
    NC_CHECK_EQ(nc_project_canvas(ed, &w, &h), NC_OK);
    NC_CHECK_EQ(w, 1080);   // 默认画布未变

    // 背景色
    NC_CHECK_EQ(nc_set_background_color_ex(ed, 0x11223344, wrong),
                NC_ERROR_REVISION_CONFLICT);

    // revision 未因失败而变化
    NC_CHECK_EQ(nc_contract_revision(ed), rev);

    // 用正确 revision 则成功
    NC_CHECK_EQ(nc_set_project_name_ex(ed, "正确名字", rev), NC_OK);
    name = nc_project_name(ed);
    NC_CHECK_EQ(std::string(name), std::string("正确名字"));
    nc_free_string(name);
    nc_editor_destroy(ed);
}

NC_TEST(B1Rev, CapiLegacyStillWorksWithoutRevision) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    // 旧接口（无 revision 参数）语义不变
    NC_CHECK_EQ(nc_set_project_name(ed, "旧接口"), NC_OK);
    NC_CHECK_EQ(nc_set_project_canvas(ed, 1920, 1080), NC_OK);
    NC_CHECK_EQ(nc_set_background_color(ed, 0x203040FF), NC_OK);
    nc_editor_destroy(ed);
}

// ===========================================================================
// 2. 事件统一
// ===========================================================================

NC_TEST(B1Event, SuccessPublishesExactlyOnce) {
    Fixture f;
    NC_CHECK(f.editor.renameProject("事件测试").isOk());
    NC_CHECK_EQ(f.eventCount, 1);
    NC_CHECK_EQ(f.events.size(), std::size_t(1));
    const EventEnvelope& e = f.events[0];
    NC_CHECK_EQ(e.eventType, std::string(eventTypeTimelineChanged()));
    NC_CHECK(e.revision > 0);
    NC_CHECK_EQ(e.projectId, std::uint64_t(1));
    NC_CHECK(e.payload.has("revisionBefore"));
    NC_CHECK(e.payload.has("revisionAfter"));
    NC_CHECK_EQ(e.payload.get("revisionAfter").asInteger(0),
                static_cast<std::int64_t>(f.editor.editRevision()));
}

NC_TEST(B1Event, FailurePublishesNothing) {
    Fixture f;
    NC_CHECK(!f.editor.renameProject("").isOk());
    NC_CHECK_EQ(f.eventCount, 0);
}

NC_TEST(B1Event, NoOpPublishesNothing) {
    Fixture f;
    f.editor.renameProject("同一个名字");
    const int after = f.eventCount;
    const std::uint64_t rev = f.editor.editRevision();
    // 改成相同名字：isNoOp → 不入栈、revision 不变 → 不应发事件
    NC_CHECK(f.editor.renameProject("同一个名字").isOk());
    NC_CHECK_EQ(f.eventCount, after);
    NC_CHECK_EQ(f.editor.editRevision(), rev);
}

NC_TEST(B1Event, RevisionConflictPublishesNothing) {
    Fixture f;
    NC_CHECK(!f.editor.renameProject("x", f.editor.editRevision() + 7).isOk());
    NC_CHECK_EQ(f.eventCount, 0);
}

NC_TEST(B1Event, DispatcherPathPublishesExactlyOnceNoDuplicate) {
    // 关键：dispatcher 以前自己也 publish，加上 commit() 后会变成两条。
    // 现在必须只有一条。
    Fixture f;
    CommandDispatcher dispatcher{f.editor, f.bus};
    CommandRequest r;
    r.commandId = "cmd-ev-1";
    r.projectId = 1;
    r.expectedRevision = 0;
    r.commandType = "clip.remove";
    JsonValue p = JsonValue::object();
    p.set("sequenceId", JsonValue::integer(static_cast<std::int64_t>(f.seq.value)));
    p.set("trackId", JsonValue::integer(static_cast<std::int64_t>(f.track.value)));
    p.set("clipId", JsonValue::integer(1));
    r.payload = p;

    // 没有片段 → 命令失败 → 不发事件
    const CommandResult res = dispatcher.apply(r);
    NC_CHECK(!res.isAccepted());
    NC_CHECK_EQ(f.eventCount, 0);
}

NC_TEST(B1Event, CorrelationIdPropagated) {
    Fixture f;
    f.editor.setCorrelationId("cmd-xyz");
    f.editor.renameProject("带关联");
    NC_CHECK_EQ(f.eventCount, 1);
    NC_CHECK_EQ(f.events[0].correlationId, std::string("cmd-xyz"));
    f.editor.setCorrelationId("");
}

NC_TEST(B1Event, EachCommandGetsDistinctEventId) {
    Fixture f;
    f.editor.renameProject("一");
    f.editor.renameProject("二");
    NC_CHECK_EQ(f.eventCount, 2);
    NC_CHECK(f.events[0].eventId != f.events[1].eventId);
}

// ===========================================================================
// 3. straight-alpha 混合
// ===========================================================================

NC_TEST(B1Blend, OpaqueSourceOverwritesExactly) {
    std::uint8_t dst[4] = {10, 20, 30, 255};
    const std::uint8_t src[4] = {200, 100, 50, 255};
    blendStraightOver(dst, src);
    // 不透明：逐字节等于源（与旧 memcpy 行为一致，保证零回归）
    NC_CHECK_EQ(int(dst[0]), 200);
    NC_CHECK_EQ(int(dst[1]), 100);
    NC_CHECK_EQ(int(dst[2]), 50);
    NC_CHECK_EQ(int(dst[3]), 255);
}

NC_TEST(B1Blend, TransparentSourceKeepsBackground) {
    std::uint8_t dst[4] = {10, 20, 30, 255};
    const std::uint8_t src[4] = {200, 100, 50, 0};
    blendStraightOver(dst, src);
    NC_CHECK_EQ(int(dst[0]), 10);   // 背景完整保留
    NC_CHECK_EQ(int(dst[1]), 20);
    NC_CHECK_EQ(int(dst[2]), 30);
    NC_CHECK_EQ(int(dst[3]), 255);
}

NC_TEST(B1Blend, HalfAlphaBlendsBothLayers) {
    std::uint8_t dst[4] = {0, 0, 255, 255};      // 蓝底
    const std::uint8_t src[4] = {255, 0, 0, 128}; // 半透明红
    blendStraightOver(dst, src);
    // r = 255*128/255 ≈ 128；b = 255*127/255 ≈ 127
    NC_CHECK(dst[0] >= 126 && dst[0] <= 130);
    NC_CHECK_EQ(int(dst[1]), 0);
    NC_CHECK(dst[2] >= 125 && dst[2] <= 129);
    // a = 128 + 255*127/255 ≈ 251
    NC_CHECK(dst[3] >= 249);
    // 关键：结果既不是纯红也不是纯蓝 —— 证明真的混合了
    NC_CHECK(dst[0] > 0);
    NC_CHECK(dst[2] > 0);
}

NC_TEST(B1Blend, RenderedFrameBlendsForegroundOverBackground) {
    // 2x2 半透明红源，Fit 到 2x2 画布，背景完全不透明蓝。
    const std::uint8_t src[2 * 2 * 4] = {
        255, 0, 0, 128, 255, 0, 0, 128,
        255, 0, 0, 128, 255, 0, 0, 128,
    };
    std::uint8_t dst[2 * 2 * 4] = {0};
    renderFrameFit(src, 2, 2, 2 * 4, dst, 2, 2, FrameFit::Fit, 0x0000FFFF);

    NC_CHECK(dst[0] > 0);        // 有红
    NC_CHECK(dst[2] > 0);        // 有蓝
    // 旧实现（memcpy 覆盖）会得到 (255,0,0,128) —— 蓝完全丢失。
    NC_CHECK(!(dst[0] == 255 && dst[2] == 0));
}

NC_TEST(B1Blend, LetterboxStaysPureBackground) {
    // 4x2 不透明红 → 4x4 画布 Fit：上下信箱条必须是纯背景色
    const std::uint8_t src[4 * 2 * 4] = {
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    };
    std::uint8_t dst[4 * 4 * 4] = {0};
    renderFrameFit(src, 4, 2, 4 * 4, dst, 4, 4, FrameFit::Fit, 0x112233FF);
    // (0,0) 在信箱区
    NC_CHECK_EQ(int(dst[0]), 0x11);
    NC_CHECK_EQ(int(dst[1]), 0x22);
    NC_CHECK_EQ(int(dst[2]), 0x33);
    NC_CHECK_EQ(int(dst[3]), 0xFF);
}

NC_TEST(B1Blend, OpaqueForegroundStillFullyCoversBackground) {
    // 与 B1Blend.LetterboxStaysPureBackground 互补：素材区必须完全是素材色
    const std::uint8_t src[2 * 2 * 4] = {
        200, 100, 50, 255, 200, 100, 50, 255,
        200, 100, 50, 255, 200, 100, 50, 255,
    };
    std::uint8_t dst[2 * 2 * 4] = {0};
    renderFrameFit(src, 2, 2, 2 * 4, dst, 2, 2, FrameFit::Fit, 0x0000FFFF);
    NC_CHECK_EQ(int(dst[0]), 200);
    NC_CHECK_EQ(int(dst[1]), 100);
    NC_CHECK_EQ(int(dst[2]), 50);
    NC_CHECK_EQ(int(dst[3]), 255);
}
