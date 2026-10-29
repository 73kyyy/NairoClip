// 轨道组联动移动（B2-7 的「移动轨道上下位置 → 整组一起移动」）。
//
// 需求原文（docs/DEVELOPMENT_BACKLOG.md，B2-7）：
//   | 移动轨道上下位置 | 整组一起移动（保持组内相对顺序） |
//
// 另：组内成员各自的片段、片段上的效果/关键帧、转场、调整图层特效、
// 静音/隐藏/锁定标志、轨道增益都存放在 Track 内部，因此换位置即整体搬移。
// 本文件用「片段 + 效果」做实证，证明它们跟着轨道走。
#include "test_framework.h"

#include "nairoclip/core/editor.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

struct GroupMoveFixture {
    Project project;
    SequenceId seqId;
    TrackId v1, v2, v3, v4;

    GroupMoveFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
    }

    /// 按顺序加一条视频轨，返回其 id。
    TrackId addVideo(const char* name) {
        TrackId id;
        TimelineEditor ed(project);
        ed.addTrack(seqId, TrackType::Video, name, &id);
        return id;
    }

    /// 当前轨道顺序（按名字首字符拼接，如 "ABCD"）。
    std::string order() const {
        std::string s;
        for (const auto& t : project.sequences[0].tracks) s += t.name;
        return s;
    }

    /// 某条轨道在序列中的当前下标。
    int indexOf(TrackId id) const {
        const auto& tracks = project.sequences[0].tracks;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    TimelineEditor editor() { return TimelineEditor(project); }
};

}  // namespace

// ---------------------------------------------------------------------------
// 单轨：无组时行为与原来一致
// ---------------------------------------------------------------------------

NC_TEST(TrackGroupMove, UngroupedMovesOnlyItself) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    NC_CHECK_EQ(f.order(), std::string("ABC"));

    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v3, 0).isOk());
    NC_CHECK_EQ(f.order(), std::string("CAB"));
}

// ---------------------------------------------------------------------------
// 组联动：搬组内任一条，整组一起动，相对顺序不变
// ---------------------------------------------------------------------------

NC_TEST(TrackGroupMove, MovingMemberMovesWholeGroup) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");  // B、C 成组
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v2, f.v3}, "组1").isOk());
    NC_CHECK_EQ(f.order(), std::string("ABC"));

    // 把组内的 C 拖到最前：整组应变成 B、C 在最前，且 B 仍在 C 之前
    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v3, 0).isOk());
    NC_CHECK_EQ(f.order(), std::string("BCA"));
}

NC_TEST(TrackGroupMove, GroupKeepsRelativeOrderWhenMovingDown) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");  // A、B 成组
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    f.v4 = f.addVideo("D");
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v1, f.v2}, "组1").isOk());
    NC_CHECK_EQ(f.order(), std::string("ABCD"));

    // 把组内的 A 拖到最后：整组落到末尾，A 仍在 B 之前
    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v1, 3).isOk());
    NC_CHECK_EQ(f.order(), std::string("CDAB"));
}

NC_TEST(TrackGroupMove, DraggingGroupByLatterMemberWorks) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    f.v4 = f.addVideo("D");
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v2, f.v3}, "组1").isOk());
    NC_CHECK_EQ(f.order(), std::string("ABCD"));

    // 拖组内**第二条**（C）到位置 3（末尾）。
    // 整组一起搬，锚点是 C 落在 3，而组内相对顺序保持（B 在 C 之前）
    // => 结果 ADBC：A、D 让位，B 紧随 C 之前占据 2、3。
    //
    // 这个用例的价值：若实现只搬被拖动的那一条（C 单独移到末尾），
    // 会得到 ABDC，与 ADBC 不同 —— 能真正区分「整组搬」和「只搬一条」。
    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v3, 3).isOk());
    NC_CHECK_EQ(f.order(), std::string("ADBC"));
    // 组必须仍然相邻
    NC_CHECK_EQ(f.indexOf(f.v3), f.indexOf(f.v2) + 1);
}

// ---------------------------------------------------------------------------
// 组内的内容跟着走（片段 + 效果）
// ---------------------------------------------------------------------------

NC_TEST(TrackGroupMove, GroupClipsAndEffectsFollowTrack) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    TimelineEditor ed0(f.project);

    // 给 B 放一个片段，并给片段挂一个效果
    MediaAsset asset;
    asset.id = AssetId{f.project.ids.next()};
    asset.name = "x.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(10);
    f.project.assets.push_back(asset);

    Clip c;
    c.asset = asset.id;
    c.start = Time{};
    c.duration = Rational::make(5);
    ClipId clipId;
    NC_CHECK(ed0.insertClip(f.seqId, f.v2, c, &clipId).isOk());
    Effect fx;
    fx.id = EffectId{f.project.ids.next()};
    fx.type = EffectType::GaussianBlur;
    fx.value = 5.0;  // 高斯模糊半径，合法范围 [0,100]
    NC_CHECK(ed0.addEffect(f.seqId, f.v2, clipId, fx).isOk());

    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v2}, "组1").isOk());

    // 整组（只有 B）上移
    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v2, 0).isOk());
    NC_CHECK_EQ(f.order(), std::string("BA"));

    // 片段与效果必须还在 B 上
    const Track* b = findTrack(f.project, f.seqId, f.v2);
    NC_CHECK(b != nullptr);
    if (b != nullptr) {
        NC_CHECK_EQ(b->clips.size(), std::size_t(1));
        NC_CHECK_EQ(b->clips[0].id.value, clipId.value);
        NC_CHECK(b->clips[0].effects.size() >= 1);
    }
    // A 上不能凭空出现片段
    const Track* a = findTrack(f.project, f.seqId, f.v1);
    NC_CHECK(a != nullptr);
    if (a != nullptr) {
        NC_CHECK_EQ(a->clips.size(), std::size_t(0));
    }
}

// ---------------------------------------------------------------------------
// 撤销：整组一次还原
// ---------------------------------------------------------------------------

NC_TEST(TrackGroupMove, UndoRestoresWholeGroup) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    f.v4 = f.addVideo("D");
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v2, f.v3}, "组1").isOk());
    NC_CHECK_EQ(f.order(), std::string("ABCD"));

    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v2, 3).isOk());
    NC_CHECK_EQ(f.order(), std::string("ADBC"));
    // 一次撤销应整组还原，而不是只还原一条
    NC_CHECK(ed.undo().isOk());
    NC_CHECK_EQ(f.order(), std::string("ABCD"));
}

NC_TEST(TrackGroupMove, RedoReappliesWholeGroup) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    f.v4 = f.addVideo("D");
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v2, f.v3}, "组1").isOk());

    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v2, 3).isOk());
    NC_CHECK_EQ(f.order(), std::string("ADBC"));
    NC_CHECK(ed.undo().isOk());
    NC_CHECK(ed.redo().isOk());
    NC_CHECK_EQ(f.order(), std::string("ADBC"));
}

// ---------------------------------------------------------------------------
// 边界与不变量
// ---------------------------------------------------------------------------

NC_TEST(TrackGroupMove, GroupNeverSplitsAcrossOthers) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    f.v4 = f.addVideo("D");
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v1, f.v3}, "组1").isOk());

    // 把 B 移到末尾：A、C 仍是相邻（成组），只是组被整体挪到了后面
    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v1, 3).isOk());
    NC_CHECK_EQ(f.indexOf(f.v3), f.indexOf(f.v1) + 1);
}

NC_TEST(TrackGroupMove, InvariantsHoldAfterGroupMove) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v1, f.v2}, "组1").isOk());

    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v2, 2).isOk());
    NC_CHECK(f.project.sequences[0].invariantsHold());
    // 组成员关系不能因搬家而丢失
    NC_CHECK(f.project.sequences[0].groupOf(f.v1) != nullptr);
    NC_CHECK(f.project.sequences[0].groupOf(f.v2) != nullptr);
}

NC_TEST(TrackGroupMove, GroupMoveDoesNotLoseTracks) {
    GroupMoveFixture f;
    f.v1 = f.addVideo("A");
    f.v2 = f.addVideo("B");
    f.v3 = f.addVideo("C");
    f.v4 = f.addVideo("D");
    TimelineEditor ed0(f.project);
    NC_CHECK(ed0.createTrackGroup(f.seqId, {f.v1, f.v2, f.v3}, "大面积组").isOk());

    auto ed = f.editor();
    NC_CHECK(ed.moveTrack(f.seqId, f.v3, 0).isOk());
    // 轨道总数不变，不能因为搬组丢轨道
    NC_CHECK_EQ(f.project.sequences[0].tracks.size(), std::size_t(4));
    NC_CHECK_EQ(f.order().size(), std::size_t(4));
}
