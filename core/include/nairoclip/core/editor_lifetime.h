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
#include <mutex>

namespace nairoclip {
namespace core {

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
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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
        std::lock_guard<std::recursive_mutex> lock(mutex_);
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

    /// 生命周期互斥量。调用方持有它可把「校验 + 使用」变成原子区。
    /// 使用 recursive_mutex：同一线程内的嵌套 JNI 调用不会自死锁。
    std::recursive_mutex& mutex() { return mutex_; }

private:
    std::atomic<void*> current_{nullptr};
    std::recursive_mutex mutex_;
};

}  // namespace core
}  // namespace nairoclip

#endif  // NAIROCLIP_CORE_EDITOR_LIFETIME_H
