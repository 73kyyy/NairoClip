#include "test_framework.h"
#include "nairoclip/core/capi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>

// =============================================================================
// LUT 的 C ABI 契约（P-01 的前置条件）
// =============================================================================
//
// 这组用例守的是「ABI 不许骗人」：
//   · 越界/NaN 强度必须**拒绝**且不写工程（否则 .nclip 里存着非法值，
//     下次打开触发校验失败、用户完全不知道哪一步弄坏的）；
//   · 清空 LUT 必须真的把路径变成空串；
//   · LUT 属于工程数据，必须可撤销；
//   · 不存在的 .cube 在 validate 阶段要给出**具体原因**，
//     不能只回一句「调用失败」。
//
// 注意：这里不测"LUT 是否改变了像素" —— 那是 tests/test_clip_lut_export.cpp
// 的职责（渲染层）。本文件只测 ABI 边界。

namespace {

/// 生成 2×2×2 的 identity .cube 文本。
std::string identityCube() {
    std::string out;
    out += "TITLE \"id2\"\n";
    out += "LUT_3D_SIZE 2\n";
    const double v[2] = {0.0, 1.0};
    for (int b = 0; b < 2; ++b) {
        for (int g = 0; g < 2; ++g) {
            for (int r = 0; r < 2; ++r) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f\n", v[r], v[g], v[b]);
                out += buf;
            }
        }
    }
    return out;
}

std::string writeTempCube(const std::string& name, const std::string& text) {
    const char* tmpdir = std::getenv("TMPDIR");
    const std::string dir = (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    const std::string path = dir + "/nairoclip_capi_lut_" + name + ".cube";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    f.close();
    return path;
}

/// 建一个「一个轨道 + 一个片段」的工程，返回片段 ID。
std::uint64_t makeProjectWithClip(NcEditor* editor) {
    const std::uint64_t asset =
        nc_import_asset(editor, "a.mp4", "/video/a.mp4", 0, 60000000);
    NC_CHECK(asset != 0);
    const std::uint64_t track = nc_add_track(editor, "V1", 0);
    NC_CHECK(track != 0);
    const std::uint64_t clip =
        nc_insert_clip(editor, 0, asset, 0, 2000000);  // 轨道 0，0..2s
    NC_CHECK(clip != 0);
    return clip;
}

}  // namespace

NC_TEST(CapiLut, SetAndGetRoundtrip) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);

    const std::string cube = writeTempCube("roundtrip", identityCube());
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), 0.75), NC_OK);

    char* path = nullptr;
    double strength = -1.0;
    NC_CHECK_EQ(nc_get_clip_lut(editor, 0, clip, &path, &strength), NC_OK);
    NC_CHECK(path != nullptr);
    NC_CHECK_EQ(std::string(path != nullptr ? path : ""), cube);
    NC_CHECK_EQ(strength, 0.75);
    nc_free_string(path);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, DefaultsToEmptyAndFullStrength) {
    // 新片段默认没有 LUT；读出来的路径必须是空串（不是 NULL），强度 1.0。
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);

    char* path = nullptr;
    double strength = -1.0;
    NC_CHECK_EQ(nc_get_clip_lut(editor, 0, clip, &path, &strength), NC_OK);
    NC_CHECK(path != nullptr);
    NC_CHECK_EQ(std::string(path != nullptr ? path : "x"), std::string());
    NC_CHECK_EQ(strength, 1.0);
    nc_free_string(path);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, NullPathClearsLut) {
    // NULL 与 "" 都必须等价于「清除」
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);

    const std::string cube = writeTempCube("clear", identityCube());
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), 1.0), NC_OK);
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, nullptr, 1.0), NC_OK);

    char* path = nullptr;
    NC_CHECK_EQ(nc_get_clip_lut(editor, 0, clip, &path, nullptr), NC_OK);
    NC_CHECK_EQ(std::string(path != nullptr ? path : "x"), std::string());
    nc_free_string(path);

    // 空串路径同样清除
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), 1.0), NC_OK);
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, "", 1.0), NC_OK);
    path = nullptr;
    NC_CHECK_EQ(nc_get_clip_lut(editor, 0, clip, &path, nullptr), NC_OK);
    NC_CHECK_EQ(std::string(path != nullptr ? path : "x"), std::string());
    nc_free_string(path);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, RejectsStrengthOutOfRange) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);
    const std::string cube = writeTempCube("range", identityCube());

    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), -0.01),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), 1.01),
                NC_ERROR_INVALID_ARGUMENT);

    // 被拒绝的值不能写进工程：仍应为默认（无 LUT / 1.0）
    char* path = nullptr;
    double strength = -1.0;
    NC_CHECK_EQ(nc_get_clip_lut(editor, 0, clip, &path, &strength), NC_OK);
    NC_CHECK_EQ(std::string(path != nullptr ? path : "x"), std::string());
    NC_CHECK_EQ(strength, 1.0);
    nc_free_string(path);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, RejectsNaNAndInfinity) {
    // NaN 会穿过 `x < 0 || x > 1` 这种朴素比较（两个分支都是 false），
    // 必须在 ABI 层显式拦掉，否则一个 NaN 会永久污染工程文件。
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);
    const std::string cube = writeTempCube("nan", identityCube());

    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), nan),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), inf),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), -inf),
                NC_ERROR_INVALID_ARGUMENT);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, BadTrackOrClipIsReported) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);
    const std::string cube = writeTempCube("bad", identityCube());

    // 负索引（曾经的真实 bug：size_t 转换后 `< 0` 恒假，导致越界读）
    NC_CHECK_EQ(nc_set_clip_lut(editor, -1, clip, cube.c_str(), 1.0),
                NC_ERROR_INVALID_ARGUMENT);
    // 越界索引
    NC_CHECK_EQ(nc_set_clip_lut(editor, 99, clip, cube.c_str(), 1.0),
                NC_ERROR_OUT_OF_RANGE);
    // 不存在的片段
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, 999999, cube.c_str(), 1.0),
                NC_ERROR_NOT_FOUND);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, NullEditorIsRejected) {
    NC_CHECK_EQ(nc_set_clip_lut(nullptr, 0, 1, "a.cube", 1.0),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK_EQ(nc_get_clip_lut(nullptr, 0, 1, nullptr, nullptr),
                NC_ERROR_INVALID_ARGUMENT);
}

NC_TEST(CapiLut, IsUndoable) {
    // LUT 是工程数据，必须能撤销。撤销后路径与强度都要回到原值
    // （只回一半会让用户看到"路径没了但强度还是新值"的错乱状态）。
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);
    const std::string cube = writeTempCube("undo", identityCube());

    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), 0.4), NC_OK);
    NC_CHECK_EQ(nc_undo(editor), NC_OK);

    char* path = nullptr;
    double strength = -1.0;
    NC_CHECK_EQ(nc_get_clip_lut(editor, 0, clip, &path, &strength), NC_OK);
    NC_CHECK_EQ(std::string(path != nullptr ? path : "x"), std::string());
    NC_CHECK_EQ(strength, 1.0);
    nc_free_string(path);

    // 重做应恢复
    NC_CHECK_EQ(nc_redo(editor), NC_OK);
    path = nullptr;
    strength = -1.0;
    NC_CHECK_EQ(nc_get_clip_lut(editor, 0, clip, &path, &strength), NC_OK);
    NC_CHECK_EQ(std::string(path != nullptr ? path : ""), cube);
    NC_CHECK_EQ(strength, 0.4);
    nc_free_string(path);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, LockedTrackRejectsLutChange) {
    NcEditor* editor = nc_editor_create();
    NC_CHECK(editor != nullptr);
    const std::uint64_t clip = makeProjectWithClip(editor);
    const std::string cube = writeTempCube("locked", identityCube());

    NC_CHECK_EQ(nc_set_track_locked(editor, 0, 1), NC_OK);
    NC_CHECK_EQ(nc_set_clip_lut(editor, 0, clip, cube.c_str(), 1.0), NC_ERROR_LOCKED);

    nc_editor_destroy(editor);
}

NC_TEST(CapiLut, ValidateAcceptsRealCube) {
    const std::string cube = writeTempCube("validate_ok", identityCube());
    std::int32_t size = 0;
    char* title = nullptr;
    char* message = nullptr;
    NC_CHECK_EQ(nc_lut_validate_file(cube.c_str(), &size, &title, &message), NC_OK);
    NC_CHECK_EQ(size, 2);
    NC_CHECK(title != nullptr);
    NC_CHECK_EQ(std::string(title != nullptr ? title : ""), std::string("id2"));
    nc_free_string(title);
    nc_free_string(message);
}

NC_TEST(CapiLut, ValidateExplainsFailure) {
    // 关键：失败必须给出**可读原因**。
    // 「调用失败」四个字对导入 .cube 的用户毫无帮助——最常见的是
    // 1D LUT、缺 LUT_3D_SIZE、数据行数不对。
    std::int32_t size = -1;
    char* title = nullptr;
    char* message = nullptr;
    const int32_t rc =
        nc_lut_validate_file("/nonexistent/nope.cube", &size, &title, &message);
    NC_CHECK_EQ(rc, NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(message != nullptr);
    if (message != nullptr && std::strlen(message) == 0) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                         "validate 失败时 message 为空——UI 无法提示用户");
    }
    nc_free_string(title);
    nc_free_string(message);
}

NC_TEST(CapiLut, ValidateRejects1DLut) {
    // 1D LUT 格式上合法但本库不支持，必须明确拒绝而不是当成 3D 硬解。
    const char* text =
        "TITLE \"one_d\"\n"
        "LUT_1D_SIZE 4\n"
        "0.0 0.0 0.0\n"
        "0.33 0.33 0.33\n"
        "0.66 0.66 0.66\n"
        "1.0 1.0 1.0\n";
    const std::string path = writeTempCube("oned", text);
    std::int32_t size = -1;
    char* message = nullptr;
    NC_CHECK_EQ(nc_lut_validate_file(path.c_str(), &size, nullptr, &message),
                NC_ERROR_INVALID_ARGUMENT);
    NC_CHECK(message != nullptr);
    nc_free_string(message);
}

NC_TEST(CapiLut, NoBuiltinPresetsIsHonest) {
    // 本库不附带任何 .cube 文件，preset_count 必须诚实地返回 0。
    // 造几个「点了没反应」的假预设比没有预设更糟。
    NC_CHECK_EQ(nc_lut_preset_count(), 0);
    NC_CHECK_EQ(std::string(nc_lut_preset_name(0)), std::string());
    NC_CHECK_EQ(std::string(nc_lut_preset_name(-1)), std::string());
}
