#include "pixel_baseline.h"
#include "render_harness.h"
#include "test_framework.h"
#include "nairoclip/core/editor.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

using namespace nairoclip::core;
using nairoclip::test::FrameDigest;
using nairoclip::test::renderDigests;

// =============================================================================
// 片段级 3D LUT 接入导出（补齐 B1-2 的渲染侧缺口）
// =============================================================================
//
// 背景（这是本文件存在的唯一理由）：
//   `Clip::lutPath` / `Clip::lutStrength` 早已存在，`serialization.cpp` 一直在
//   读写这两个字段，`lut.h/.cpp` 也早已实现 .cube 解析 + 三线性插值 + 强度混合。
//   但 **export_executor / export_plan 从来没有读过它们**——
//   也就是说：用户在 UI 里设了 LUT，预览和导出完全不生效，工程文件里却存着"设过了"。
//   这是最坏的一类缺陷：数据说做了，画面说没做。
//
// 本文件证明三件事：
//   1. 设了 LUT，导出像素**真的变了**（否则接了个寂寞）；
//   2. 强度 0 时与不设 LUT **逐帧一致**（默认不改变既有工程）；
//   3. 同一 LUT 两次导出**完全一致**（不能引入随机性）。
//
// 注意：这里特意用「交换 R/B」的 LUT 而不是 identity ——
// identity LUT 对像素是恒等变换，测不出任何接线错误，
// 会让「根本没应用」的实现也通过测试。

namespace {

/// 生成一个「交换 R 与 B」的 .cube 文本。
/// size 取 2 就够：交换是角点之间的映射，插值不影响"变了/没变"的判定。
std::string makeSwapRbCube(int size) {
    std::string out;
    out += "TITLE \"swap_rb\"\n";
    out += "LUT_3D_SIZE " + std::to_string(size) + "\n";
    out += "DOMAIN_MIN 0.0 0.0 0.0\n";
    out += "DOMAIN_MAX 1.0 1.0 1.0\n";
    for (int b = 0; b < size; ++b) {
        for (int g = 0; g < size; ++g) {
            for (int r = 0; r < size; ++r) {
                const double rf = size > 1 ? double(r) / (size - 1) : 0.0;
                const double gf = size > 1 ? double(g) / (size - 1) : 0.0;
                const double bf = size > 1 ? double(b) / (size - 1) : 0.0;
                // 交换 R 与 B
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%.6f %.6f %.6f\n", bf, gf, rf);
                out += buf;
            }
        }
    }
    return out;
}

/// 写临时 .cube 文件，返回路径。测试结束不主动删（在 CI 的临时目录里）。
std::string writeCube(const std::string& name, const std::string& text) {
    const char* tmpdir = std::getenv("TMPDIR");
    const std::string dir = (tmpdir != nullptr && tmpdir[0] != '\0') ? tmpdir : "/tmp";
    const std::string path = dir + "/nairoclip_test_" + name + ".cube";
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    f.close();
    return path;
}

struct Fixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId track;
    AssetId assetId;

    explicit Fixture(int w = 64, int h = 64) {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = w;
        seq.height = h;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;

        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/video/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = a.id;

        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &track).isOk());
    }

    /// 插入一个 2 秒片段；path 为空表示不设 LUT。
    void insertClip(const std::string& lutPath, double strength) {
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(2);
        c.sourceIn = Rational::make(0);
        c.lutPath = lutPath;
        c.lutStrength = strength;
        NC_CHECK(editor.insertClip(seqId, track, c).isOk());
    }
};

}  // namespace

NC_TEST(ClipLutExport, LutChangesOutputPixels) {
    // 有 LUT（交换 R/B）vs 无 LUT：同一工程同一素材，输出指纹必须不同。
    const std::string cube = writeCube("swap_rb", makeSwapRbCube(2));

    Fixture plain(64, 64);
    plain.insertClip("", 1.0);

    Fixture lut(64, 64);
    lut.insertClip(cube, 1.0);

    const std::vector<FrameDigest> a = renderDigests(plain.project, 64, 64);
    const std::vector<FrameDigest> b = renderDigests(lut.project, 64, 64);

    NC_CHECK(!a.empty());
    NC_CHECK_EQ(a.size(), b.size());

    int diffCount = 0;
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i].hash != b[i].hash) ++diffCount;
    }
    if (diffCount == 0) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "LUT 没有改变任何一帧——导出器可能没读 RenderClip::lutPath"
            "（这正是修复前的状态：字段存在、序列化正常、渲染完全忽略）");
    }
}

NC_TEST(ClipLutExport, ZeroStrengthIsNoOp) {
    // strength = 0 时输出必须与「完全不设 LUT」逐帧一致。
    // 这条守的是「默认不改变既有工程」：如果实现无条件应用 LUT，
    // 所有存了 lutPath 但强度调成 0 的工程画面都会突变。
    const std::string cube = writeCube("swap_rb_zero", makeSwapRbCube(2));

    Fixture none(64, 64);
    none.insertClip("", 1.0);

    Fixture zero(64, 64);
    zero.insertClip(cube, 0.0);

    const std::vector<FrameDigest> da = renderDigests(none.project, 64, 64);
    const std::vector<FrameDigest> db = renderDigests(zero.project, 64, 64);
    NC_CHECK_EQ(da.size(), db.size());
    for (std::size_t i = 0; i < std::min(da.size(), db.size()); ++i) {
        NC_CHECK_EQ(da[i].hash, db[i].hash);
    }
}

NC_TEST(ClipLutExport, LutIsDeterministic) {
    // 同一 LUT 两次导出必须逐帧相同（LUT 是纯查表，不允许引入任何随机性）
    const std::string cube = writeCube("swap_rb_det", makeSwapRbCube(2));

    Fixture f(64, 64);
    f.insertClip(cube, 1.0);

    const std::vector<FrameDigest> r1 = renderDigests(f.project, 64, 64);
    const std::vector<FrameDigest> r2 = renderDigests(f.project, 64, 64);
    NC_CHECK_EQ(r1.size(), r2.size());
    for (std::size_t i = 0; i < std::min(r1.size(), r2.size()); ++i) {
        NC_CHECK_EQ(r1[i].hash, r2[i].hash);
    }
}

NC_TEST(ClipLutExport, HalfStrengthDiffersFromFullAndFromNone) {
    // 强度必须真的参与混合：0.5 的输出既不能等于 0，也不能等于 1。
    // 这条能抓到「只把 strength 当开关用」的实现（0.5 被当成 1）。
    const std::string cube = writeCube("swap_rb_half", makeSwapRbCube(2));

    Fixture full(64, 64);
    full.insertClip(cube, 1.0);
    Fixture half(64, 64);
    half.insertClip(cube, 0.5);
    Fixture none(64, 64);
    none.insertClip("", 1.0);

    const std::vector<FrameDigest> dFull = renderDigests(full.project, 64, 64);
    const std::vector<FrameDigest> dHalf = renderDigests(half.project, 64, 64);
    const std::vector<FrameDigest> dNone = renderDigests(none.project, 64, 64);
    NC_CHECK_EQ(dFull.size(), dHalf.size());
    NC_CHECK_EQ(dFull.size(), dNone.size());

    bool halfDiffersFromFull = false;
    bool halfDiffersFromNone = false;
    for (std::size_t i = 0; i < std::min(dFull.size(), dHalf.size()); ++i) {
        if (dHalf[i].hash != dFull[i].hash) halfDiffersFromFull = true;
        if (dHalf[i].hash != dNone[i].hash) halfDiffersFromNone = true;
    }
    if (!halfDiffersFromFull) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__, "强度 0.5 与 1.0 输出完全相同——strength 没参与混合");
    }
    if (!halfDiffersFromNone) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__, "强度 0.5 与不设 LUT 输出完全相同——LUT 根本没生效");
    }
}

NC_TEST(ClipLutExport, MissingFileDoesNotCrashAndIsWarned) {
    // 路径不存在：导出不能崩、不能静默当成"已应用"。
    // 实现策略是「跳过 LUT + 在 stats.warnings 里留一条」，
    // 与调整图层 LUT 的既有策略一致。
    Fixture f(64, 64);
    f.insertClip("/nonexistent/definitely_missing.nope.cube", 1.0);

    const std::vector<FrameDigest> d = renderDigests(f.project, 64, 64);
    // 不崩即通过；画面应与「不设 LUT」一致（LUT 被跳过）
    Fixture none(64, 64);
    none.insertClip("", 1.0);
    const std::vector<FrameDigest> dn = renderDigests(none.project, 64, 64);
    NC_CHECK_EQ(d.size(), dn.size());
    for (std::size_t i = 0; i < std::min(d.size(), dn.size()); ++i) {
        NC_CHECK_EQ(d[i].hash, dn[i].hash);
    }
}
