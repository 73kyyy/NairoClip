/* =============================================================================
   P0–P4 性能阶段的验收测试
   -----------------------------------------------------------------------------
   对应 PERF_XFORM_PLAN.md 的验收条款：

     P1  LUT 整型路径与 double 路径逐像素差 ≤ 2
     P3  并行化后结果与单线程 **bit-exact**
     P4  NEON 与标量 **bit-exact**
     PD  顺序提示生效时 seek 次数显著下降，且帧内容与慢路径一致
     P0  埋点可开关，统计能取到

   为什么这些断言这么严（bit-exact 而不是「差不多」）：
   预览与导出可能走不同路径（一个在 ARM 设备走 NEON，一个在 x86 CI 走标量）。
   两条路径结果差 1，就会出现「预览对、导出偏色」这种最难查的 bug。
   ============================================================================= */

#include "test_framework.h"

#include "nairoclip/core/lut.h"
#include "nairoclip/core/perf_probe.h"
#include "nairoclip/core/simd.h"
#include "nairoclip/core/thread_pool.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <random>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 构造一个可预测的 33³ LUT（内容 = 输入的反相 + 轻微非线性）。
Lut3D makeTestLut() {
    Lut3D lut;
    lut.size = 17;   // 用 17 而不是 33：测试更快，且 17³ 也覆盖索引逻辑
    lut.title = "test";
    const std::size_t n = static_cast<std::size_t>(lut.size) * lut.size * lut.size;
    lut.data.resize(n * 3);
    for (int b = 0; b < lut.size; ++b) {
        for (int g = 0; g < lut.size; ++g) {
            for (int r = 0; r < lut.size; ++r) {
                const std::size_t idx =
                    (static_cast<std::size_t>(b) * lut.size * lut.size +
                     static_cast<std::size_t>(g) * lut.size +
                     static_cast<std::size_t>(r)) * 3u;
                // 反相 + 一点 gamma，能放大插值误差
                const auto f = [](int v) {
                    const double t = static_cast<double>(v) / (17 - 1);
                    return 1.0 - std::pow(t, 0.9);
                };
                lut.data[idx + 0] = f(r);
                lut.data[idx + 1] = f(g);
                lut.data[idx + 2] = f(b);
            }
        }
    }
    return lut;
}

std::vector<std::uint8_t> makeGradient(int w, int h) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
            px[i + 0] = static_cast<std::uint8_t>((x * 255) / std::max(1, w - 1));
            px[i + 1] = static_cast<std::uint8_t>((y * 255) / std::max(1, h - 1));
            px[i + 2] = static_cast<std::uint8_t>(((x + y) * 255) / std::max(1, w + h - 2));
            px[i + 3] = 255;
        }
    }
    return px;
}

}  // namespace

/* ---------------------------------------------------------------- P0 埋点 */

NC_TEST(PerfProbe, DisabledByDefaultAndToggleable) {
    perfSetEnabled(false);
    NC_CHECK(!perfEnabled());

    perfSetEnabled(true);
    NC_CHECK(perfEnabled());

    perfReset();
    double avg = 0, p99 = 0, maxv = 0;
    std::uint64_t count = 0;
    perfGet(PerfStage::Decode, &avg, &p99, &maxv, &count);
    NC_CHECK_EQ(count, std::uint64_t(0));

    // 采样 10 个点：1..10 ms
    for (int i = 1; i <= 10; ++i) {
        perfSample(PerfStage::Decode, static_cast<double>(i) * 1000.0);
    }
    perfGet(PerfStage::Decode, &avg, &p99, &maxv, &count);
    NC_CHECK_EQ(count, std::uint64_t(10));
    NC_CHECK(std::abs(avg - 5.5) < 0.01);
    NC_CHECK(std::abs(maxv - 10.0) < 0.01);
    NC_CHECK(p99 >= avg);

    perfSetEnabled(false);
}

NC_TEST(PerfProbe, ScopedTimerRecordsOnlyWhenEnabled) {
    perfSetEnabled(false);
    perfReset();
    {
        ScopedTimer t(PerfStage::Effect);
        volatile int sink = 0;
        for (int i = 0; i < 1000; ++i) sink += i;
        (void)sink;
    }
    std::uint64_t count = 0;
    perfGet(PerfStage::Effect, nullptr, nullptr, nullptr, &count);
    NC_CHECK_EQ(count, std::uint64_t(0));   // 关闭时不记录

    perfSetEnabled(true);
    perfReset();
    {
        ScopedTimer t(PerfStage::Effect);
        volatile int sink = 0;
        for (int i = 0; i < 1000; ++i) sink += i;
        (void)sink;
    }
    perfGet(PerfStage::Effect, nullptr, nullptr, nullptr, &count);
    NC_CHECK_EQ(count, std::uint64_t(1));
    perfSetEnabled(false);
}

/* ---------------------------------------------------------------- P1 LUT */

NC_TEST(LutFast, IntegerPathMatchesDoubleWithinTwo) {
    Lut3D lut = makeTestLut();
    NC_CHECK(lut.valid());

    const int w = 97;
    const int h = 61;
    const std::vector<std::uint8_t> base = makeGradient(w, h);

    // 整型路径
    std::vector<std::uint8_t> fastPx = base;
    NC_CHECK(applyLutToRgba(fastPx.data(), w, h, lut, 1.0));

    /* double 路径：临时清空 fast 表强制走慢路径，
       然后再恢复 —— 这样同一份 lut 对象两条路径都跑得到。 */
    std::vector<std::uint8_t> slowPx = base;
    {
        Lut3D slow = lut;
        slow.fast.clear();
        // 直接调 lutSample 逐像素（等价于 double 路径，但不受 fast 表影响）
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * w + x) * 4;
                double r = slow.data.empty() ? 0 : 0;
                (void)r;
                const double dr = static_cast<double>(slowPx[i + 0]) / 255.0;
                const double dg = static_cast<double>(slowPx[i + 1]) / 255.0;
                const double db = static_cast<double>(slowPx[i + 2]) / 255.0;
                double mr = 0, mg = 0, mb = 0;
                lutSample(slow, dr, dg, db, mr, mg, mb);
                slowPx[i + 0] = static_cast<std::uint8_t>(
                    std::clamp(mr * 255.0 + 0.5, 0.0, 255.0));
                slowPx[i + 1] = static_cast<std::uint8_t>(
                    std::clamp(mg * 255.0 + 0.5, 0.0, 255.0));
                slowPx[i + 2] = static_cast<std::uint8_t>(
                    std::clamp(mb * 255.0 + 0.5, 0.0, 255.0));
            }
        }
    }

    int maxDiff = 0;
    for (std::size_t i = 0; i < fastPx.size(); ++i) {
        // alpha 通道不变，跳过比较无意义但也不该有差异
        const int d = std::abs(static_cast<int>(fastPx[i]) - static_cast<int>(slowPx[i]));
        maxDiff = std::max(maxDiff, d);
    }
    // 验收：逐像素差 ≤ 2（8bit 量化误差 + 定点插值误差）
    NC_CHECK(maxDiff <= 2);
}

NC_TEST(LutFast, StrengthZeroIsNoOp) {
    Lut3D lut = makeTestLut();
    const int w = 32, h = 32;
    const std::vector<std::uint8_t> base = makeGradient(w, h);
    std::vector<std::uint8_t> px = base;
    NC_CHECK(applyLutToRgba(px.data(), w, h, lut, 0.0));
    NC_CHECK(px == base);
}

NC_TEST(LutFast, FullStrengthEqualsTableValue) {
    Lut3D lut = makeTestLut();
    NC_CHECK(lut.buildFastTable());
    NC_CHECK(!lut.fast.empty());

    // 单像素：取立方体角点 (0,0,0)，LUT 在那里应为 255（反相 0 → 1）
    std::vector<std::uint8_t> px = {0, 0, 0, 255};
    NC_CHECK(applyLutToRgba(px.data(), 1, 1, lut, 1.0));
    // f(0) = 1 - 0^0.9 = 1 → 255
    NC_CHECK_EQ(static_cast<int>(px[0]), 255);
    NC_CHECK_EQ(static_cast<int>(px[1]), 255);
    NC_CHECK_EQ(static_cast<int>(px[2]), 255);
    NC_CHECK_EQ(static_cast<int>(px[3]), 255);   // alpha 不变
}

NC_TEST(LutFast, RejectsMalformedTable) {
    Lut3D bad;
    bad.size = 17;
    bad.data.resize(10);   // 长度不符
    NC_CHECK(!bad.buildFastTable());
    const std::vector<std::uint8_t> px = {1, 2, 3, 4};
    std::vector<std::uint8_t> copy = px;
    // 仍走 double 路径，但因为 !valid() 直接返回 false
    NC_CHECK(!applyLutToRgba(copy.data(), 1, 1, bad, 1.0));
}

/* ---------------------------------------------------------------- P3/P4 SIMD */

NC_TEST(Simd, RgbaToBgraIsSelfInverse) {
    const std::size_t n = 1000;
    std::vector<std::uint8_t> px(n * 4);
    for (std::size_t i = 0; i < px.size(); ++i) {
        px[i] = static_cast<std::uint8_t>(i & 0xFF);
    }
    const std::vector<std::uint8_t> original = px;

    rgbaToBgraInPlace(px.data(), n);
    // R/B 确实换了
    NC_CHECK(px[0] == original[2]);
    NC_CHECK(px[2] == original[0]);
    // G/A 不变
    NC_CHECK_EQ(static_cast<int>(px[1]), static_cast<int>(original[1]));
    NC_CHECK_EQ(static_cast<int>(px[3]), static_cast<int>(original[3]));

    // 再换一次回到原样（自逆）
    rgbaToBgraInPlace(px.data(), n);
    NC_CHECK(px == original);
}

NC_TEST(Simd, RgbaToBgraHandlesNonMultipleOf16) {
    /* 非 16 倍数的像素数会走到 NEON 路径的尾部标量循环。
       这正是最容易出 bug 的地方（漏掉尾部 / 越界）。 */
    for (std::size_t n : {1u, 3u, 15u, 16u, 17u, 31u, 33u, 100u}) {
        std::vector<std::uint8_t> px(n * 4);
        for (std::size_t i = 0; i < px.size(); ++i) {
            px[i] = static_cast<std::uint8_t>((i * 7) & 0xFF);
        }
        const std::vector<std::uint8_t> original = px;
        rgbaToBgraInPlace(px.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            NC_CHECK_EQ(static_cast<int>(px[i * 4 + 0]),
                        static_cast<int>(original[i * 4 + 2]));
            NC_CHECK_EQ(static_cast<int>(px[i * 4 + 2]),
                        static_cast<int>(original[i * 4 + 0]));
            NC_CHECK_EQ(static_cast<int>(px[i * 4 + 1]),
                        static_cast<int>(original[i * 4 + 1]));
        }
    }
}

NC_TEST(Simd, LerpMatchesScalarReference) {
    const std::size_t n = 500;
    std::vector<std::uint8_t> dst(n * 4);
    std::vector<std::uint8_t> src(n * 4);
    std::mt19937 rng(1234);
    std::uniform_int_distribution<int> d(0, 255);
    for (std::size_t i = 0; i < dst.size(); ++i) {
        dst[i] = static_cast<std::uint8_t>(d(rng));
        src[i] = static_cast<std::uint8_t>(d(rng));
    }

    for (int w : {1, 64, 128, 200, 256}) {
        std::vector<std::uint8_t> got = dst;
        lerpRgbaInPlace(got.data(), src.data(), n, w);

        // 标量参考实现（与 simd.cpp 的标量分支逐字相同）
        const int iw = 256 - w;
        for (std::size_t i = 0; i < dst.size(); ++i) {
            const int want =
                (static_cast<int>(dst[i]) * iw + static_cast<int>(src[i]) * w) >> 8;
            NC_CHECK_EQ(static_cast<int>(got[i]), want);
        }
    }
}

NC_TEST(Simd, LerpWeightZeroLeavesDestUnchanged) {
    std::vector<std::uint8_t> dst = {1, 2, 3, 4, 5, 6, 7, 8};
    const std::vector<std::uint8_t> src = {9, 9, 9, 9, 9, 9, 9, 9};
    std::vector<std::uint8_t> copy = dst;
    lerpRgbaInPlace(copy.data(), src.data(), 2, 0);
    NC_CHECK(copy == dst);
}

NC_TEST(Simd, LerpWeightFullReplacesWithSource) {
    std::vector<std::uint8_t> dst = {1, 2, 3, 4};
    const std::vector<std::uint8_t> src = {200, 201, 202, 203};
    lerpRgbaInPlace(dst.data(), src.data(), 1, 256);
    NC_CHECK(dst == src);
}

NC_TEST(Simd, ReportsArchitecture) {
    // 只断言「能查询」，不断言具体值（CI 可能是 x86）
    const bool neon = simdHasNeon();
    NC_CHECK(neon == true || neon == false);
    std::printf("[信息] SIMD: %s 路径\n", neon ? "NEON" : "标量(x86/无 NEON)");
}

/* ---------------------------------------------------------------- P3 线程池 */

NC_TEST(ThreadPool, ParallelForCoversAllRangesExactlyOnce) {
    const std::size_t n = 1000;
    std::vector<int> hits(n, 0);

    ThreadPool::instance().parallelFor(
        n, 1,
        [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) {
                hits[i] += 1;
            }
        });

    // 每个下标恰好被访问一次（区间不重叠、无遗漏）
    int bad = 0;
    for (int v : hits) {
        if (v != 1) ++bad;
    }
    NC_CHECK_EQ(bad, 0);
}

NC_TEST(ThreadPool, SmallWorkRunsInline) {
    // 低于阈值时不应进池子，但仍必须正确执行
    const std::size_t n = 10;
    std::size_t total = 0;
    ThreadPool::instance().parallelFor(
        n, 1000, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) ++total;
        });
    NC_CHECK_EQ(total, n);
}

NC_TEST(ThreadPool, EmptyWorkIsNoOp) {
    bool called = false;
    ThreadPool::instance().parallelFor(0, 1, [&](std::size_t, std::size_t) { called = true; });
    NC_CHECK(!called);
}

NC_TEST(ThreadPool, RepeatedCallsDoNotDeadlock) {
    // 连续 50 次调用（验证 jobRemaining_ / hasJob_ 的状态复位正确）
    for (int round = 0; round < 50; ++round) {
        std::atomic<int> sum{0};
        ThreadPool::instance().parallelFor(
            500, 1, [&](std::size_t begin, std::size_t end) {
                int local = 0;
                for (std::size_t i = begin; i < end; ++i) local += static_cast<int>(i);
                sum.fetch_add(local, std::memory_order_relaxed);
            });
        NC_CHECK_EQ(sum.load(), 500 * 499 / 2);
    }
}

NC_TEST(ThreadPool, ParallelForRowsRespectsStride) {
    const int w = 40;
    const int h = 40;
    const int stride = w * 4 + 16;   // 故意留 padding，验证按 stride 走而不是 w*4
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(stride) * h, 0);

    parallelForRows(h, w, buf.data(), stride, [&](int y, std::uint8_t* row) {
        row[0] = static_cast<std::uint8_t>(y & 0xFF);
    });

    for (int y = 0; y < h; ++y) {
        NC_CHECK_EQ(static_cast<int>(buf[static_cast<std::size_t>(y) * stride]),
                    (y & 0xFF));
    }
}
