# S1-3 Android Durable Project Store

## Implemented on the remediation branch

### Native serialization boundary
- Added typed JNI entry points for `nc_project_save()` and `nc_project_load()`.
- Both entry points use `EditorHandleGuard`, preserving the existing native editor lifetime lock.
- Native strings and error details are released with `nc_free_string()`.

### Android private storage
- Added `NativeEngineBridge.saveProjectFile()`, `loadProjectFile()`, and `deleteProject()`.
- Files are constrained to safe `.nclip` basenames in the application-private `filesDir`; arbitrary paths and traversal components are rejected.
- Save uses Android `AtomicFile`, writes UTF-8 serialized project text, calls `FileDescriptor.sync()`, and commits with `finishWrite()`; failure calls `failWrite()`.
- Reads and writes are bounded to 64 MiB. Oversized or empty files are rejected before parsing.
- Draft listing attempts `AtomicFile.openRead()` for orphaned `.bak` sidecars so an interrupted replacement can restore its last committed version.
- Permanent deletion uses `AtomicFile.delete()` and verifies the base, `.bak`, and `.new` sidecars are absent.
- A random project filename is allocated on first save and reused by the active bridge instance.
- Draft listing now returns modification time and sorts newest first.
- Listing is serialized with save/load/delete under the same bridge lock; directory I/O and backup-recovery failures return explicit `ok:false` responses instead of masquerading as an empty folder.
- The home screen preserves its last successfully loaded draft list when a refresh fails, rather than replacing it with an empty list.
- Deletion also handles interrupted-write sidecars when the base `.nclip` file is absent.

### Editor integration
- Opening a draft calls `loadProjectFile()` before `Engine.boot()` reads the project state.
- A failed/missing load aborts editor startup with a visible error instead of silently opening a blank project.
- A new editor session immediately creates its first local draft.
- Successful mapped edits schedule a debounced local save; page hide and app background transitions flush pending dirty state.
- Home's existing permanent-delete action now reaches the implemented bridge method and only reports success after the file deletion succeeds.

## Important limitations still open

1. Autosave is attached to the mapped `edit()` path and a mutation-prefix allowlist in the generic `engineCall()` path. Any future mutation API must be added to that allowlist or explicitly schedule a save.
2. No Android instrumentation tests have yet exercised process death, AtomicFile recovery, disk-full/write failures, or corrupted project files.
3. No Gradle/NDK build or device runtime verification has been run after these changes.
4. This implements Android local project durability only; it does not complete cross-platform persistence or durable RecoveryStore integration.
5. Draft metadata currently reports filename, byte size, and filesystem modification time; duration extraction is not yet implemented.

## Verification status

- JNI, Kotlin bridge, and editor integration were read back from the remediation branch after commits.
- Static anchor checks confirm the new methods and editor call paths are present.
- Compilation, instrumentation, crash recovery, and end-to-end save/open/delete tests remain **not run**.
- S1-3 is **partially implemented, not complete**.
