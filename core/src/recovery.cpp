#include "nairoclip/core/recovery.h"

#include <algorithm>

namespace nairoclip {
namespace core {

// ---------------- InMemoryRecoveryStore ----------------

Result InMemoryRecoveryStore::write(std::size_t slot, const std::string& data) {
    slots_[slot] = data;
    return Result::ok();
}

Result InMemoryRecoveryStore::read(std::size_t slot, std::string& out) const {
    const auto it = slots_.find(slot);
    if (it == slots_.end()) {
        return Result::fail(ErrorCode::NotFound, "快照不存在");
    }
    out = it->second;
    return Result::ok();
}

void InMemoryRecoveryStore::remove(std::size_t slot) { slots_.erase(slot); }

std::vector<std::size_t> InMemoryRecoveryStore::list() const {
    std::vector<std::size_t> result;
    result.reserve(slots_.size());
    for (const auto& kv : slots_) {
        result.push_back(kv.first);
    }
    return result;
}

// ---------------- AutoSaveManager ----------------

AutoSaveManager::AutoSaveManager(RecoveryStore& store, AutoSavePolicy policy)
    : store_(store), policy_(policy) {}

void AutoSaveManager::markDirty(const Time& now) {
    dirty_ = true;
    hasDirtyTime_ = true;
    lastDirtyAt_ = now;
}

void AutoSaveManager::markClean() {
    dirty_ = false;
    hasDirtyTime_ = false;
    clearSnapshots();
}

bool AutoSaveManager::tick(const Time& now, const std::function<std::string()>& snapshotProvider) {
    if (!dirty_ || !hasDirtyTime_ || !snapshotProvider) {
        return false;
    }
    // 条件 A：距上次修改至少 idleDelay（防抖（静止后保存）
    const bool idleReached = (now - lastDirtyAt_) >= policy_.idleDelay;
    // 条件 B：周期性保存。首次快照以「上次修改」为基准，之后以「上次快照」为基准。
    const Time base = snapshotCount() > 0 ? lastSnapshotAt_ : lastDirtyAt_;
    const bool intervalReached = (now - base) >= policy_.interval;
    if (!idleReached && !intervalReached) {
        return false;
    }
    const std::string data = snapshotProvider();
    if (data.empty()) {
        return false;
    }
    if (data.size() > policy_.maxSnapshotBytes) {
        return false;  // 防御异常大快照
    }
    const std::size_t slot = nextSlot_ % (policy_.maxSnapshots == 0 ? 1 : policy_.maxSnapshots);
    const Result r = store_.write(slot, data);
    if (!r.isOk()) {
        return false;
    }
    /* S1-3：记录真实写入顺序（同一槽位被覆盖时也要记，恢复时以顺序为准）。 */
    writeOrder_.push_back(slot);
    ++nextSlot_;
    lastSnapshotAt_ = now;
    return true;
}

bool AutoSaveManager::hasRecoverable() const { return !store_.list().empty(); }

Result AutoSaveManager::recoverLatest(std::string& out) const {
    /* S1-3 修复：按**真实写入顺序**倒序取最近快照。
       修复前实现：`store_.list()` 返回按 slot 编号排序的键（std::map 序），
       却被当作写入顺序逆序遍历 —— 环形槽位回绕后（例如 3 槽写 5 次：
       0,1,2,0,1）会先读到 slot 2（第 3 次写入的旧快照），
       把工程恢复到更早的状态，丢失最近编辑。 */
    if (writeOrder_.empty()) {
        return Result::fail(ErrorCode::NotFound, "没有可恢复的快照");
    }
    // 从最近一次写入往前找第一个仍存在且能读出的槽位
    for (std::size_t i = writeOrder_.size(); i > 0; --i) {
        if (store_.read(writeOrder_[i - 1], out).isOk()) {
            return Result::ok();
        }
    }
    return Result::fail(ErrorCode::NotFound, "快照读取失败");
}

void AutoSaveManager::clearSnapshots() {
    for (const std::size_t slot : store_.list()) {
        store_.remove(slot);
    }
    writeOrder_.clear();   // S1-3：写入顺序与实际快照必须同步清空
}

std::size_t AutoSaveManager::snapshotCount() const {
    return store_.list().size();
}

}  // namespace core
}  // namespace nairoclip
