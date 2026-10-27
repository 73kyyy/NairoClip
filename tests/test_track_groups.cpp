#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 三条轨道：V1（视频）/ A1（音频）/ S1（字幕）。
struct GroupFixture {
    Project project;
    SequenceId seqId;
    TrackId v1, a1, s1;
    explicit GroupFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        TimelineEditor editor(project);
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &v1).isOk());
        NC_CHECK(editor.addTrack(seqId, TrackType::Audio, "A1", &a1).isOk());
        NC_CHECK(editor.addTrack(seqId, TrackType::Subtitle, "S1", &s1).isOk());
    }
    Sequence& seq() { return project.sequences[0]; }
    const Track* track(TrackId id) const { return project.sequences[0].findTrack(id); }
};

}  // namespace

// =============================================================================
// 轨道成组（B2-7）
// =============================================================================

NC_TEST(TrackGroups, CreateCrossTypeGroup) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    TrackGroupId gid;
    // 跨类型组合：视频 + 音频 + 字幕
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1, f.s1}, "主画面组", &gid).isOk());
    NC_CHECK(gid.isValid());
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(1));
    NC_CHECK(f.seq().trackGroups[0].name == std::string("主画面组"));
    NC_CHECK_EQ(f.seq().trackGroups[0].tracks.size(), std::size_t(3));
    NC_CHECK(f.seq().invariantsHold());
}

NC_TEST(TrackGroups, CreateIsUndoable) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1}, "组").isOk());
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(1));
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(0));
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(1));
}

NC_TEST(TrackGroups, RejectsEmptyGroup) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    const Result r = editor.createTrackGroup(f.seqId, {}, "空组");
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(0));
}

NC_TEST(TrackGroups, RejectsNonexistentTrack) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    const Result r = editor.createTrackGroup(f.seqId, {f.v1, TrackId{9999}}, "组");
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::NotFound);
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(0));
}

NC_TEST(TrackGroups, TrackCannotBelongToTwoGroups) {
    // 一条轨道只能属于一个组——两个组同时管一条轨会让联动语义歧义
    GroupFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1}, "组1").isOk());
    const Result r = editor.createTrackGroup(f.seqId, {f.v1, f.s1}, "组2");
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
    // 消息要指明是"已属于其它组"，不能只说操作失败
    NC_CHECK(r.message.find("已属于") != std::string::npos);
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(1));
    NC_CHECK(f.seq().invariantsHold());
}

NC_TEST(TrackGroups, RemoveGroupIsUndoable) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    TrackGroupId gid;
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1}, "组", &gid).isOk());
    NC_CHECK(editor.removeTrackGroup(f.seqId, gid).isOk());
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(0));
    // 解组后可重新成组（组约束已释放）
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.s1}, "新组").isOk());
    // 撤销新组 → 撤销解组 → 原组恢复
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.seq().trackGroups.size(), std::size_t(1));
    NC_CHECK_EQ(f.seq().trackGroups[0].tracks.size(), std::size_t(2));
}

NC_TEST(TrackGroups, GroupMuteIsLinkedAndSingleUndo) {
    // 核心语义：一次操作切所有成员；一次撤销还原全部
    GroupFixture f;
    TimelineEditor editor(f.project);
    TrackGroupId gid;
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1}, "组", &gid).isOk());
    const std::size_t before = editor.undoCount();

    NC_CHECK(editor.setGroupMuted(f.seqId, gid, true).isOk());
    NC_CHECK(f.track(f.v1)->muted);
    NC_CHECK(f.track(f.a1)->muted);
    // 组外轨道不受影响
    NC_CHECK(!f.track(f.s1)->muted);
    // 只产生一条历史记录（不是 N 条）
    NC_CHECK_EQ(editor.undoCount() - before, std::size_t(1));

    NC_CHECK(editor.undo().isOk());
    NC_CHECK(!f.track(f.v1)->muted);
    NC_CHECK(!f.track(f.a1)->muted);
}

NC_TEST(TrackGroups, GroupMuteRestoresMixedOriginalStates) {
    // 成员原本状态不一致时，撤销必须各自还原（不能一刀切成 false）
    GroupFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.setTrackMuted(f.seqId, f.v1, true).isOk());
    NC_CHECK(editor.setTrackMuted(f.seqId, f.a1, false).isOk());
    TrackGroupId gid;
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1}, "组", &gid).isOk());

    NC_CHECK(editor.setGroupMuted(f.seqId, gid, true).isOk());
    NC_CHECK(f.track(f.v1)->muted);
    NC_CHECK(f.track(f.a1)->muted);

    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.track(f.v1)->muted);    // 原本是 true
    NC_CHECK(!f.track(f.a1)->muted);   // 原本是 false
}

NC_TEST(TrackGroups, GroupHiddenAndLocked) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    TrackGroupId gid;
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1, f.s1}, "组", &gid).isOk());

    NC_CHECK(editor.setGroupHidden(f.seqId, gid, true).isOk());
    NC_CHECK(f.track(f.v1)->hidden);
    NC_CHECK(f.track(f.s1)->hidden);

    NC_CHECK(editor.setGroupLocked(f.seqId, gid, true).isOk());
    NC_CHECK(f.track(f.v1)->locked);
    NC_CHECK(f.track(f.a1)->locked);

    // 解锁可撤销
    NC_CHECK(editor.setGroupLocked(f.seqId, gid, false).isOk());
    NC_CHECK(!f.track(f.v1)->locked);
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.track(f.v1)->locked);
}

NC_TEST(TrackGroups, GroupOfFindsMembership) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    TrackGroupId gid;
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1}, "组", &gid).isOk());
    NC_CHECK(f.seq().groupOf(f.v1) != nullptr);
    NC_CHECK(f.seq().groupOf(f.a1) != nullptr);
    NC_CHECK(f.seq().groupOf(f.s1) == nullptr);
    NC_CHECK_EQ(f.seq().groupOf(f.v1)->id.value, gid.value);
}

NC_TEST(TrackGroups, InvariantsRejectDanglingMember) {
    // 手工构造坏数据：组引用了不存在的轨道 → 不变量必须报警
    GroupFixture f;
    TrackGroup g;
    g.id = TrackGroupId{f.project.ids.next()};
    g.name = "坏组";
    g.tracks.push_back(f.v1);
    g.tracks.push_back(TrackId{9999});
    f.seq().trackGroups.push_back(g);
    NC_CHECK(!f.seq().invariantsHold());

    // 同一轨道出现在两个组 → 也非法
    GroupFixture f2;
    TrackGroup g1;
    g1.id = TrackGroupId{f2.project.ids.next()};
    g1.tracks.push_back(f2.v1);
    TrackGroup g2;
    g2.id = TrackGroupId{f2.project.ids.next()};
    g2.tracks.push_back(f2.v1);
    f2.seq().trackGroups.push_back(g1);
    f2.seq().trackGroups.push_back(g2);
    NC_CHECK(!f2.seq().invariantsHold());
}

// ---- 序列化 ----

NC_TEST(TrackGroups, Roundtrip) {
    GroupFixture f;
    TimelineEditor editor(f.project);
    TrackGroupId gid;
    NC_CHECK(editor.createTrackGroup(f.seqId, {f.v1, f.a1, f.s1}, "主画面组", &gid).isOk());

    Project out;
    const NclipLoadResult r = loadProjectFromNclip(saveProjectToNclip(f.project), out);
    NC_CHECK(r.result.isOk());
    NC_CHECK_EQ(out.sequences[0].trackGroups.size(), std::size_t(1));
    const TrackGroup& g = out.sequences[0].trackGroups[0];
    NC_CHECK(g.name == std::string("主画面组"));
    NC_CHECK_EQ(g.tracks.size(), std::size_t(3));
    NC_CHECK_EQ(g.id.value, gid.value);
    NC_CHECK(out.sequences[0].invariantsHold());
}

NC_TEST(TrackGroups, EmptyGroupsOmitField) {
    GroupFixture f;
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("trackGroups") == std::string::npos);
}

NC_TEST(TrackGroups, DanglingGroupDroppedOnLoad) {
    // 组引用的轨道不存在 → 整组丢弃（保留会让联动操作永远失败）
    const std::string text =
        "{\"format\":\"nclip\",\"version\":1,\"project\":{\"name\":\"t\"},\"assets\":[],"
        "\"sequences\":[{\"id\":1,\"name\":\"s\",\"frameRate\":[30,1],\"width\":1920,"
        "\"height\":1080,\"tracks\":[{\"id\":2,\"type\":\"video\",\"name\":\"v\","
        "\"clips\":[]}],\"trackGroups\":[{\"id\":9,\"name\":\"坏组\",\"tracks\":[2,777]}],"
        "\"markers\":[]}]}";
    Project out;
    const NclipLoadResult r = loadProjectFromNclip(text, out);
    NC_CHECK(r.result.isOk());
    NC_CHECK_EQ(out.sequences[0].trackGroups.size(), std::size_t(0));
    NC_CHECK(out.sequences[0].invariantsHold());
}
