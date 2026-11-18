# S1-3 Persistence Platform Gap Audit

- Branch: `work/v3.3.3-full-remediation`
- Scope: project save/load/delete, Android draft enumeration, recovery durability
- Status: confirmed blockers; persistence is not complete

## Confirmed implementation state

### 1. Core serialization is memory-only at the C ABI boundary

`nc_project_save()` returns serialized `.nclip` JSON text.
`nc_project_load()` parses supplied text and replaces the in-memory project.
Neither API accepts a filesystem path or performs disk I/O. They are serialization primitives, not durable project storage.

### 2. Android project-file persistence is implemented; recovery snapshots are separate

The current Android bridge implements app-private project-file operations: `saveProjectFile()`, `loadProjectFile()`, `listProjects()`, and `deleteProject()`. The storage root is derived from the application private `filesDir`; filenames are restricted to a bounded `.nclip` allowlist rather than accepting arbitrary paths from JavaScript. Writes use Android `AtomicFile` and synchronize the file descriptor before finishing the write. Reads enforce a size bound and use the AtomicFile backup recovery path.

This is durable project-file storage, not the separate crash-recovery snapshot subsystem. The bridge's project list reports file metadata; it does not establish that `AutoSaveManager` or `FileRecoveryStore` is active.

### 3. Opening a draft is connected to project loading

The editor bootstrap consumes the `project=<filename>` query parameter and calls the bridge load operation before booting the editing engine. A load failure is surfaced and prevents silently opening an empty project. New projects receive a persistent draft identity and subsequent project mutations trigger debounced project-file saves.

### 4. Draft deletion removes the backing file

The current `home.js` deletion flow calls `NCEngine.deleteProject()`, requires an explicit successful result, and then refreshes the list from disk. A list-refresh failure preserves the last known list rather than presenting a false empty state.

These project-file operations correct the earlier draft-list/open/delete gaps. They do not close the remaining S1-3 recovery integration gap described below.

### 5. RecoveryStore has a core file-backed implementation, but is not integrated

A new C++17 `FileRecoveryStore` stores each slot in an individual `.ncr` file. The current `NCR2` record header carries a monotonically increasing sequence number, payload length, and FNV-1a checksum; both reads and directory scans reject truncated or checksum-mismatched payloads. Writes use a same-directory temporary file, flush, POSIX `fsync` where available, and rename. This checksum detects accidental corruption/torn writes; it is not a cryptographic authenticity mechanism. The store is not yet integrated into the Android editor lifecycle.

This is a core storage primitive, not yet an Android recovery feature: `NcEditor` does not own/configure it, no C ABI/JNI method configures its private directory, and editor mutations do not yet drive `AutoSaveManager::markDirty/tick`. The store mutex protects calls through one instance only; multi-process or multiple-instance writers to the same directory are not coordinated. Android project files use a separate `AtomicFile` repository and should not be described as proof that the C++ recovery slots are durable or connected. Crash/disk-fault and Android instrumentation tests have not been run.

## Required implementation work

1. Preserve and independently test the existing Android `AtomicFile` project repository, filename allowlist, project load-before-boot flow, debounced saves, and real disk deletion. Add instrumentation coverage for backup recovery, failed writes, process recreation, and malformed filenames.
2. Define a single-writer ownership contract for `FileRecoveryStore`. Either enforce one instance/process per recovery directory or add an inter-process locking/unique-temp protocol before allowing concurrent writers. The current per-instance mutex is not sufficient for multiple instances.
3. Integrate the recovery store and `AutoSaveManager` into `NcEditor`/C ABI/JNI using a fixed app-private directory. Never accept a filesystem path supplied by WebView/JavaScript.
4. Connect real project mutation events to dirty tracking and periodic snapshot ticks; establish a deterministic shutdown/normal-save policy that clears recovery data only after a successful durable project save.
5. Add startup recovery discovery and an explicit user-confirmed recovery flow, including validation through the project deserializer before replacing editor state.
6. Add crash/process-kill, disk-full, partial-write, checksum mismatch, rename-failure, concurrent-writer, path-traversal, and migration tests.
7. Run core tests and Android instrumentation/build verification on supported environments; record results and platform limitations rather than inferring durability from static inspection.

## Verification and status

- Read the C ABI save/load implementation, Android bridge draft listing, JNI surface, editor bootstrap references, and recovery implementation.
- Corrected the misleading in-memory-only draft deletion UI behavior.
- Added a core `FileRecoveryStore` primitive with an `NCR2` sequence/length/FNV-1a checksum header, validation during scan/read, bounded scan allocation, and corruption/truncation test cases; this is not yet wired to Android or `NcEditor`.
- Build and runtime tests have not been run; the new file-store tests are authored but unexecuted.
- S1-3 remains **incomplete**.
