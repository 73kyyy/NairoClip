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

/// 可观测的递归互斥量（S1-1 M-2 修正版）。
///
/// 为什么不用 std::recursive_mutex：
///   测试必须能**直接观测**"某线程已尝试获取锁且失败、正在等待"，
///   而 started 标记 + 有限次 yield() 无法区分"尚未被调度"与"已被阻塞"。
///
/// 与上一版的关键差异（修正）：
///   上一版在调用 inner_.lock() **之前无条件** fetch_add —— 无竞争的正常
///   获取也会短暂增加计数，因此计数并不代表"竞争等待者"，也无法建立
///   "计数增加 ⇔ 获取失败" 的因果关系。
///   本版改为：**先用 try_lock() 探测**，
///     · try_lock 成功 → 无竞争获取，计数**不变**；
///     · try_lock 失败 → 才记录一次竞争，然后阻塞在 lock() 上；
///       拿到锁后撤销记录。
///   因此 `contendedCount() > 0` 的**精确含义**是：
///     "有线程在此锁上 try_lock 失败过、且尚未完成获取"。
///
/// ⚠️ 语义边界（不得夸大）：
///   contendedCount() > 0 **不表示**操作系统已把该线程挂起。
///   它表示"该线程已观测到获取失败并进入等待"；由于此时锁由他人持有，
///   它不可能继续推进 —— 这就是测试所需的全部强度。
///
/// 四种状态：
///   ① 正常无竞争获取：try_lock 成功，contended 不变
///   ② 同线程递归获取：owner == self，直接 ++recursion_，contended 不变
///   ③ 真实竞争：try_lock 失败 → contended++ → lock() 阻塞
///   ④ 竞争者最终获得：lock() 返回 → contended--
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
        /* ③ 真实竞争：try_lock 失败后才记录 */
        contended_.fetch_add(1, std::memory_order_acq_rel);
        try {
            inner_.lock();
        } catch (...) {
            /* 异常路径：撤销记录，避免计数泄漏 */
            contended_.fetch_sub(1, std::memory_order_acq_rel);
            throw;
        }
        /* ④ 竞争者最终获得 */
        contended_.fetch_sub(1, std::memory_order_acq_rel);
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

    /// 已观测到真实竞争、尚未完成获取的线程数（仅供测试观测）。
    /// 语义：try_lock 失败后自增，取得锁后自减。
    /// **不代表**操作系统已挂起该线程。
    int contendedCount() const {
        return contended_.load(std::memory_order_acquire);
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
    std::atomic<int> contended_{0};
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
