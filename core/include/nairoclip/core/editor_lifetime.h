// S1-1：Editor 生命周期仲裁器。
//
// 设计目标（严格单所有者）：
//   · 同一时刻最多存在一个 live Editor，创建/销毁/访问全部经此仲裁；
//   · 第二次创建必须**失败**，不得把已有实例的句柄交给第二个所有者；
//   · 销毁具有单次语义：只有"确实是当前实例"才放行，重复销毁安全忽略；
//   · 访问必须先校验句柄有效性，且"校验 → 使用"期间对象不得被销毁。
//
// 为什么放在 core 而不是直接写在 nc_jni.cpp：
//   本类是纯 C++（无 JNI 依赖），因此可以被主机端单元测试**直接复用**，
//   测试验证的就是生产代码本身，而不是另写一份等价逻辑。
//   JNI 层只做 jlong ↔ void* 的转换与 RAII 包装。
#ifndef NAIROCLIP_CORE_EDITOR_LIFETIME_H
#define NAIROCLIP_CORE_EDITOR_LIFETIME_H

#include <atomic>
#include <cstdint>
#include <mutex>

namespace nairoclip {
namespace core {

namespace detail {

/// 可观测的递归互斥量（S1-1 M-2）。
///
/// 为什么不用 std::recursive_mutex：
///   测试必须能**直接观测**"某线程已进入获取锁的路径、且正被阻塞"。
///   仅靠 started 标记 + 有限次 yield() 无法区分：
///     · 线程还没被调度到；
///     · 线程已尝试获取锁；
///     · 线程确实被持有中的临界区挡住。
///   本类型在**阻塞前**自增 waiters_，因此当
///     (a) 锁被别的线程持有，且 (b) waiterCount() > 0
///   时可以确定：该线程已到达锁获取点，且不可能继续 —— 即确实被阻塞。
///
/// 语义与 std::recursive_mutex 等价（同一线程可重入）。
/// 额外开销：仅在有竞争时一次原子自增/自减；无竞争路径不变。
class RecursiveMutex {
public:
    void lock() {
        if (ownerId() == currentThreadId()) {
            ++recursion_;                       // 同线程重入
            return;
        }
        waiters_.fetch_add(1, std::memory_order_acq_rel);
        inner_.lock();
        waiters_.fetch_sub(1, std::memory_order_acq_rel);
        owner_.store(currentThreadId(), std::memory_order_release);
        recursion_ = 1;
    }

    void unlock() {
        if (--recursion_ == 0) {
            owner_.store(0, std::memory_order_release);
            inner_.unlock();
        }
    }

    bool try_lock() {
        if (ownerId() == currentThreadId()) {
            ++recursion_;
            return true;
        }
        if (!inner_.try_lock()) {
            return false;
        }
        owner_.store(currentThreadId(), std::memory_order_release);
        recursion_ = 1;
        return true;
    }

    /// 正在**等待**获取本锁的线程数（仅供测试观测）。
    /// 无竞争时为 0；某线程卡在 lock() 上期间 >= 1。
    int waiterCount() const { return waiters_.load(std::memory_order_acquire); }

    /// 当前持有者线程 id（0 = 未持有）。仅供测试断言。
    std::uint64_t ownerId() const { return owner_.load(std::memory_order_acquire); }

private:
    static std::uint64_t currentThreadId() {
        static std::atomic<std::uint64_t> next{1};
        thread_local const std::uint64_t id =
            next.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    std::mutex inner_;
    std::atomic<std::uint64_t> owner_{0};
    std::atomic<int> waiters_{0};
    int recursion_ = 0;   // 仅由持有者线程读写
};

}  // namespace detail

/// Editor 生命周期仲裁器（严格单所有者）。
///
/// 线程安全：所有公有方法内部加锁；`mutex()` 供调用方把
/// 「校验句柄 → 使用对象」扩展为同一临界区（见 nc_jni.cpp 的守卫）。
class EditorLifetime {
public:
    /// tryAdopt 的结果。
    enum class AdoptResult {
        kAdopted,        ///< 成功成为唯一所有者
        kAlreadyExists,  ///< 已有所有者；created 的所有权仍在调用方（须销毁）
        kInvalidArgument ///< created == nullptr
    };

    /// 尝试把 `created` 登记为唯一 live 实例。
    ///
    /// 成功时 `current() == created`；失败时**不改变**既有状态，
    /// 且 `created` 仍归调用方所有（调用方必须自行销毁，避免泄漏）。
    AdoptResult tryAdopt(void* created) {
        if (created == nullptr) {
            return AdoptResult::kInvalidArgument;
        }
        std::lock_guard<Mutex> lock(mutex_);
        void* expected = nullptr;
        if (!current_.compare_exchange_strong(expected, created,
                                              std::memory_order_acq_rel,
                                              std::memory_order_acquire)) {
            return AdoptResult::kAlreadyExists;
        }
        return AdoptResult::kAdopted;
    }

    /// 尝试释放 `handle` 的所有权。
    ///
    /// 仅当 `handle` 确实是当前实例时返回 true（并原子地置空），
    /// 调用方随后才可真正销毁对象。返回 false 表示句柄已过期 /
    /// 已被释放 / 不属于当前实例 —— 此时**绝不能**销毁，否则就是
    /// 重复释放（double free）。
    bool beginRelease(void* handle) {
        if (handle == nullptr) {
            return false;
        }
        std::lock_guard<Mutex> lock(mutex_);
        void* expected = handle;
        return current_.compare_exchange_strong(expected, nullptr,
                                                std::memory_order_acq_rel,
                                                std::memory_order_acquire);
    }

    /// 在生命周期锁内校验 `handle` 并执行 fn。
    ///
    /// 仅当 `handle` 确实是当前实例时执行 fn 并返回 true；
    /// 过期 / 已释放 / 不属于当前实例一律返回 false（**不执行** fn）。
    /// 「校验 → 使用」在同一临界区内完成，销毁无法插入。
    template <typename Fn>
    bool withHandle(void* handle, Fn&& fn) {
        std::lock_guard<Mutex> lock(mutex_);
        void* cur = current_.load(std::memory_order_acquire);
        if (cur == nullptr || handle == nullptr || handle != cur) {
            return false;
        }
        fn(cur);
        return true;
    }

    /// 当前实例（可能为 nullptr）。无锁读取，仅供诊断/断言。
    void* current() const {
        return current_.load(std::memory_order_acquire);
    }

    /// 生命周期互斥量类型（可观测的递归互斥量）。
    /// 对外语义等同 std::recursive_mutex，额外提供 waiterCount() 供测试
    /// 确定性观测锁竞争，不改变任何加锁/解锁行为。
    using Mutex = detail::RecursiveMutex;

    /// 生命周期互斥量。调用方持有它可把「校验 + 使用」变成原子区。
    /// 递归语义：同一线程内的嵌套 JNI 调用不会自死锁。
    Mutex& mutex() { return mutex_; }

private:
    std::atomic<void*> current_{nullptr};
    Mutex mutex_;
};

}  // namespace core
}  // namespace nairoclip

#endif  // NAIROCLIP_CORE_EDITOR_LIFETIME_H
