// =============================================================================
// 内置 LUT 预设（20 个，程序化生成）
// =============================================================================
// 背景：`nc_lut_preset_count()` 原来刻意返回 0，理由是「不附带 .cube 文件，
// 造假预设比没有更糟」。这个判断只对了一半 —— 预设不需要 .cube，
// Lut3D 就是 size³×3 的数组，可以按公式生成真实的颜色变换。
#include "test_framework.h"
#include "nairoclip/core/lut.h"
#include <cmath>
#include <set>
#include <string>

using namespace nairoclip::core;

namespace {
/// 采样 LUT 的指定输入点（三线性插值版，走公开 API）。
void sampleAt(const Lut3D& lut, double r, double g, double b, double out[3]) {
    // 直接用 data 做三线性插值（与 lutSample 的语义一致，但避免依赖
    // 那个函数的参数约定，测试要独立于实现细节）
    const int n = lut.size;
    const auto at = [&](int ri, int gi, int bi, int c) {
        ri = ri < 0 ? 0 : (ri >= n ? n - 1 : ri);
        gi = gi < 0 ? 0 : (gi >= n ? n - 1 : gi);
        bi = bi < 0 ? 0 : (bi >= n ? n - 1 : bi);
        return lut.data[((static_cast<std::size_t>(bi) * n + static_cast<std::size_t>(gi)) * n +
                         static_cast<std::size_t>(ri)) * 3u + static_cast<std::size_t>(c)];
    };
    const double scale = static_cast<double>(n - 1);
    const double fr = r * scale, fg = g * scale, fb = b * scale;
    const int r0 = static_cast<int>(std::floor(fr)), g0 = static_cast<int>(std::floor(fg)),
              b0 = static_cast<int>(std::floor(fb));
    const double dr = fr - r0, dg = fg - g0, db = fb - b0;
    for (int c = 0; c < 3; ++c) {
        double v = 0.0;
        for (int dz = 0; dz < 2; ++dz) {
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) {
                    const double w = (dx ? dr : 1 - dr) * (dy ? dg : 1 - dg) *
                                     (dz ? db : 1 - db);
                    v += at(r0 + dx, g0 + dy, b0 + dz, c) * w;
                }
            }
        }
        out[c] = v;
    }
}
}  // namespace

NC_TEST(LutPresets, CountIsTwenty) {
    /* 20 个是产品规格（UI 里展示的数量），不是实现巧合。
       C ABI 的 nc_lut_preset_count() 直接转发到这里，见 capi.cpp。 */
    NC_CHECK_EQ(lutPresetCount(), 20);
}

NC_TEST(LutPresets, NamesAreUniqueAndNonEmpty) {
    std::set<std::string> seen;
    for (int i = 0; i < lutPresetCount(); ++i) {
        const std::string name = lutPresetName(i);
        NC_CHECK(!name.empty());
        // 重名会让 UI 里两个预设看起来一样，用户无法分辨
        NC_CHECK(seen.insert(name).second);
    }
}

NC_TEST(LutPresets, OutOfRangeReturnsEmpty) {
    NC_CHECK_EQ(std::string(lutPresetName(-1)), std::string(""));
    NC_CHECK_EQ(std::string(lutPresetName(lutPresetCount())), std::string(""));
    Lut3D lut;
    NC_CHECK(!lutPresetBuild(-1, lut));
    NC_CHECK(!lutPresetBuild(9999, lut));
}

NC_TEST(LutPresets, AllPresetsAreValid) {
    for (int i = 0; i < lutPresetCount(); ++i) {
        Lut3D lut;
        NC_CHECK(lutPresetBuild(i, lut));
        NC_CHECK(lut.valid());
        NC_CHECK_EQ(lut.size, 33);
        NC_CHECK_EQ(lut.data.size(), std::size_t(33 * 33 * 33 * 3));
    }
}

NC_TEST(LutPresets, AllPresetsAreRealTransforms) {
    /* **核心断言**：每个预设都必须真的改变颜色。
       如果某个预设是恒等表（输出 == 输入），那它就是假预设 ——
       用户点了没反应，正是原注释担心的那种情况。
       这里用中灰 + 三原色各采一次，任一通道变化 > 0.002 即算有效。 */
    const double probes[4][3] = {{0.5, 0.5, 0.5}, {0.8, 0.2, 0.2}, {0.2, 0.8, 0.2}, {0.2, 0.2, 0.8}};
    for (int i = 0; i < lutPresetCount(); ++i) {
        Lut3D lut;
        NC_CHECK(lutPresetBuild(i, lut));
        bool changed = false;
        for (const auto& p : probes) {
            double out[3];
            sampleAt(lut, p[0], p[1], p[2], out);
            for (int c = 0; c < 3; ++c) {
                if (std::fabs(out[c] - p[c]) > 0.002) {
                    changed = true;
                }
            }
        }
        NC_CHECK(changed);
    }
}

NC_TEST(LutPresets, PresetPathLoadsWithoutFile) {
    /* `loadCubeLutFile("preset:N")` 必须识别前缀并生成内存表 ——
       这是预览与导出共用的入口，走它才能保证「预览 = 导出」。 */
    for (int i : {0, 5, 14, 19}) {
        Lut3D lut;
        std::string err;
        const std::string path = "preset:" + std::to_string(i);
        NC_CHECK(loadCubeLutFile(path, lut, err));
        NC_CHECK(err.empty());
        NC_CHECK(lut.valid());
        NC_CHECK_EQ(lut.title, std::string(lutPresetName(i)));
    }
}

NC_TEST(LutPresets, InvalidPresetPathFails) {
    Lut3D lut;
    std::string err;
    NC_CHECK(!loadCubeLutFile("preset:999", lut, err));
    NC_CHECK(!err.empty());   // 必须给出原因，不能静默失败
}

NC_TEST(LutPresets, GrayscalePresetActuallyGrays) {
    /* 「黑白」预设必须把三通道拉成相等。
       只测「变了」不够 —— 一个错误实现可能只改亮度不改通道差异。 */
    int grayIndex = -1;
    for (int i = 0; i < lutPresetCount(); ++i) {
        if (std::string(lutPresetName(i)) == "黑白") {
            grayIndex = i;
        }
    }
    NC_CHECK(grayIndex >= 0);
    if (grayIndex < 0) return;

    Lut3D lut;
    NC_CHECK(lutPresetBuild(grayIndex, lut));
    double out[3];
    sampleAt(lut, 0.9, 0.2, 0.1, out);   // 强红色输入
    NC_CHECK(std::fabs(out[0] - out[1]) < 0.01);
    NC_CHECK(std::fabs(out[1] - out[2]) < 0.01);
}
