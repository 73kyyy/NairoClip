# S1-3 / S1-8D / S3-1 Follow-up Evidence Check (2026-11-27)

Repository: `73kyyy/NairoClip`  
Branch: `work/v3.3.3-s5-native-editor-foundation`  
Scope: read-only source verification against the current branch. No protected runtime source, Manifest, or build configuration was changed.

## Current evidence

### S1-3 — project persistence and recovery

Confirmed current implementation:
- `RecoveryStore` is an abstract platform interface.
- The only concrete implementation in the core source is `InMemoryRecoveryStore`.
- `AutoSaveManager::writeOrder_`, `nextSlot_`, dirty timestamps, and snapshot ordering are process-local state.
- `recoverLatest()` depends on the in-memory `writeOrder_`; a new process cannot reconstruct the true latest-write order from the current interface alone.
- Existing tests cover in-memory ring wrap and NCLIP serialization behavior, not Android filesystem durability or process-restart recovery.

Still unproven:
- Android atomic file replacement with durable flush semantics.
- Persistent index / sequence metadata and latest-valid-snapshot discovery after process restart.
- Corruption/truncation rejection and fallback to the previous valid snapshot.
- Real file-backed save → force-stop/process death → relaunch → field-by-field recovery.
- Explicit persisted `Project::id` assertion in that integration path.

**Disposition: blocked / not accepted.** Do not claim S1-3 complete.

### S1-8D — Android backup policy

Confirmed current `android/app/src/main/AndroidManifest.xml`:
- `android:allowBackup="true"`.
- No `android:fullBackupContent` attribute.
- No `android:dataExtractionRules` attribute.
- The current repository tree does not contain `android/app/src/main/res/xml/backup_rules.xml` or `data_extraction_rules.xml`.

The task registry requires the Manifest attribute and both API-generation-specific XML rule files. File ownership requires S1-8D to precede S5-1 because both touch the Manifest.

**Disposition: incomplete / blocked by A-12.** No Manifest or backup-rule file was changed in this follow-up.

### S3-1 — checked time and export-plan boundary

Confirmed current source:
- `timeAtFrame()`, `frameAtTime()`, and `toMicroseconds()` still return plain values and do not expose per-call overflow status.
- `compileExportPlan()` still computes sequence duration, clip/compound offsets, and transition endpoints using ordinary Rational operators.
- `exportPlanValid()` checks structure and ranges but does not establish that plan construction had no Rational overflow.
- `tests/test_time.cpp` covers ordinary 30fps / 30000÷1001 conversions and normal microsecond conversion; it has no overflow-rejection assertions.
- `tests/test_export_plan.cpp` covers normal plan compilation and structural invalidity; it has no assertion that a time-overflow plan is rejected.
- The existing checked-time API design document is still a proposal and explicitly does not implement or change runtime APIs.

**Disposition: blocked / not accepted.** The next implementation must add per-operation checked results and propagate errors through export-plan compilation; it must not rely on an old thread-local sticky overflow flag as the safety boundary.

## Gate and ownership handling

- S1 tasks remain subject to A-12; this review does not grant approval.
- S1-3 and S1-8C share the `serialization.cpp` read-function family and must remain serial.
- S1-8D and S5-1 share `AndroidManifest.xml` and must remain serial.
- S5-0B remains blocked by S1-3 and S3-1 evidence and the applicable approval gates.
- No Launcher Activity cutover, WebView fallback removal, or PR merge is authorized by this audit.

## Permitted next work

1. Prepare an implementation-level Android file-backed RecoveryStore design and crash/restart test plan, then wait for the applicable A-12/file-owner authorization before protected writes.
2. Implement the checked Rational/Time/ExportPlan path only after the S3-1 implementation scope and ownership are authorized.
3. Complete S1-8D as a separate serial change with both backup rule files and Manifest configuration, after A-12 authorization.
4. Continue independent read-only audits and UI component work that do not cross these gates.

This document records evidence only. It does not mark any gate as passed.
