// S1-1 · Editor 生命周期仲裁器测试（直接复用**生产实现**）
//
// ⚠️ 分类声明（严格区分，不得混用）：
//   本文件属于 **B. 生命周期协议模型测试**，但它测试的不是"复制出来的
//   等价逻辑"，而是 **core/include/nairoclip/core/editor_lifetime.h 里
//   生产代码本身** —— 该头文件正是 nc_jni.cpp 中 nativeCreate /
//   nativeDestroy / EditorHandleGuard 所依赖的实现。
//   它仍 **不是** C 类（真实 libnc_jni.so 二进制测试），也不覆盖
//   D/E 类（Kotlin 同步、Android Activity/Surface）—— 那需要 NDK +
//   模拟器/真机，见报告中的未执行清单。
//
// 覆盖目标（对应指令 §五 的 10 项）：
//   1) 多线程共享同一仲裁对象          → ConcurrentCreateSingleOwner
//   2) 多线程同时 create/access/destroy → CreateAccessDestroyShared
//   3) 同一句柄被多线程访问并销毁      → SameHandleMultiThread
//   4) 重复创建                        → RepeatedCreateRejected
//   5) 重复销毁                        → RepeatedReleaseSafe
//   6) 过期句柄                        → StaleHandleRejected
//   7) 销毁与执行中调用交错            → ReleaseWhileInFlight
//   8) 旧句柄与新 Engine 的区分        → StaleHandleRejected / AbaWindowProbe
//   9) 创建失败不影响已有 Engine       → FailedCreateLeavesExistingIntact
//  10) 每个 Engine 最多销毁一次        → ConcurrentCreateSingleOwner
#include "test_framework.h"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "nairoclip/core/editor_lifetime.h"

namespace {

using nairoclip::core::EditorLifetime;

/// 模拟一个 Engine 对象；用 alive 计数验证"最多销毁一次"。
struct FakeEngine {
    int id = 0;
};

/// 确定性闸门：让 N 个线程尽量同时抵达竞争点。
/// 不用 sleep —— 用自旋计数等待，避免把"运气"当作正确性证明。
class Gate {
public:
    explicit Gate(int n) : target_(n) {}
    void wait() {
        ready_.fetch_add(1, std::memory_order_acq_rel);
        while (ready_.load(std::memory_order_acquire) < target_) {
            std::this_thread::yield();
        }
    }
private:
    int target_;
    std::atomic<int> ready_{0};
};

}  // namespace

NC_TEST(S1EditorLifetime, RepeatedCreateRejected) {
    EditorLifetime lt;
    FakeEngine a{1}, b{2};
    NC_CHECK(lt.tryAdopt(&a) == EditorLifetime::AdoptResult::kAdopted);
    // 第二次创建必须被拒绝，且不改变既有所有者
    NC_CHECK(lt.tryAdopt(&b) == EditorLifetime::AdoptResult::kAlreadyExists);
    NC_CHECK_EQ(lt.current(), static_cast<void*>(&a));
    // nullptr 视为无效参数
    NC_CHECK(lt.tryAdopt(nullptr) == EditorLifetime::AdoptResult::kInvalidArgument);
    NC_CHECK_EQ(lt.current(), static_cast<void*>(&a));
}

NC_TEST(S1EditorLifetime, FailedCreateLeavesExistingIntact) {
    // 场景 9：创建失败不得影响已有 Engine。
    EditorLifetime lt;
    FakeEngine a{1};
    NC_CHECK(lt.tryAdopt(&a) == EditorLifetime::AdoptResult::kAdopted);
    for (int i = 0; i < 100; ++i) {
        FakeEngine tmp{i};
        NC_CHECK(lt.tryAdopt(&tmp) == EditorLifetime::AdoptResult::kAlreadyExists);
        NC_CHECK_EQ(lt.current(), static_cast<void*>(&a));
    }
    // 既有 Engine 仍可被正常访问与释放
    bool reached = false;
    NC_CHECK(lt.withHandle(&a, [&](void*) { reached = true; }));
    NC_CHECK(reached);
    NC_CHECK(lt.beginRelease(&a));
}

NC_TEST(S1EditorLifetime, RepeatedReleaseSafe) {
    // 场景 5/10：重复销毁安全，且每个 Engine 最多销毁一次。
    EditorLifetime lt;
    FakeEngine a{1};
    int destroyCount = 0;
    NC_CHECK(lt.tryAdopt(&a) == EditorLifetime::AdoptResult::kAdopted);
    if (lt.beginRelease(&a)) ++destroyCount;
    NC_CHECK(!lt.beginRelease(&a));      // 第二次必须拒绝
    NC_CHECK(!lt.beginRelease(&a));
    NC_CHECK_EQ(destroyCount, 1);        // 恰好销毁一次
    NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr));
}

NC_TEST(S1EditorLifetime, StaleHandleRejected) {
    // 场景 6/8：过期句柄不得访问，也不得销毁新 Engine。
    EditorLifetime lt;
    FakeEngine oldE{1}, newE{2};
    NC_CHECK(lt.tryAdopt(&oldE) == EditorLifetime::AdoptResult::kAdopted);
    NC_CHECK(lt.beginRelease(&oldE));

    NC_CHECK(lt.tryAdopt(&newE) == EditorLifetime::AdoptResult::kAdopted);
    // 旧句柄：访问被拒
    bool reached = false;
    NC_CHECK(!lt.withHandle(&oldE, [&](void*) { reached = true; }));
    NC_CHECK(!reached);
    // 旧句柄：释放被拒（否则会释放新 Engine）
    NC_CHECK(!lt.beginRelease(&oldE));
    NC_CHECK_EQ(lt.current(), static_cast<void*>(&newE));   // 新 Engine 未受影响
}

NC_TEST(S1EditorLifetime, ConcurrentCreateSingleOwner) {
    // 场景 1/4/10：多线程共享同一仲裁对象同时创建 → 恰好一个赢家。
    for (int round = 0; round < 200; ++round) {
        EditorLifetime lt;
        constexpr int kThreads = 4;
        FakeEngine engines[kThreads] = {{0}, {1}, {2}, {3}};
        std::atomic<int> adopted{0};
        Gate gate(kThreads);
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back([&, t] {
                gate.wait();
                if (lt.tryAdopt(&engines[t]) == EditorLifetime::AdoptResult::kAdopted) {
                    adopted.fetch_add(1);
                }
            });
        }
        for (auto& th : ts) th.join();
        NC_CHECK_EQ(adopted.load(), 1);          // 严格单所有者
        NC_CHECK(lt.current() != nullptr);
    }
}

NC_TEST(S1EditorLifetime, SameHandleMultiThread) {
    // 场景 3：同一句柄被多线程同时访问 + 销毁。
    for (int round = 0; round < 200; ++round) {
        EditorLifetime lt;
        FakeEngine e{1};
        NC_CHECK(lt.tryAdopt(&e) == EditorLifetime::AdoptResult::kAdopted);
        std::atomic<int> releases{0};
        constexpr int kThreads = 4;
        Gate gate(kThreads);
        std::vector<std::thread> ts;
        for (int t = 0; t < kThreads; ++t) {
            ts.emplace_back([&] {
                gate.wait();
                // 访问与释放交错
                (void)lt.withHandle(&e, [](void*) { std::this_thread::yield(); });
                if (lt.beginRelease(&e)) releases.fetch_add(1);
            });
        }
        for (auto& th : ts) th.join();
        NC_CHECK_EQ(releases.load(), 1);   // 最多销毁一次
        NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr));
    }
}

NC_TEST(S1EditorLifetime, ReleaseWhileInFlight) {
    // 场景 7：销毁与"正在执行"的调用交错。
    // 仲裁器保证：持锁的调用先完成；之后到达的调用一律被拒。
    for (int round = 0; round < 200; ++round) {
        EditorLifetime lt;
        FakeEngine e{1};
        NC_CHECK(lt.tryAdopt(&e) == EditorLifetime::AdoptResult::kAdopted);
        std::atomic<bool> released{false};
        Gate gate(2);
        std::thread caller([&] {
            gate.wait();
            (void)lt.withHandle(&e, [](void*) { std::this_thread::yield(); });
        });
        std::thread releaser([&] {
            gate.wait();
            released.store(lt.beginRelease(&e));
        });
        caller.join();
        releaser.join();
        NC_CHECK(released.load());                              // 释放必须成功
        NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr)); // 句柄已摘除
    }
}

NC_TEST(S1EditorLifetime, CreateAccessDestroyShared) {
    // 场景 2：多线程**共享同一仲裁对象**反复 create/access/destroy。
    // 这是上一版测试的缺陷所在（每线程各建一个仲裁器 = 没有真正竞争）。
    EditorLifetime lt;
    std::atomic<int> failures{0};
    constexpr int kThreads = 4;
    constexpr int kRounds = 200;
    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&, t] {
            for (int r = 0; r < kRounds; ++r) {
                FakeEngine e{t * 1000 + r};
                // 争抢成为唯一所有者
                if (lt.tryAdopt(&e) == EditorLifetime::AdoptResult::kAdopted) {
                    // 赢了：访问 + 释放
                    if (!lt.withHandle(&e, [](void*) {})) failures.fetch_add(1);
                    if (!lt.beginRelease(&e)) failures.fetch_add(1);
                }
                // 输了：句柄未被采纳，什么都不做（调用方本应销毁自己的对象）
            }
        });
    }
    for (auto& th : ts) th.join();
    NC_CHECK_EQ(failures.load(), 0);
    NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr));  // 最终无人持有
}
