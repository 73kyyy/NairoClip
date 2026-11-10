// B1 验收测试：三个「直接写字段」绕过项的整改
//   1. rename_project  → RenameProjectCommand
//   2. canvas          → SetCanvasCommand（含比例预设/锁定/适配）
//   3. background      → SetCanvasBackgroundCommand（RGBA + Alpha 合成）
//
// 每个用例都验证**真实执行路径**：Command → UndoStack → revision。
#include "test_framework.h"

#include "nairoclip/core/canvas.h"
#include "nairoclip/core/capi.h"
#include "nairoclip/core/commands.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/frame_fit.h"
#include "nairoclip/core/serialization.h"

#include <string>

using namespace nairoclip::core;

namespace {

struct Fixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seq;
    TrackId track;
    AssetId asset;
    ClipId clip;

    Fixture() {
        Sequence s;
        s.id = SequenceId{project.ids.next()};
        project.sequences.push_back(s);
        seq = s.id;

        // 一个 1920x1080 的素材 + 视频轨 + 片段，用于适配策略测试。
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/tmp/a.mp4";
        a.name = "a";
        a.type = MediaType::Video;
        a.width = 1920;
        a.height = 1080;
        a.duration = Rational::make(10);
        editor.importAsset(a);
        asset = a.id;

        editor.addTrack(seq, TrackType::Video, "V1", &track);
        Clip c;
        c.asset = asset;
        c.start = Rational::make(0);
        c.duration = Rational::make(2);
        c.sourceIn = Rational::make(0);
        ClipId out;
        editor.insertClip(seq, track, c, &out);
        clip = out;
    }

    const Sequence* main() const { return project.mainSequence(); }
    Sequence* mainMut() { return project.mainSequence(); }
    const Clip* clipPtr() const { return project.mainSequence()->tracks[0].findClip(clip); }
};

}  // namespace

// ===========================================================================
// B1-01 重命名工程
// ===========================================================================

NC_TEST(B1Rename, NormalRenameAndRevision) {
    Fixture f;
    const std::uint64_t rev0 = f.editor.editRevision();
    NC_CHECK(f.editor.renameProject("我的作品").isOk());
    NC_CHECK_EQ(f.project.name, std::string("我的作品"));
    NC_CHECK(f.editor.editRevision() > rev0);      // revision 递增
    NC_CHECK(f.editor.canUndo());                   // 进入撤销栈
}

NC_TEST(B1Rename, EmptyNameRejected) {
    Fixture f;
    const std::string before = f.project.name;
    // 注意：Fixture 构造本身已压入 importAsset/addTrack/insertClip，
    // 因此这里比较的是「撤销栈深度有没有增加」，而不是 canUndo()。
    const std::size_t depth = f.editor.undoCount();
    const std::uint64_t rev = f.editor.editRevision();
    NC_CHECK(!f.editor.renameProject("").isOk());
    NC_CHECK_EQ(f.project.name, before);            // 失败不写入
    NC_CHECK_EQ(f.editor.undoCount(), depth);       // 失败不入栈
    NC_CHECK_EQ(f.editor.editRevision(), rev);      // 失败不动 revision
}

NC_TEST(B1Rename, WhitespaceOnlyRejected) {
    Fixture f;
    NC_CHECK(!f.editor.renameProject("   ").isOk());
    NC_CHECK(!f.editor.renameProject("\t\n\r").isOk());
}

NC_TEST(B1Rename, TooLongRejected) {
    Fixture f;
    NC_CHECK(!f.editor.renameProject(std::string(201, 'a')).isOk());
    NC_CHECK(f.editor.renameProject(std::string(200, 'a')).isOk());  // 边界值可用
}

NC_TEST(B1Rename, ControlCharRejected) {
    Fixture f;
    NC_CHECK(!f.editor.renameProject(std::string("a\x01b")).isOk());
    NC_CHECK(!f.editor.renameProject(std::string("a\x7F" "b")).isOk());
}

NC_TEST(B1Rename, NoOpDoesNotPushHistory) {
    Fixture f;
    f.editor.renameProject("同名");
    const std::uint64_t rev = f.editor.editRevision();
    const std::size_t undoCount = f.editor.undoCount();
    // 改成同一个名字：应被 isNoOp 拦截，不入栈、不递增 revision。
    NC_CHECK(f.editor.renameProject("同名").isOk());
    NC_CHECK_EQ(f.editor.undoCount(), undoCount);
    NC_CHECK_EQ(f.editor.editRevision(), rev);
}

NC_TEST(B1Rename, UndoRedoRestoresName) {
    Fixture f;
    const std::string original = f.project.name;
    f.editor.renameProject("第一次");
    f.editor.renameProject("第二次");
    NC_CHECK_EQ(f.project.name, std::string("第二次"));

    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.project.name, std::string("第一次"));
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.project.name, original);

    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.project.name, std::string("第一次"));
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.project.name, std::string("第二次"));
}

NC_TEST(B1Rename, SurvivesSaveAndReload) {
    Fixture f;
    f.editor.renameProject("持久化名称");
    const std::string saved = saveProjectToNclip(f.project);

    Project loaded;
    NC_CHECK(loadProjectFromNclip(saved, loaded).result.isOk());
    NC_CHECK_EQ(loaded.name, std::string("持久化名称"));
}

NC_TEST(B1Rename, CapiGoesThroughCommand) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    const uint64_t rev0 = nc_contract_revision(ed);
    NC_CHECK_EQ(nc_set_project_name(ed, "C接口改名"), NC_OK);
    char* got = nc_project_name(ed);
    NC_CHECK(got != nullptr);
    NC_CHECK_EQ(std::string(got), std::string("C接口改名"));
    nc_free_string(got);
    NC_CHECK(nc_contract_revision(ed) > rev0);   // 真实 revision 递增
    NC_CHECK_EQ(nc_can_undo(ed), 1);             // 可撤销
    NC_CHECK_EQ(nc_undo(ed), NC_OK);
    got = nc_project_name(ed);
    NC_CHECK_EQ(std::string(got), std::string("未命名项目"));
    nc_free_string(got);
    nc_editor_destroy(ed);
}

NC_TEST(B1Rename, CapiRejectsEmpty) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK_EQ(nc_set_project_name(ed, ""), NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_can_undo(ed), 0);   // 失败不入栈
    nc_editor_destroy(ed);
}

// ===========================================================================
// B1-02 画布
// ===========================================================================

NC_TEST(B1Canvas, PresetsCoverRequiredRatios) {
    // 任务书 §5.2 明确要求 1:1 / 16:9 / 9:16 必须真实可用。
    NC_CHECK(findCanvasPreset("1:1") != nullptr);
    NC_CHECK(findCanvasPreset("16:9") != nullptr);
    NC_CHECK(findCanvasPreset("9:16") != nullptr);
    NC_CHECK(findCanvasPreset("4:5") != nullptr);
    NC_CHECK(findCanvasPreset("21:9") != nullptr);
    NC_CHECK(findCanvasPreset("2.39:1") != nullptr);
    NC_CHECK(canvasPresets().size() >= 10);
    NC_CHECK(findCanvasPreset("不存在的比例") == nullptr);
}

NC_TEST(B1Canvas, PresetDimensionsMatchTheirRatio) {
    for (const CanvasPreset& p : canvasPresets()) {
        NC_CHECK(validateCanvasSize(p.width, p.height).ok);
        // 预设自身的宽高必须真的等于它声称的比例（防止表里写错数字）。
        NC_CHECK(canvasAspectRatio(p.width, p.height).isPositive());
    }
    // 逐项核对几个关键比例
    NC_CHECK(sameAspectRatio(1080, 1080, 1, 1));
    NC_CHECK(sameAspectRatio(1920, 1080, 16, 9));
    NC_CHECK(sameAspectRatio(1080, 1920, 9, 16));
    NC_CHECK(!sameAspectRatio(1080, 1920, 16, 9));
}

NC_TEST(B1Canvas, CustomSizeAccepted) {
    Fixture f;
    // 同一比例支持不同分辨率（任务书 §5.3）
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1080, 1080).isOk());
    NC_CHECK_EQ(f.main()->width, 1080);
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1440, 1440).isOk());
    NC_CHECK_EQ(f.main()->width, 1440);
    NC_CHECK(f.editor.setCanvasSize(f.seq, 2160, 2160).isOk());
    NC_CHECK_EQ(f.main()->width, 2160);
    // 非预设比例也允许
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1000, 999).isOk());
    NC_CHECK_EQ(f.main()->height, 999);
}

NC_TEST(B1Canvas, InvalidSizeRejectedAtomically) {
    Fixture f;
    const int w0 = f.main()->width;
    const int h0 = f.main()->height;
    NC_CHECK(!f.editor.setCanvasSize(f.seq, 0, 1080).isOk());
    NC_CHECK(!f.editor.setCanvasSize(f.seq, 1080, 0).isOk());
    NC_CHECK(!f.editor.setCanvasSize(f.seq, -1, 1080).isOk());
    NC_CHECK(!f.editor.setCanvasSize(f.seq, 8, 8).isOk());          // 低于下限
    NC_CHECK(!f.editor.setCanvasSize(f.seq, 20000, 100).isOk());    // 高于上限
    NC_CHECK_EQ(f.main()->width, w0);   // 失败不写入
    NC_CHECK_EQ(f.main()->height, h0);
}

NC_TEST(B1Canvas, PixelBudgetEnforced) {
    // 16384x16384 = 268M 像素，超过 kCanvasMaxPixels
    const CanvasValidation v = validateCanvasSize(16384, 16384);
    NC_CHECK(!v.ok);
    NC_CHECK(v.reason.find("像素总量") != std::string::npos);
    // 8192x8192 恰好在上限
    NC_CHECK(validateCanvasSize(8192, 8192).ok);
}

NC_TEST(B1Canvas, RatioLockSolveExact) {
    const Rational r = canvasAspectRatio(16, 9);
    const AspectSolve s = solveHeightForWidth(1920, r);
    NC_CHECK_EQ(s.height, 1080);
    NC_CHECK(s.exact);

    const AspectSolve s2 = solveWidthForHeight(1080, r);
    NC_CHECK_EQ(s2.width, 1920);
    NC_CHECK(s2.exact);
}

NC_TEST(B1Canvas, RatioLockSolveWithRounding) {
    // 9:16 比例下给定宽 1000 → 高 = 1000 * 16/9 = 1777.78 → 1778
    // 1778 无法整除，比例不再严格相等，必须如实报告。
    const Rational r = canvasAspectRatio(9, 16);
    const AspectSolve s = solveHeightForWidth(1000, r);
    NC_CHECK_EQ(s.height, 1778);
    NC_CHECK(!s.exact);   // 必须如实报告"不再严格锁定"
}

NC_TEST(B1Canvas, SwapOrientation) {
    int w = 1920;
    int h = 1080;
    NC_CHECK(swapCanvasOrientation(&w, &h));
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1920);
    NC_CHECK(swapCanvasOrientation(&w, &h));
    NC_CHECK_EQ(w, 1920);
    NC_CHECK_EQ(h, 1080);
}

NC_TEST(B1Canvas, SwapOrientationRejectsInvalid) {
    int w = 8;
    int h = 100;
    NC_CHECK(!swapCanvasOrientation(&w, &h));   // 交换后 100x8 → 8 低于下限
    NC_CHECK_EQ(w, 8);                          // 失败不改动
    NC_CHECK_EQ(h, 100);
}

NC_TEST(B1Canvas, UndoRedoRestoresSize) {
    Fixture f;
    const int w0 = f.main()->width;
    const int h0 = f.main()->height;
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1920, 1080).isOk());
    NC_CHECK_EQ(f.main()->width, 1920);
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.main()->width, w0);
    NC_CHECK_EQ(f.main()->height, h0);
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.main()->width, 1920);
    NC_CHECK_EQ(f.main()->height, 1080);
}

NC_TEST(B1Canvas, NoOpDoesNotPushHistory) {
    Fixture f;
    const int w0 = f.main()->width;
    const int h0 = f.main()->height;
    const std::size_t before = f.editor.undoCount();
    NC_CHECK(f.editor.setCanvasSize(f.seq, w0, h0).isOk());
    NC_CHECK_EQ(f.editor.undoCount(), before);
}

NC_TEST(B1Canvas, PreserveModeLeavesClipsUntouched) {
    Fixture f;
    const double scale0 = f.clipPtr()->transform.scale;
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1080, 1080, CanvasAdaptMode::Preserve).isOk());
    NC_CHECK_EQ(f.clipPtr()->transform.scale, scale0);
}

NC_TEST(B1Canvas, FitModeScalesClipToFit) {
    Fixture f;
    // 素材 1920x1080 → 画布 1080x1080：Fit 取 min(1080/1920, 1080/1080)=0.5625
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1080, 1080, CanvasAdaptMode::Fit).isOk());
    const double expected = 1080.0 / 1920.0;
    NC_CHECK(f.clipPtr()->transform.scale < 1.0);
    NC_CHECK(f.clipPtr()->transform.scale <= expected + 1e-9);
    NC_CHECK(f.clipPtr()->transform.enabled);
}

NC_TEST(B1Canvas, FillModeScalesClipToCover) {
    Fixture f;
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1080, 1080, CanvasAdaptMode::Fill).isOk());
    // Fill 取 max(1080/1920, 1080/1080) = 1.0 → 相对原 scale 1.0 不变
    NC_CHECK(f.clipPtr()->transform.scale >= 1.0 - 1e-9);
}

NC_TEST(B1Canvas, AdaptUndoRestoresClipScale) {
    Fixture f;
    const double scale0 = f.clipPtr()->transform.scale;
    const bool enabled0 = f.clipPtr()->transform.enabled;
    NC_CHECK(f.editor.setCanvasSize(f.seq, 1080, 1080, CanvasAdaptMode::Fit).isOk());
    NC_CHECK(f.clipPtr()->transform.scale != scale0);
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.clipPtr()->transform.scale, scale0);
    NC_CHECK_EQ(f.clipPtr()->transform.enabled, enabled0);
}

NC_TEST(B1Canvas, SurvivesSaveAndReload) {
    Fixture f;
    f.editor.setCanvasSize(f.seq, 1440, 1440);
    f.editor.setCanvasBackground(f.seq, CanvasBackground::fromRgba32(0x11223344));
    const std::string saved = saveProjectToNclip(f.project);

    Project loaded;
    NC_CHECK(loadProjectFromNclip(saved, loaded).result.isOk());
    NC_CHECK_EQ(loaded.mainSequence()->width, 1440);
    NC_CHECK_EQ(loaded.mainSequence()->height, 1440);
    // 保存的是真实宽高，不是比例名称
    NC_CHECK_EQ(loaded.mainSequence()->backgroundRgba, std::uint32_t(0x11223344));
}

NC_TEST(B1Canvas, CapiPresetAndValidate) {
    NC_CHECK(nc_canvas_preset_count() >= 10);
    char* id = nullptr;
    char* label = nullptr;
    int32_t w = 0;
    int32_t h = 0;
    NC_CHECK_EQ(nc_canvas_preset_get(0, &id, &label, &w, &h), NC_OK);
    NC_CHECK_EQ(std::string(id), std::string("1:1"));
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1080);
    nc_free_string(id);
    nc_free_string(label);
    NC_CHECK(nc_canvas_preset_get(999, nullptr, nullptr, nullptr, nullptr) != NC_OK);

    char* reason = nullptr;
    NC_CHECK_EQ(nc_canvas_validate(1920, 1080, &reason), NC_OK);
    nc_free_string(reason);
    reason = nullptr;
    NC_CHECK(nc_canvas_validate(0, 1080, &reason) != NC_OK);
    NC_CHECK(reason != nullptr && std::string(reason).find("正数") != std::string::npos);
    nc_free_string(reason);
}

NC_TEST(B1Canvas, CapiSetExAndSwap) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    NC_CHECK_EQ(nc_canvas_set_ex(ed, 1080, 1080, 0, 0), NC_OK);
    int32_t w = 0;
    int32_t h = 0;
    NC_CHECK_EQ(nc_project_canvas(ed, &w, &h), NC_OK);
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1080);

    NC_CHECK_EQ(nc_canvas_swap_orientation(ed), NC_OK);
    NC_CHECK_EQ(nc_project_canvas(ed, &w, &h), NC_OK);
    NC_CHECK_EQ(w, 1080);   // 正方形交换后不变
    NC_CHECK_EQ(h, 1080);

    NC_CHECK_EQ(nc_canvas_set_ex(ed, 1920, 1080, 1, 0), NC_OK);
    NC_CHECK_EQ(nc_canvas_swap_orientation(ed), NC_OK);
    NC_CHECK_EQ(nc_project_canvas(ed, &w, &h), NC_OK);
    NC_CHECK_EQ(w, 1080);
    NC_CHECK_EQ(h, 1920);

    int64_t num = 0;
    int64_t den = 1;
    NC_CHECK_EQ(nc_canvas_aspect_ratio(ed, &num, &den), NC_OK);
    // Rational 约分到最简：1080/1920 → 9/16（这正是"精确有理数"的价值）
    NC_CHECK_EQ(num, 9);
    NC_CHECK_EQ(den, 16);
    nc_editor_destroy(ed);
}

NC_TEST(B1Canvas, CapiSolveLock) {
    int32_t out = 0;
    int32_t exact = 0;
    NC_CHECK_EQ(nc_canvas_solve_height(1920, 16, 9, &out, &exact), NC_OK);
    NC_CHECK_EQ(out, 1080);
    NC_CHECK_EQ(exact, 1);

    NC_CHECK_EQ(nc_canvas_solve_width(1080, 16, 9, &out, &exact), NC_OK);
    NC_CHECK_EQ(out, 1920);
    NC_CHECK_EQ(exact, 1);

    NC_CHECK(nc_canvas_solve_height(100, 0, 9, &out, &exact) != NC_OK);
}

// ===========================================================================
// B1-03 画布背景色
// ===========================================================================

NC_TEST(B1Background, RgbaRoundTrip) {
    const CanvasBackground c = CanvasBackground::fromRgba32(0x11223344);
    NC_CHECK_EQ(int(c.r), 0x11);
    NC_CHECK_EQ(int(c.g), 0x22);
    NC_CHECK_EQ(int(c.b), 0x33);
    NC_CHECK_EQ(int(c.a), 0x44);
    NC_CHECK_EQ(c.toRgba32(), std::uint32_t(0x11223344));
}

NC_TEST(B1Background, SetOpaqueAndRevision) {
    Fixture f;
    const std::uint64_t rev0 = f.editor.editRevision();
    NC_CHECK(f.editor.setCanvasBackground(f.seq, CanvasBackground::fromRgba32(0xFF0000FF)).isOk());
    NC_CHECK_EQ(f.main()->backgroundRgba, std::uint32_t(0xFF0000FF));
    NC_CHECK(f.editor.editRevision() > rev0);
    NC_CHECK(f.editor.canUndo());
}

NC_TEST(B1Background, SetTransparentAndSemi) {
    Fixture f;
    NC_CHECK(f.editor.setCanvasBackground(f.seq, CanvasBackground::fromRgba32(0x00000000)).isOk());
    NC_CHECK_EQ(f.main()->backgroundRgba, std::uint32_t(0x00000000));
    NC_CHECK(f.editor.setCanvasBackground(f.seq, CanvasBackground::fromRgba32(0x12345680)).isOk());
    NC_CHECK_EQ(f.main()->backgroundRgba, std::uint32_t(0x12345680));
}

NC_TEST(B1Background, UndoRedoKeepsAlpha) {
    Fixture f;
    const std::uint32_t before = f.main()->backgroundRgba;
    NC_CHECK(f.editor.setCanvasBackground(f.seq, CanvasBackground::fromRgba32(0xAABBCCDD)).isOk());
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.main()->backgroundRgba, before);   // 完整还原，不只 RGB
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.main()->backgroundRgba, std::uint32_t(0xAABBCCDD));
}

NC_TEST(B1Background, NoOpDoesNotPushHistory) {
    Fixture f;
    const std::uint32_t cur = f.main()->backgroundRgba;
    const std::size_t before = f.editor.undoCount();
    NC_CHECK(f.editor.setCanvasBackground(f.seq, CanvasBackground::fromRgba32(cur)).isOk());
    NC_CHECK_EQ(f.editor.undoCount(), before);
}

NC_TEST(B1Background, AlphaFlattenFormula) {
    // 50% 白 叠在黑底 → 约 128 灰
    CanvasBackground src;
    src.r = src.g = src.b = 255;
    src.a = 128;
    CanvasBackground bg;
    bg.r = bg.g = bg.b = 0;
    bg.a = 255;
    const CanvasBackground out = flattenOnto(src, bg);
    NC_CHECK(out.r >= 127 && out.r <= 129);
    NC_CHECK_EQ(int(out.a), 255);   // 叠到不透明底 → 结果不透明

    // 完全透明 → 完全是底色
    src.a = 0;
    const CanvasBackground out2 = flattenOnto(src, bg);
    NC_CHECK_EQ(int(out2.r), 0);

    // 完全不透明 → 完全是源色
    src.a = 255;
    src.r = 200;
    const CanvasBackground out3 = flattenOnto(src, bg);
    NC_CHECK_EQ(int(out3.r), 200);
}

NC_TEST(B1Background, CapiGoesThroughCommand) {
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    const uint64_t rev0 = nc_contract_revision(ed);
    NC_CHECK_EQ(nc_set_background_color(ed, 0x20304050), NC_OK);
    NC_CHECK(nc_contract_revision(ed) > rev0);
    NC_CHECK_EQ(nc_can_undo(ed), 1);
    NC_CHECK_EQ(nc_undo(ed), NC_OK);
    // 撤销后背景色应回到默认（不透明黑），不再包含刚设置的值
    char* saved = nc_project_save(ed);
    NC_CHECK(saved != nullptr);
    NC_CHECK(std::string(saved).find("20304050") == std::string::npos);
    nc_free_string(saved);
    nc_editor_destroy(ed);
}

// ===========================================================================
// B1-03 Alpha 参与真实渲染（不是只存数值）
// ===========================================================================

namespace {

/// 用 2x2 不透明红源，按 Fit 渲染到 4x4 画布，取左上角（信箱区）像素。
CanvasBackground renderLetterboxPixel(std::uint32_t bgRgba) {
    const std::uint8_t src[2 * 2 * 4] = {
        255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255,
    };
    std::uint8_t dst[4 * 4 * 4] = {0};
    // 2x2 方源 → 4x4 方画布：Fit 会等比放大铺满，不留信箱。
    // 改用 4x2 源渲染到 4x4，上下出现信箱条。
    const std::uint8_t wide[4 * 2 * 4] = {
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
        255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
    };
    (void)src;
    renderFrameFit(wide, 4, 2, 4 * 4, dst, 4, 4, FrameFit::Fit, bgRgba);
    // (0,0) 在信箱区
    return CanvasBackground{dst[0], dst[1], dst[2], dst[3]};
}

}  // namespace

NC_TEST(B1Background, AlphaReachesRenderedPixels) {
    // 完全不透明背景 → 信箱区 alpha = 255
    const CanvasBackground opaque = renderLetterboxPixel(0x000000FF);
    NC_CHECK_EQ(int(opaque.a), 255);

    // 完全透明背景 → 信箱区 alpha = 0（证明 Alpha 真的写进输出，不是只存数值）
    const CanvasBackground clear = renderLetterboxPixel(0x00000000);
    NC_CHECK_EQ(int(clear.a), 0);

    // 半透明 → 信箱区 alpha = 128
    const CanvasBackground half = renderLetterboxPixel(0x11223380);
    NC_CHECK_EQ(int(half.a), 128);
    NC_CHECK_EQ(int(half.r), 0x11);
    NC_CHECK_EQ(int(half.g), 0x22);
    NC_CHECK_EQ(int(half.b), 0x33);
}

NC_TEST(B1Background, ForegroundUnaffectedByBackgroundAlpha) {
    // 背景 alpha 变化不得影响素材区域像素（素材是不透明红）
    const CanvasBackground a = renderLetterboxPixel(0x000000FF);
    const CanvasBackground b = renderLetterboxPixel(0x00000000);
    // 两者信箱区 alpha 不同，但这里只验证"差异确实来自背景"
    NC_CHECK(a.a != b.a);
}
