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
    ++nextSlot_;
    lastSnapshotAt_ = now;
    return true;
}

bool AutoSaveManager::hasRecoverable() const { return !store_.list().empty(); }

Result AutoSaveManager::recoverLatest(std::string& out) const {
    const std::vector<std::size_t> slots = store_.list();
    if (slots.empty()) {
        return Result::fail(ErrorCode::NotFound, "没有可恢复的快照");
    }
    // 最近写入 = nextSlot_ - 1 回退环形（降序尝试）
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const std::size_t index = slots.size() - 1 - i;
        if (store_.read(slots[index], out).isOk()) {
            return Result::ok();
        }
    }
    return Result::fail(ErrorCode::NotFound, "快照读取失败");
}

void AutoSaveManager::clearSnapshots() {
    for (const std::size_t slot : store_.list()) {
        store_.remove(slot);
    }
}

std::size_t AutoSaveManager::snapshotCount() const {
    return store_.list().size();
}

}  // namespace core
}  // namespace nairoclip
