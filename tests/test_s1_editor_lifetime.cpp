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
#include <chrono>
#include <cstdint>
#include <thread>
#include <vector>

#include "nairoclip/core/editor_lifetime.h"

namespace {

using nairoclip::core::EditorLifetime;
/// 直接测试锁本身时使用的类型别名（与生产同一类型）。
using Mutex = EditorLifetime::Mutex;

/// 模拟一个 Engine 对象；用 alive 计数验证"最多销毁一次"。
struct FakeEngine {
    int id = 0;
};

/// 有界等待：条件成立返回 true；超时返回 false。
///
/// 用途：**防止异常情况下 CI 无限期卡住**。
/// 注意：本函数**不是**并发正确性的证明手段 —— 正确性仍由被等待的
/// 条件本身（failedTryLockCount / ownerId / holderEntered）承担。
/// 超时只用于把"条件永远不会成立"这种情况转成一次明确的测试失败。
template <typename Pred>
bool waitUntil(Pred pred, int timeoutMs = 15000) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::yield();
    }
    return true;
}

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
    /* 反证：无竞争时失败计数必须为 0 —— 否则"failedTryLockCount>0
       说明发生过获取尝试"这条判据就恒真、失去意义。 */
    NC_CHECK_EQ(lt.mutex().failedTryLockCount(), 0);
    NC_CHECK_EQ(lt.mutex().ownerId(), 0);
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
    //
    // 确定性依据（不使用 sleep；等待循环带超时保护，仅防 CI 卡死，
    // 不作为并发正确性证明）：
    //   1) 持锁线程进入临界区后，从**外部** try_lock() 必须失败
    //      → 证明锁确实被持有（ownerId() 亦指向 holder）；
    //   2) 竞争者启动后等待 failedTryLockCount() 达到 kContenders
    //      → 该计数只在 try_lock() 返回失败后自增，因此 >0 表示这些线程
    //        已**实际尝试获取**（排除"还没被调度到"）。
    //      注意：try_lock 的失败本身可能来自他人持锁，也可能是标准允许的
    //      虚假失败；本测试之所以能据此断言"无法推进"，是因为第 1 步
    //      已独立确认锁被 holder 持有 —— 两者结合才构成竞争证据。
    //   3) 放行后竞争者继续，恰好一次 beginRelease 成功。
    for (int round = 0; round < 100; ++round) {
        EditorLifetime lt;
        FakeEngine e{1};
        NC_CHECK(lt.tryAdopt(&e) == EditorLifetime::AdoptResult::kAdopted);

        std::atomic<bool> holderEntered{false};
        std::atomic<bool> allowFinish{false};
        std::atomic<int>  accesses{0};
        std::atomic<int>  releases{0};
        std::atomic<int>  contendersDone{0};

        constexpr int kContenders = 3;

        std::thread holder([&] {
            (void)lt.withHandle(&e, [&](void*) {
                holderEntered.store(true, std::memory_order_release);
                while (!allowFinish.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            });
        });
        if (!waitUntil([&] { return holderEntered.load(std::memory_order_acquire); })) {
            allowFinish.store(true, std::memory_order_release);   // 放行以免永久阻塞
            holder.join();
            NC_CHECK(false);   // 超时：holder 未进入临界区
            return;
        }

        // (1) 证明锁被持有：外部 try_lock 必须失败
        {
            const bool probe = lt.mutex().try_lock();
            NC_CHECK(!probe);
            if (probe) {
                lt.mutex().unlock();   // 不应发生；防止后续卡死
            }
            NC_CHECK(lt.mutex().ownerId() != 0);
        }

        std::vector<std::thread> cs;
        for (int t = 0; t < kContenders; ++t) {
            cs.emplace_back([&] {
                if (lt.withHandle(&e, [](void*) {})) {
                    accesses.fetch_add(1, std::memory_order_acq_rel);
                }
                if (lt.beginRelease(&e)) {
                    releases.fetch_add(1, std::memory_order_acq_rel);
                }
                contendersDone.fetch_add(1, std::memory_order_acq_rel);
            });
        }

        // (2) 等待竞争者实际尝试获取（有界，超时则记为失败而非卡死）
        const bool sawContention = waitUntil(
            [&] { return lt.mutex().failedTryLockCount() >= kContenders; });
        NC_CHECK(sawContention);
        NC_CHECK(lt.mutex().failedTryLockCount() >= kContenders);
        if (sawContention) {
            NC_CHECK_EQ(contendersDone.load(), 0);   // 无一完成
            NC_CHECK_EQ(releases.load(), 0);         // 无一释放成功
            NC_CHECK_EQ(accesses.load(), 0);         // 无一访问成功
            NC_CHECK_EQ(lt.current(), static_cast<void*>(&e));  // Engine 仍存活
        }

        // (3) 放行 → 竞争者继续（无论是否超时都必须放行并 join）
        allowFinish.store(true, std::memory_order_release);
        holder.join();
        for (auto& c : cs) c.join();
        if (!sawContention) return;   // 已记录失败

        NC_CHECK_EQ(contendersDone.load(), kContenders);
        NC_CHECK_EQ(releases.load(), 1);         // 同一 Engine 最多一次有效释放
        NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr));

        bool reached = false;
        NC_CHECK(!lt.withHandle(&e, [&](void*) { reached = true; }));
        NC_CHECK(!reached);
    }
}

NC_TEST(S1EditorLifetime, ReleaseWhileInFlight) {
    // 场景 7：in-flight 调用完成之前，Engine 不能被释放。
    //
    // 与 SameHandleMultiThread 相同的确定性依据：
    //   · 外部 try_lock 失败 + ownerId() 指向 holder → 锁确实被持有；
    //   · failedTryLockCount() 达到 1 → 销毁线程已实际尝试获取并失败；
    //     结合上一行（锁被持有）→ 它不可能推进。
    //     （try_lock 允许虚假失败，故单看计数不足以断定竞争，
    //       必须与"锁被持有"这一独立事实结合。）
    //   · 此时断言 current() 仍指向 e、releaseDone 仍为 false
    //     → 证明"释放不能越过正在持有的临界区"；
    //   · 放行后释放成功；随后旧句柄访问被拒。
    for (int round = 0; round < 100; ++round) {
        EditorLifetime lt;
        FakeEngine e{1};
        NC_CHECK(lt.tryAdopt(&e) == EditorLifetime::AdoptResult::kAdopted);

        std::atomic<bool> holderEntered{false};
        std::atomic<bool> allowFinish{false};
        std::atomic<bool> releaseResult{false};
        std::atomic<bool> releaseDone{false};

        std::thread holder([&] {
            (void)lt.withHandle(&e, [&](void*) {
                holderEntered.store(true, std::memory_order_release);
                while (!allowFinish.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            });
        });
        if (!waitUntil([&] { return holderEntered.load(std::memory_order_acquire); })) {
            allowFinish.store(true, std::memory_order_release);
            holder.join();
            NC_CHECK(false);   // 超时：holder 未进入临界区
            return;
        }

        // 证明锁被持有
        {
            const bool probe = lt.mutex().try_lock();
            NC_CHECK(!probe);
            if (probe) {
                lt.mutex().unlock();
            }
        }

        std::thread releaser([&] {
            releaseResult.store(lt.beginRelease(&e));
            releaseDone.store(true, std::memory_order_release);
        });

        // 确定性：销毁线程已实际尝试获取（有界等待，超时记为失败）
        const bool sawAttempt = waitUntil(
            [&] { return lt.mutex().failedTryLockCount() >= 1; });
        NC_CHECK(sawAttempt);
        NC_CHECK(lt.mutex().failedTryLockCount() >= 1);
        if (sawAttempt) {
            NC_CHECK(!releaseDone.load());                      // 释放尚未完成
            NC_CHECK_EQ(lt.current(), static_cast<void*>(&e));  // Engine 仍存活
        }

        // 放行（无论是否超时都必须放行并 join，避免永久阻塞）
        allowFinish.store(true, std::memory_order_release);
        holder.join();
        releaser.join();
        if (!sawAttempt) return;   // 已记录失败

        NC_CHECK(releaseDone.load());
        NC_CHECK(releaseResult.load());
        NC_CHECK_EQ(lt.current(), static_cast<void*>(nullptr));

        // 释放成功后旧句柄访问被拒
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

// ---------------------------------------------------------------------------
// S1RecursiveMutex：锁状态语义的定点测试
// 对应要求：无竞争 / 递归 / 尝试获取失败（记录）/ 竞争者最终获得 / 多线程+递归不死锁
// ---------------------------------------------------------------------------

NC_TEST(S1RecursiveMutex, UncontendedLockKeepsContendedZero) {
    // ① 正常无竞争获取：竞争计数必须保持 0
    Mutex m;
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
    NC_CHECK_EQ(m.ownerId(), 0);

    m.lock();
    NC_CHECK_EQ(m.failedTryLockCount(), 0);   // 无竞争不计数
    NC_CHECK(m.ownerId() != 0);

    m.unlock();
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
    NC_CHECK_EQ(m.ownerId(), 0);

    // 无竞争的 try_lock 同样不计数
    NC_CHECK(m.try_lock());
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
    m.unlock();
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
}

NC_TEST(S1RecursiveMutex, RecursiveLockNotCountedAsContention) {
    // ② 同线程递归获取：不得被误计为竞争等待者
    Mutex m;
    m.lock();
    m.lock();
    m.lock();
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
    NC_CHECK(m.ownerId() != 0);

    m.unlock();
    m.unlock();
    m.unlock();
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
    NC_CHECK_EQ(m.ownerId(), 0);

    // 递归 try_lock 也不计竞争
    m.lock();
    NC_CHECK(m.try_lock());
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
    m.unlock();
    m.unlock();
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
}

NC_TEST(S1RecursiveMutex, ContentionObservedWhenHolderHolds) {
    // ③ 主线程持锁期间，竞争者实际尝试获取 → 失败记录准确增加
    // ④ 主线程释放后竞争者取得并释放 → 计数归零
    //
    // 语义说明：try_lock() 允许偶发虚假失败，因此"计数 > 0"单独不足以
    // 断定竞争；本测试之所以能作为竞争证据，是因为**同时**断言了
    // ownerId() 仍指向主线程（锁确实被持有）。
    for (int round = 0; round < 100; ++round) {
        Mutex m;
        m.lock();                       // 主线程持有
        const std::uint64_t holder = m.ownerId();
        NC_CHECK(holder != 0);

        std::atomic<bool> started{false};
        std::thread contender([&] {
            started.store(true, std::memory_order_release);
            m.lock();                   // try_lock 失败 → 记录 → 阻塞
            m.unlock();
        });

        // 有界等待（防 CI 卡死；正确性由后续断言承担）
        const bool began = waitUntil([&] { return started.load(std::memory_order_acquire); });
        const bool sawAttempt =
            began && waitUntil([&] { return m.failedTryLockCount() >= 1; });

        NC_CHECK(began);
        NC_CHECK(sawAttempt);
        if (sawAttempt) {
            NC_CHECK(m.failedTryLockCount() >= 1);
            NC_CHECK_EQ(m.ownerId(), holder);   // 锁仍由主线程持有
        }

        m.unlock();                          // 放行（无论是否超时都要放行）
        contender.join();
        if (!sawAttempt) return;             // 已记录失败

        // ④ 竞争者取得并释放后，计数归零
        NC_CHECK_EQ(m.failedTryLockCount(), 0);
        NC_CHECK_EQ(m.ownerId(), 0);
    }
}

NC_TEST(S1RecursiveMutex, MultiThreadContentionWithRecursionNoDeadlock) {
    // ⑤ 多线程竞争 + 递归获取：不死锁，最终状态干净
    Mutex m;
    std::atomic<long long> done{0};
    constexpr int kThreads = 4;
    constexpr int kRounds = 500;

    std::vector<std::thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&] {
            for (int r = 0; r < kRounds; ++r) {
                m.lock();
                m.lock();               // 递归
                done.fetch_add(1, std::memory_order_relaxed);
                m.unlock();
                m.unlock();
            }
        });
    }
    for (auto& th : ts) th.join();

    NC_CHECK_EQ(done.load(), static_cast<long long>(kThreads) * kRounds);
    NC_CHECK_EQ(m.failedTryLockCount(), 0);
    NC_CHECK_EQ(m.ownerId(), 0);
}
