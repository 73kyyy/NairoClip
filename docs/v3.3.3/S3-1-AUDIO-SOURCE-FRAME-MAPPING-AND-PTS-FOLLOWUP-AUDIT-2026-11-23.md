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


## 8. Concrete FFmpeg writer finding: block.start is ignored

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



## 9. FFmpeg finish/flush error propagation and AAC tail validity

A further review of `FFmpegMediaWriter::finish()` identifies a separate reliability issue beyond the logical tail-padding concern:

- The video encoder flush call `avcodec_send_frame(videoCodec, nullptr)` is not checked.
- Video packet receive errors other than EAGAIN/EOF simply break the loop; packet rescale/write return values are not checked in this flush path.
- The audio tail frame send result `avcodec_send_frame(audioCodec, audioFrame)` is ignored.
- The audio encoder flush send result `avcodec_send_frame(audioCodec, nullptr)` is ignored.
- Audio packet receive errors other than EAGAIN/EOF simply break; `av_interleaved_write_frame` results are ignored in this flush path.
- The final trailer write is checked, but successful trailer writing cannot prove that all encoder packets were drained and written successfully.

This can make `finish()` report success after an encoder-send, packet-receive, or mux-write failure during flush. The caller then has no reliable distinction between a complete export and a truncated/corrupt tail.

### AAC tail sample accounting

The FIFO tail contains `remaining` logical samples per channel, but the code copies `tailSamples` and sets `audioFrame->nb_samples = frameSize`, zero-padding the rest. It then advances `audioPts` by the full encoder frame size. The code shown here does not explicitly set packet skip-samples/discard-padding metadata or otherwise carry the logical tail sample count into the muxer.

This does not prove that every output container will expose the padded samples as audible duration: codec/container behavior and FFmpeg's encoder side data may affect the final presentation. It does prove that the writer itself does not visibly preserve the logical tail count as an explicit contract. The actual result must be established by decoding the output and measuring decoded sample count and stream/container duration for non-frame-aligned tails.

### Required correction design

- Treat every send, receive, rescale/mux write, and trailer operation as a checked operation. Preserve the first failure and return a failed `Result`; do not silently break and return success.
- Drain encoders until the documented EOF condition. Distinguish EAGAIN from EOF and unexpected errors.
- Preserve the logical number of valid tail samples separately from the padded encoder frame size.
- Use codec-supported trailing-padding/skip-samples metadata or a container-supported duration mechanism where appropriate; verify behavior per supported output container and codec.
- Do not advance logical audio duration by padded samples. Encoder PTS progression and presentation duration must be separate concepts.
- Ensure cleanup/abort behavior is deterministic after a flush failure, and do not leave a writer appearing successfully finished.
- Add fault-injection tests for failed send, receive, packet mux write, and trailer write.
- Add encoded-file tests for AAC tails with 1, 2, 1023, 1024, 1025 and frameSize-1 remaining samples, at 44.1 kHz and 48 kHz; decode the output and assert decoded presentation sample count, packet PTS/duration, stream duration and A/V end alignment within a declared codec-delay tolerance.

## 10. Existing FFmpeg writer test coverage is insufficient for exact duration

The current `FFmpegEncodeWithAudio` test feeds 43 blocks × 1024 = 44,032 samples at 44.1 kHz (approximately 0.99846 s), then only checks that the reopened media duration exceeds 0.5 s. It does not assert exact audio sample count, decoded duration, packet PTS continuity, or A/V end alignment.

The upsampling regression writes 40 × 1024 input frames at 44.1 kHz and validates successful writes, but the shown test does not assert the decoded sample count, per-channel signal identity, stream duration, or exact output tail. The comments describe distinct channel frequencies as useful for detecting channel swaps, but the test needs actual decoded-channel assertions to prove that property.

The test suite therefore catches important memory-safety regressions, but does not currently establish the exact PTS/tail guarantees required for production export.


## 11. Resampler drain and input-block contract (latest HEAD 708b8377)

A further pass over the concrete FFmpegMediaWriter confirms that finish() drains the codec FIFO and encoder, but does not visibly drain the SwrContext itself:

- writeAudioBlock() creates one SwrContext lazily from the first block's input channel layout and sample rate.
- Each call uses swr_get_delay() to size that call's output buffer and calls swr_convert() with the current block as input.
- finish() begins by processing the already-populated audioFifo; there is no swr_convert(..., nullptr, 0) loop to retrieve samples still buffered inside the resampler.
- Therefore, samples delayed by the resampling filter at end-of-input are not explicitly transferred into audioFifo before encoder flushing. The exact lost-tail amount depends on the resampler configuration and must be measured, but the absence of an explicit drain is a source-level defect in the end-of-stream contract.

The writer also creates the resampler only once, using the first block's sample rate and channel layout. Later calls pass each block's sampleRate to swr_get_delay() and swr_convert() but do not reconfigure the already-initialized resampler. A stream that changes input sample rate or channel count mid-export is therefore not safely supported and may be interpreted with inconsistent parameters.

Input validation is incomplete before calculations:
- block.channels is used as a divisor when computing inSamples, without first rejecting zero or negative channel counts.
- block.sampleRate is not validated as positive before it is passed into resampler setup and delay calculations.
- block.samples.size() is divided by the channel count without verifying exact divisibility; malformed interleaved data can silently discard a partial trailing frame.
- The conversion to int inSamples is not guarded against values above INT_MAX.
- The writer does not verify that channel count/layout remains consistent with the initialized resampler and encoder.

### Required correction and tests

1. Validate sampleRate > 0, channels within the supported channel range, non-empty sample vector frame alignment, and safe size_t-to-int conversion before touching the resampler.
2. Freeze the input sample-rate/channel-layout contract for the lifetime of a writer, or explicitly flush/recreate/reconfigure the resampler at a well-defined discontinuity boundary. Never pass a new rate as a per-call argument to a context initialized for a different rate.
3. At end-of-input, repeatedly call swr_convert() with null input and a correctly sized output buffer until the resampler has no delayed output; append all drained samples to the FIFO before padding/encoding the final codec frame.
4. Check every resampler return value and preserve the first failure. A failed resampler drain must make finish() fail and trigger deterministic abort/cleanup rather than reporting a complete export.
5. Add tests for 44.1-to-48 kHz and 48-to-44.1 kHz with non-frame-aligned tails, including short input smaller than the resampler delay; compare decoded sample count and duration against the declared rational input duration.
6. Add malformed-block tests for zero/negative sample rate, zero/negative channels, non-divisible interleaved sample counts, oversized block counts, and mid-stream sample-rate/channel changes.
7. Add a discontinuous OutputAudioBlock.start test independently of resampling; either reject the discontinuity under a contiguous-stream contract or preserve it under an explicit timestamp-aware writer contract.

This is a source-level finding and test-design update only. No production source, test, Activity, Manifest, or runtime UI file was changed. It does not alter the registered S1-3/S3-1/A-12/S5 approval gates.
