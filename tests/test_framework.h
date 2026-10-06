#pragma once
// NairoClip 轻量测试框架（零依赖）：用例自动注册、统一执行、失败计数。
#include <cstdio>
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

inline int runAll() {
    int failedCases = 0;
    const int total = static_cast<int>(registry().size());
    for (auto& tc : registry()) {
        const int before = failureCount();
        std::printf("[ RUN  ] %s.%s\n", tc.suite.c_str(), tc.name.c_str());
        tc.fn();
        if (failureCount() == before) {
            std::printf("[  OK  ] %s.%s\n", tc.suite.c_str(), tc.name.c_str());
        } else {
            ++failedCases;
        }
    }
    std::printf("\n===== %d cases | %d failed | %d checks failed =====\n", total, failedCases,
                failureCount());
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
