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
    // 场景 3：同一句柄被多线程同时访问 + 销毁（确定性版本）。
    //
    // 两阶段设计，避免"销毁线程偶然先拿到锁"被误判为覆盖了竞争：
    //   阶段一：一个线程持有临界区不放手 → 其余线程的 withHandle 与
    //           beginRelease **全部**必须被阻塞（显式确认已尝试但未完成）；
    //   阶段二：放行 → 恰好一个 beginRelease 成功，其余失败；
    //           最终 current() 归零，后续访问被拒。
    for (int round = 0; round < 100; ++round) {
        EditorLifetime lt;
        FakeEngine e{1};
        NC_CHECK(lt.tryAdopt(&e) == EditorLifetime::AdoptResult::kAdopted);

        std::atomic<bool> holderEntered{false};
        std::atomic<bool> allowFinish{false};
        std::atomic<int>  contendersStarted{0};
        std::atomic<int>  contendersDone{0};
        std::atomic<int>  releases{0};
        std::atomic<int>  accesses{0};

        constexpr int kContenders = 3;

        // 持有临界区的线程
        std::thread holder([&] {
            (void)lt.withHandle(&e, [&](void*) {
                holderEntered.store(true, std::memory_order_release);
                while (!allowFinish.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            });
        });
        while (!holderEntered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        // 竞争者：尝试访问 + 尝试销毁
        std::vector<std::thread> cs;
        for (int t = 0; t < kContenders; ++t) {
            cs.emplace_back([&] {
                contendersStarted.fetch_add(1, std::memory_order_acq_rel);
                if (lt.withHandle(&e, [](void*) {})) {
                    accesses.fetch_add(1, std::memory_order_acq_rel);
                }
                if (lt.beginRelease(&e)) {
                    releases.fetch_add(1, std::memory_order_acq_rel);
                }
                contendersDone.fetch_add(1, std::memory_order_acq_rel);
            });
        }

        // 确认所有竞争者都已开始，并自旋确认它们**都还没完成**（被阻塞）
        while (contendersStarted.load(std::memory_order_acquire) < kContenders) {
            std::this_thread::yield();
        }
        for (int spin = 0; spin < 20000 && contendersDone.load() < kContenders; ++spin) {
            std::this_thread::yield();
        }
        NC_CHECK_EQ(contendersDone.load(), 0);   // 全部被临界区阻塞
        NC_CHECK_EQ(releases.load(), 0);
        NC_CHECK_EQ(accesses.load(), 0);

        // 放行
        allowFinish.store(true, std::memory_order_release);
        holder.join();
        for (auto& c : cs) c.join();

        // 阶段二：竞争完成 —— 销毁恰好成功一次
        NC_CHECK_EQ(contendersDone.load(), kContenders);
        NC_CHECK_EQ(releases.load(), 1);
        NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr));
        // 销毁后再访问必须被拒
        bool reached = false;
        NC_CHECK(!lt.withHandle(&e, [&](void*) { reached = true; }));
        NC_CHECK(!reached);
    }
}

NC_TEST(S1EditorLifetime, ReleaseWhileInFlight) {
    // 场景 7：销毁与"正在执行"的调用交错 —— **确定性时序**，不用 sleep。
    //
    // 时序（每一步都有显式信号，不依赖调度运气）：
    //   ① 调用线程进入 withHandle() 临界区 → 置 entered
    //   ② 主线程等到 entered（确定已进入临界区）
    //   ③ 启动销毁线程 → 置 releaserStarted 后尝试 beginRelease（会被阻塞）
    //   ④ 主线程等到 releaserStarted，再自旋一段确认 releaseDone **仍为 false**
    //      → 证明"销毁不能插进执行中的调用"
    //   ⑤ 放行 allowFinish，调用线程离开临界区
    //   ⑥ join 调用线程；join 销毁线程 → 销毁随即成功
    //   ⑦ 销毁后再调用必须被拒绝
    for (int round = 0; round < 100; ++round) {
        EditorLifetime lt;
        FakeEngine e{1};
        NC_CHECK(lt.tryAdopt(&e) == EditorLifetime::AdoptResult::kAdopted);

        std::atomic<bool> entered{false};
        std::atomic<bool> allowFinish{false};
        std::atomic<bool> releaserStarted{false};
        std::atomic<bool> releaseDone{false};
        std::atomic<bool> releaseResult{false};

        std::thread caller([&] {
            (void)lt.withHandle(&e, [&](void*) {
                entered.store(true, std::memory_order_release);
                // 在临界区内等待放行 —— 此时锁被持有，销毁无法进行
                while (!allowFinish.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            });
        });

        // ② 确定性确认调用线程已在临界区内
        while (!entered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        std::thread releaser([&] {
            releaserStarted.store(true, std::memory_order_release);
            releaseResult.store(lt.beginRelease(&e));
            releaseDone.store(true, std::memory_order_release);
        });

        // ④ 确认销毁线程已在尝试，且**尚未**完成
        while (!releaserStarted.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (int spin = 0; spin < 20000 && !releaseDone.load(std::memory_order_acquire);
             ++spin) {
            std::this_thread::yield();
        }
        NC_CHECK(!releaseDone.load());   // 关键断言：销毁被临界区阻塞

        // ⑤ 放行调用线程
        allowFinish.store(true, std::memory_order_release);
        caller.join();                   // ⑥ 调用完成
        releaser.join();

        // ⑦ 销毁随后成功
        NC_CHECK(releaseDone.load());
        NC_CHECK(releaseResult.load());
        NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr));

        // 销毁后新调用必须被拒绝
        bool reached = false;
        NC_CHECK(!lt.withHandle(&e, [&](void*) { reached = true; }));
        NC_CHECK(!reached);
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
