#include "nairoclip/core/recovery.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#if defined(__unix__) || defined(__ANDROID__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace nairoclip {
namespace core {

// ---------------- RecoveryStore ordering ----------------

std::vector<std::size_t> RecoveryStore::listNewestFirst() const {
    auto slots = list();
    std::sort(slots.begin(), slots.end(), std::greater<std::size_t>());
    return slots;
}

// ---------------- FileRecoveryStore ----------------

namespace {
constexpr const char* kRecoveryMagic = "NCR1";

bool syncFilePath(const std::filesystem::path& path) {
#if defined(__unix__) || defined(__ANDROID__)
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    const bool ok = (::fsync(fd) == 0);
    ::close(fd);
    return ok;
#else
    (void)path;
    return true;
#endif
}

bool syncDirectoryPath(const std::filesystem::path& path) {
#if defined(__unix__) || defined(__ANDROID__)
    const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0) return false;
    const bool ok = (::fsync(fd) == 0);
    ::close(fd);
    return ok;
#else
    (void)path;
    return true;
#endif
}
}  // namespace

FileRecoveryStore::FileRecoveryStore(std::filesystem::path directory, std::size_t maxSnapshotBytes)
    : directory_(std::move(directory)), maxSnapshotBytes_(maxSnapshotBytes) {}

std::filesystem::path FileRecoveryStore::slotPath(std::size_t slot) const {
    return directory_ / (std::to_string(slot) + ".ncr");
}

std::vector<FileRecoveryStore::Entry> FileRecoveryStore::scanEntries() const {
    std::vector<Entry> entries;
    std::error_code ec;
    if (!std::filesystem::is_directory(directory_, ec) || ec) return entries;
    for (std::filesystem::directory_iterator it(directory_, ec), end; !ec && it != end; it.increment(ec)) {
        const auto status = it->symlink_status(ec);
        if (ec || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status)) {
            ec.clear();
            continue;
        }
        const auto name = it->path().filename().string();
        if (name.size() < 5 || name.substr(name.size() - 4) != ".ncr") continue;
        std::size_t slot = 0;
        try {
            std::size_t used = 0;
            slot = static_cast<std::size_t>(std::stoull(name.substr(0, name.size() - 4), &used));
            if (used != name.size() - 4 || std::to_string(slot) != name.substr(0, name.size() - 4)) continue;
        } catch (...) {
            continue;
        }
        std::ifstream input(it->path(), std::ios::binary);
        std::string magic;
        std::uint64_t sequence = 0;
        if (!(input >> magic >> sequence) || magic != kRecoveryMagic) continue;
        entries.push_back(Entry{slot, sequence});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.sequence != b.sequence) return a.sequence > b.sequence;
        return a.slot > b.slot;
    });
    return entries;
}

Result FileRecoveryStore::write(std::size_t slot, const std::string& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (data.empty() || data.size() > maxSnapshotBytes_) {
        return Result::fail(ErrorCode::OutOfRange, "恢复快照为空或超过大小限制");
    }
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    if (ec) return Result::fail(ErrorCode::Internal, "无法创建恢复目录: " + ec.message());

    std::uint64_t sequence = 1;
    const auto entries = scanEntries();
    if (!entries.empty()) {
        if (entries.front().sequence == std::numeric_limits<std::uint64_t>::max()) {
            return Result::fail(ErrorCode::OutOfRange, "恢复快照序号耗尽");
        }
        sequence = entries.front().sequence + 1;
    }

    const auto destination = slotPath(slot);
    const auto temp = directory_ / (std::to_string(slot) + ".ncr.tmp");
    const auto status = std::filesystem::symlink_status(destination, ec);
    if (!ec && std::filesystem::is_symlink(status)) {
        return Result::fail(ErrorCode::InvalidArgument, "恢复槽位不能是符号链接");
    }
    ec.clear();
    {
        std::ofstream output(temp, std::ios::binary | std::ios::trunc);
        if (!output) return Result::fail(ErrorCode::Internal, "无法创建恢复快照临时文件");
        output << kRecoveryMagic << ' ' << sequence << '\n';
        output.write(data.data(), static_cast<std::streamsize>(data.size()));
        output.flush();
        if (!output) {
            output.close();
            std::filesystem::remove(temp, ec);
            return Result::fail(ErrorCode::Internal, "恢复快照写入失败");
        }
    }
    if (!syncFilePath(temp)) {
        std::filesystem::remove(temp, ec);
        return Result::fail(ErrorCode::Internal, "恢复快照 fsync 失败");
    }
    std::filesystem::rename(temp, destination, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return Result::fail(ErrorCode::Internal, "恢复快照原子替换失败");
    }
    if (!syncDirectoryPath(directory_)) {
        return Result::fail(ErrorCode::Internal, "恢复目录 fsync 失败");
    }
    return Result::ok();
}

Result FileRecoveryStore::read(std::size_t slot, std::string& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto path = slotPath(slot);
    std::error_code ec;
    const auto status = std::filesystem::symlink_status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) {
        return Result::fail(ErrorCode::NotFound, "恢复快照不存在或文件类型非法");
    }
    const auto fileBytes = std::filesystem::file_size(path, ec);
    if (ec || (fileBytes > maxSnapshotBytes_ && fileBytes - maxSnapshotBytes_ > 64)) {
        return Result::fail(ErrorCode::OutOfRange, "恢复快照文件超过大小限制或无法读取大小");
    }
    std::ifstream input(path, std::ios::binary);
    std::string magic;
    std::uint64_t sequence = 0;
    if (!(input >> magic >> sequence) || magic != kRecoveryMagic) {
        return Result::fail(ErrorCode::Internal, "恢复快照头损坏");
    }
    input.get();
    std::string data((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!input.eof() && input.fail()) return Result::fail(ErrorCode::Internal, "恢复快照读取失败");
    out = std::move(data);
    return Result::ok();
}

void FileRecoveryStore::remove(std::size_t slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code ec;
    const auto path = slotPath(slot);
    const auto status = std::filesystem::symlink_status(path, ec);
    if (!ec && std::filesystem::is_regular_file(status) && !std::filesystem::is_symlink(status)) {
        std::filesystem::remove(path, ec);
        if (!ec) (void)syncDirectoryPath(directory_);
    }
}

std::vector<std::size_t> FileRecoveryStore::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto entries = scanEntries();
    std::vector<std::size_t> slots;
    slots.reserve(entries.size());
    for (const auto& entry : entries) slots.push_back(entry.slot);
    std::sort(slots.begin(), slots.end());
    return slots;
}

std::vector<std::size_t> FileRecoveryStore::listNewestFirst() const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto entries = scanEntries();
    std::vector<std::size_t> slots;
    slots.reserve(entries.size());
    for (const auto& entry : entries) slots.push_back(entry.slot);
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

    std::size_t candidate = newestValid;
    for (std::size_t visited = 0; visited < capacity; ++visited) {
        candidate = (candidate + 1 == capacity) ? 0 : candidate + 1;
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
