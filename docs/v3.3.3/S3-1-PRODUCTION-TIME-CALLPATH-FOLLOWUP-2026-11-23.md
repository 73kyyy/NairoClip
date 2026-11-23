# S3-1 Production Time Call-Path Follow-up — 2026-11-23

## Scope

Repository: `73kyyy/NairoClip`
PR: #32
Reviewed source HEAD: `945b0cd7e15603abb07272b1ae875126967f9a48`

This is a read-only call-path audit and remediation plan. No Rational, Time, export, preview, C ABI, JNI, persistence, or protected source was modified. The S3-1 approval status is unchanged.

## Production call path confirmed

### Export entry

`core/src/capi.cpp::ncExportRunImpl`:

1. Resolves the main sequence.
2. Calls `compileExportPlan(editor->project, *seq)`.
3. Builds FFmpeg reader/writer factories and export configuration.
4. Copies `editor->project` into `exportSnapshot`.
5. Calls `runExport(plan, ..., &exportSnapshot)`.

The plan is therefore compiled from the live project before the separate project snapshot is taken. Both currently occur synchronously in this function, but the plan and the project pointer passed to the executor are not constructed as one atomic source snapshot. The eventual checked API should compile both the render plan and all project-dependent execution data from one captured project revision under the editor's existing synchronization boundary.

### Export validation and execution

- `compileExportPlan` compiles clip timeline ends, source-outs, sequence duration, compound offsets, and transition endpoints using the legacy Rational operators.
- `exportPlanValid` checks dimensions, positive frame rate, nonnegative duration, clip intervals, source intervals, and positive speed. It does not inspect the thread-local overflow flag or distinguish a saturated value from a legitimate `INT64_MAX/1` value.
- `executeExport` calls `exportPlanValid` before rendering. Its video frame loop uses `timeAtFrame(fps, frameIndex)`, but that legacy function returns a plain `Time` and cannot report overflow or invalid-frame-time errors.
- `runExport` receives the already compiled plan; it does not recompile or establish a checked time boundary.

### Preview path

- `core/src/capi.cpp::nc_preview_render` converts the integer microsecond argument using `fromMicroseconds`, then calls `PreviewRenderer::renderAt`.
- `PreviewRenderer::renderAt` derives its cache key with `static_cast<int64_t>(timelineTime.toDouble() * 1e6 + 0.5)`.
- The preview renderer and export executor are separate render paths over the same project/time model. Preview cache time conversion therefore needs the same checked integer-microsecond policy as export and C ABI serialization.

## Confirmed time-risk inventory

### P0 — checked plan compilation must precede any writer side effect

`ncExportRunImpl` currently compiles a legacy plan and `runExport` creates/opens the writer before `executeExport` validates the plan. Although executeExport rejects a plan that fails its current validator, overflow cannot reliably be distinguished by that validator. The target design must reject all checked-time errors before creating/opening an output writer or touching the destination file.

### P0 — plan and project snapshot are not a single captured revision

The plan is compiled from `editor->project`, then the project is copied and passed to `runExport`. The checked migration should capture a stable project snapshot first, identify its revision, and compile the plan from that exact snapshot. If the editor synchronization contract does not guarantee this sequence, the current ordering risks a plan/project mismatch.

### P0 — sequence/clip/transition arithmetic silently propagates saturation

The reviewed production chain includes:
- `Sequence::duration()` calling `Clip::end()`;
- `compileClip()` calling `clip.end()` and `clip.sourceOut()`;
- compound expansion adding nested offsets to timeline start/end;
- transition compilation adding duration to the source clip end.

All use legacy Rational arithmetic. The plan validator only sees the resulting values and cannot tell whether they were saturated.

### P1 — speed-curve source-time integration leaves the exact domain

`export_executor.cpp::mapSourceTime` uses `double` for local seconds, source seconds, fixed-step integration, and conversion back to microsecond Rational. This may be an intentional numerical integration approximation for nonlinear speed curves, but its error budget, overflow behavior, and deterministic boundary behavior are not explicitly checked. It must not be silently treated as exact rational frame mapping.

Required treatment:
- retain deterministic fixed-step integration only with explicit finite/range checks;
- reject values outside representable source-time bounds before float-to-integer conversion;
- define a maximum mapping error in microseconds or frames;
- test reverse/freeze/curve boundaries and long-duration clips;
- use checked rational arithmetic for all linear segments and boundaries.

### P1 — frame-index derivation in export has a floating-point path

The adjustment-layer frame index in `finalizeFrame` is calculated from `frame.timestamp.toDouble() * fps.value.toDouble() + 0.5`. Frame timestamps are already produced by a rational frame clock, so this conversion is avoidable. Use a checked frame-index conversion based on the plan's frame rate and frame timestamp, or carry the authoritative frame index into finalization.

Other effect functions accept floating-point seconds by API design; these are rendering parameters, not replacements for authoritative timeline/frame identity. Their input conversion still needs finite/range checks.

### P1 — preview cache key uses double-to-int conversion

`PreviewRenderer::renderAt` converts the Rational time to a double and then to signed microseconds. This can lose exactness for sufficiently large values and makes cache identity depend on floating-point rounding. Replace it with checked `toMicroseconds` and propagate failure rather than generating a potentially colliding/invalid key.

### P1 — C ABI serialization fans out legacy conversion calls

`core/src/capi.cpp` serializes many time values using `toMicroseconds` (project duration, assets, clips, source ranges, transitions, keyframes, subtitles, audio fades, analysis reports, thumbnails, markers, chapters, and template durations). The C ABI currently exposes integer outputs without a checked conversion result at each site. Migration must define one consistent policy:
- return an explicit C API error and leave outputs in documented safe defaults when conversion fails; or
- provide checked variants while preserving legacy ABI wrappers with explicit fail-closed behavior.

Do not independently cast through double in selected endpoints.

### P2 — preview warm-up and analysis APIs still contain double conversions

The C API warm-up path derives timestamps from `double` seconds and converts to microseconds. Analysis and media-segmentation paths also accept/emit seconds as doubles. These can remain approximate external interchange formats only if their domain/range/rounding rules are documented and validated at the boundary.

## Required checked migration sequence

1. Introduce the previously proposed `CheckedRational` and explicit error enum without changing the legacy Rational ABI.
2. Add checked arithmetic and checked `timeAtFrame`, `frameAtTime`, and `toMicroseconds` APIs.
3. Add checked sequence duration and clip/source/transition interval construction.
4. Implement `compileExportPlanChecked` using a single immutable project snapshot. It must return an error, not a partially valid plan, on any arithmetic/validation failure.
5. Make the export entrypoint obtain the snapshot, compile the checked plan, validate all output dimensions/frame rate/intervals, and only then create/open the writer.
6. Carry the authoritative integer frame index through export finalization rather than recomputing it through double.
7. Migrate preview cache-key conversion to checked integer microseconds; define how invalid/out-of-range preview requests are reported to JNI.
8. Migrate C ABI time serialization using checked conversion and documented error/output semantics.
9. Audit audio PTS generation, resampling block timestamps, clip source-time mapping, and A/V end alignment separately. Do not claim the time migration complete until those paths have explicit evidence.
10. Keep nonlinear speed-curve integration as a separately specified approximation with range/error tests; use checked arithmetic around its input/output boundaries.

## Required test matrix

- Rational boundary arithmetic: INT64_MIN/MAX, denominator sign normalization, zero denominator, division by zero, and sticky-flag independence.
- Clip end/source-out overflow, sequence duration overflow, compound offsets, transition end overflow.
- Export plan rejection before writer factory creation/open and before output file creation.
- Stable snapshot consistency: mutate the editor after snapshot capture and prove the plan and execution data remain on the same revision.
- Frame rates: 24, 25, 30, 30000/1001, 60, plus invalid and extreme rates.
- Long duration and frame-index boundaries, including final frame just below sequence duration.
- Speed curve: monotonic and non-monotonic profiles, reverse, freeze, zero/negative/non-finite values, and maximum allowed mapping error.
- Preview: exact microsecond cache-key identity, large timestamps, invalid requests, cache collision regression.
- Audio: sample-rate conversion, block PTS continuity, gap/overlap, speed changes, and final A/V duration agreement.
- C ABI: every time-bearing output function must have checked failure-path tests and stable ABI behavior.

## Gate status

S3-1 remains not accepted. This audit does not change A-12 or any other approval/dependency gate and does not authorize edits to protected Rational/time/export implementation. It adds production call-path evidence and a concrete migration/test sequence only.
