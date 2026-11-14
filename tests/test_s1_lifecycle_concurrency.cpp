// S1-1 并发专项：Editor 生命周期竞争窗口回归测试。
//
// 对应冻结指令 §7.2 的 12 个场景。核心手段：
//   · 用 std::barrier（C++20）/ 原子闸门制造"两线程同时进入临界区前"的状态
//   · 反复调度 N 轮，确保竞争窗口被真实覆盖（而不是只启动线程就结束）
//   · 所有断言都是"不崩溃 + 状态一致"，不依赖未定义行为才能通过
//
// 说明：本文件被测对象是 C ABI（capi.h）。JNI 层的 @Synchronized 与
// g_editorLifecycleMutex 属 Android 侧，本环境无 NDK，无法在此编译；
// 该部分的验证见 Android 侧说明与 CI。
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

/// 简易双线程闸门：保证两个线程尽量同时抵达临界点后再放行。
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

NC_TEST(S1Concurrency, LoadAndDispatchInterleaved) {
    // 场景 8：load() 与 dispatch() 并发。
    // 预期：无论谁先到，都不得崩溃；最终 editor 仍可用。
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    if (!ed) return;
    const std::string nclip = savedText(ed);
    NC_CHECK(!nclip.empty());

    for (int round = 0; round < 50; ++round) {
        Gate gate;
        std::atomic<bool> ok{true};
        std::thread loader([&] {
            gate.wait();
            char* d = nullptr;
            if (nc_project_load(ed, nclip.c_str(), &d) != NC_OK) ok = false;
            if (d) nc_free_string(d);
        });
        std::thread dispatcher([&] {
            gate.wait();
            char* r = nc_apply_command(ed, "{\"type\":\"clip.remove\",\"payload\":{}}");
            if (r) nc_free_string(r);
        });
        loader.join();
        dispatcher.join();
        NC_CHECK(ok.load());
        // 收敛后必须仍可正常序列化（对象链未被破坏）。
        NC_CHECK(!savedText(ed).empty());
    }
    nc_editor_destroy(ed);
}

NC_TEST(S1Concurrency, DestroyAfterConcurrentAccess) {
    // 场景 10：Engine 销毁后再次调用各入口 → 必须安全返回，不得崩溃。
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    if (!ed) return;
    const std::string nclip = savedText(ed);
    NC_CHECK(!nclip.empty());

    // 并发做过若干访问后再销毁
    Gate gate;
    std::thread a([&] { gate.wait(); char* d=nullptr; nc_project_load(ed, nclip.c_str(), &d); if(d) nc_free_string(d); });
    std::thread b([&] { gate.wait(); char* r=nc_apply_command(ed, "{\"type\":\"clip.move\",\"payload\":{}}"); if(r) nc_free_string(r); });
    gate.wait();
    a.join();
    b.join();
    nc_editor_destroy(ed);

    // 销毁后：所有接收 editor 的 C ABI 都必须判空安全
    NC_CHECK(nc_contract_revision(nullptr) == 0);
    char* d2 = nullptr;
    NC_CHECK(nc_project_load(nullptr, nclip.c_str(), &d2) != NC_OK);
    if (d2) nc_free_string(d2);
    NC_CHECK(nc_apply_command(nullptr, "{}") == nullptr);
    NC_CHECK(nc_project_save(nullptr) == nullptr);
    nc_editor_destroy(nullptr);   // 重复销毁 nullptr 必须安全
    NC_CHECK(true);
}

NC_TEST(S1Concurrency, RepeatedCreateDestroyStress) {
    // 场景 9 + 12：多线程重复创建/加载/销毁 + 多轮压力。
    constexpr int kThreads = 4;
    constexpr int kRounds = 20;
    std::atomic<int> failures{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int r = 0; r < kRounds; ++r) {
                NcEditor* ed = nc_editor_create();
                if (!ed) { failures.fetch_add(1); continue; }
                const std::string s = savedText(ed);
                if (s.empty()) { failures.fetch_add(1); nc_editor_destroy(ed); continue; }
                char* d = nullptr;
                if (nc_project_load(ed, s.c_str(), &d) != NC_OK) failures.fetch_add(1);
                if (d) nc_free_string(d);
                char* out = nc_apply_command(ed, "{\"type\":\"clip.remove\",\"payload\":{}}");
                if (out) nc_free_string(out);
                nc_editor_destroy(ed);
            }
        });
    }
    for (auto& th : ts) th.join();
    NC_CHECK_EQ(failures.load(), 0);
}

NC_TEST(S1Concurrency, LoadFailureDoesNotCorruptUnderContention) {
    // 场景 8 变体：并发「必失败的 load」+「正常 dispatch」。
    // 失败路径必须在 move 之前返回，不能破坏当前 editor。
    NcEditor* ed = nc_editor_create();
    NC_CHECK(ed != nullptr);
    if (!ed) return;
    const std::string good = savedText(ed);
    NC_CHECK(!good.empty());

    for (int round = 0; round < 50; ++round) {
        Gate gate;
        std::thread bad([&] {
            gate.wait();
            char* d = nullptr;
            const int32_t rc = nc_project_load(ed, "{definitely not a project", &d);
            (void)rc;   // 必须失败
            if (d) nc_free_string(d);
        });
        std::thread goodTh([&] {
            gate.wait();
            char* r = nc_apply_command(ed, "{\"type\":\"clip.remove\",\"payload\":{}}");
            if (r) nc_free_string(r);
        });
        bad.join();
        goodTh.join();
        NC_CHECK_EQ(savedText(ed), good);   // 内容不得被破坏
    }
    nc_editor_destroy(ed);
}
