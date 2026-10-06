#pragma once

#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 自动保存策略。
struct AutoSavePolicy {
    /// 修改后经过该间隔自动保存一次。
    Time interval = fromSeconds(30);
    /// 距上次修改超过该时长（无后续修改）也触发一次保存（防抖后的最终保存）。
    Time idleDelay = fromSeconds(2);
    /// 恢复快照最多保留数量（环形淘汰）。
    std::size_t maxSnapshots = 5;
    /// 快照文本的最大字节数（超出拒绝保存，防御异常大文件）。
    std::size_t maxSnapshotBytes = 64 * 1024 * 1024;
};

/// 恢复存储接口：由平台层实现（Android/iOS/桌面各自的文件系统）。
class RecoveryStore {
public:
    virtual ~RecoveryStore() = default;
    /// 写入快照。slot 为快照槽位（0..N-1）。
    virtual Result write(std::size_t slot, const std::string& data) = 0;
    /// 读取快照；不存在返回失败。
    virtual Result read(std::size_t slot, std::string& out) const = 0;
    /// 删除快照。
    virtual void remove(std::size_t slot) = 0;
    /// 列出存在的槽位（升序）。
    virtual std::vector<std::size_t> list() const = 0;
};

/// 内存恢复存储（测试与预览用）。
class InMemoryRecoveryStore : public RecoveryStore {
public:
    Result write(std::size_t slot, const std::string& data) override;
    Result read(std::size_t slot, std::string& out) const override;
    void remove(std::size_t slot) override;
    std::vector<std::size_t> list() const override;

private:
    std::map<std::size_t, std::string> slots_;
};

/// 崩溃恢复管理器：负责「脏标记 → 定时快照 → 异常退出后恢复」。
/// 状态机：
///   clean --markDirty()--> dirty --tick()达到条件--> 写入快照（仍然 dirty）
///   正常退出：markClean()（清除恢复标志）
///   崩溃：下次启动时 hasRecoverable() 为 true，可恢复最近快照
class AutoSaveManager {
public:
    AutoSaveManager(RecoveryStore& store, AutoSavePolicy policy = {});

    /// 标记工程有未保存修改。now 为当前时间。
    void markDirty(const Time& now);
    /// 标记一次正常保存/正常退出（清除恢复点）。
    void markClean();

    bool isDirty() const { return dirty_; }

    /// 时钟推进：到达策略条件时调用 snapshotProvider 生成快照并写入。
    /// 返回本次是否真正写入了快照。
    bool tick(const Time& now, const std::function<std::string()>& snapshotProvider);

    /// 是否存在可恢复的快照。
    bool hasRecoverable() const;
    /// 尝试恢复最近一次快照。返回值表示成功与否。
    Result recoverLatest(std::string& out) const;

    /// 清除所有快照。
    void clearSnapshots();

    const AutoSavePolicy& policy() const { return policy_; }
    std::size_t snapshotCount() const;

private:
    RecoveryStore& store_;
    AutoSavePolicy policy_;
    bool dirty_ = false;
    bool hasDirtyTime_ = false;
    Time lastDirtyAt_;
    Time lastSnapshotAt_;
    std::size_t nextSlot_ = 0;
};

}  // namespace core
}  // namespace nairoclip
