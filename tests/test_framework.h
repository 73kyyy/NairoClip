#pragma once
// NairoClip 轻量测试框架（零依赖）：用例自动注册、统一执行、失败计数。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace nairoclip {
namespace test {

struct TestCase {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back(TestCase{suite, name, std::move(fn)});
    }
};

inline int& failureCount() {
    static int c = 0;
    return c;
}

inline void reportFailure(const char* file, int line, const std::string& msg) {
    std::printf("    [FAIL] %s:%d  %s\n", file, line, msg.c_str());
    ++failureCount();
}

/// 堆一致性探针（NC_TEST_CHECK_HEAP=1）。
///
/// 分配一批尺寸互不相同的小块再释放，强制 glibc 遍历 unsorted bin /
/// tcache 并校验相邻 chunk 的 size 字段。堆被写坏时这里会 abort，
/// 把「延迟暴露」的破坏钉到真正发生它的那个用例上。
struct HeapProbe {
    bool enabled = false;
    HeapProbe() {
        const char* v = std::getenv("NC_TEST_CHECK_HEAP");
        enabled = v != nullptr && v[0] != '0';
    }
    void probe() const {
        if (!enabled) return;
        // 覆盖足够多的尺寸档位。
        //
        // 早先只用了 16 + i*24 这一条等差序列（128 个块，最大 ~3KB），
        // 结果「导出后探针通过」却仍在后面崩 —— 因为被写坏的那个 chunk
        // 尺寸可能不在序列里，探针根本没复用到它，等于没检查。
        //
        // 现在三档齐上：
        //   1) 小尺寸密集（16..2KB，覆盖 tcache 与 smallbin）
        //   2) 中尺寸（2KB..64KB，覆盖 large bin 起点）
        //   3) 精确复刻 malloc_printerr 报过的请求量（1048 / 2048），
        //      这两处是实测 glibc 报错时报出的 bytes 值
        constexpr int kSmall = 96;
        constexpr int kMid = 24;
        constexpr int kExact = 2;
        void* blocks[kSmall + kMid + kExact];
        std::size_t sizes[kSmall + kMid + kExact];
        int n = 0;
        for (int i = 0; i < kSmall; ++i, ++n) sizes[n] = 16 + static_cast<std::size_t>(i) * 21;
        for (int i = 0; i < kMid; ++i, ++n) {
            sizes[n] = 2048 + static_cast<std::size_t>(i) * 2600;
        }
        sizes[n++] = 1048;
        sizes[n++] = 2048;
        for (int i = 0; i < n; ++i) {
            blocks[i] = std::malloc(sizes[i]);
            if (blocks[i] != nullptr) std::memset(blocks[i], 0xCD, sizes[i]);
        }
        // 全部写满后再释放：glibc 会把它们并入 unsorted bin，
        // 并复核每个 chunk 的 header 是否与本该的一致。
        for (int i = 0; i < n; ++i) std::free(blocks[i]);
    }
};
inline HeapProbe& heapProbe() {
    static HeapProbe p;
    return p;
}

/// 分段执行支持（NC_TEST_SHARD / NC_TEST_SHARDS 环境变量）。
///
/// 为什么需要：全量套件单次运行约 9 分钟，在有回合预算限制的环境里
/// 很容易在跑完前被截断，导致「跑了一半」既不算通过也不算失败。
/// 分成两段各跑 ~4.5 分钟可以把结果拿到手。
///
/// 按注册顺序取模分片：同一份代码每次分片结果稳定可复现。
inline int runAll() {
    int shard = 0;
    int shards = 1;
    if (const char* s = std::getenv("NC_TEST_SHARDS")) {
        const int v = std::atoi(s);
        if (v > 0) shards = v;
    }
    if (const char* s = std::getenv("NC_TEST_SHARD")) {
        const int v = std::atoi(s);
        if (v >= 0) shard = v;
    }
    int failedCases = 0;
    const int total = static_cast<int>(registry().size());
    // NC_TEST_SKIP=N：跳过注册顺序前 N 个用例。
    // 用途：CI 上出现「跨用例堆污染」时二分定位污染源 ——
    // 从中间开始跑仍然崩，说明污染源在后半段；不崩则在前半段。
    int skip = 0;
    if (const char* s = std::getenv("NC_TEST_SKIP")) {
        const int v = std::atoi(s);
        if (v > 0) skip = v;
    }
    // NC_TEST_MAX=N：只跑注册顺序前 N 个用例（配合 NC_TEST_SKIP 表示区间）。
    // 用途：valgrind 下跑全量太慢，只跑到出问题的位置即可拿到报告。
    int maxIndex = -1;
    if (const char* s = std::getenv("NC_TEST_MAX")) {
        const int v = std::atoi(s);
        if (v > 0) maxIndex = v;
    }
    // NC_TEST_EXCLUDE：按名字排除用例，逗号分隔，如
    //   NC_TEST_EXCLUDE=Capi.WatermarkRealExportEndToEnd,Other.Case
    //
    // 为什么需要它（而不是只用 SKIP/MAX）：
    // SKIP/MAX 是**位置型**的，只要新增或删掉任何一个用例，索引就整体漂移。
    // CI 里为此写死过「崩溃在索引 267」，之后每加一批测试都要同步改数字，
    // 忘改就会把崩溃用例又跑起来（或误跳过好用例）。
    // 按名字排除与注册顺序无关，新增测试不会让它失效。
    std::vector<std::string> excludes;
    if (const char* s = std::getenv("NC_TEST_EXCLUDE")) {
        std::string acc;
        for (const char* p = s;; ++p) {
            if (*p == ',' || *p == '\0') {
                // 去掉首尾空白，允许 " A, B " 这种写法
                std::size_t b = acc.find_first_not_of(" \t");
                std::size_t e = acc.find_last_not_of(" \t");
                if (b != std::string::npos) {
                    excludes.push_back(acc.substr(b, e - b + 1));
                }
                acc.clear();
                if (*p == '\0') break;
            } else {
                acc.push_back(*p);
            }
        }
    }
    auto excluded = [&excludes](const std::string& suite, const std::string& name) {
        const std::string full = suite + "." + name;
        for (const auto& x : excludes) {
            if (x == full) return true;
        }
        return false;
    };

    // NC_TEST_INCLUDE：只跑指定前缀的用例，逗号分隔，如
    //   NC_TEST_INCLUDE=CapiLut.,ClipLutExport.
    //
    // 用途：全量在 CI 上会撞到既有的 glibc 堆破坏而中途 abort，
    // 后面的用例根本没执行 —— 于是「新加的测试到底过没过」无法判定
    // （没跑到 和 跑过了 分不清）。按前缀单独跑一遍给出独立结论。
    // 与 NC_TEST_EXCLUDE 同时存在时，先 include 再 exclude。
    std::vector<std::string> includes;
    if (const char* s = std::getenv("NC_TEST_INCLUDE")) {
        std::string acc;
        for (const char* p = s;; ++p) {
            if (*p == ',' || *p == '\0') {
                std::size_t b = acc.find_first_not_of(" \t");
                std::size_t e = acc.find_last_not_of(" \t");
                if (b != std::string::npos) {
                    includes.push_back(acc.substr(b, e - b + 1));
                }
                acc.clear();
                if (*p == '\0') break;
            } else {
                acc.push_back(*p);
            }
        }
    }
    auto selected = [&includes](const std::string& suite, const std::string& name) {
        if (includes.empty()) return true;   // 未指定 = 全选
        const std::string full = suite + "." + name;
        for (const auto& x : includes) {
            // 支持前缀（"CapiLut." 命中 "CapiLut.Foo"）与全名
            if (full.compare(0, x.size(), x) == 0) return true;
        }
        return false;
    };

    int index = -1;
    for (auto& tc : registry()) {
        ++index;
        if (index < skip) {
            continue;
        }
        if (maxIndex >= 0 && index >= maxIndex) {
            break;
        }
        if (!selected(tc.suite, tc.name)) {
            continue;
        }
        if (excluded(tc.suite, tc.name)) {
            std::printf("[ SKIP ] %s.%s（NC_TEST_EXCLUDE）\n", tc.suite.c_str(),
                        tc.name.c_str());
            continue;
        }
        if (shards > 1 && (index % shards) != shard) {
            continue;
        }
        const int before = failureCount();
        std::printf("[ RUN  ] %s.%s\n", tc.suite.c_str(), tc.name.c_str());
        tc.fn();
        // NC_TEST_CHECK_HEAP=1：每个用例后做一次堆一致性检查。
        //
        // 为什么需要：CI（x64/glibc）报 "malloc(): invalid size (unsorted)"
        // 时，崩溃点往往离真凶上百个用例 —— 堆元数据被写坏后并不会立即报，
        // 要等下一次 malloc 才炸。而 ASAN 用自己的分配器，没有 glibc 的
        // unsorted bin，这类破坏在 ASAN 下完全不可见（实测：ASAN 全量跑完
        // 1444 用例 0 报错，非 ASAN 仍在同一处崩）。
        //
        // 做法：malloc/free 一轮大小不一的块，迫使 glibc 遍历 unsorted bin
        // 并复核 chunk header；被写坏的 header 会在这里立刻 abort。
        // 于是「崩溃点」就被钉在真正写坏它的那个用例上。
        heapProbe().probe();
        if (failureCount() == before) {
            std::printf("[  OK  ] %s.%s\n", tc.suite.c_str(), tc.name.c_str());
        } else {
            ++failedCases;
        }
    }
    if (shards > 1) {
        std::printf("\n===== 分片 %d/%d：共 %d cases | %d failed | %d checks failed =====\n",
                    shard, shards, total, failedCases, failureCount());
    } else {
        std::printf("\n===== %d cases | %d failed | %d checks failed =====\n", total, failedCases,
                    failureCount());
    }
    return failedCases == 0 ? 0 : 1;
}

}  // namespace test
}  // namespace nairoclip

#define NC_TEST(suite, name)                                                 \
    static void nc_test_##suite##_##name();                                  \
    static ::nairoclip::test::Registrar nc_reg_##suite##_##name(             \
        #suite, #name, nc_test_##suite##_##name);                            \
    static void nc_test_##suite##_##name()

#define NC_CHECK(cond)                                                       \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ::nairoclip::test::reportFailure(__FILE__, __LINE__,             \
                                             "CHECK(" #cond ")");            \
        }                                                                    \
    } while (0)

#define NC_CHECK_EQ(a, b)                                                    \
    do {                                                                     \
        if (!((a) == (b))) {                                                 \
            ::nairoclip::test::reportFailure(__FILE__, __LINE__,             \
                                             "CHECK_EQ(" #a ", " #b ")");   \
        }                                                                    \
    } while (0)
