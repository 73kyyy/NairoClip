// S1-1 · JNI 生命周期协议并发测试（Category B：协议级）
//
// ⚠️ 范围声明（必须与 C ABI 测试区分）：
//   · 本文件测试的是 **nc_jni.cpp 中 EditorHandleGuard 所实现的协议**
//     （atomic 句柄 + recursive_mutex + CAS 单次销毁 + 句柄校验），
//     以独立实现复刻同一协议，从而可在无 NDK 的主机环境执行。
//   · 它 **不是** 对真实 libnc_jni.so 的调用，也不覆盖 Kotlin/Android
//     生命周期（Category C/D）。真实 JNI 二进制测试需要 NDK + 模拟器，
//     见报告中的未执行项。
//   · 因此本文件通过 **不能** 替代 Category B（真实 JNI）结论。
//
// 覆盖目标交错（对应指令 §五 的 11 项）：
//   1) Native 调用执行中销毁 Engine      → DestroyWhileInFlight
//   2) 多线程同时 release                → ConcurrentDestroySingleWinner
//   3) release 与 blitFrame 竞争          → AccessVersusDestroy
//   7) 重复 nativeCreate                 → RepeatedCreateKeepsSingleInstance
//   8) 重复 nativeDestroy                → RepeatedDestroyIsSafe
//   9) 过期句柄调用                      → StaleHandleRejected
//  11) 多轮创建/访问/销毁压力            → CreateAccessDestroyStress
#include "test_framework.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {

/// 与 nc_jni.cpp 的 EditorHandleGuard 协议一一对应的最小实现。
/// 用整数 token 代替 NcEditor*，语义相同。
class LifetimeProtocol {
public:
    /// 模拟 nativeCreate：CAS 保证单实例；已有实例时销毁新建者并返回既有。
    std::uintptr_t create(std::uintptr_t created, int* destroyedDuplicates) {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        std::uintptr_t expected = 0;
        if (!current_.compare_exchange_strong(expected, created,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            ++(*destroyedDuplicates);   // 新创建的被销毁（不泄漏）
            return expected;            // 返回既有有效句柄
        }
        return created;
    }

    /// 模拟 nativeDestroy：只有"确实是当前实例"才销毁（单次语义）。
    bool destroy(std::uintptr_t h) {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        std::uintptr_t expected = h;
        if (!current_.compare_exchange_strong(expected, 0,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            return false;   // 过期 / 重复销毁 → 不销毁
        }
        return true;        // 调用方此刻才真正 free
    }

    /// 模拟 EditorHandleGuard：校验句柄并持锁执行。
    template <typename Fn>
    bool withHandle(std::uintptr_t h, Fn&& fn) {
        std::lock_guard<std::recursive_mutex> lock(mu_);
        const std::uintptr_t cur = current_.load(std::memory_order_acquire);
        if (cur == 0 || h != cur) return false;   // 句柄无效
        fn(cur);
        return true;
    }

    std::uintptr_t current() const {
        return current_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uintptr_t> current_{0};
    std::recursive_mutex mu_;
};

/// 双线程闸门：确保两线程尽量同时抵达竞争点。
class Gate {
public:
    void wait() {
        ready_.fetch_add(1, std::memory_order_acq_rel);
        while (ready_.load(std::memory_order_acquire) < 2) std::this_thread::yield();
    }
private:
    std::atomic<int> ready_{0};
};

}  // namespace

NC_TEST(S1JniProtocol, RepeatedCreateKeepsSingleInstance) {
    LifetimeProtocol p;
    int dupDestroyed = 0;
    const std::uintptr_t first  = p.create(0x1000, &dupDestroyed);
    const std::uintptr_t second = p.create(0x2000, &dupDestroyed);
    NC_CHECK_EQ(first, static_cast<std::uintptr_t>(0x1000));
    NC_CHECK_EQ(second, first);          // 不覆盖：返回既有实例
    NC_CHECK_EQ(dupDestroyed, 1);        // 新建的 0x2000 被销毁，无泄漏
    NC_CHECK_EQ(p.current(), first);
}

NC_TEST(S1JniProtocol, RepeatedDestroyIsSafe) {
    LifetimeProtocol p;
    int dup = 0;
    const std::uintptr_t h = p.create(0x1000, &dup);
    NC_CHECK(p.destroy(h));              // 第一次：成功
    NC_CHECK(!p.destroy(h));             // 第二次：拒绝（不 double free）
    NC_CHECK_EQ(p.current(), static_cast<std::uintptr_t>(0));
}

NC_TEST(S1JniProtocol, StaleHandleRejected) {
    LifetimeProtocol p;
    int dup = 0;
    const std::uintptr_t oldH = p.create(0x1000, &dup);
    NC_CHECK(p.destroy(oldH));
    const std::uintptr_t newH = p.create(0x2000, &dup);
    NC_CHECK(newH != oldH);
    // 用过期句柄访问：必须被拒绝，不能碰到新实例
    bool reached = false;
    NC_CHECK(!p.withHandle(oldH, [&](std::uintptr_t) { reached = true; }));
    NC_CHECK(!reached);
    // 有效句柄可用
    NC_CHECK(p.withHandle(newH, [&](std::uintptr_t t) { NC_CHECK_EQ(t, newH); }));
}

NC_TEST(S1JniProtocol, ConcurrentDestroySingleWinner) {
    // 场景 2/8：多线程同时 release，只能有一个真正执行销毁。
    for (int round = 0; round < 200; ++round) {
        LifetimeProtocol p;
        int dup = 0;
        const std::uintptr_t h = p.create(0x1000, &dup);
        std::atomic<int> winners{0};
        Gate gate;
        std::vector<std::thread> ts;
        for (int t = 0; t < 4; ++t) {
            ts.emplace_back([&] {
                gate.wait();
                if (p.destroy(h)) winners.fetch_add(1);
            });
        }
        // gate 需要 2 个到达即放行，这里 4 个线程会分两批通过，仍有效
        for (auto& th : ts) th.join();
        NC_CHECK_EQ(winners.load(), 1);          // 恰好一次销毁
        NC_CHECK_EQ(p.current(), static_cast<std::uintptr_t>(0));
    }
}

NC_TEST(S1JniProtocol, DestroyWhileInFlight) {
    // 场景 1/3：Native 调用执行中销毁 Engine。
    // 协议保证：持锁的访问会先完成；destroy 之后到达的访问一律被拒绝。
    for (int round = 0; round < 200; ++round) {
        LifetimeProtocol p;
        int dup = 0;
        const std::uintptr_t h = p.create(0x1000, &dup);
        std::atomic<bool> accessRejected{false};
        std::atomic<bool> destroyed{false};
        Gate gate;
        std::thread accessor([&] {
            gate.wait();
            // 若 destroy 先完成 → 拒绝；若访问先拿到锁 → 正常执行
            if (!p.withHandle(h, [](std::uintptr_t) {
                    std::this_thread::yield();
                })) {
                accessRejected.store(true);
            }
        });
        std::thread destroyer([&] {
            gate.wait();
            destroyed.store(p.destroy(h));
        });
        accessor.join();
        destroyer.join();
        NC_CHECK(destroyed.load());                       // 销毁必须成功
        NC_CHECK_EQ(p.current(), static_cast<std::uintptr_t>(0));  // 句柄已摘除
        // accessRejected 两种取值都合法：取决于谁先拿到锁。
        (void)accessRejected;
    }
}

NC_TEST(S1JniProtocol, CreateAccessDestroyStress) {
    // 场景 11：多轮创建 → 访问 → 销毁压力（4 线程 × 50 轮）。
    std::atomic<int> failures{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t) {
        ts.emplace_back([&] {
            LifetimeProtocol p;   // 每线程独立仲裁器（对应单实例语义）
            for (int r = 0; r < 50; ++r) {
                int dup = 0;
                const std::uintptr_t h = p.create(0x1000 + r, &dup);
                if (h == 0) { failures.fetch_add(1); continue; }
                if (!p.withHandle(h, [](std::uintptr_t) {})) failures.fetch_add(1);
                if (!p.destroy(h)) failures.fetch_add(1);
                if (p.current() != 0) failures.fetch_add(1);
            }
        });
    }
    for (auto& th : ts) th.join();
    NC_CHECK_EQ(failures.load(), 0);
}
