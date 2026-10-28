#include "pixel_baseline.h"
#include "render_harness.h"
#include "test_framework.h"
#include "nairoclip/core/editor.h"

#include <string>
#include <vector>

using namespace nairoclip::core;
using nairoclip::test::FrameDigest;
using nairoclip::test::renderDigests;

// =============================================================================
// 摇晃接入导出（补齐 B2-3 的渲染侧缺口）
// =============================================================================
//
// 背景：`Clip::shake` 加上后，如果 `export_executor` 不读它，字段就只是
// 存了个寂寞——测试必须能证明「导出时真的抖了」。
//
// 发现过程：先做变异「导出时不应用摇晃」→ **全量 1438 用例全绿**，
// 说明当时根本没有导出级覆盖。本文件就是补这个洞。

namespace {

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

    /// 插入一个 2 秒片段，可选摇晃。
    void insertClip(double amplitude, double frequency, std::uint32_t seed,
                    ShakeTrajectory traj = ShakeTrajectory::Horizontal) {
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(2);
        c.sourceIn = Rational::make(0);
        c.shake.amplitude = amplitude;
        c.shake.frequency = frequency;
        c.shake.seed = seed;
        c.shake.trajectory = traj;
        NC_CHECK(editor.insertClip(seqId, track, c).isOk());
    }
};

}  // namespace

NC_TEST(ShakeExport, ShakeChangesOutputPixels) {
    // 有摇晃 vs 无摇晃：同一工程同一素材，输出指纹必须不同
    Fixture plain(64, 64);
    plain.insertClip(0.0, 0.0, 0);

    Fixture shaken(64, 64);
    shaken.insertClip(0.05, 10.0, 7);

    const std::vector<FrameDigest> a = renderDigests(plain.project, 64, 64);
    const std::vector<FrameDigest> b = renderDigests(shaken.project, 64, 64);

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
            "摇晃没有改变任何一帧的输出——导出器可能没读 Clip::shake");
    }
}

NC_TEST(ShakeExport, ZeroAmplitudeIsNoOp) {
    // amplitude = 0 时输出必须与「完全没有 shake 字段」逐帧一致。
    // 这条守的是「默认不摇晃」：如果实现无条件应用 shake，
    // 所有既有工程的画面都会被改变。
    Fixture a(64, 64);
    a.insertClip(0.0, 10.0, 0);  // 频率有值但振幅 0

    Fixture b(64, 64);
    b.insertClip(0.0, 0.0, 0);

    const std::vector<FrameDigest> da = renderDigests(a.project, 64, 64);
    const std::vector<FrameDigest> db = renderDigests(b.project, 64, 64);
    NC_CHECK_EQ(da.size(), db.size());
    for (std::size_t i = 0; i < std::min(da.size(), db.size()); ++i) {
        NC_CHECK_EQ(da[i].hash, db[i].hash);
    }
}

NC_TEST(ShakeExport, ShakeIsDeterministic) {
    // 同一 seed 两次导出必须逐帧相同（摇动不能引入随机性）
    Fixture f(64, 64);
    f.insertClip(0.06, 12.0, 12345);

    const std::vector<FrameDigest> r1 = renderDigests(f.project, 64, 64);
    const std::vector<FrameDigest> r2 = renderDigests(f.project, 64, 64);
    NC_CHECK_EQ(r1.size(), r2.size());
    for (std::size_t i = 0; i < std::min(r1.size(), r2.size()); ++i) {
        NC_CHECK_EQ(r1[i].hash, r2[i].hash);
    }
}

NC_TEST(ShakeExport, DifferentSeedsGiveDifferentOutput) {
    // 不同 seed → 不同轨迹 → 输出应不同。
    //
    // **必须用 Random 轨迹**：seed 只在 `ShakeTrajectory::Random` 分支里参与
    // 计算（见 shake_effect.cpp 的 shakeOffsetAt）。Horizontal/Vertical/Circular
    // 是纯正弦，与 seed 无关——第一版这条用例用默认的 Horizontal，
    // 结果两个不同 seed 产出完全相同的画面，报错时一度让人以为 seed 没生效。
    Fixture s1(64, 64);
    s1.insertClip(0.06, 10.0, 1, ShakeTrajectory::Random);
    Fixture s2(64, 64);
    s2.insertClip(0.06, 10.0, 999, ShakeTrajectory::Random);

    const std::vector<FrameDigest> a = renderDigests(s1.project, 64, 64);
    const std::vector<FrameDigest> b = renderDigests(s2.project, 64, 64);
    NC_CHECK_EQ(a.size(), b.size());

    bool anyDiff = false;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (a[i].hash != b[i].hash) {
            anyDiff = true;
            break;
        }
    }
    if (!anyDiff) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                         "不同 seed 产生完全相同输出——seed 未参与计算");
    }
}

NC_TEST(ShakeExport, LargerAmplitudeChangesMore) {
    // 振幅越大，与基准的差异帧数应不少于小振幅（单调性弱断言）。
    // 不用「差异像素数」是因为指纹无法量化差异程度；用「有差异的帧数」。
    Fixture plain(64, 64);
    plain.insertClip(0.0, 10.0, 5);
    Fixture small(64, 64);
    small.insertClip(0.01, 10.0, 5);
    Fixture large(64, 64);
    large.insertClip(0.08, 10.0, 5);

    const std::vector<FrameDigest> base = renderDigests(plain.project, 64, 64);
    const std::vector<FrameDigest> s = renderDigests(small.project, 64, 64);
    const std::vector<FrameDigest> l = renderDigests(large.project, 64, 64);

    auto countDiff = [](const std::vector<FrameDigest>& x,
                        const std::vector<FrameDigest>& y) {
        int n = 0;
        for (std::size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
            if (x[i].hash != y[i].hash) ++n;
        }
        return n;
    };
    const int smallDiff = countDiff(base, s);
    const int largeDiff = countDiff(base, l);
    NC_CHECK(largeDiff >= smallDiff);
    NC_CHECK(largeDiff > 0);
}
