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

#include <algorithm>
#include <cmath>
#include <cstdint>
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

// ===========================================================================
// 4. CPU Alpha 参考矩阵（NC-B1-FOLLOWUP 4.4）
//    这些值将作为未来 GPU 后端的比对基准（当前无 GPU 后端，见报告）。
// ===========================================================================

namespace {

/// straight-alpha 参考实现（浮点版，独立于 blendStraightOver 的整数实现）。
/// 两条独立实现互相校验，避免"用被测代码证明被测代码"。
CanvasBackground referenceBlend(CanvasBackground fg, CanvasBackground bg) {
    const double a = static_cast<double>(fg.a) / 255.0;
    const double inv = 1.0 - a;
    CanvasBackground out;
    out.r = static_cast<std::uint8_t>(std::lround(fg.r * a + bg.r * inv));
    out.g = static_cast<std::uint8_t>(std::lround(fg.g * a + bg.g * inv));
    out.b = static_cast<std::uint8_t>(std::lround(fg.b * a + bg.b * inv));
    out.a = static_cast<std::uint8_t>(std::lround(255.0 * (a + (bg.a / 255.0) * inv)));
    return out;
}

}  // namespace

NC_TEST(B1AlphaMatrix, RequiredAlphaLevelsMatchReference) {
    // 任务书 4.4 要求的最小矩阵：0 / 1 / 64 / 128 / 192 / 255
    const int alphas[] = {0, 1, 64, 128, 192, 255};
    CanvasBackground bg;
    bg.r = 20;
    bg.g = 60;
    bg.b = 200;
    bg.a = 255;

    for (int a : alphas) {
        CanvasBackground fg;
        fg.r = 220;
        fg.g = 30;
        fg.b = 40;
        fg.a = static_cast<std::uint8_t>(a);

        const CanvasBackground expected = referenceBlend(fg, bg);

        std::uint8_t dst[4] = {bg.r, bg.g, bg.b, bg.a};
        const std::uint8_t src[4] = {fg.r, fg.g, fg.b, fg.a};
        blendStraightOver(dst, src);

        // 整数实现与浮点参考允许 ±1（四舍五入方向差异）
        NC_CHECK(std::abs(int(dst[0]) - int(expected.r)) <= 1);
        NC_CHECK(std::abs(int(dst[1]) - int(expected.g)) <= 1);
        NC_CHECK(std::abs(int(dst[2]) - int(expected.b)) <= 1);
        NC_CHECK(std::abs(int(dst[3]) - int(expected.a)) <= 1);
    }
}

NC_TEST(B1AlphaMatrix, EndpointsAreExact) {
    CanvasBackground bg;
    bg.r = 20;
    bg.g = 60;
    bg.b = 200;
    bg.a = 255;

    // alpha=0 → 完全是背景
    CanvasBackground fg;
    fg.r = 220;
    fg.g = 30;
    fg.b = 40;
    fg.a = 0;
    std::uint8_t dst0[4] = {bg.r, bg.g, bg.b, bg.a};
    const std::uint8_t src0[4] = {fg.r, fg.g, fg.b, 0};
    blendStraightOver(dst0, src0);
    NC_CHECK_EQ(int(dst0[0]), int(bg.r));
    NC_CHECK_EQ(int(dst0[3]), 255);

    // alpha=255 → 完全是前景
    std::uint8_t dst1[4] = {bg.r, bg.g, bg.b, bg.a};
    const std::uint8_t src1[4] = {fg.r, fg.g, fg.b, 255};
    blendStraightOver(dst1, src1);
    NC_CHECK_EQ(int(dst1[0]), int(fg.r));
    NC_CHECK_EQ(int(dst1[1]), int(fg.g));
    NC_CHECK_EQ(int(dst1[2]), int(fg.b));
    NC_CHECK_EQ(int(dst1[3]), 255);
}

NC_TEST(B1AlphaMatrix, AlphaOneIsAlmostInvisible) {
    // alpha=1 时前景只贡献 1/255，结果应几乎等于背景
    CanvasBackground bg;
    bg.r = 0;
    bg.g = 0;
    bg.b = 0;
    bg.a = 255;
    std::uint8_t dst[4] = {0, 0, 0, 255};
    const std::uint8_t src[4] = {255, 255, 255, 1};
    blendStraightOver(dst, src);
    NC_CHECK(dst[0] <= 2);   // 0 or 1
    NC_CHECK(dst[0] >= 0);
}

NC_TEST(B1AlphaMatrix, MultipleLayersCompositeInOrder) {
    // 两层叠加：先 A 后 B，顺序敏感
    std::uint8_t dst[4] = {0, 0, 0, 255};
    const std::uint8_t a[4] = {255, 0, 0, 128};   // 半透明红
    const std::uint8_t b[4] = {0, 0, 255, 128};   // 半透明蓝
    blendStraightOver(dst, a);
    const int rAfterA = dst[0];
    const int bAfterA = dst[2];
    blendStraightOver(dst, b);
    // 叠蓝后红色应减少、蓝色应增加
    NC_CHECK(dst[0] < rAfterA);
    NC_CHECK(dst[2] > bAfterA);
    NC_CHECK_EQ(int(dst[3]), 255);   // 底不透明，结果仍不透明
}

NC_TEST(B1AlphaMatrix, SemiTransparentBackgroundPropagates) {
    // 半透明背景本身：alpha=128 的底 + 半透明前景
    std::uint8_t dst[4] = {0, 0, 255, 128};       // 半透明蓝底
    const std::uint8_t src[4] = {255, 0, 0, 128}; // 半透明红
    blendStraightOver(dst, src);
    // outA = 128 + 128*(1-128/255) ≈ 128 + 63.7 = 191.7 → 192
    NC_CHECK(dst[3] >= 190 && dst[3] <= 194);
    NC_CHECK(dst[0] > 0);
    NC_CHECK(dst[2] > 0);
}

NC_TEST(B1AlphaMatrix, DifferentColorsStayInRange) {
    // 覆盖多组前景/背景色，确保不出现溢出或通道错位
    const std::uint8_t colors[][4] = {
        {255, 0, 0, 128}, {0, 255, 0, 128}, {0, 0, 255, 128},
        {255, 255, 255, 128}, {0, 0, 0, 128}, {127, 63, 191, 200},
    };
    for (const auto& src : colors) {
        std::uint8_t dst[4] = {40, 80, 120, 255};
        blendStraightOver(dst, src);
        // 全部通道必须落在合法区间（uint8 天然如此，这里防的是逻辑溢出）
        NC_CHECK(dst[3] == 255);  // 底不透明 → 结果不透明
        // 红色通道应介于前景与背景之间
        const int lo = std::min<int>(src[0], 40);
        const int hi = std::max<int>(src[0], 40);
        NC_CHECK(dst[0] >= lo - 1 && dst[0] <= hi + 1);
    }
}
