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

/// 可观测的递归互斥量（S1-1 M-2 语义收尾版）。
///
/// 为什么不用 std::recursive_mutex：
///   测试需要可观测的"获取尝试"信号，以便区分
///     · 线程还没被调度到；
///     · 线程已经实际尝试获取锁；
///   而 started 标记 + 有限次 yield() 无法区分这两者。
///
/// 实现要点：`lock()` 先用 `try_lock()` 探测，仅在**失败**时记录一次，
/// 然后阻塞在 `lock()` 上；拿到锁后撤销记录。
///
/// ⚠️ 关于 `std::mutex::try_lock()` 的标准语义（必须如实反映）：
///   C++ 标准允许 `try_lock()` **偶发虚假失败**（spurious failure）——
///   即使没有其他线程持有该锁，它也可能返回 false。
///   因此**每一次失败都不能被断言为"已确认真实竞争"**。
///   本类只如实记录"发生过失败"，不推断失败的原因。
///
/// `failedTryLockCount()` 的**精确含义**：
///   "try_lock() 返回过失败、且对应线程尚未完成其后续 lock() 获取的线程数"。
///   它**不证明**失败一定由其他线程持锁造成；
///   它**不证明**线程已被操作系统挂起（挂起是调度器行为，本类型无法观测）。
///
/// 何时可作为竞争证据：
///   在测试**另行确认 holder 确实持锁**的前提下（例如外部 try_lock 失败、
///   ownerId() 指向 holder 线程），此时本计数 > 0 才可用于证明
///   "竞争者已经实际尝试获取锁，且因锁被持有而无法推进"。
///
/// 四种状态（文字定义）：
///   ① 正常无竞争获取：owner != self 且 try_lock 成功 → 计数**不变**
///   ② 同线程递归获取：owner == self → 直接 ++recursion_，计数**不变**
///   ③ 尝试获取失败：try_lock 返回 false → 计数 **+1**，随后阻塞在 lock()
///      （注意：该失败可能由他人持锁造成，也可能是标准允许的虚假失败）
///   ④ 竞争者最终获得：lock() 返回 → 计数 **−1**
///
class RecursiveMutex {
public:
    void lock() {
        const std::uint64_t me = currentThreadId();
        if (owner_.load(std::memory_order_acquire) == me) {
            ++recursion_;                       // ② 递归获取
            return;
        }
        if (inner_.try_lock()) {                // ① 无竞争获取
            owner_.store(me, std::memory_order_release);
            recursion_ = 1;
            return;
        }
        /* ③ try_lock 失败：记录一次（原因不作断言，可能是他人持锁，
           也可能是标准允许的虚假失败） */
        failedTryLock_.fetch_add(1, std::memory_order_acq_rel);
        try {
            inner_.lock();
        } catch (...) {
            /* 异常路径：撤销记录，避免计数泄漏 */
            failedTryLock_.fetch_sub(1, std::memory_order_acq_rel);
            throw;
        }
        /* ④ 竞争者最终获得 */
        failedTryLock_.fetch_sub(1, std::memory_order_acq_rel);
        owner_.store(me, std::memory_order_release);
        recursion_ = 1;
    }

    void unlock() {
        if (--recursion_ == 0) {
            owner_.store(0, std::memory_order_release);
            inner_.unlock();
        }
    }

    bool try_lock() {
        const std::uint64_t me = currentThreadId();
        if (owner_.load(std::memory_order_acquire) == me) {
            ++recursion_;
            return true;
        }
        if (!inner_.try_lock()) {
            return false;
        }
        owner_.store(me, std::memory_order_release);
        recursion_ = 1;
        return true;
    }

    /// try_lock() 返回过失败、且尚未完成后续 lock() 获取的线程数。
    ///
    /// 语义边界（不得夸大）：
    ///   · **不证明**失败由其他线程持锁造成 —— C++ 标准允许 try_lock
    ///     偶发虚假失败；
    ///   · **不证明**对应线程已被操作系统挂起；
    ///   · 只有在调用方**另行确认锁被他人持有**时，本计数 > 0 才能作为
    ///     "竞争者已实际尝试获取且无法推进"的证据。
    ///
    /// 仅供测试观测；生产代码不依赖本值做任何判断。
    int failedTryLockCount() const {
        return failedTryLock_.load(std::memory_order_acquire);
    }

    /// 当前持有者线程 id（0 = 未持有）。仅供测试断言。
    std::uint64_t ownerId() const {
        return owner_.load(std::memory_order_acquire);
    }

private:
    static std::uint64_t currentThreadId() {
        static std::atomic<std::uint64_t> next{1};
        thread_local const std::uint64_t id =
            next.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    std::mutex inner_;
    std::atomic<std::uint64_t> owner_{0};
    std::atomic<int> failedTryLock_{0};
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
    ///
    /// 加锁/解锁/try_lock 的行为与 std::recursive_mutex 完全一致；
    /// 额外提供 failedTryLockCount() 与 ownerId() 两个**只读观测**接口，
    /// 供主机端测试判断"是否发生过获取尝试"，不参与任何生产决策。
    /// 这两个接口不改变锁语义，也不引入新的同步路径。
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
