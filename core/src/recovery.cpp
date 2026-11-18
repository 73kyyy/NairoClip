#include "nairoclip/core/recovery.h"

#include <algorithm>

namespace nairoclip {
namespace core {

// ---------------- RecoveryStore ordering ----------------

std::vector<std::size_t> RecoveryStore::listNewestFirst() const {
    auto slots = list();
    std::sort(slots.begin(), slots.end(), std::greater<std::size_t>());
    return slots;
}

// ---------------- InMemoryRecoveryStore ----------------

Result InMemoryRecoveryStore::write(std::size_t slot, const std::string& data) {
    slots_[slot] = data;
    writeOrder_.erase(std::remove(writeOrder_.begin(), writeOrder_.end(), slot), writeOrder_.end());
    writeOrder_.push_back(slot);
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

void InMemoryRecoveryStore::remove(std::size_t slot) {
    slots_.erase(slot);
    writeOrder_.erase(std::remove(writeOrder_.begin(), writeOrder_.end(), slot), writeOrder_.end());
}

std::vector<std::size_t> InMemoryRecoveryStore::list() const {
    std::vector<std::size_t> result;
    result.reserve(slots_.size());
    for (const auto& kv : slots_) {
        result.push_back(kv.first);
    }
    return result;
}

std::vector<std::size_t> InMemoryRecoveryStore::listNewestFirst() const {
    std::vector<std::size_t> result;
    result.reserve(writeOrder_.size());
    for (auto it = writeOrder_.rbegin(); it != writeOrder_.rend(); ++it) {
        if (slots_.find(*it) != slots_.end()) result.push_back(*it);
    }
    return result;
}

// ---------------- AutoSaveManager ----------------

AutoSaveManager::AutoSaveManager(RecoveryStore& store, AutoSavePolicy policy)
    : store_(store), policy_(policy) {
    // Reconcile persisted slots with the current retention policy. Slot IDs may
    // outlive a policy change, so never derive the next slot from max(slot)+1:
    // that can overflow and can point outside the configured ring.
    const std::size_t capacity = policy_.maxSnapshots;
    if (capacity == 0) {
        nextSlot_ = 0;
        return;
    }

    std::vector<bool> occupied(capacity, false);
    const auto existing = store_.list();
    for (const std::size_t slot : existing) {
        if (slot >= capacity) {
            // A smaller retention policy must not leave unreachable snapshots
            // accumulating indefinitely on disk.
            store_.remove(slot);
        } else {
            occupied[slot] = true;
        }
    }
    const auto newestFirst = store_.listNewestFirst();

    if (newestFirst.empty()) return;

    // Prefer the first free slot after the newest valid slot. If the ring is
    // full, overwrite the oldest valid slot (the back of newestFirst).
    std::size_t newestValid = capacity;
    for (const std::size_t slot : newestFirst) {
        if (slot < capacity) {
            newestValid = slot;
            break;
        }
    }
    if (newestValid == capacity) return;

    for (std::size_t offset = 1; offset <= capacity; ++offset) {
        const std::size_t candidate = (newestValid + offset) % capacity;
        if (!occupied[candidate]) {
            nextSlot_ = candidate;
            return;
        }
    }

    for (auto it = newestFirst.rbegin(); it != newestFirst.rend(); ++it) {
        if (*it < capacity) {
            nextSlot_ = *it;
            return;
        }
    }
}

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
    if (!dirty_ || !hasDirtyTime_ || !snapshotProvider || policy_.maxSnapshots == 0) {
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
    const std::size_t slot = nextSlot_ % policy_.maxSnapshots;
    const Result r = store_.write(slot, data);
    if (!r.isOk()) {
        return false;
    }
    nextSlot_ = (slot + 1) % policy_.maxSnapshots;
    lastSnapshotAt_ = now;
    return true;
}

bool AutoSaveManager::hasRecoverable() const { return !store_.list().empty(); }

Result AutoSaveManager::recoverLatest(std::string& out) const {
    // 顺序属于 RecoveryStore，而非 AutoSaveManager 的进程内状态。
    const auto newestFirst = store_.listNewestFirst();
    if (newestFirst.empty()) {
        return Result::fail(ErrorCode::NotFound, "没有可恢复的快照");
    }
    for (const std::size_t slot : newestFirst) {
        if (store_.read(slot, out).isOk()) return Result::ok();
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
