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
        // 尺寸刻意错开，避免全部命中同一个 bin（那样检查不到相邻 chunk）
        constexpr int kCount = 128;
        void* blocks[kCount];
        for (int i = 0; i < kCount; ++i) {
            blocks[i] = std::malloc(static_cast<std::size_t>(16 + i * 24));
            if (blocks[i] != nullptr) {
                std::memset(blocks[i], 0xCD, static_cast<std::size_t>(16 + i * 24));
            }
        }
        // 全部写满后再释放：此时 glibc 会把它们合并进 unsorted bin，
        // 并复核每个 chunk 的 header 是否与本该的一致。
        for (int i = 0; i < kCount; ++i) {
            std::free(blocks[i]);
        }
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
    int index = -1;
    for (auto& tc : registry()) {
        ++index;
        if (index < skip) {
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
