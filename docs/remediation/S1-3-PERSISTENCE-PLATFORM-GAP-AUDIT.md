# S1-3 Persistence Platform Gap Audit

- Branch: `work/v3.3.3-full-remediation`
- Scope: project save/load/delete, Android draft enumeration, recovery durability
- Status: confirmed blockers; persistence is not complete

## Confirmed implementation state

### 1. Core serialization is memory-only at the C ABI boundary

`nc_project_save()` returns serialized `.nclip` JSON text.
`nc_project_load()` parses supplied text and replaces the in-memory project.
Neither API accepts a filesystem path or performs disk I/O. They are serialization primitives, not durable project storage.

### 2. Android currently enumerates draft files but does not persist projects

`NativeEngineBridge.listProjects()` scans only the top-level `filesDir` for `.nclip` files and returns filename and byte size. It does not expose a save, load-by-filename, or delete operation. The returned records do not include actual duration or modification time.

### 3. Opening a draft is not connected to project loading

`home.js` navigates to the editor with a `project=<filename>` query parameter. The inspected editor bootstrap and NativeEngineBridge do not implement a corresponding load-from-disk operation. The parameter alone does not load a project.

### 4. Draft deletion previously changed only the in-memory list

The previous `deletePicked()` filtered `H.projects` and displayed a removal toast without deleting the backing file. This was misleading because the draft reappeared after a reload.

A corrective change now refuses to claim deletion while `NCEngine.deleteProject()` is unavailable. If that API is added later, the UI will require an explicit positive result and then reload the list from disk.

### 5. RecoveryStore is not a durable platform store

The current concrete implementation inspected in `recovery.cpp` is `InMemoryRecoveryStore`. The `RecoveryStore` interface allows platform implementations, but no Android filesystem implementation or JNI/C ABI integration was found in the repository tree. `AutoSaveManager` therefore cannot survive process death when used with the in-memory implementation.

## Required implementation work

1. Add a platform-owned project repository with safe filename validation and a fixed private storage root.
2. Implement atomic save: write a temporary file in the same directory, flush/fsync where supported, validate the completed serialization, then atomically rename; preserve the previous valid file on failure.
3. Implement load-by-ID/file through the platform bridge, with path traversal and symlink protections; do not accept arbitrary absolute paths from JavaScript.
4. Implement permanent delete with explicit success/failure results and safe handling of missing files.
5. Make editor startup consume the requested project identifier and load it before state/preview initialization.
6. Add durable Android RecoveryStore using app-private files, bounded slots, atomic writes, and startup recovery discovery.
7. Add crash/process-kill, disk-full, partial-write, corrupted-file, concurrent-save, rename-failure, path-traversal, and migration tests.
8. Add platform instrumentation tests. Core in-memory tests alone do not prove filesystem durability.

## Verification and status

- Read the C ABI save/load implementation, Android bridge draft listing, JNI surface, editor bootstrap references, and recovery implementation.
- Corrected the misleading in-memory-only draft deletion UI behavior.
- No durable save/load/delete or Android RecoveryStore implementation was added in this audit.
- Build and runtime tests have not been run.
- S1-3 remains **incomplete**.
