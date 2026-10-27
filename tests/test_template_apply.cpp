#include "test_framework.h"
#include "nairoclip/core/serialization.h"
#include "nairoclip/core/template_apply.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 造一个带主序列 + 若干素材的工程。
Project makeProject() {
    Project p;
    p.name = "模板测试工程";
    Sequence seq;
    seq.id = SequenceId{1};
    seq.name = "主序列";
    seq.frameRate = fps30();
    p.sequences.push_back(seq);
    p.ids.restore(100);

    for (int i = 0; i < 5; ++i) {
        MediaAsset a;
        a.id = AssetId{static_cast<std::uint64_t>(i + 1)};
        a.path = "/tmp/m" + std::to_string(i + 1) + ".mp4";
        a.name = "m" + std::to_string(i + 1);
        a.type = MediaType::Video;
        a.duration = fromMicroseconds(10000000);  // 10 秒
        p.assets.push_back(a);
    }
    return p;
}

EditTemplate mkSmallTemplate(int slotCount) {
    EditTemplate t;
    t.id = "apply_test";
    t.name = "应用测试";
    t.category = "卡点";
    for (int i = 0; i < slotCount; ++i) {
        TemplateSlot s;
        s.duration = fromMicroseconds(1000000);  // 1 秒
        t.slots.push_back(s);
    }
    t.totalDuration = t.slotsDuration();
    return t;
}

std::vector<MaterialAssignment> assignAll(const EditTemplate& t, int assetCount) {
    std::vector<MaterialAssignment> out;
    for (std::size_t i = 0; i < t.slots.size(); ++i) {
        MaterialAssignment m;
        m.slotIndex = i;
        m.asset = AssetId{static_cast<std::uint64_t>((i % static_cast<std::size_t>(assetCount)) + 1)};
        out.push_back(m);
    }
    return out;
}

}  // namespace

// =============================================================================
// B2-3 模板应用管线
// =============================================================================

NC_TEST(TemplateApply, InsertsAllSlots) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(3);
    TemplateApplyResult out;

    const Result r = applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out);
    NC_CHECK(r.isOk());
    NC_CHECK_EQ(out.clipsInserted, 3);
    NC_CHECK_EQ(out.tracksUsed, 1);  // 只有主轴（无前景槽位）
}

NC_TEST(TemplateApply, SlotsAreLaidOutBackToBack) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(3);
    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out).isOk());

    const Sequence* seq = p.mainSequence();
    NC_CHECK(seq != nullptr);
    if (seq == nullptr) return;
    NC_CHECK_EQ(seq->tracks.size(), static_cast<std::size_t>(1));
    const Track& tr = seq->tracks[0];
    NC_CHECK_EQ(tr.clips.size(), static_cast<std::size_t>(3));
    // 首尾相接：0-1, 1-2, 2-3 秒
    for (std::size_t i = 0; i < tr.clips.size(); ++i) {
        const std::int64_t expectStart = static_cast<std::int64_t>(i) * 1000000;
        NC_CHECK_EQ(toMicroseconds(tr.clips[i].start), expectStart);
        NC_CHECK_EQ(toMicroseconds(tr.clips[i].duration), static_cast<std::int64_t>(1000000));
    }
}

NC_TEST(TemplateApply, UndoRestoresProjectExactly) {
    // 核心验收：撤销后必须与原状**逐字节一致**（不只是片段数回到原值）
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(3);

    const std::string before = saveProjectToNclip(p);

    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out).isOk());
    const std::string afterApply = saveProjectToNclip(p);
    NC_CHECK(afterApply != before);  // 确实改了

    NC_CHECK(editor.undo().isOk());
    const std::string afterUndo = saveProjectToNclip(p);
    if (afterUndo != before) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                         "撤销后工程与原始状态不一致（序列化文本不同）");
    }
}

NC_TEST(TemplateApply, SingleUndoStepForWholeTemplate) {
    // 套用一次模板 = 一条撤销记录，不能是「N 段就 N 步」
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(4);

    const std::size_t before = editor.undoCount();
    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out).isOk());
    NC_CHECK_EQ(editor.undoCount(), before + 1);
}

NC_TEST(TemplateApply, RedoReappliesTemplate) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(2);
    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out).isOk());
    const std::string applied = saveProjectToNclip(p);

    NC_CHECK(editor.undo().isOk());
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(saveProjectToNclip(p), applied);
}

NC_TEST(TemplateApply, ForegroundSlotsGetOwnTrack) {
    Project p = makeProject();
    TimelineEditor editor(p);
    EditTemplate t = mkSmallTemplate(2);
    t.slots[0].role = LayerRole::Main;
    t.slots[1].role = LayerRole::Foreground2;
    t.totalDuration = t.slotsDuration();

    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out).isOk());
    NC_CHECK_EQ(out.tracksUsed, 2);

    const Sequence* seq = p.mainSequence();
    NC_CHECK(seq != nullptr);
    if (seq != nullptr) {
        NC_CHECK_EQ(seq->tracks.size(), static_cast<std::size_t>(2));
    }
}

NC_TEST(TemplateApply, EffectsAreAdded) {
    Project p = makeProject();
    TimelineEditor editor(p);
    EditTemplate t = mkSmallTemplate(1);
    t.slots[0].effects.push_back(TemplateEffect{EffectType::Brightness, 0.2});
    t.slots[0].effects.push_back(TemplateEffect{EffectType::ZoomPunch, 0.5});
    t.totalDuration = t.slotsDuration();

    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out).isOk());
    NC_CHECK_EQ(out.effectsAdded, 2);

    const Sequence* seq = p.mainSequence();
    NC_CHECK(seq != nullptr);
    if (seq != nullptr) {
        NC_CHECK_EQ(seq->tracks[0].clips[0].effects.size(), static_cast<std::size_t>(2));
    }
}

NC_TEST(TemplateApply, TransitionsBetweenAdjacentClips) {
    Project p = makeProject();
    TimelineEditor editor(p);
    EditTemplate t = mkSmallTemplate(2);
    t.slots[1].inTransition = TransitionType::CrossDissolve;
    t.slots[1].transitionDuration = fromMicroseconds(300000);
    t.totalDuration = t.slotsDuration();

    TemplateApplyResult out;
    NC_CHECK(applyTemplate(editor, SequenceId{1}, t, assignAll(t, 5), out).isOk());
    // 转场可能因引擎的相邻判定而失败——如实记录，不能静默
    NC_CHECK(out.transitionsAdded + static_cast<int>(out.warnings.size()) >= 1);
}

NC_TEST(TemplateApply, MissingAssignmentIsSkippedWithWarning) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(3);

    // 只给第 1、3 个槽位指定素材
    std::vector<MaterialAssignment> assigns;
    assigns.push_back(MaterialAssignment{0, AssetId{1}});
    assigns.push_back(MaterialAssignment{2, AssetId{2}});

    TemplateApplyResult out;
    const Result r = applyTemplate(editor, SequenceId{1}, t, assigns, out);
    NC_CHECK(r.isOk());
    NC_CHECK_EQ(out.clipsInserted, 2);
    NC_CHECK_EQ(out.warnings.size(), static_cast<std::size_t>(1));
    NC_CHECK(out.warnings[0].find("槽位 2") != std::string::npos);
}

NC_TEST(TemplateApply, UnknownAssetIsRejected) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(1);
    std::vector<MaterialAssignment> assigns;
    assigns.push_back(MaterialAssignment{0, AssetId{9999}});  // 不存在

    TemplateApplyResult out;
    const Result r = applyTemplate(editor, SequenceId{1}, t, assigns, out);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::NotFound);
    // 失败不能留下半成品
    const Sequence* seq = p.mainSequence();
    if (seq != nullptr) {
        NC_CHECK(seq->tracks.empty());
    }
}

NC_TEST(TemplateApply, InvalidSlotIndexIsRejected) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(2);
    std::vector<MaterialAssignment> assigns;
    assigns.push_back(MaterialAssignment{5, AssetId{1}});  // 越界

    TemplateApplyResult out;
    const Result r = applyTemplate(editor, SequenceId{1}, t, assigns, out);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
}

NC_TEST(TemplateApply, DuplicateSlotAssignmentIsRejected) {
    Project p = makeProject();
    TimelineEditor editor(p);
    const EditTemplate t = mkSmallTemplate(2);
    std::vector<MaterialAssignment> assigns;
    assigns.push_back(MaterialAssignment{0, AssetId{1}});
    assigns.push_back(MaterialAssignment{0, AssetId{2}});  // 重复

    TemplateApplyResult out;
    const Result r = applyTemplate(editor, SequenceId{1}, t, assigns, out);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
}

NC_TEST(TemplateApply, InvalidTemplateIsRejected) {
    Project p = makeProject();
    TimelineEditor editor(p);
    EditTemplate t;  // 无 id/name/slots
    std::vector<MaterialAssignment> assigns;
    TemplateApplyResult out;
    NC_CHECK(!applyTemplate(editor, SequenceId{1}, t, assigns, out).isOk());
    // 拒绝后不应创建任何轨道
    const Sequence* seq = p.mainSequence();
    if (seq != nullptr) {
        NC_CHECK(seq->tracks.empty());
    }
}

NC_TEST(TemplateApply, AssignmentsFromFillWorks) {
    const EditTemplate t = mkSmallTemplate(2);
    std::vector<FillCandidate> cands;
    for (int i = 0; i < 3; ++i) {
        FillCandidate c;
        c.asset.id = AssetId{static_cast<std::uint64_t>(i + 1)};
        c.asset.type = MediaType::Video;
        c.asset.duration = fromMicroseconds(10000000);
        cands.push_back(c);
    }
    const FillResult fill = fillTemplateSlots(t, cands);
    NC_CHECK(fill.complete);
    const auto assigns = assignmentsFromFill(fill, cands);
    NC_CHECK_EQ(assigns.size(), static_cast<std::size_t>(2));
    for (const auto& a : assigns) {
        NC_CHECK(a.asset.value >= 1 && a.asset.value <= 3);
    }
}

NC_TEST(TemplateApply, DescribeResultIsReadable) {
    TemplateApplyResult r;
    r.clipsInserted = 5;
    r.transitionsAdded = 2;
    r.effectsAdded = 3;
    r.tracksUsed = 2;
    const std::string d = describeApplyResult(r);
    NC_CHECK(d.find("5") != std::string::npos);
    NC_CHECK(d.find("轨道") != std::string::npos);
}

NC_TEST(TemplateApply, WorksWithBuiltinTemplate) {
    Project p = makeProject();
    // 内置模板槽位最长 3 秒，素材 10 秒足够
    TimelineEditor editor(p);
    const std::vector<MaterialAssignment> assigns = [&] {
        std::vector<MaterialAssignment> out;
        const EditTemplate* t = findBuiltinTemplate("keyframe_shake");
        NC_CHECK(t != nullptr);
        if (t == nullptr) return out;
        for (std::size_t i = 0; i < t->slots.size(); ++i) {
            out.push_back(MaterialAssignment{i, AssetId{(i % 5) + 1}});
        }
        return out;
    }();

    const EditTemplate* tmpl = findBuiltinTemplate("keyframe_shake");
    NC_CHECK(tmpl != nullptr);
    if (tmpl == nullptr) return;

    const std::string before = saveProjectToNclip(p);
    TemplateApplyResult out;
    const Result r = applyTemplate(editor, SequenceId{1}, *tmpl, assigns, out);
    NC_CHECK(r.isOk());
    NC_CHECK_EQ(out.clipsInserted, static_cast<int>(tmpl->slots.size()));

    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(saveProjectToNclip(p), before);
}
