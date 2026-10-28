// B2-9 素材区拖动排序（核心层）。
//
// 交付标准（docs/DEVELOPMENT_BACKLOG.md）：「排序后序列化往返一致」
//
// 设计说明：素材顺序不是「视图状态」，而是 assets 数组顺序本身
// （序列化按数组顺序写）。所以不引入 asset.order 字段 ——
// 多一个与数组顺序并存的字段，两者不一致时无法裁决谁是真的。
#include "test_framework.h"

#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 建一个有 n 个素材的工程，素材名依次为 A、B、C...
struct AssetFixture {
    Project project;
    SequenceId seqId;
    std::vector<AssetId> ids;

    explicit AssetFixture(int n) {
        project.name = "素材排序测试";
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        for (int i = 0; i < n; ++i) {
            MediaAsset a;
            a.id = AssetId{project.ids.next()};
            a.name = std::string(1, static_cast<char>('A' + i)) + ".mp4";
            a.type = MediaType::Video;
            a.duration = Rational::make(10);
            project.assets.push_back(a);
            ids.push_back(a.id);
        }
    }

    /// 当前顺序的素材名首字母拼接（如 "ABCD"），用于断言顺序。
    std::string order() const {
        std::string s;
        for (const auto& a : project.assets) s += a.name.substr(0, 1);
        return s;
    }

    TimelineEditor editor() { return TimelineEditor(project); }
};

/// 走真实的保存→载入路径读回素材顺序。
std::string orderAfterRoundTrip(const Project& p) {
    const std::string text = saveProjectToNclip(p);
    Project loaded;
    const NclipLoadResult r = loadProjectFromNclip(text, loaded);
    if (!r.result.isOk()) return "<载入失败>";
    std::string s;
    for (const auto& a : loaded.assets) s += a.name.substr(0, 1);
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------
// 基础语义
// ---------------------------------------------------------------------------

NC_TEST(AssetReorder, MovesForward) {
    AssetFixture f(4);  // ABCD
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(0, 2).isOk());  // A 移到下标 2
    NC_CHECK_EQ(f.order(), std::string("BCAD"));
}

NC_TEST(AssetReorder, MovesBackward) {
    AssetFixture f(4);  // ABCD
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(3, 1).isOk());  // D 移到下标 1
    NC_CHECK_EQ(f.order(), std::string("ADBC"));
}

NC_TEST(AssetReorder, MoveToSameIndexIsNoOp) {
    AssetFixture f(4);
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(2, 2).isOk());
    NC_CHECK_EQ(f.order(), std::string("ABCD"));
}

NC_TEST(AssetReorder, MoveToFirstAndLast) {
    AssetFixture f(3);  // ABC
    {
        auto ed = f.editor();
        NC_CHECK(ed.reorderAssets(2, 0).isOk());  // C 移到最前
        NC_CHECK_EQ(f.order(), std::string("CAB"));
    }
    {
        auto ed = f.editor();
        NC_CHECK(ed.reorderAssets(0, 2).isOk());  // C 移到最末
        NC_CHECK_EQ(f.order(), std::string("ABC"));
    }
}

// ---------------------------------------------------------------------------
// 边界：越界一律拒绝，且不改动顺序
//
// 为什么必须拒绝而不是截断：UI 的落点提示按「有效范围」绘制，
// 若后端静默截断到边界，用户看到的落点与实际结果会不一致。
// ---------------------------------------------------------------------------

NC_TEST(AssetReorder, RejectsOutOfRange) {
    AssetFixture f(3);
    auto ed = f.editor();
    NC_CHECK(!ed.reorderAssets(-1, 1).isOk());
    NC_CHECK(!ed.reorderAssets(0, -1).isOk());
    NC_CHECK(!ed.reorderAssets(3, 0).isOk());
    NC_CHECK(!ed.reorderAssets(0, 3).isOk());
    NC_CHECK(!ed.reorderAssets(100, 0).isOk());
    NC_CHECK_EQ(f.order(), std::string("ABC"));  // 全部拒绝，顺序未变
}

NC_TEST(AssetReorder, EmptyProjectRejectsAnyMove) {
    AssetFixture f(0);
    auto ed = f.editor();
    // 空素材池：0 也是越界（有效范围为空），不能因为「移 0 到 0」就放行
    NC_CHECK(!ed.reorderAssets(0, 0).isOk());
    NC_CHECK_EQ(f.order(), std::string(""));
}

NC_TEST(AssetReorder, SingleAssetOnlyAllowsSelfMove) {
    AssetFixture f(1);
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(0, 0).isOk());   // 原地：允许（no-op）
    NC_CHECK(!ed.reorderAssets(0, 1).isOk());  // 越界：拒绝
    NC_CHECK_EQ(f.order(), std::string("A"));
}

// ---------------------------------------------------------------------------
// 撤销 / 重做
// ---------------------------------------------------------------------------

NC_TEST(AssetReorder, UndoRestoresOriginalOrder) {
    AssetFixture f(4);  // ABCD
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(0, 2).isOk());
    NC_CHECK_EQ(f.order(), std::string("BCAD"));
    NC_CHECK(ed.undo().isOk());
    NC_CHECK_EQ(f.order(), std::string("ABCD"));
}

NC_TEST(AssetReorder, RedoReappliesMove) {
    AssetFixture f(4);
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(0, 2).isOk());
    NC_CHECK(ed.undo().isOk());
    NC_CHECK(ed.redo().isOk());
    NC_CHECK_EQ(f.order(), std::string("BCAD"));
}

NC_TEST(AssetReorder, MultipleUndoInReverseOrder) {
    AssetFixture f(4);  // ABCD
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(0, 3).isOk());  // BCDA
    NC_CHECK_EQ(f.order(), std::string("BCDA"));
    NC_CHECK(ed.reorderAssets(0, 1).isOk());  // CBDA
    NC_CHECK_EQ(f.order(), std::string("CBDA"));
    NC_CHECK(ed.undo().isOk());
    NC_CHECK_EQ(f.order(), std::string("BCDA"));
    NC_CHECK(ed.undo().isOk());
    NC_CHECK_EQ(f.order(), std::string("ABCD"));
}

// 关键用例：撤销必须按「素材 id」找回，而不是对索引做逆运算。
// 若实现用 reorder(to, from) 做 revert，在中间夹了别的命令后会错位。
NC_TEST(AssetReorder, UndoAfterInterleavedMove) {
    AssetFixture f(5);  // ABCDE
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(0, 2).isOk());  // BCADE
    NC_CHECK_EQ(f.order(), std::string("BCADE"));
    NC_CHECK(ed.reorderAssets(4, 0).isOk());  // EBCAD
    NC_CHECK_EQ(f.order(), std::string("EBCAD"));
    NC_CHECK(ed.undo().isOk());  // 撤销第二步 -> BCADE
    NC_CHECK_EQ(f.order(), std::string("BCADE"));
    NC_CHECK(ed.undo().isOk());  // 撤销第一步 -> ABCDE
    NC_CHECK_EQ(f.order(), std::string("ABCDE"));
}

NC_TEST(AssetReorder, NoOpDoesNotEnableUndo) {
    AssetFixture f(3);
    auto ed = f.editor();
    // 原地移动不产生历史：撤销栈为空时 undo 应失败
    NC_CHECK(ed.reorderAssets(1, 1).isOk());
    NC_CHECK(!ed.undo().isOk());
    NC_CHECK_EQ(f.order(), std::string("ABC"));
}

// ---------------------------------------------------------------------------
// 持久化（B2-9 的交付标准）
// ---------------------------------------------------------------------------

NC_TEST(AssetReorder, OrderSurvivesSerializationRoundTrip) {
    AssetFixture f(4);  // ABCD
    {
        auto ed = f.editor();
        NC_CHECK(ed.reorderAssets(0, 2).isOk());  // BCAD
        NC_CHECK_EQ(f.order(), std::string("BCAD"));
    }
    // 走真实 save/load，而不是只验内存里的 vector
    NC_CHECK_EQ(orderAfterRoundTrip(f.project), std::string("BCAD"));
}

NC_TEST(AssetReorder, UnchangedOrderRoundTrips) {
    AssetFixture f(4);
    // 未重排时也必须保持原顺序（防止序列化按 id 或名重新排序）
    NC_CHECK_EQ(orderAfterRoundTrip(f.project), std::string("ABCD"));
}

NC_TEST(AssetReorder, AssetIdsPreservedAfterRoundTrip) {
    AssetFixture f(3);
    {
        auto ed = f.editor();
        NC_CHECK(ed.reorderAssets(2, 0).isOk());  // CAB
    }
    const std::string text = saveProjectToNclip(f.project);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK_EQ(loaded.assets.size(), std::size_t(3));
    // id 必须原样保留：时间轴片段靠 id 引用素材，id 变了引用就断了
    NC_CHECK_EQ(loaded.assets[0].id.value, f.ids[2].value);
    NC_CHECK_EQ(loaded.assets[1].id.value, f.ids[0].value);
    NC_CHECK_EQ(loaded.assets[2].id.value, f.ids[1].value);
}

// ---------------------------------------------------------------------------
// 与时间轴引用共存
// ---------------------------------------------------------------------------

NC_TEST(AssetReorder, ReorderDoesNotAffectClipReferences) {
    AssetFixture f(3);
    auto ed = f.editor();
    TrackId trackId;
    NC_CHECK(ed.addTrack(f.seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip c;
    c.asset = f.ids[0];  // 引用素材 A
    c.start = Time{};
    c.duration = Rational::make(5);
    ClipId clipId;
    NC_CHECK(ed.insertClip(f.seqId, trackId, c, &clipId).isOk());

    // 把 A 移到最后，片段引用必须仍然指向 A
    NC_CHECK(ed.reorderAssets(0, 2).isOk());
    NC_CHECK_EQ(f.order(), std::string("BCA"));
    const Track* t = findTrack(f.project, f.seqId, trackId);
    NC_CHECK(t != nullptr);
    if (t != nullptr) {
        NC_CHECK_EQ(t->clips.size(), std::size_t(1));
        NC_CHECK_EQ(t->clips[0].asset.value, f.ids[0].value);
        NC_CHECK_EQ(t->clips[0].id.value, clipId.value);
    }
}

NC_TEST(AssetReorder, SequenceInvariantsHoldAfterReorder) {
    AssetFixture f(4);
    auto ed = f.editor();
    NC_CHECK(ed.reorderAssets(1, 3).isOk());
    // invariantsHold 在 Sequence 上（不是 Project）
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(1));
    NC_CHECK(f.project.sequences[0].invariantsHold());
}
