# S1-7 Cancellation and Executor Lifecycle Audit

- Repository: `73kyyy/NairoClip`
- Branch: `work/v3.3.3-full-remediation`
- Scope: cancellation propagation, executor acknowledgement, and engine teardown
- Status: confirmed integration gaps; no false claim of executor cancellation

## Confirmed behavior

### 1. Generic task cancellation is currently bookkeeping only

The call chain is:

`NativeEngineBridge.taskCancel()`
→ `nativeTaskCancel()`
→ `nc_task_cancel()`
→ `TaskRegistry::cancel()`.

The final operation only sets `cancelRequested=true` and `state=CancelRequested`. It does not signal a worker, cancellation token, condition variable, or model runtime. The separate `taskCancelAck()` path calls `acknowledgeCancel()`, but the acknowledgement is caller-driven rather than tied to an observed executor stop.

The generic task methods are exposed through `engine.js`, but the inspected `ai-edit.js` and `editor.js` contain no calls to the generic task APIs or the typed AI task lifecycle APIs. Therefore the task registry is not demonstrated to wrap the visible AI feature execution paths.

### 2. Export cancellation is a separate protocol

`executeExport()` supports cooperative cancellation only when its progress callback returns false. `ExportQueue::remove()` and `cancelAll()` change queue state, while the header explicitly assigns stopping the corresponding worker to the platform layer. These APIs are not automatically connected to `TaskRegistry` cancellation.

### 3. Engine teardown does not own or join a task executor

`nc_editor_destroy()` deletes the dispatcher, timeline editor, and `NcEditor`. It does not call a task-executor shutdown/join interface. The Android bridge serializes JNI access with the lifecycle lock and shuts down its watchdog, but that watchdog only calls `nc_task_tick()`; it is not the AI/export execution worker.

This audit does not establish that an active background worker currently dereferences freed editor state. It establishes that no executor ownership/stop/join contract is present in the inspected task lifecycle path, so safe teardown cannot be proven from this code alone.

## Required remediation before claiming S1-7 complete

1. Introduce an owned executor/cancellation contract (per task ID) with a cooperative stop token or equivalent.
2. Make `nc_task_cancel` signal the registered executor, not merely mutate the registry.
3. Allow `Cancelled` only after the executor has actually stopped; acknowledgement must originate from that stop completion path.
4. Prevent late progress/result callbacks after cancellation and after editor shutdown.
5. Add an editor shutdown sequence: reject new tasks, request cancellation, wait for owned workers to quiesce, then destroy task/runtime/editor state.
6. Connect AI and export execution entry points to the same lifecycle contract, or explicitly maintain separate typed contracts with equivalent guarantees.
7. Add tests for cancellation propagation, cancellation-vs-completion races, worker stop acknowledgement, editor destruction during active work, and callback-after-destroy rejection.

## Related fix already applied

The TaskRegistry now rejects `complete()` and `completeWithResult()` while `CancelRequested`, preventing a late success callback from overwriting a previously registered cancellation request. Regression coverage is in `tests/test_task_registry.cpp` under `CompletionCannotOverrideCancellationRequest`.

## Verification

- The cancellation and teardown call sites were read from the remediation branch.
- This is a source-level audit only. Native build, runtime tests, and Android instrumentation tests have not been run.
- S1-7 remains **incomplete** until executor propagation and shutdown ownership are implemented and tested.
