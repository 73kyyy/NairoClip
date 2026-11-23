# Audio Sample Mapping and Native Callback Contract Audit — 2026-11-23

Target: `73kyyy/NairoClip`
PR #32: Draft / open / not merged
Audited HEAD: `51db0673beae030447a2043915745f6e2638c697`
Scope: static source and existing-test inspection. No runtime code or tests changed.

## 1. New audio source-sample mapping defects

### 1.1 Constant-speed audio is not sampled at the clip's playback rate

Evidence:
- `AudioMixer::prepare()` copies `RenderClip.speed` into `TrackedClip.speed`.
- `AudioMixer::mixInto()` computes the starting source timestamp using `clip.speed`:
  `sourceIn + localTime * speed` (or the reverse start formula).
- After seeking once to that timestamp, the sample loop computes `srcIndex = i` when source/output sample rates match. It does not multiply the per-output-frame source index by `clip.speed`.
- The only index scaling currently present is for a difference between source and output sample rates. That conversion is independent of clip playback speed.
- Existing `tests/test_audio_mixer.cpp` covers duration, fades, gain, seek rejection, and denoise block-size consistency; it contains no actual 2x/0.5x audio source progression or audio/video speed-sync test.

Impact: for a clip with speed other than 1x, the seek start is shifted according to speed, but subsequent samples are consumed at approximately 1x source rate. The audio content/time mapping therefore does not follow the video clip's playback rate. The precise audible symptom depends on reader block length and the clip's source range; this is a source-level mismatch, not merely a theoretical test gap.

Required correction:
- define the source-frame coordinate for every output frame from the same canonical timeline-to-source mapping used by video;
- resample/interpolate at fractional source-frame positions, rather than integer `i` alone;
- ensure source block acquisition spans the full mapped source-frame interval, not merely one default decoded block;
- test 0.5x, 1x, 2x, non-integer speeds, multiple source sample rates, clip boundaries, and audio/video source-time agreement.

### 1.2 Reverse audio currently seeks to the reverse start but reads forward

Evidence:
- `TrackedClip.reversed` is copied from `RenderClip.reversed`.
- `mixInto()` computes the reverse source start as `sourceIn + (timelineDuration - local) * speed`.
- It then calls `reader->seek(sourceTime)` and uses increasing `srcIndex = i` for the output loop.
- No descending source-frame index or reverse-buffer operation is present in this loop.
- The denoise path explicitly excludes reversed clips, but the ordinary reader path still reads forward from the selected seek point.

Impact: the code does not implement reverse sample traversal. It starts near the reverse-positioned source timestamp but advances forward through the decoded block. This cannot produce correct reverse audio playback.

Required correction:
- establish the exact reverse interval convention (including whether `sourceIn` denotes the low endpoint or first forward sample);
- decode/cache a source window covering the mapped interval;
- traverse/interpolate source samples in descending order;
- test impulse markers and monotonic ramps so direction can be asserted deterministically, plus reverse+speed and clip-boundary cases.

### 1.3 Speed curves are absent from the audio tracked-clip model

The prior follow-up already established that `RenderClip.speedCurve` is not copied into `AudioMixer::TrackedClip`, while video `mapSourceTime()` integrates the curve. This is a separate issue from constant-speed sample indexing and reverse traversal. All three must be corrected under one shared source-time mapping contract.

## 2. Audio output end-boundary defects (expanded)

### 2.1 Normal mixer output uses full blocks past the sequence end

`AudioMixer::readBlock()` checks only `cursor_ < duration_`, always allocates 1024 frames, mixes the full block, and advances by the full block duration. The existing 1-second/48-kHz test explicitly expects 47 full blocks (48,128 frames rather than 48,000).

### 2.2 Silence fallback is also not bounded by the exact sequence end

In `export_executor.cpp`, when the mixer is not ready but an audio track exists, the fallback loops each clip and emits a full 1024-frame silent block while `cursor < clip.timelineEnd`. It advances by 1024 frames without truncating the final block to the clip end or plan duration.

Additional consequences:
- the final silence block can extend past the clip endpoint;
- the loop is clip-driven rather than sequence-duration-driven;
- gaps are only partially represented by moving the cursor to the next clip start, and overlapping/adjacent clips may produce block boundaries unrelated to the canonical output sample grid;
- no explicit test asserts exact silent-frame coverage or end PTS.

Required correction:
- drive silence generation from one monotonic sequence output cursor;
- use checked conversion from remaining timeline duration to sample frames;
- emit at most the remaining number of frames and preserve exact sample-grid continuity;
- avoid duplicate/overlapping silence blocks when clips overlap;
- test gaps, overlaps, adjacent clips, empty/muted tracks, sub-block clips, and non-integral frame durations.

## 3. Native editor callback contract review

### 3.1 NativeEditorScreen is deliberately a presentation component

The screen accepts a large set of callbacks and state snapshots. Many command callbacks are nullable and the UI intentionally displays a “waiting for host” state or disables an operation when the callback is absent. This is safer than pretending a command succeeded.

The timeline view exposes callbacks for:
- playhead seek;
- clip selection and selection clearing;
- track visibility and mute.

The panel layer also exposes callbacks for clip mutation, speed, audio, denoise, effects, transitions, text, subtitles, keyframes and animation.

### 3.2 A real host is still needed to make these callbacks a working editor

The current tree still has:
- an empty `NativeAppShell.kt`;
- a WebView-based `MainActivity` loading the legacy home page;
- no app-level Compose root/Navigation host shown in the launcher;
- no demonstrated ViewModel/lifecycle owner that supplies coherent state and all command callbacks to `NativeEditorScreen`.

Therefore the callback APIs and command adapter should be treated as integration surfaces, not proof that the native editor is fully connected. The next safe native work is to specify and test a host contract without changing Launcher/Manifest or deleting the legacy path before A-5/A-6/S1-8D approval.

### 3.3 Automatic subtitles remain host-dependent

The panel disables start unless a non-null `onStart` is supplied and state is available. The coordinator requires a selected URI, validated source range, and ready speech session. A host must connect those prerequisites and own coordinator cleanup. No UI-only change can substitute for this ownership.

## 4. Approval boundaries and validation

- S1-3 persistence and S3-1 checked-time production-path acceptance remain unresolved.
- A-12 and other recorded approval gates remain unchanged.
- Do not change protected time/persistence production code until the applicable gates authorize it.
- Do not switch the Launcher Activity/Manifest or remove WebView fallback.
- No runtime source or test code was modified in this audit.
- The current CI run for HEAD `51db0673` was still in progress at the time of this inspection; do not describe it as passed until the run concludes.
- Existing host/NDK/JVM CI does not establish Compose visual regression or physical-device acceptance.

## 5. Ordered remediation proposal (subject to gates)

1. Complete checked time API and explicit overflow rejection before production timeline arithmetic changes.
2. Introduce one shared deterministic timeline-to-source-time/source-frame mapping contract.
3. Implement speed-aware fractional audio resampling and reverse sample traversal.
4. Carry and apply speed curves consistently in audio and video.
5. Fix exact final-block trimming in both AudioMixer and silence fallback.
6. Add deterministic audio mapping tests (ramp/impulse fixtures), exact sample-count/PTS tests, and A/V sync tests.
7. Build the native app host/ViewModel and wire each callback to adapter + authoritative snapshot readback only after the host/API contract and approval prerequisites are satisfied.
