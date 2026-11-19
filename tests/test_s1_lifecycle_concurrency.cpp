// S1-1 / S1-5b 生命周期回归：遵守 C ABI 单句柄串行化契约。
//
// C ABI 明确规定：同一个 NcEditor 句柄不允许多线程并发调用。
// 因此本文件只并发操作彼此独立的句柄；单句柄 load→dispatch 测试保持串行。
// EditorLifetime / Mutex 的共享仲裁竞争由 test_s1_editor_lifetime.cpp 覆盖。
#include "test_framework.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "nairoclip/core/capi.h"

namespace {

std::string savedText(NcEditor* ed) {
    char* t = nc_project_save(ed);
    if (t == nullptr) return std::string();
    std::string s(t);
    nc_free_string(t);
    return s;
}

bool dispatchTouchesEditor(NcEditor* ed) {
    char* result = nc_apply_command(
        ed, "{\\"type\\":\\"clip.remove\\",\\"payload\\":{}}");
    if (result == nullptr) return false;
    nc_free_string(result);
    return true;
}

/// 双线程闸门，仅用于让两个独立句柄的操作同时起跑。
class Gate {
public:
    void wait() {
        ready_.fetch_add(1, std::memory_order_acq_rel);
        while (ready_.load(std::memory_order_acquire) < 2) {
            std::this_thread::yield();
        }
    }
private:
    std::atomic<int> ready_{0};
};

}  // namespace

NC_TEST(S1Concurrency, IndependentEditorsConcurrentLifecycle) {
    // 多线程可并行操作不同句柄；每个句柄内部严格串行。
    constexpr int kThreads = 4;
    constexpr int kRounds = 25;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            NcEditor* ed = nc_editor_create();
            if (ed == nullptr) {
                failures.fetch_add(1);
                return;
            }

            const std::string project = savedText(ed);
            if (project.empty()) {
                failures.fetch_add(1);
                nc_editor_destroy(ed);
                return;
            }

            for (int round = 0; round < kRounds; ++round) {
                char* detail = nullptr;
                const int32_t rc = nc_project_load(ed, project.c_str(), &detail);
                if (detail != nullptr) nc_free_string(detail);
                if (rc != NC_OK || !dispatchTouchesEditor(ed) || savedText(ed).empty()) {
                    failures.fetch_add(1);
                    break;
                }
            }
            nc_editor_destroy(ed);
        });
    }

    for (auto& thread : threads) thread.join();
    NC_CHECK_EQ(failures.load(), 0);
}

NC_TEST(S1Concurrency, DestroyAfterIndependentConcurrentAccess) {
    // 两个线程分别访问不同句柄；全部 join 后再销毁，避免悬空句柄竞态。
    NcEditor* first = nc_editor_create();
    NcEditor* second = nc_editor_create();
    NC_CHECK(first != nullptr);
    NC_CHECK(second != nullptr);
    if (first == nullptr || second == nullptr) {
        nc_editor_destroy(first);
        nc_editor_destroy(second);
        return;
    }

    const std::string firstProject = savedText(first);
    const std::string secondProject = savedText(second);
    NC_CHECK(!firstProject.empty());
    NC_CHECK(!secondProject.empty());

    Gate gate;
    std::atomic<bool> firstOk{true};
    std::atomic<bool> secondOk{true};
    std::thread a([&] {
        gate.wait();
        char* detail = nullptr;
        if (nc_project_load(first, firstProject.c_str(), &detail) != NC_OK) firstOk = false;
        if (detail != nullptr) nc_free_string(detail);
        if (!dispatchTouchesEditor(first)) firstOk = false;
    });
    std::thread b([&] {
        gate.wait();
        char* detail = nullptr;
        if (nc_project_load(second, secondProject.c_str(), &detail) != NC_OK) secondOk = false;
        if (detail != nullptr) nc_free_string(detail);
        if (!dispatchTouchesEditor(second)) secondOk = false;
    });
    a.join();
    b.join();

    NC_CHECK(firstOk.load());
    NC_CHECK(secondOk.load());
    nc_editor_destroy(first);
    nc_editor_destroy(second);

    // 销毁后：空句柄入口必须安全返回。
    NC_CHECK(nc_contract_revision(nullptr) == 0);
    char* detail = nullptr;
    NC_CHECK(nc_project_load(nullptr, firstProject.c_str(), &detail) != NC_OK);
    if (detail != nullptr) nc_free_string(detail);
    NC_CHECK(nc_apply_command(nullptr, "{}") == nullptr);
    NC_CHECK(nc_project_save(nullptr) == nullptr);
    nc_editor_destroy(nullptr);
    NC_CHECK(true);
}

NC_TEST(S1Concurrency, RepeatedCreateDestroyStress) {
    // 每个线程独立创建、使用和销毁自己的句柄。
    constexpr int kThreads = 4;
    constexpr int kRounds = 20;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&] {
            for (int round = 0; round < kRounds; ++round) {
                NcEditor* ed = nc_editor_create();
                if (ed == nullptr) {
                    failures.fetch_add(1);
                    continue;
                }
                const std::string project = savedText(ed);
                if (project.empty()) {
                    failures.fetch_add(1);
                    nc_editor_destroy(ed);
                    continue;
                }
                char* detail = nullptr;
                if (nc_project_load(ed, project.c_str(), &detail) != NC_OK) {
                    failures.fetch_add(1);
                }
                if (detail != nullptr) nc_free_string(detail);
                if (!dispatchTouchesEditor(ed)) failures.fetch_add(1);
                nc_editor_destroy(ed);
            }
        });
    }

    for (auto& thread : threads) thread.join();
    NC_CHECK_EQ(failures.load(), 0);
}

NC_TEST(S1Concurrency, LoadFailureDoesNotCorruptIndependentEditors) {
    // 错误载入与正常分发并行，但分别作用于不同句柄。
    // 这样既验证多句柄并发，又不违反单句柄串行化约束。
    NcEditor* badEditor = nc_editor_create();
    NcEditor* goodEditor = nc_editor_create();
    NC_CHECK(badEditor != nullptr);
    NC_CHECK(goodEditor != nullptr);
    if (badEditor == nullptr || goodEditor == nullptr) {
        nc_editor_destroy(badEditor);
        nc_editor_destroy(goodEditor);
        return;
    }

    const std::string badBefore = savedText(badEditor);
    const std::string goodBefore = savedText(goodEditor);
    NC_CHECK(!badBefore.empty());
    NC_CHECK(!goodBefore.empty());

    for (int round = 0; round < 50; ++round) {
        Gate gate;
        std::atomic<bool> badRejected{false};
        std::atomic<bool> goodDispatched{false};

        std::thread bad([&] {
            gate.wait();
            char* detail = nullptr;
            const int32_t rc = nc_project_load(
                badEditor, "{definitely not a project", &detail);
            badRejected.store(rc != NC_OK, std::memory_order_release);
            if (detail != nullptr) nc_free_string(detail);
        });
        std::thread good([&] {
            gate.wait();
            goodDispatched.store(
                dispatchTouchesEditor(goodEditor), std::memory_order_release);
        });

        bad.join();
        good.join();
        NC_CHECK(badRejected.load(std::memory_order_acquire));
        NC_CHECK(goodDispatched.load(std::memory_order_acquire));
        NC_CHECK_EQ(savedText(badEditor), badBefore);
        NC_CHECK(!savedText(goodEditor).empty());
    }

    nc_editor_destroy(badEditor);
    nc_editor_destroy(goodEditor);
}
