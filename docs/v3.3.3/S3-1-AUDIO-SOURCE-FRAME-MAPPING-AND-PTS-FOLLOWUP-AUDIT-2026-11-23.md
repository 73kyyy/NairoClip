# S3-1 Audio Source-Frame Mapping, Tail PTS and S5 Native UI Follow-up Audit

Date: 2026-11-23  
Repository: `73kyyy/NairoClip`  
Reviewed ref: `76c33399174547b5ccb8f96674b7fb6c2a12a5aa`  
Scope: read-only source audit and remediation/test design. No runtime source, tests, Activity, or Manifest files were changed.

## 1. Executive result

This pass confirms three independent audio correctness gaps and one native UI integration boundary:

1. Audio mixing seeks to a speed-adjusted source timestamp at each output block, but advances through the returned source block at 1:1 output-frame indexing (apart from sample-rate conversion). Constant-speed audio therefore does not follow the clip's playback speed.
2. Reverse audio starts from a reverse-adjusted seek timestamp but still reads source samples in increasing index order. It is not reverse playback.
3. AudioMixer emits a full 1024-frame block for the final partial interval. The current one-second regression test explicitly accepts 47 × 1024 = 48,128 frames at 48 kHz, 128 frames (2.667 ms) beyond the one-second sequence.
4. The no-mixer silence fallback also emits fixed 1024-frame blocks without trimming the final block, and its cursor logic jumps over gaps instead of emitting silence for them.
5. The native toolbar's level-2 row is currently expanded inline immediately after its level-1 item in the same horizontally scrollable Row. The toggle/cross-tool replacement behavior matches the requested interaction model at source level. However, the native editor still has fixed vertical timeline sizing and is not installed as the app runtime host.

These findings do not change S1-3, S3-1, A-12, A-5, A-6, S1-8D, or any other registered approval gate.

## 2. Audio source-frame mapping

### 2.1 Current implementation

In `core/src/audio_mixer.cpp`, `prepare()` copies `RenderClip.speed` and `RenderClip.reversed` into `TrackedClip`, but does not carry `sourceOut` or `speedCurve`.

In `mixInto()`:

- The overlap's initial source timestamp is calculated from timeline-local time and constant `clip.speed` (or the reverse expression).
- The reader is seeked to that timestamp for the current overlap.
- The sample loop then starts with `srcIndex = i`; it only scales this index when source and output sample rates differ.
- There is no per-output-sample multiplication by playback speed, no fractional source-frame interpolation, and no descending source index for reverse playback.

Consequences at equal source/output sample rates:

| Playback mode | Required source-frame advance per output frame | Current sample-loop advance |
|---|---:|---:|
| 0.5× | 0.5 (fractional interpolation required) | 1 |
| 1× | 1 | 1 |
| 2× | 2 | 1 |
| Reverse 1× | -1 | +1 |

The block-start seek does not repair the intra-block mapping. At 2×, the block reads only about half the intended source-time span; at 0.5×, it advances too quickly through source samples. Reverse playback reads forward from the reverse-start position.

### 2.2 Contract gap at the reader boundary

`MediaReader::seek(sourceTime)` is documented as seeking to a nearby keyframe or exact position depending on the implementation. `readAudioBlock()` is documented only as reading the next audio block. The interface does not specify:

- whether the first returned sample is exactly at the requested source timestamp;
- whether a decoder may return preroll samples before the requested timestamp;
- how `AudioBlock.start` relates to the first sample after seek;
- whether the block can contain samples before or after a requested source interval;
- how discontinuities, resampler delay, and end-of-stream are represented.

The Mock reader resets its audio block counter on seek and sets the next block's `start` to the seek base, but this mock behavior does not prove the production decoder's seek contract.

### 2.3 Required design before implementation

Define one authoritative source-frame mapping contract shared by preview and export:

- Use the clip's half-open source interval `[sourceIn, sourceOut)`.
- Carry source bounds, constant speed, reverse state, and speed-curve snapshot into the audio render clip.
- Define timeline-frame → source-frame mapping with explicit rational/timebase conversion and a documented rounding/interpolation policy.
- For forward playback, use fractional source positions and a real resampler/interpolator; for reverse playback, read/interpolate in descending source order.
- Do not assume a single decoded block covers the required source interval. Maintain a bounded source window or decode enough source frames for the mapped output interval, including resampler filter taps.
- Define seek/preroll trimming from `AudioBlock.start` and sample rate. Reject malformed blocks (invalid rate/channels or sample vector not divisible by channel count).
- For speed curves, use the same source-time integration/mapping as video rather than an independent approximation.
- Preserve denoise stream semantics only after deciding whether denoise runs in source-time or timeline-time. Reverse/variable-speed denoise must not silently use the current forward-only sequential stream path.

## 3. Exact output duration and final audio block

### 3.1 AudioMixer tail

`AudioMixer::readBlock()` allocates exactly `blockFrames_ * channels` samples (currently 1024 frames), calls `mixInto()`, and advances the cursor by the full 1024-frame duration whenever the cursor is merely less than sequence duration.

The test `AudioMixer.ReadBlocksCoverDuration` in `tests/test_audio_mixer.cpp` expects 47 blocks for a one-second, 48 kHz sequence and asserts every block contains 1024 frames. This is 48,128 output frames, not 48,000. The test therefore codifies an overrun rather than exact coverage.

Required behavior:

- `validFrames = min(blockFrames, ceil_or_checked_frame_count(duration - cursor))` under one explicitly selected sample-boundary policy.
- Allocate/write only `validFrames * channels` samples for the last block.
- Ensure every block's start and sample count describe a contiguous, non-overlapping interval.
- Define whether duration-to-sample conversion floors, ceils, or uses a checked rational boundary; use the same policy in audio and video export.
- Add tests for durations below one block, exact block multiples, one sample beyond a block, 1 second at 44.1/48 kHz, fractional frame durations, and non-zero seeks.

### 3.2 Silence fallback

In `core/src/export_executor.cpp`, the fallback path creates fixed 1024-frame silent blocks while `cursor < clip.timelineEnd`. It does not trim the last block to the clip/sequence end. It also moves `cursor` forward to a later clip's start without emitting a silence block for the intervening gap.

Required behavior:

- Generate silence over the complete output interval, including leading, inter-clip, and trailing gaps when silence output is required.
- Clip every emitted block to the exact remaining sequence duration.
- Merge or order intervals deterministically; do not let overlapping clip intervals duplicate silent output time.
- Add tests for a leading gap, an internal gap, a trailing gap, adjacent clips, overlapping clips, empty audio track, muted tracks, and a sequence ending mid-block.

## 4. Writer/PTS boundary

`OutputAudioBlock` carries `start`, `sampleRate`, `channels`, and interleaved samples. `MediaWriter::writeAudioBlock()` accepts the block, but the abstract interface does not document the required PTS timebase, monotonicity, gap policy, or whether implementations must preserve the exact sample count. The core Mock writer validates only that samples are non-empty and increments a block counter; it does not verify timestamps, sample counts, duration, or monotonic continuity.

Before changing writer code, audit each concrete platform writer and define:

- PTS = exact block start converted into the encoder's declared audio timebase;
- duration = exact number of frames in the block;
- monotonic PTS and no accidental overlap or gap;
- explicit resampler/encoder delay and flush behavior;
- sample-rate/channel conversion ownership (core mixer versus platform writer);
- exact final packet/encoder flush behavior and container duration.

Add a timestamp-aware test writer that records every block's start, sample count, sample rate and channels, then asserts contiguous exact coverage. Add encoded-file probes for duration, audio stream timebase, start_time, packet PTS/DTS, and A/V end alignment.

## 5. Native editor integration and responsive behavior

### 5.1 Runtime host remains WebView

At this reviewed ref:

- `android/app/src/main/java/com/nairoclip/app/MainActivity.kt` still creates a WebView, installs `NCEngine` / auth / preview bridges, loads `pages/01-home.html`, and layers a `PreviewSurfaceView` beneath the WebView.
- `mobile/native/android/NativeAppShell.kt` contains only the package declaration and a comment; it does not host Compose navigation or app-level state.
- `NativeEditorScreen` and its timeline are therefore native component foundations, not the active application runtime.

No Activity or Manifest change is authorized by this audit.

### 5.2 Responsive gaps remain

The native editor still assigns the timeline panel a fixed `218.dp` height. `NativeTimelineView` uses fixed density-based geometry for ruler height (27dp), track label gutter (120dp), lane height (47dp), row gap (4dp), and small fixed text sizes. These values are not yet derived from a shared responsive metrics/window-size policy.

The previously committed responsive specification remains the required target. The next implementation/test slice should:

- introduce shared design tokens and a window metrics policy, including scaled density for Canvas text;
- calculate editor preview/timeline/panel allocation from available height and window class rather than fixed 218dp;
- use shared geometry for drawing and hit-testing;
- handle compact width/height, font scale, orientation, split-screen, fold/unfold, system insets and IME;
- preserve minimum touch targets and prevent circular controls from stretching;
- use UI tests with geometry assertions plus screenshot regression at representative aspect ratios and font scales.

### 5.3 Level-2 toolbar behavior

The current `NativeToolBar` inserts the selected tool's level-2 items immediately after that level-1 item inside the same horizontally scrollable `Row`. The parent selection callback toggles the current expansion closed, replaces it when another level-1 tool is selected, and clears the old subtool context. This is structurally aligned with the requested horizontal expansion model; it is not a drawer.

Still required: Compose UI tests proving the expansion order, second-tap collapse, switching tools, narrow-width scrolling/visibility of the expanded items, and no accidental vertical panel/layout overlap.

## 6. Test matrix to add after approval

### Audio mapping
- 0.5× / 1× / 2× sine-wave frequency and duration checks.
- Reverse playback with a ramp/impulse fixture proving descending sample order.
- Source/output rates: 44.1, 48, 96 kHz; mono/stereo/multichannel.
- Source interval bounds and last-sample behavior at exact clip end.
- Variable speed curves, freeze, denoise on/off, and reverse/denoise policy.
- Reader seek preroll and irregular block sizes (including a requested range spanning multiple blocks).

### Exact PTS / duration
- 1-frame, 1023/1024/1025-frame, one-second and fractional-duration sequences.
- Leading/internal/trailing silence and clip gaps/overlaps.
- Monotonic block start, exact sample counts, no overlap, no missing samples.
- Encoded output probe for audio duration and A/V end alignment.

### Native UI
- Window sizes: compact/medium/expanded; portrait/landscape; split-screen/foldable.
- Font scales 1.0/1.3/1.5/2.0; IME open/closed; gesture and three-button navigation insets.
- Timeline drawing and hit-test geometry parity.
- Level-2 toolbar inline expansion, collapse, switch, and narrow-window behavior.
- Surface create/resize/detach/reattach stress on physical devices.

## 7. Gate and validation status

- This document is a static source audit and remediation/test design only.
- No production audio code, test code, native UI code, Activity, or Manifest was modified.
- S1-3 remains blocked by the absence of a durable Android RecoveryStore and real process-death/restart recovery evidence.
- S3-1 remains not accepted until checked time handling and production call-path rejection are implemented and tested.
- A-12 and the registered S5 sequencing gates remain unchanged.
- PR #32 remains Draft/open/unmerged. CI success does not authorize runtime integration or entrypoint changes.
- CI and Android validation workflow results are reported separately from physical-device validation; no real-device validation is claimed here.


## 4.1 Concrete FFmpeg writer finding: block.start is ignored

The concrete core writer is `core/src/media_ffmpeg_writer.cpp`. Its `writeAudioBlock()` implementation resamples each incoming block, appends converted samples to an internal FIFO, and assigns encoded frame PTS from a monotonically increasing `audioPts` sample counter. It does not read `block.start` when assigning PTS.

Consequences:

- A block whose declared start is later than the preceding block is still appended immediately after the previous block. Timeline gaps are compressed unless the caller supplies explicit silence samples.
- A block with a discontinuous or overlapping start is also appended contiguously; the writer cannot preserve or detect the declared timeline discontinuity.
- Correctness currently depends on the upstream producer supplying a perfectly contiguous stream starting at zero. The writer interface does not state or validate this precondition.
- The existing `MockMediaWriter` cannot catch this because it checks only that a block is non-empty.

The writer's `finish()` path also pads any remaining FIFO samples with zeros to a full encoder frame and advances `audioPts` by the full frame size. The reviewed code does not explicitly attach a packet skip-samples/discard-padding instruction for the padded tail. The actual muxed duration and decoder-visible tail therefore require an encoded-file regression probe; do not assume the logical sample duration is preserved merely because the input block count is correct.

Required writer decision:

1. Either make `OutputAudioBlock.start` authoritative and explicitly handle gaps/overlaps/discontinuities in the encoder timebase, including FIFO flush/restart rules; or formally require a contiguous stream and validate every block start against the expected next sample timestamp.
2. If using a contiguous stream contract, the producer must synthesize exact silence for every gap and must never overrun sequence duration.
3. Preserve the logical valid sample count through encoder flush. Where the codec requires a padded final frame, write correct skip/discard padding metadata or use a muxing strategy that preserves the intended presentation duration.
4. Add tests with a block starting at 500 ms after an initial 100 ms block, a deliberate overlap, a one-sample gap, and a non-frame-aligned tail. Inspect both encoded packet PTS and decoded output duration/sample count.

This concrete writer finding strengthens the earlier interface-contract concern: the current FFmpeg implementation is sample-counter-driven, not timestamp-driven.
