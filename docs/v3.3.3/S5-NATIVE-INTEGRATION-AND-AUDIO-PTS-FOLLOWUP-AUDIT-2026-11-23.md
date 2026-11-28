# S5 Native UI Integration + Audio PTS Follow-up Audit — 2026-11-23

Target: `73kyyy/NairoClip`
PR: #32 — Draft / open / not merged
Audited HEAD: `cd839a580d9bab25fa4101a15524d4bba05223be`
Scope: static source inspection only. No runtime source or test code changed in this audit.

## 1. Current validation status

At the audited HEAD, GitHub Actions reports both workflows successful:
- S5 Android Validation: Host CMake/targeted CTest, Android Kotlin compile/JVM unit tests, and Android NDK arm64-v8a core cross-compile.
- CI: build-and-test, UBSan, and ASan.

These results establish compilation and the configured host/JVM test suites only. They do not establish Compose UI behavior, screenshot/visual regression, Android instrumented behavior, device-specific Surface lifecycle correctness, or physical-device acceptance.

## 2. Native UI: components exist, but the native application host is still absent

### 2.1 Launcher remains WebView-based

Evidence:
- `android/app/src/main/java/com/nairoclip/app/MainActivity.kt` imports and constructs `WebView`, configures `WebViewAssetLoader`, loads `/assets/pages/01-home.html`, and calls `setContentView(root)` with a FrameLayout containing the preview Surface and WebView.
- `mobile/native/android/NativeAppShell.kt` is only a package declaration and comment (106 bytes); it does not define a Compose root, navigation host, or application state owner.
- MainActivity has no `setContent { ... }` call and does not instantiate `NativeEditorScreen`.

Conclusion: native Compose components are not yet the active app shell. This is consistent with the existing A-5/A-6/S1-8D constraints; this audit does not authorize changing the launcher, Manifest, or removing WebView.

### 2.2 Editor state contract is useful but not hosted

`NativeEditorHostState.kt` and `NativeEditorHostReducer` provide:
- immutable timeline/history/selection/playhead state;
- snapshot-loaded, mutation-committed, failure and selection events;
- normalization of selection against visible/enabled clips;
- clamping playhead to timeline duration;
- preservation of the last good timeline when refresh fails.

However, this is a pure reducer contract. No Activity/ViewModel/application host currently owns it, dispatches I/O, observes lifecycle, or feeds the state into the Compose editor. It is therefore not an end-to-end native editor state loop.

### 2.3 Editor command adapter is materially implemented, but UI-to-engine ownership remains incomplete

`NativeEditorCommandAdapter.kt` exposes real bridge operations for timeline snapshots, track visibility/mute, subtitles, text clips, speed, speed curves, clip audio/denoise, effects, filters/LUT/color grading, masks, transitions, keyframes, clip mutations, undo/redo and history.

The adapter serializes mutations and authoritative state readback under the bridge state lock. This is a sound foundation and avoids UI-side optimistic guesses.

The missing piece is the app-level host that:
1. owns the editor handle and adapter lifetime;
2. maps selected UI targets to adapter targets;
3. dispatches operations off the UI thread where appropriate;
4. publishes the returned authoritative snapshot and history together;
5. handles project switching, errors, lifecycle teardown and restoration;
6. connects every Compose callback to the corresponding adapter operation.

A component callback signature or adapter method is not evidence that the production UI invokes it.

### 2.4 Automatic subtitles: worker/coordinator logic exists, UI host integration is not established

`NativeAutoSubtitleCoordinator.kt` has a real state machine:
- requires a selected media URI plus validated `NativeSpeechSourceRange`;
- refuses to silently use the legacy timeline-offset-only overload;
- waits for the speech session to become ready;
- runs decode/transcribe/insert asynchronously;
- publishes state changes on the main thread and drops stale versions;
- exposes cancellation and completion/error state.

`NativeAutoSubtitlePanel.kt` is presentational and correctly does not fabricate recognition results. It only invokes optional `onStart` / `onCancel` callbacks supplied by a host.

But MainActivity still hosts the WebView and NativeAppShell is empty. No active native host has been demonstrated that constructs the coordinator, supplies the selected clip URI/source range, loads the model, collects state, invokes panel callbacks, and disposes the worker/session with lifecycle. Therefore this is a functioning lower-level workflow, not yet a verified production native-page integration.

### 2.5 Preview Surface controller

`NativePreviewSurfaceController.kt`:
- serializes native render work on a dedicated executor;
- coalesces queued scrub requests to the newest timestamp;
- invalidates stale work across Surface generation changes;
- posts valid completions to the main thread;
- detaches and clears listeners on dispose.

This is a good concurrency foundation. It still requires host-level ownership and device tests for rotation, Surface replacement, detach/reattach, background/foreground, Activity recreation, and teardown while JNI is in flight. No physical-device acceptance is claimed.

## 3. Audio timeline / PTS findings

### 3.1 Final audio block currently exceeds the exact sequence duration

Evidence:
- `core/src/audio_mixer.cpp::AudioMixer::readBlock` returns while `cursor_ < duration_`, always allocates `blockFrames_ * channels` samples (default 1024 frames), then advances the cursor by the full 1024-frame duration.
- `tests/test_audio_mixer.cpp::ReadBlocksCoverDuration` uses a 1-second clip at 48 kHz, asserts 47 blocks, and asserts every block contains 1024 frames.
- 48,000 frames / 1,024 frames per block = 46.875 blocks. The current 47 full blocks represent 48,128 frames, exceeding one second by 128 frames (approximately 2.667 ms).
- `export_executor.cpp` passes each complete `OutputAudioBlock` to the writer; `export_runner.cpp` calls `writer->writeAudioBlock(block)` without trimming the final block to the plan duration.

Impact: exported audio can extend past the exact sequence endpoint by up to 1,023 samples per channel (about 21.31 ms at 48 kHz), depending on the duration remainder. The current unit test codifies full-block overrun instead of asserting exact sample-frame coverage.

Required correction design:
- calculate the remaining output frames from `duration_ - cursor_` using checked time/sample conversion;
- allocate and mix only `min(blockFrames_, remainingFrames)` frames for the final block;
- preserve `out.start` as the exact first-sample PTS;
- apply the same exact-tail rule to the no-input/silence fallback in `export_executor.cpp`;
- replace the existing test expectation with exact total frame count, exact final block size, and end-PTS assertions for durations both divisible and non-divisible by 1024 frames.

This is an S3-1 time/PTS production-path concern. No implementation change is made here pending the applicable approval gate.

### 3.2 Audio speed-curve mapping is not aligned with video speed-curve mapping

Evidence:
- `RenderClip` in `core/include/nairoclip/core/export_plan.h` carries a `SpeedCurve speedCurve` snapshot.
- Video export's `mapSourceTime()` in `core/src/export_executor.cpp` explicitly integrates `clip.speedCurve` when non-empty (currently using a fixed 64-step double-precision integration).
- `AudioMixer::TrackedClip` in `core/include/nairoclip/core/audio_mixer.h` carries only `speed` and `reversed`; it has no speed-curve snapshot.
- `AudioMixer::prepare()` copies `renderClip.speed` and `renderClip.reversed`, but not `renderClip.speedCurve`.
- `AudioMixer::mixInto()` maps source time using only constant `clip.speed` (or reverse constant-speed mapping).

Impact: when an audio-bearing clip has a non-empty speed curve, the video and audio paths do not use the same source-time mapping. Depending on the curve, audio duration/content can drift relative to the video. This is separate from the fixed-block tail issue.

Required correction design:
- define one deterministic source-time mapping contract shared by video and audio, including reverse/freeze/curve behavior and boundary semantics;
- carry the compiled curve into the audio tracked-clip representation;
- avoid independently reimplementing curve integration in the two paths;
- define bounded numerical error or use checked rational/fixed-point integration where feasible;
- test constant speed, variable speed, reverse, clip boundaries, long clips, and audio/video source-time agreement at shared timeline sample points.

This finding is not an assertion that every audio clip currently drifts; it is a concrete conditional mismatch for clips with active speed curves.

### 3.3 Audio block timestamps are timeline-start timestamps, but exact end/sample-count contract is underspecified

`OutputAudioBlock.start` is populated from `AudioMixer::cursor_` and forwarded to the writer. This gives each block a timeline start time. The output contract does not itself state that the final block must be truncated at sequence end, and the current test expects padded full blocks.

The writer-facing contract should explicitly define:
- sample rate and channel layout;
- start PTS as the time of sample frame zero;
- exact sample-frame count per block;
- permitted rounding when timeline duration is not an integer sample count;
- whether blocks may extend past sequence duration (recommended: no);
- final audio end-time tolerance and A/V end alignment.

## 4. Work still blocked / not changed

- S1-3 persistence remains unaccepted; S3-1 Rational checked-time production path remains unaccepted.
- A-12 and other previously recorded approval gates remain unchanged.
- No Manifest/Launcher switch, no WebView removal, and no protected persistence/time implementation changes were made.
- No Compose UI tests, screenshot tests, instrumented tests, or physical-device tests were added or run in this audit.
- No runtime source or test code was modified; this commit adds evidence/design documentation only.

## 5. Recommended next implementation sequence (subject to gates)

1. Complete the S3-1 checked-time API and export-plan validation approval path before changing time-critical production code.
2. Correct AudioMixer exact-tail sample count and silence fallback, with tests that assert exact sample totals and PTS.
3. Unify video/audio speed-curve source-time mapping and add cross-path synchronization tests.
4. Add a real native application host/ViewModel and navigation/state ownership without switching Launcher until A-5/S1-8D approvals permit it.
5. Connect every native editor and subtitle callback to the command adapter/coordinator, then verify each operation through engine readback.
6. Add Compose UI geometry/interaction tests, screenshot regression, and physical-device Surface/IME/window-size acceptance.

