// S1-1 生命周期回归测试（Dispatcher 重建 / eventBus 接线 / project.id 恢复）
//
// 背景：nc_project_load 会整体替换工程并重建 TimelineEditor。
// 修复前存在三处连带缺陷（见 core/src/capi.cpp 中 S1-1 注释）：
//   1) CommandDispatcher 未被重建 → 悬垂 TimelineEditor& → UAF
//   2) TimelineEditor::setEventBus 未重新接线 → commit() 不再发事件
//   3) project.id 被 move 覆盖为 0 → EventBus 静默丢弃全部事件
//
// 本文件覆盖：工程加载、Dispatcher 重建、命令分发、失败回滚、对象释放。
#include "test_framework.h"

#include <cstring>
#include <string>

#include "nairoclip/core/capi.h"

namespace {

/// 取当前工程的可回载文本（失败返回空串）。
std::string savedProjectText(NcEditor* editor) {
    char* text = nc_project_save(editor);
    if (text == nullptr) {
        return std::string();
    }
    std::string out(text);
    nc_free_string(text);
    return out;
}

/// 执行一次会真正解引用 dispatcher::editor_ 的命令。
///
/// clip.remove 是 dispatcher 支持的三种命令之一（contract/dispatch.cpp:17），
/// apply() 内部会调用 editor_.setCorrelationId() / editor_.removeClip()。
/// 若 dispatcher 仍指向已释放的 editor，这里就是 UAF 的触发点。
bool dispatchTouchesEditor(NcEditor* editor) {
    char* result = nc_apply_command(editor, "{\"type\":\"clip.remove\",\"payload\":{}}");
    if (result == nullptr) {
        return false;
    }
    nc_free_string(result);
    return true;
}

}  // namespace

NC_TEST(S1Lifecycle, LoadKeepsDispatcherUsable) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    const std::string nclip = savedProjectText(editor);
    NC_CHECK(!nclip.empty());

    // 一次加载后必须仍能通过 dispatcher 分发命令。
    // 修复前：dispatcher 指向已 delete 的 TimelineEditor → 悬垂引用。
    char* detail = nullptr;
    const int32_t rc = nc_project_load(editor, nclip.c_str(), &detail);
    if (detail != nullptr) {
        nc_free_string(detail);
    }
    NC_CHECK_EQ(rc, static_cast<int32_t>(NC_OK));
    NC_CHECK(dispatchTouchesEditor(editor));

    nc_editor_destroy(editor);
}

NC_TEST(S1Lifecycle, RepeatedLoadDispatchIsStable) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    const std::string nclip = savedProjectText(editor);
    NC_CHECK(!nclip.empty());

    // 冻结验收要求：连续 load→dispatch，验证每次重建后的对象链都正确。
    // 循环次数取 100，与 S1-1 验收口径一致。
    constexpr int kRounds = 100;
    int okRounds = 0;
    for (int i = 0; i < kRounds; ++i) {
        char* detail = nullptr;
        const int32_t rc = nc_project_load(editor, nclip.c_str(), &detail);
        if (detail != nullptr) {
            nc_free_string(detail);
        }
        if (rc != NC_OK) {
            break;
        }
        if (!dispatchTouchesEditor(editor)) {
            break;
        }
        ++okRounds;
    }
    NC_CHECK_EQ(okRounds, kRounds);

    nc_editor_destroy(editor);
}

NC_TEST(S1Lifecycle, LoadKeepsProjectContentIntact) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    const std::string before = savedProjectText(editor);
    NC_CHECK(!before.empty());

    char* detail = nullptr;
    const int32_t rc = nc_project_load(editor, before.c_str(), &detail);
    if (detail != nullptr) {
        nc_free_string(detail);
    }
    NC_CHECK_EQ(rc, static_cast<int32_t>(NC_OK));

    // 同内容回载后再次序列化必须一致：若 project.id 未恢复，工程文本会变化。
    const std::string after = savedProjectText(editor);
    NC_CHECK_EQ(before, after);

    nc_editor_destroy(editor);
}

NC_TEST(S1Lifecycle, FailedLoadKeepsEditorUsable) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    const std::string good = savedProjectText(editor);
    NC_CHECK(!good.empty());

    // 非法文本必须失败，且**不得破坏当前编辑器状态**：
    // 失败路径在 move 之前返回，旧 editor/dispatcher 必须仍可用。
    char* detail = nullptr;
    const int32_t bad = nc_project_load(editor, "{not a project", &detail);
    if (detail != nullptr) {
        nc_free_string(detail);
    }
    NC_CHECK(bad != NC_OK);

    // 失败之后仍能正常分发命令、正常保存。
    NC_CHECK(dispatchTouchesEditor(editor));
    NC_CHECK_EQ(savedProjectText(editor), good);

    nc_editor_destroy(editor);
}

NC_TEST(S1Lifecycle, DestroyAfterLoadReleasesObjects) {
    // 覆盖「加载后释放」路径：修复前 dispatcher 悬垂，
    // 销毁时 delete 顺序错误会暴露双重释放或访问已释放对象。
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    const std::string nclip = savedProjectText(editor);
    NC_CHECK(!nclip.empty());

    char* detail = nullptr;
    const int32_t rc = nc_project_load(editor, nclip.c_str(), &detail);
    if (detail != nullptr) {
        nc_free_string(detail);
    }
    NC_CHECK_EQ(rc, static_cast<int32_t>(NC_OK));

    nc_editor_destroy(editor);   // 必须无崩溃、无泄漏
    NC_CHECK(true);
}

NC_TEST(S1Lifecycle, DispatcherRevisionTracksLiveEditor) {
    // 决定性断言（不依赖内存是否被复用，也不依赖 FFmpeg 素材）：
    //   nc_contract_revision -> editor->dispatcher->currentRevision()
    //                        -> editor_.editRevision()   ← 解引用 dispatcher 持有的引用
    // 修复前 dispatcher 悬垂到已 delete 的旧 TimelineEditor，其 editRevision()
    // 读到的是旧对象里那份独立的 undoStack_ 版本号，与当前工程无关。
    // 用 nc_set_project_name_ex 的「乐观并发」语义把这条链变成可观测判据：
    // 它要求 expected_revision 与当前 revision 一致才执行。
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    const std::string nclip = savedProjectText(editor);
    NC_CHECK(!nclip.empty());

    // 一次重命名：revision 应前进。
    const uint64_t rev0 = nc_contract_revision(editor);
    const int32_t rc0 = nc_set_project_name_ex(editor, "S1Probe", rev0);
    NC_CHECK_EQ(rc0, static_cast<int32_t>(NC_OK));
    const uint64_t rev1 = nc_contract_revision(editor);
    NC_CHECK(rev1 > rev0);          // 编辑确实推进了 revision

    // 加载工程 → editor 与 dispatcher 一并重建。
    char* detail = nullptr;
    const int32_t rcLoad = nc_project_load(editor, nclip.c_str(), &detail);
    if (detail != nullptr) {
        nc_free_string(detail);
    }
    NC_CHECK_EQ(rcLoad, static_cast<int32_t>(NC_OK));

    // 重建后：新 editor 的 revision 从 0 开始。若 dispatcher 悬垂到旧对象，
    // 这里读到的仍是旧值（== rev1），乐观并发校验就会用错基准。
    const uint64_t revAfterLoad = nc_contract_revision(editor);
    NC_CHECK(revAfterLoad < rev1);   // 新 editor 的 revision 必须"归零"

    // 用当前 revision 做一次带校验的编辑，必须成功并再次推进。
    const int32_t rc2 = nc_set_project_name_ex(editor, "S1Probe2", revAfterLoad);
    NC_CHECK_EQ(rc2, static_cast<int32_t>(NC_OK));
    NC_CHECK(nc_contract_revision(editor) > revAfterLoad);

    nc_editor_destroy(editor);
}

NC_TEST(S1Lifecycle, LoadInvalidatesPreviewState) {
    // S1-1 回归：工程替换后必须作废预览渲染器。
    //
    // 缺陷背景（本轮主动排查发现）：
    //   previewSignatureOf() = editRevision*A + loadCount*B + directEditCount*C + canvas
    //   —— 不含素材路径与工程身份。重建后的新 editor 其 editRevision 归 0，
    //   若画布尺寸相同，签名可能与替换前完全一致 ⇒ 不会 clearFrameCache()；
    //   而 clearFrameCache() 本身也不清 readers_（按 AssetId 缓存的解码器）
    //   ⇒ 预览可能继续用旧工程的解码器渲染新工程内容。
    //
    // 本用例可验证的不变量（不依赖 FFmpeg）：
    //   连续两次 nc_project_load 后，editor 仍可用、revision 语义正确、
    //   且再次序列化内容一致 —— 覆盖"替换后状态被正确重置"这一路径。
    //   （FFmpeg 构建下额外的 cached_frames==0 断言见下方条件分支。）
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    if (editor == nullptr) {
        return;
    }
    const std::string nclip = savedProjectText(editor);
    NC_CHECK(!nclip.empty());

    for (int i = 0; i < 3; ++i) {
        char* detail = nullptr;
        const int32_t rc = nc_project_load(editor, nclip.c_str(), &detail);
        if (detail != nullptr) {
            nc_free_string(detail);
        }
        NC_CHECK_EQ(rc, static_cast<int32_t>(NC_OK));
        // 替换后工程内容必须与载入文本一致（id 已恢复为 ProjectId{1}）
        NC_CHECK_EQ(savedProjectText(editor), nclip);
    }

    // 预览状态：有 FFmpeg 时渲染器可重建且帧缓存为空（已作废）；
    // 无 FFmpeg 时 previewFactory 为空，nc_preview_cache_stats 返回错误码，
    // 属预期降级 —— 此时不断言 cached_frames，只断言"不崩溃"。
    int64_t hits = 0, misses = 0, opens = 0;
    int32_t cached = -1, capacity = 0;
    const int32_t statsRc = nc_preview_cache_stats(
        editor, &hits, &misses, &opens, &cached, &capacity);
    if (statsRc == NC_OK) {
        // FFmpeg 构建：替换后缓存必须已作废
        NC_CHECK_EQ(cached, 0);
    }

    nc_editor_destroy(editor);
}
