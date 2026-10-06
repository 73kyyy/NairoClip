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
