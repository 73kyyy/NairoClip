# ThreadPool Reentrancy Fix · Implementation Record

- Repository: `73kyyy/NairoClip`
- Branch: `work/v3.3.3-full-remediation`
- Scope: prerequisite hardening for S1-7 / thread-pool concurrency safety
- Verification level: source write + GitHub read-back verified; **build and runtime test not yet executed**

## Defect

In `core/src/thread_pool.cpp::ThreadPool::parallelFor()`, when `hasJob_` was true, the function invoked the user callback `fn(0, count)` while holding `mutex_`.

A callback that re-entered `parallelFor()` would attempt to acquire the same non-recursive mutex and deadlock. The previous comment promised synchronous fallback but the lock scope made that fallback unsafe.

## Change

- Determine the inline-fallback decision while holding `mutex_`.
- Release `mutex_` before invoking user-provided callback code.
- Keep the existing job publication and condition-variable notification path unchanged.
- Add `ThreadPool.ReentrantParallelForFallsBackWithoutDeadlock` in `tests/test_thread_safety.cpp`, which invokes a nested parallel loop from an active worker callback and verifies all seven nested items are processed.

## Commits

- Source fix: `8122d58d5f8a82d8ce53523d4a65771ba46648da`
- Regression test: `84e362b2d9f4556791662c7e758b7b5f06ee4922`

## Read-back verification

Both changed files were fetched again from the remediation branch. The source contains the post-lock `if (runInline) { fn(0, count); return; }` path and the test file contains the new reentrancy test.

## Remaining verification

- Compile the native test target.
- Run the focused ThreadPool/ThreadSafety tests.
- Run the full native suite under normal, ASAN, and UBSAN configurations.
- Run TSan where the toolchain supports it.
- Confirm the test cannot hang under CI timeout and that no regression occurs under concurrent callers.

This record does not claim any test passed.