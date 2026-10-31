// =============================================================================
// C ABI 时间轴编辑测试（文档 Part 3-A「剪辑」基础操作 + Part 2-B 轨道控制
// + Part 5「草稿数据结构」+ Part 5(续) 第32节撤销栈）
// =============================================================================
// 覆盖：
//   1. 工程信息（名称/画布/帧率读写与边界校验）；
//   2. 历史（计数/历史名/清空/深度限制 20 步）；
//   3. 素材管理（数量/读取/引用查询/删除保护）；
//   4. 轨道管理（读/删/重命名/锁定/静音/隐藏）；
//   5. 片段编辑（删除/分割/移动/跨轨移动/裁剪/变速/波纹删除/复制/定格/倒放）；
//   6. 片段查询与属性（信息/启用/不透明度）。
#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cmath>
#include <cstdint>
#include <string>

// 说明：capi.h 是纯 C 接口（extern "C"），不使用 core 命名空间。

namespace {

/// 创建带 1 段素材 + 1 条视频轨 + 1 个片段（[1s, 5s)）的编辑器。
NcEditor* makeEditor(std::uint64_t* outAsset, std::uint64_t* outClip) {
    NcEditor* editor = nc_editor_create();
    if (editor == nullptr) {
        return nullptr;
    }
    const std::uint64_t asset =
        nc_import_asset(editor, "a.mp4", "/videos/a.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    const std::uint64_t clip = nc_insert_clip(editor, 0, asset, 1000000, 4000000);
    if (outAsset != nullptr) *outAsset = asset;
    if (outClip != nullptr) *outClip = clip;
    return editor;
}

std::string takeString(char* text) {
    if (text == nullptr) {
        return std::string();
    }
    const std::string result(text);
    nc_free_string(text);
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// 1. 工程信息
// ---------------------------------------------------------------------------

NC_TEST(CapiTimeline, ProjectNameRoundtrip) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    NC_CHECK(takeString(nc_project_name(editor)).size() > 0);  // 默认名
    NC_CHECK_EQ(nc_set_project_name(editor, "春游Vlog"), NC_OK);
    NC_CHECK_EQ(takeString(nc_project_name(editor)), std::string("春游Vlog"));
    // 超长名称拒绝（UI 防御）
    const std::string longName(300, 'x');
    NC_CHECK_EQ(nc_set_project_name(editor, longName.c_str()), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_project_name(editor, nullptr), NC_ERROR_INVALID_ARGUMENT);
    // NULL 安全
    NC_CHECK(nc_project_name(nullptr) == nullptr);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, ProjectCanvasAndFrameRate) {
    NcEditor* editor = nc_editor_create();
    std::int32_t w = 0;
    std::int32_t h = 0;
    NC_CHECK_EQ(nc_project_canvas(editor, &w, &h), NC_OK);
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1920);
    // 切换为 16:9（文档 Part 3-C「比例」）
    NC_CHECK_EQ(nc_set_project_canvas(editor, 1920, 1080), NC_OK);
    NC_CHECK_EQ(nc_project_canvas(editor, &w, &h), NC_OK);
    NC_CHECK_EQ(w, 1920);
    NC_CHECK_EQ(h, 1080);
    // 边界校验：过小/过大拒绝
    NC_CHECK_EQ(nc_set_project_canvas(editor, 1, 1080), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_project_canvas(editor, 1920, 99999), NC_ERROR_INVALID_ARGUMENT);
    // 帧率读写
    std::int64_t num = 0;
    std::int64_t den = 0;
    NC_CHECK_EQ(nc_project_frame_rate(editor, &num, &den), NC_OK);
    NC_CHECK_EQ(num, std::int64_t(30));
    NC_CHECK_EQ(den, std::int64_t(1));
    NC_CHECK_EQ(nc_set_project_frame_rate(editor, 60, 1), NC_OK);
    NC_CHECK_EQ(nc_project_frame_rate(editor, &num, &den), NC_OK);
    NC_CHECK_EQ(num, std::int64_t(60));
    NC_CHECK_EQ(nc_set_project_frame_rate(editor, 0, 1), NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 2. 历史（撤销/重做；文档 Part 5(续) 第32节）
// ---------------------------------------------------------------------------

NC_TEST(CapiTimeline, UndoCountAndHistoryNames) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    NC_CHECK(editor != nullptr);
    NC_CHECK_EQ(nc_can_undo(editor), 1);
    NC_CHECK(nc_undo_count(editor) >= 3);  // 导入 + 加轨 + 插入
    // 历史名（按时间顺序）
    const std::string first = takeString(nc_undo_history_name(editor, 0));
    NC_CHECK(!first.empty());
    NC_CHECK(nc_undo_history_name(editor, 999) == nullptr);  // 越界
    // 撤销 → 可重做
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 0);
    NC_CHECK_EQ(nc_can_redo(editor), 1);
    NC_CHECK_EQ(nc_redo_count(editor), 1);
    NC_CHECK_EQ(nc_redo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    // 清空历史（文档：退出剪辑页重新进入时调用）
    NC_CHECK_EQ(nc_clear_undo_history(editor), NC_OK);
    NC_CHECK_EQ(nc_can_undo(editor), 0);
    NC_CHECK_EQ(nc_undo_count(editor), 0);
    NC_CHECK_EQ(nc_undo(editor), NC_ERROR_OUT_OF_RANGE);  // 空栈撤销失败
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, UndoDepthLimitEnforced) {
    // 文档第 32 节：最大可撤销步数 20 步，超出后最早记录被移除
    NcEditor* editor = nc_editor_create();
    NC_CHECK_EQ(nc_set_undo_depth_limit(editor, 20), NC_OK);
    const std::uint64_t asset = nc_import_asset(editor, "a.mp4", "/v.mp4", 0, 60000000);
    (void)nc_add_track(editor, "V1", 0);
    // 连续插入 30 个不重叠片段（各 1s）
    for (int i = 0; i < 30; ++i) {
        const std::uint64_t id =
            nc_insert_clip(editor, 0, asset, static_cast<std::int64_t>(i) * 1000000, 1000000);
        NC_CHECK(id != 0);
    }
    // 深度限制 20：最多撤销 20 步
    int undos = 0;
    while (nc_can_undo(editor) == 1 && undos < 100) {
        NC_CHECK_EQ(nc_undo(editor), NC_OK);
        ++undos;
    }
    NC_CHECK_EQ(undos, 20);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 10);  // 剩余 10 段（20 步已撤销）
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 3. 素材管理（文档 Part 5「轨道数据」/ Part 6 素材丢失）
// ---------------------------------------------------------------------------

NC_TEST(CapiTimeline, AssetListAndGet) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t a1 = nc_import_asset(editor, "视频1", "/v/a.mp4", 0, 5000000);
    const std::uint64_t a2 = nc_import_asset(editor, "音频1", "/a/b.wav", 1, 3000000);
    NC_CHECK(a1 != 0);
    NC_CHECK(a2 != 0);
    NC_CHECK_EQ(nc_asset_count(editor), 2);
    std::uint64_t id = 0;
    char* name = nullptr;
    char* path = nullptr;
    std::int32_t type = -1;
    std::int64_t duration = 0;
    NC_CHECK_EQ(nc_asset_get(editor, 0, &id, &name, &path, &type, &duration), NC_OK);
    NC_CHECK_EQ(id, a1);
    NC_CHECK_EQ(takeString(name), std::string("视频1"));
    NC_CHECK_EQ(takeString(path), std::string("/v/a.mp4"));
    NC_CHECK_EQ(type, 0);
    NC_CHECK_EQ(duration, std::int64_t(5000000));
    NC_CHECK_EQ(nc_asset_get(editor, 1, &id, nullptr, nullptr, &type, nullptr), NC_OK);
    NC_CHECK_EQ(type, 1);
    NC_CHECK_EQ(nc_asset_get(editor, 5, nullptr, nullptr, nullptr, nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, AssetReferenceProtection) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    // 被引用 → 拒绝删除（core 语义：InvalidArgument「素材仍被时间轴引用」）
    NC_CHECK_EQ(nc_asset_is_referenced(editor, asset), 1);
    NC_CHECK_EQ(nc_remove_asset(editor, asset), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_asset_count(editor), 1);
    // 删除片段后可删除素材
    NC_CHECK_EQ(nc_remove_clip(editor, 0, clip), NC_OK);
    NC_CHECK_EQ(nc_asset_is_referenced(editor, asset), 0);
    NC_CHECK_EQ(nc_remove_asset(editor, asset), NC_OK);
    NC_CHECK_EQ(nc_asset_count(editor), 0);
    // 参数校验
    NC_CHECK_EQ(nc_remove_asset(editor, 0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_asset_is_referenced(editor, 0), -1);
    nc_editor_destroy(editor);
}

// B2-9：素材顺序调整的 C ABI。
// 核心层（TimelineEditor::reorderAssets + ReorderAssetsCommand）已有 17 个
// 用例覆盖语义；这里只验证 C ABI 这一层：参数校验、返回值映射、
// 顺序真的改变、以及可撤销。
NC_TEST(CapiTimeline, AssetReorder) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t a1 = nc_import_asset(editor, "A", "/a.mp4", 0, 1000000);
    const std::uint64_t a2 = nc_import_asset(editor, "B", "/b.mp4", 0, 1000000);
    const std::uint64_t a3 = nc_import_asset(editor, "C", "/c.mp4", 0, 1000000);
    NC_CHECK(a1 != 0 && a2 != 0 && a3 != 0);
    NC_CHECK_EQ(nc_asset_count(editor), 3);

    auto assetAt = [&](int32_t index) -> std::uint64_t {
        std::uint64_t id = 0;
        if (nc_asset_get(editor, index, &id, nullptr, nullptr, nullptr, nullptr) != NC_OK) {
            return 0;
        }
        return id;
    };

    // 初始顺序 A, B, C
    NC_CHECK_EQ(assetAt(0), a1);
    NC_CHECK_EQ(assetAt(1), a2);
    NC_CHECK_EQ(assetAt(2), a3);

    // 把 A 移到末尾（to_index = 移动后位置）
    NC_CHECK_EQ(nc_asset_reorder(editor, 0, 2), NC_OK);
    NC_CHECK_EQ(assetAt(0), a2);
    NC_CHECK_EQ(assetAt(1), a3);
    NC_CHECK_EQ(assetAt(2), a1);

    // 可撤销：revert 按 id 找回，回到 A, B, C
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(assetAt(0), a1);
    NC_CHECK_EQ(assetAt(1), a2);
    NC_CHECK_EQ(assetAt(2), a3);

    // 重做
    NC_CHECK_EQ(nc_redo(editor), NC_OK);
    NC_CHECK_EQ(assetAt(2), a1);
    NC_CHECK_EQ(nc_undo(editor), NC_OK);

    // 原地移动是 no-op，且不产生撤销记录。
    // 注意：不能断言 nc_can_undo == 0 —— 上面 3 次 nc_import_asset
    // 本身就入栈（ImportAssetCommand 没有 isNoOp 重写），撤销栈非空。
    // 正确判据是「no-op 前后 nc_undo_count 不变」。
    const int32_t undoBefore = nc_undo_count(editor);
    NC_CHECK_EQ(nc_asset_reorder(editor, 1, 1), NC_OK);
    NC_CHECK_EQ(nc_undo_count(editor), undoBefore);

    // 参数校验：一律 InvalidArgument，不静默截断。
    // （核心层 ReorderAssetsCommand::apply 对负索引和越界统一返回
    //   ErrorCode::InvalidArgument，没有 OutOfRange。）
    NC_CHECK_EQ(nc_asset_reorder(editor, -1, 0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_asset_reorder(editor, 0, -1), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_asset_reorder(editor, 3, 0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_asset_reorder(editor, 0, 3), NC_ERROR_INVALID_ARGUMENT);

    // editor 为空
    NC_CHECK_EQ(nc_asset_reorder(nullptr, 0, 1), NC_ERROR_INVALID_ARGUMENT);

    // 空素材池：(0,0) 不能因为 from==to 被当成 no-op 而静默成功，
    // 必须报错 —— isNoOp 对此有专门处理（索引非法时返回 false）。
    NcEditor* empty = nc_editor_create();
    NC_CHECK_EQ(nc_asset_count(empty), 0);
    NC_CHECK_EQ(nc_asset_reorder(empty, 0, 0), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_undo_count(empty), 0);
    nc_editor_destroy(empty);

    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 4. 轨道管理（文档 Part 2-B 第8节）
// ---------------------------------------------------------------------------

NC_TEST(CapiTimeline, TrackGetAndFlags) {
    NcEditor* editor = nc_editor_create();
    const std::uint64_t tid = nc_add_track(editor, "V1", 0);
    NC_CHECK(tid != 0);
    std::uint64_t id = 0;
    char* name = nullptr;
    std::int32_t type = -1;
    std::int32_t locked = -1;
    std::int32_t muted = -1;
    std::int32_t hidden = -1;
    std::int32_t solo = -1;
    double gain = 0.0;
    NC_CHECK_EQ(nc_track_get(editor, 0, &id, &name, &type, &locked, &muted, &hidden, &solo,
                             &gain),
                NC_OK);
    NC_CHECK_EQ(id, tid);
    NC_CHECK_EQ(takeString(name), std::string("V1"));
    NC_CHECK_EQ(type, 0);
    NC_CHECK_EQ(locked, 0);
    NC_CHECK_EQ(muted, 0);
    NC_CHECK_EQ(hidden, 0);
    NC_CHECK(std::abs(gain) < 1e-9);
    // 标志位读写（文档「轨道控制列」👁🔒🔊）
    NC_CHECK_EQ(nc_set_track_locked(editor, 0, 1), NC_OK);
    NC_CHECK_EQ(nc_set_track_muted(editor, 0, 1), NC_OK);
    NC_CHECK_EQ(nc_set_track_hidden(editor, 0, 1), NC_OK);
    NC_CHECK_EQ(nc_track_get(editor, 0, nullptr, nullptr, nullptr, &locked, &muted, &hidden,
                             nullptr, nullptr),
                NC_OK);
    NC_CHECK_EQ(locked, 1);
    NC_CHECK_EQ(muted, 1);
    NC_CHECK_EQ(hidden, 1);
    // 非法值拒绝
    NC_CHECK_EQ(nc_set_track_locked(editor, 0, 2), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_track_get(editor, 9, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr),
                NC_ERROR_OUT_OF_RANGE);
    // NULL 安全
    NC_CHECK_EQ(nc_track_get(nullptr, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, TrackRenameAndRemove) {
    NcEditor* editor = nc_editor_create();
    (void)nc_add_track(editor, "V1", 0);
    (void)nc_add_track(editor, "A1", 1);
    NC_CHECK_EQ(nc_rename_track(editor, 0, "主视频"), NC_OK);
    char* name = nullptr;
    NC_CHECK_EQ(nc_track_get(editor, 0, nullptr, &name, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr),
                NC_OK);
    NC_CHECK_EQ(takeString(name), std::string("主视频"));
    // 删除轨道（含片段）可撤销
    NC_CHECK_EQ(nc_remove_track(editor, 1), NC_OK);
    NC_CHECK_EQ(nc_track_count(editor), 1);
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_track_count(editor), 2);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 5. 片段编辑（文档 Part 3-A 第12节「剪辑」）
// ---------------------------------------------------------------------------

NC_TEST(CapiTimeline, SplitAndRemove) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    // 分割：片段 [1s,5s) 在 3s 处 → 两段
    NC_CHECK_EQ(nc_split_clip(editor, 0, clip, 3000000), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);
    // 左半 [1s,3s)
    std::uint64_t id = 0;
    std::int64_t start = 0;
    std::int64_t duration = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 0, &id, &start, &duration, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(1000000));
    NC_CHECK_EQ(duration, std::int64_t(2000000));
    NC_CHECK_EQ(id, clip);
    // 右半 [3s,5s)
    NC_CHECK_EQ(nc_clip_info(editor, 0, 1, &id, &start, &duration, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(3000000));
    NC_CHECK_EQ(duration, std::int64_t(2000000));
    // 分割点越界拒绝（恰好为片段起点；core 语义：OutOfRange「必须位于片段内部」）
    NC_CHECK_EQ(nc_split_clip(editor, 0, clip, 1000000), NC_ERROR_OUT_OF_RANGE);
    // 删除右半
    NC_CHECK_EQ(nc_remove_clip(editor, 0, id), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    // 撤销恢复
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, MoveTrimAndSpeed) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    // 移动到 [2s, 6s)
    NC_CHECK_EQ(nc_move_clip(editor, 0, clip, 2000000), NC_OK);
    std::int64_t start = 0;
    std::int64_t duration = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 0, nullptr, &start, &duration, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(2000000));
    // 裁剪起点 → [3s, 6s)
    NC_CHECK_EQ(nc_trim_clip_start(editor, 0, clip, 3000000), NC_OK);
    NC_CHECK_EQ(nc_clip_info(editor, 0, 0, nullptr, &start, &duration, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(3000000));
    NC_CHECK_EQ(duration, std::int64_t(3000000));
    // 裁剪终点 → [3s, 5s)
    NC_CHECK_EQ(nc_trim_clip_end(editor, 0, clip, 5000000), NC_OK);
    NC_CHECK_EQ(nc_clip_info(editor, 0, 0, nullptr, nullptr, &duration, nullptr, nullptr,
                             nullptr, nullptr),
                NC_OK);
    NC_CHECK_EQ(duration, std::int64_t(2000000));
    // 变速 2x（文档 0.1x~100x；core 限制 0.1~10）
    NC_CHECK_EQ(nc_set_clip_speed(editor, 0, clip, 2, 1), NC_OK);
    std::int64_t sn = 0;
    std::int64_t sd = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 0, nullptr, nullptr, nullptr, nullptr, &sn, &sd, nullptr),
                NC_OK);
    NC_CHECK_EQ(sn, std::int64_t(2));
    NC_CHECK_EQ(sd, std::int64_t(1));
    // 越界速度拒绝（> 10x）
    NC_CHECK_EQ(nc_set_clip_speed(editor, 0, clip, 20, 1), NC_ERROR_INVALID_ARGUMENT);
    // 非法分母
    NC_CHECK_EQ(nc_set_clip_speed(editor, 0, clip, 1, 0), NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, MoveAcrossTracksAndCrossChecks) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    (void)nc_add_track(editor, "V2", 0);
    NC_CHECK_EQ(nc_move_clip_to_track(editor, 0, 1, clip, 500000), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 0);
    NC_CHECK_EQ(nc_clip_count(editor, 1), 1);
    // 同轨移动应被拒绝（文档：源=目标时用 move_clip）
    NC_CHECK_EQ(nc_move_clip_to_track(editor, 1, 1, clip, 0), NC_ERROR_INVALID_ARGUMENT);
    // 撤销回原轨
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, RippleDelete) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    // 追加上 [5s,7s) 与 [7s,9s)
    const std::uint64_t c2 = nc_insert_clip(editor, 0, asset, 5000000, 2000000);
    const std::uint64_t c3 = nc_insert_clip(editor, 0, asset, 7000000, 2000000);
    NC_CHECK(c2 != 0);
    NC_CHECK(c3 != 0);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 3);
    // 波纹删除中间段 [5s,7s) → 后续片段前移 2s
    const std::uint64_t ids[1] = {c2};
    NC_CHECK_EQ(nc_ripple_delete(editor, 0, ids, 1), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);
    std::int64_t start = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 1, nullptr, &start, nullptr, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(5000000));  // 原 7s → 前移 2s
    // 参数校验
    NC_CHECK_EQ(nc_ripple_delete(editor, 0, nullptr, 0), NC_ERROR_INVALID_ARGUMENT);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, DuplicateClip) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    // 复制 [1s,5s) → 副本 [5s,9s)
    const std::uint64_t copy = nc_duplicate_clip(editor, 0, clip);
    NC_CHECK(copy != 0);
    NC_CHECK(copy != clip);  // 新 ID
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);
    std::int64_t start = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 1, nullptr, &start, nullptr, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(5000000));
    // 目标位置被占用时拒绝：重复制原片段（其后继位置 [5s,9s) 已被副本占用）
    NC_CHECK_EQ(nc_duplicate_clip(editor, 0, clip), std::uint64_t(0));
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);  // 未增加
    // 撤销后回到 1 段
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 1);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, FreezeFrame) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    // 在 [1s,5s) 的 3s 处定格 2s：[3s,5s) 已被占用 → 先分割并删除右半
    NC_CHECK_EQ(nc_split_clip(editor, 0, clip, 3000000), NC_OK);
    std::uint64_t rightId = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 1, &rightId, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(nc_remove_clip(editor, 0, rightId), NC_OK);
    // 在 3s 处定格 2 秒
    const std::uint64_t frozen = nc_freeze_frame(editor, 0, clip, 3000000, 2000000);
    NC_CHECK(frozen != 0);
    NC_CHECK_EQ(nc_clip_count(editor, 0), 2);
    std::int64_t start = 0;
    std::int64_t duration = 0;
    std::int64_t sn = 0;
    std::int64_t sd = 0;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 1, nullptr, &start, &duration, nullptr, &sn, &sd,
                             nullptr),
                NC_OK);
    NC_CHECK_EQ(start, std::int64_t(3000000));
    NC_CHECK_EQ(duration, std::int64_t(2000000));
    // 定格 = 极慢速映射（源时间近似不动）
    NC_CHECK(sn > 0 && sd >= sn);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, ReverseClipAndPersistence) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    // 倒放（文档「剪辑 → 倒放」）
    NC_CHECK_EQ(nc_reverse_clip(editor, 0, clip), NC_OK);
    // 序列化往返：reversed 标记应写入 .nclip 并可载回
    char* text = nc_project_save(editor);
    NC_CHECK(text != nullptr);
    const std::string saved(text);
    nc_free_string(text);
    NC_CHECK(saved.find("\"reversed\"") != std::string::npos);
    NcEditor* reloaded = nc_editor_create();
    NC_CHECK_EQ(nc_project_load(reloaded, saved.c_str(), nullptr), NC_OK);
    // 载入后片段仍是倒放（通过再次序列化检查标记保留）
    char* text2 = nc_project_save(reloaded);
    NC_CHECK(text2 != nullptr);
    const std::string saved2(text2);
    nc_free_string(text2);
    NC_CHECK(saved2.find("\"reversed\"") != std::string::npos);
    nc_editor_destroy(reloaded);
    // 撤销/重做可用
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_redo(editor), NC_OK);
    // 不存在的片段
    NC_CHECK_EQ(nc_reverse_clip(editor, 0, 9999), NC_ERROR_NOT_FOUND);
    nc_editor_destroy(editor);
}

// ---------------------------------------------------------------------------
// 6. 片段查询与属性
// ---------------------------------------------------------------------------

NC_TEST(CapiTimeline, ClipInfoAndEnabled) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    std::uint64_t id = 0;
    std::uint64_t aid = 0;
    std::int32_t enabled = -1;
    NC_CHECK_EQ(nc_clip_info(editor, 0, 0, &id, nullptr, nullptr, &aid, nullptr, nullptr,
                             &enabled),
                NC_OK);
    NC_CHECK_EQ(id, clip);
    NC_CHECK_EQ(aid, asset);
    NC_CHECK_EQ(enabled, 1);
    NC_CHECK_EQ(nc_set_clip_enabled(editor, 0, clip, 0), NC_OK);
    NC_CHECK_EQ(nc_clip_info(editor, 0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             &enabled),
                NC_OK);
    NC_CHECK_EQ(enabled, 0);
    NC_CHECK_EQ(nc_set_clip_enabled(editor, 0, clip, 5), NC_ERROR_INVALID_ARGUMENT);
    // 越界下标
    NC_CHECK_EQ(nc_clip_info(editor, 0, 9, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr),
                NC_ERROR_OUT_OF_RANGE);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, ClipOpacity) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    double opacity = 0.0;
    // 默认 1.0
    NC_CHECK_EQ(nc_get_clip_opacity(editor, 0, clip, &opacity), NC_OK);
    NC_CHECK(std::abs(opacity - 1.0) < 1e-9);
    // 设置 0.5（文档「剪辑 → 不透明度」）
    NC_CHECK_EQ(nc_set_clip_opacity(editor, 0, clip, 0.5), NC_OK);
    NC_CHECK_EQ(nc_get_clip_opacity(editor, 0, clip, &opacity), NC_OK);
    NC_CHECK(std::abs(opacity - 0.5) < 1e-9);
    // 越界拒绝
    NC_CHECK_EQ(nc_set_clip_opacity(editor, 0, clip, 1.5), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_clip_opacity(editor, 0, clip, -0.1), NC_ERROR_INVALID_ARGUMENT);
    // 撤销恢复
    NC_CHECK_EQ(nc_undo(editor), NC_OK);
    NC_CHECK_EQ(nc_get_clip_opacity(editor, 0, clip, &opacity), NC_OK);
    NC_CHECK(std::abs(opacity - 1.0) < 1e-9);
    nc_editor_destroy(editor);
}

NC_TEST(CapiTimeline, LockedTrackBlocksEdits) {
    std::uint64_t asset = 0;
    std::uint64_t clip = 0;
    NcEditor* editor = makeEditor(&asset, &clip);
    NC_CHECK_EQ(nc_set_track_locked(editor, 0, 1), NC_OK);
    // 锁定轨道的编辑全部被拒绝（文档第 8.5 节「不允许任何形式的误触编辑」）
    NC_CHECK_EQ(nc_remove_clip(editor, 0, clip), NC_ERROR_LOCKED);
    NC_CHECK_EQ(nc_split_clip(editor, 0, clip, 3000000), NC_ERROR_LOCKED);
    NC_CHECK_EQ(nc_move_clip(editor, 0, clip, 0), NC_ERROR_LOCKED);
    NC_CHECK_EQ(nc_trim_clip_end(editor, 0, clip, 8000000), NC_ERROR_LOCKED);
    NC_CHECK_EQ(nc_set_clip_speed(editor, 0, clip, 2, 1), NC_ERROR_LOCKED);
    NC_CHECK_EQ(nc_duplicate_clip(editor, 0, clip), std::uint64_t(0));
    // 解锁后恢复可编辑
    NC_CHECK_EQ(nc_set_track_locked(editor, 0, 0), NC_OK);
    NC_CHECK_EQ(nc_move_clip(editor, 0, clip, 0), NC_OK);
    nc_editor_destroy(editor);
}
