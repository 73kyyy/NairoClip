# TaskRegistry Start Transition Fix

- Repository: `73kyyy/NairoClip`
- Branch: `work/v3.3.3-full-remediation`
- Scope: S1-7 task-state correctness hardening
- Verification level: source write + GitHub read-back verified; build/runtime tests not executed

## Defect

`TaskRegistry::start(taskId, nowMs)` previously rejected only terminal states. It therefore accepted repeated starts while a task was already Running and accepted starts while it was CancelRequested.

A repeated Running start overwrote `startedAtMs` and `lastHeartbeatMs`. This allowed a caller to continually postpone total-timeout and heartbeat-timeout detection without reporting real progress. Starting a CancelRequested task also changed its state back to Running while leaving the cancellation-request flag set, creating contradictory state.

## Change

- Restrict start to the single legal transition `Queued -> Running`.
- Preserve the original timeout clocks after the first start.
- Reject attempts to restart a running task or revive a cancellation-requested task.
- Add regression tests for timeout-clock preservation and cancellation-state preservation.

## Files

- `core/src/contract/task.cpp`
- `tests/test_task_registry.cpp`

## Commits

- Implementation: `3365a6f958a3aa740a7e6803a4f3c3b156626f12`
- Regression tests: `4d98b4d8d841c8e9d065e4f450ed0f4a1ff42f19`

## Read-back

The implementation guard, both regression tests, and the test source were fetched again from the remediation branch. The test source contains 28 `NC_TEST` cases and is already part of the repository test target.

## Remaining verification

- Build the native test target.
- Run focused TaskRegistry tests.
- Run full native tests under normal, ASAN, UBSAN and available TSan configurations.
- Confirm all C ABI and Kotlin callers obey the single-engine serialization contract.

This record does not claim that tests passed.

## Additional cancellation-timeout bypass found during follow-up review

A second state-machine defect was found in the same registry:

- `setProgress()` and `heartbeat()` rejected terminal states but still accepted updates in `CancelRequested`.
- A worker could therefore continue refreshing `lastHeartbeatMs` after cancellation had been requested, indefinitely postponing the cancellation watchdog timeout.
- Both methods now reject updates while the task is `CancelRequested`. The executor must acknowledge cancellation or the watchdog will fail the stuck task.
- Added `ProgressCannotRefreshCancellationTimeout`, which verifies progress and heartbeat rejection, unchanged heartbeat clock, and watchdog timeout.

Additional commits:
- Implementation: `338095277c8fff9e4b9c256be0d31438017a3062`
- Regression test: `5960a6609983c8f8fb13e2c8d1bb0a5cf48a60a1`

The updated source and test were read back from the remediation branch. Compilation and test execution remain pending.

## Monotonic clock hardening

A further cross-layer review found that Android supplied `System.currentTimeMillis()` to the task watchdog. Wall-clock corrections can move this clock forward or backward and make elapsed-time deadlines unreliable.

Changes:
- Replaced all seven task/watchdog timestamp calls in `NativeEngineBridge.kt` with `SystemClock.elapsedRealtime()`.
- Updated the bridge and TaskRegistry API comments to state the monotonic-clock contract.
- `TaskRegistry::heartbeat()` now rejects timestamps older than the previous heartbeat without mutating progress, stage note, or heartbeat time.
- Added `StaleHeartbeatTimestampIsRejectedWithoutMutation` to cover stale callback rejection and subsequent watchdog timeout.

Commits:
- Android monotonic clock: `8bf379c73011ec4c8a6e3b62c9ff8e3d3d77e9e3`
- stale heartbeat guard: `b94b6111aa54e849e404f44784580794e2202b31`
- regression test: `cd01e7110254ec8dd0d393719d3a008fe688abc8`
- bridge clock documentation: `e6ab792c3677ed559805d7a24f201eacca6612ec`
- TaskRegistry clock contract: `6bea89788c1110151e73e65e24eb6d7c4f4ad16b`

Read-back checks confirmed that no `System.currentTimeMillis()` remains in the bridge, all seven task clock call sites use `elapsedRealtime()`, and the stale-timestamp guard and regression test are present.

Build and runtime tests remain pending; no test pass is claimed.
