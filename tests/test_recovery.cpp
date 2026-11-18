#include "test_framework.h"
#include "nairoclip/core/recovery.h"
#include "nairoclip/core/serialization.h"
using namespace nairoclip::core;
namespace {
std::string snapshot() { return "{\"format\":\"nclip\",\"version\":1}"; }
AutoSavePolicy smallPolicy() {
    AutoSavePolicy p;
    p.interval = fromSeconds(10);
    p.idleDelay = fromSeconds(1);
    p.maxSnapshots = 3;
    return p;
}
}  // namespace
NC_TEST(Recovery, FileStorePersistsOrderingAcrossStoreReconstruction) {
    const auto dir = std::filesystem::temp_directory_path() / "nairoclip-recovery-persistence-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    {
        FileRecoveryStore store(dir);
        NC_CHECK(store.write(0, "snapshot-1").isOk());
        NC_CHECK(store.write(1, "snapshot-2").isOk());
        NC_CHECK(store.write(0, "snapshot-3").isOk());
    }
    {
        FileRecoveryStore reopened(dir);
        const auto order = reopened.listNewestFirst();
        NC_CHECK_EQ(order.size(), std::size_t(2));
        NC_CHECK_EQ(order[0], std::size_t(0));
        NC_CHECK_EQ(order[1], std::size_t(1));
        std::string latest;
        NC_CHECK(reopened.read(order[0], latest).isOk());
        NC_CHECK_EQ(latest, std::string("snapshot-3"));
    }
    std::filesystem::remove_all(dir, ec);
}

NC_TEST(Recovery, FileStoreEnforcesSnapshotSizeLimit) {
    const auto dir = std::filesystem::temp_directory_path() / "nairoclip-recovery-size-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    FileRecoveryStore store(dir, 8);
    NC_CHECK(!store.write(0, "123456789").isOk());
    NC_CHECK(store.write(0, "12345678").isOk());
    std::string out;
    NC_CHECK(store.read(0, out).isOk());
    NC_CHECK_EQ(out, std::string("12345678"));
    std::filesystem::remove_all(dir, ec);
}

NC_TEST(Recovery, AutoSaveRecoversLatestFromReopenedFileStore) {
    const auto dir = std::filesystem::temp_directory_path() / "nairoclip-autosave-reopen-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    AutoSavePolicy policy = smallPolicy();
    policy.maxSnapshots = 2;
    {
        FileRecoveryStore store(dir);
        AutoSaveManager manager(store, policy);
        for (int i = 0; i < 3; ++i) {
            manager.markDirty(fromSeconds(i * 10));
            const std::string value = "persisted-" + std::to_string(i + 1);
            NC_CHECK(manager.tick(fromSeconds(i * 10 + 2), [value] { return value; }));
        }
    }
    {
        FileRecoveryStore reopened(dir);
        AutoSaveManager manager(reopened, policy);
        std::string latest;
        NC_CHECK(manager.recoverLatest(latest).isOk());
        NC_CHECK_EQ(latest, std::string("persisted-3"));
    }
    std::filesystem::remove_all(dir, ec);
}

NC_TEST(Recovery, FileStoreIgnoresCorruptAndTemporaryFiles) {
    const auto dir = std::filesystem::temp_directory_path() / "nairoclip-recovery-corrupt-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    {
        std::ofstream corrupt(dir / "4.ncr", std::ios::binary);
        corrupt << "not-a-recovery-file";
        std::ofstream temporary(dir / "5.ncr.tmp", std::ios::binary);
        temporary << "NCR2 99 7 0\\npartial";
    }
    FileRecoveryStore store(dir);
    NC_CHECK(store.write(2, "valid-snapshot").isOk());
    const auto slots = store.list();
    NC_CHECK_EQ(slots.size(), std::size_t(1));
    NC_CHECK_EQ(slots[0], std::size_t(2));
    std::string out;
    NC_CHECK(!store.read(4, out).isOk());
    std::filesystem::remove_all(dir, ec);
}

NC_TEST(Recovery, FileStoreRejectsTruncatedAndCorruptedPayloads) {
    const auto dir = std::filesystem::temp_directory_path() / "nairoclip-recovery-integrity-test";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    FileRecoveryStore store(dir);
    NC_CHECK(store.write(0, "complete-payload").isOk());
    const auto path = dir / "0.ncr";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file << "NCR2 10 100 123\\nshort";
    }
    std::string out;
    NC_CHECK(!store.read(0, out).isOk());
    NC_CHECK(store.list().empty());
    NC_CHECK(store.write(0, "complete-payload").isOk());
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-1, std::ios::end);
        file.put('X');
    }
    NC_CHECK(!store.read(0, out).isOk());
    NC_CHECK(store.list().empty());
    std::filesystem::remove_all(dir, ec);
}

NC_TEST(Recovery, StoreReadWrite) {
    InMemoryRecoveryStore store;
    NC_CHECK(store.write(0, "abc").isOk());
    std::string out;
    NC_CHECK(store.read(0, out).isOk());
    NC_CHECK_EQ(out, std::string("abc"));
    NC_CHECK(!store.read(1, out).isOk());
    NC_CHECK_EQ(store.list().size(), std::size_t(1));
    store.remove(0);
    NC_CHECK_EQ(store.list().size(), std::size_t(0));
}
NC_TEST(Recovery, CleanTickProducesNothing) {
    InMemoryRecoveryStore store;
    AutoSaveManager mgr(store, smallPolicy());
    NC_CHECK(!mgr.tick(fromSeconds(100), [] { return snapshot(); }));
    NC_CHECK_EQ(mgr.snapshotCount(), std::size_t(0));
}
NC_TEST(Recovery, IdleSaveAfterDebounce) {
    InMemoryRecoveryStore store;
    AutoSaveManager mgr(store, smallPolicy());
    mgr.markDirty(fromSeconds(0));
    NC_CHECK(!mgr.tick(fromMilliseconds(500), [] { return snapshot(); }));
    NC_CHECK(mgr.tick(fromSeconds(2), [] { return snapshot(); }));
    NC_CHECK_EQ(mgr.snapshotCount(), std::size_t(1));
    NC_CHECK(mgr.hasRecoverable());
}
NC_TEST(Recovery, PeriodicSaveBasedOnInterval) {
    InMemoryRecoveryStore store;
    AutoSaveManager mgr(store, smallPolicy());
    mgr.markDirty(fromSeconds(0));
    NC_CHECK(mgr.tick(fromSeconds(2), [] { return snapshot(); }));   // idle 触发
    mgr.markDirty(fromSeconds(3));
    // 3.5s：距上次修改仅 0.5s（< idleDelay=1s），距上次快照 1.5s（< interval=10s）→ 不保存
    NC_CHECK(!mgr.tick(fromMilliseconds(3500), [] { return snapshot(); }));
    NC_CHECK(mgr.tick(fromSeconds(15), [] { return snapshot(); }));  // interval 触发
    NC_CHECK_EQ(mgr.snapshotCount(), std::size_t(2));
}
NC_TEST(Recovery, RingBufferEvictsOldest) {
    InMemoryRecoveryStore store;
    AutoSavePolicy p = smallPolicy();
    p.maxSnapshots = 2;
    AutoSaveManager mgr(store, p);
    int version = 0;
    for (int i = 0; i < 4; ++i) {
        mgr.markDirty(fromSeconds(i * 10));
        ++version;
        const int v = version;
        NC_CHECK(mgr.tick(fromSeconds(i * 10 + 2),
                          [v] { return std::string("{\"v\":") + std::to_string(v) + "}"; }));
    }
    NC_CHECK_EQ(mgr.snapshotCount(), std::size_t(2));
    std::string out;
    NC_CHECK(mgr.recoverLatest(out).isOk());
    NC_CHECK(out.find("4") != std::string::npos);
}
NC_TEST(Recovery, MarkCleanClearsSnapshots) {
    InMemoryRecoveryStore store;
    AutoSaveManager mgr(store, smallPolicy());
    mgr.markDirty(fromSeconds(0));
    NC_CHECK(mgr.tick(fromSeconds(2), [] { return snapshot(); }));
    NC_CHECK(mgr.hasRecoverable());
    mgr.markClean();
    NC_CHECK(!mgr.hasRecoverable());
    NC_CHECK(!mgr.isDirty());
}
NC_TEST(Recovery, EmptySnapshotRejected) {
    InMemoryRecoveryStore store;
    AutoSaveManager mgr(store, smallPolicy());
    mgr.markDirty(fromSeconds(0));
    NC_CHECK(!mgr.tick(fromSeconds(5), [] { return std::string(); }));
    NC_CHECK_EQ(mgr.snapshotCount(), std::size_t(0));
}
NC_TEST(Recovery, OversizedSnapshotRejected) {
    InMemoryRecoveryStore store;
    AutoSavePolicy p = smallPolicy();
    p.maxSnapshotBytes = 32;
    AutoSaveManager mgr(store, p);
    mgr.markDirty(fromSeconds(0));
    NC_CHECK(!mgr.tick(fromSeconds(5), [] { return std::string(64, 'x'); }));
    NC_CHECK_EQ(mgr.snapshotCount(), std::size_t(0));
}
NC_TEST(Recovery, RecoverWithoutSnapshotFails) {
    InMemoryRecoveryStore store;
    AutoSaveManager mgr(store, smallPolicy());
    std::string out;
    NC_CHECK(!mgr.recoverLatest(out).isOk());
}
NC_TEST(Recovery, SnapshotIntegratesWithSerialization) {
    InMemoryRecoveryStore store;
    AutoSaveManager mgr(store, smallPolicy());
    Project project;
    project.name = "崩溃恢复工程";
    mgr.markDirty(fromSeconds(0));
    NC_CHECK(mgr.tick(fromSeconds(2), [&project] { return saveProjectToNclip(project); }));
    std::string snapshotText;
    NC_CHECK(mgr.recoverLatest(snapshotText).isOk());
    Project restored;
    NC_CHECK(loadProjectFromNclip(snapshotText, restored).result.isOk());
    NC_CHECK_EQ(restored.name, std::string("崩溃恢复工程"));
}

NC_TEST(Recovery, LatestOrderingBelongsToStoreNotManagerLifetime) {
    InMemoryRecoveryStore store;
    AutoSavePolicy p = smallPolicy();
    p.maxSnapshots = 3;
    AutoSaveManager first(store, p);
    for (int i = 0; i < 5; ++i) {
        first.markDirty(fromSeconds(i * 10));
        const int captured = i + 1;
        NC_CHECK(first.tick(fromSeconds(i * 10 + 2), [captured] {
            return std::string("{\"v\":") + std::to_string(captured) + "}";
        }));
    }
    AutoSaveManager afterRestart(store, p);
    std::string out;
    NC_CHECK(afterRestart.recoverLatest(out).isOk());
    NC_CHECK(out.find("\"v\":5") != std::string::npos);
}

NC_TEST(Recovery, ZeroSnapshotCapacityDisablesWrites) {
    InMemoryRecoveryStore store;
    AutoSavePolicy p = smallPolicy();
    p.maxSnapshots = 0;
    AutoSaveManager mgr(store, p);
    mgr.markDirty(fromSeconds(0));
    NC_CHECK(!mgr.tick(fromSeconds(2), [] { return snapshot(); }));
    NC_CHECK_EQ(mgr.snapshotCount(), std::size_t(0));
}

NC_TEST(Recovery, RestartReconcilesSlotsAfterRetentionPolicyShrinks) {
    InMemoryRecoveryStore store;
    AutoSavePolicy original = smallPolicy();
    original.maxSnapshots = 5;
    {
        AutoSaveManager mgr(store, original);
        for (int i = 0; i < 4; ++i) {
            mgr.markDirty(fromSeconds(i * 10));
            const int captured = i + 1;
            NC_CHECK(mgr.tick(fromSeconds(i * 10 + 2), [captured] {
                return std::string("snapshot-") + std::to_string(captured);
            }));
        }
    }

    AutoSavePolicy reduced = smallPolicy();
    reduced.maxSnapshots = 2;
    AutoSaveManager restarted(store, reduced);
    restarted.markDirty(fromSeconds(100));
    NC_CHECK(restarted.tick(fromSeconds(102), [] { return std::string("snapshot-5"); }));
    NC_CHECK_EQ(restarted.snapshotCount(), std::size_t(2));
    std::string latest;
    NC_CHECK(restarted.recoverLatest(latest).isOk());
    NC_CHECK_EQ(latest, std::string("snapshot-5"));
}

NC_TEST(Recovery, StoreOrderingTracksSlotOverwrite) {
    InMemoryRecoveryStore store;
    NC_CHECK(store.write(0, "old-zero").isOk());
    NC_CHECK(store.write(1, "middle").isOk());
    NC_CHECK(store.write(0, "new-zero").isOk());
    const auto order = store.listNewestFirst();
    NC_CHECK_EQ(order.size(), std::size_t(2));
    NC_CHECK_EQ(order[0], std::size_t(0));
    NC_CHECK_EQ(order[1], std::size_t(1));
    std::string latest;
    NC_CHECK(store.read(order[0], latest).isOk());
    NC_CHECK_EQ(latest, std::string("new-zero"));
}
