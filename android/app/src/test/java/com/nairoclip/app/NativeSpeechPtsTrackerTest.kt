package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Test

class NativeSpeechPtsTrackerTest {
    @Test
    fun acceptsSmallTimestampQuantizationAroundCumulativeSampleClock() {
        val tracker = NativeSpeechPtsTracker(48000)
        tracker.accept(1_000_000L, 480L)
        tracker.accept(1_011_000L, 480L) // +1ms from the cumulative 10ms clock
        tracker.accept(1_021_000L, 480L) // Still measured from the first PTS, not prior jitter.
        assertEquals(1_000_000L, tracker.firstPresentationTimeUs())
    }

    @Test
    fun measuresFortyFourKhzTimestampAgainstCumulativeFramesWithoutRoundingDrift() {
        val tracker = NativeSpeechPtsTracker(44100)
        tracker.accept(2_000_000L, 441L)
        tracker.accept(2_010_023L, 441L) // 23us timestamp quantization.
        tracker.accept(2_020_001L, 441L) // Expected from first PTS + 882 frames, not prior PTS.
        assertEquals(2_000_000L, tracker.firstPresentationTimeUs())
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsZeroFrameOutputBuffer() {
        NativeSpeechPtsTracker(16000).accept(0L, 0L)
    }

    @Test
    fun rejectsNegativeOrOverflowingTimestampInput() {
        val tracker = NativeSpeechPtsTracker(16000)
        try {
            tracker.accept(-1L, 160L)
            throw AssertionError("negative PTS must be rejected")
        } catch (_: IllegalArgumentException) {
            // Expected.
        }
    }

    @Test
    fun rejectsPositiveGapLargerThanCodecQuantizationTolerance() {
        val tracker = NativeSpeechPtsTracker(48000)
        tracker.accept(0L, 480L)
        expectDiscontinuity {
            tracker.accept(12_001L, 480L) // Expected 10ms; +2.001ms is not rounding.
        }
    }

    @Test
    fun rejectsNegativeOverlapLargerThanCodecQuantizationTolerance() {
        val tracker = NativeSpeechPtsTracker(48000)
        tracker.accept(100_000L, 480L)
        expectDiscontinuity {
            tracker.accept(107_999L, 480L) // Expected 110ms; -2.001ms overlap.
        }
    }

    @Test
    fun rejectsFormerlyAcceptedFiftyMillisecondDiscontinuity() {
        val tracker = NativeSpeechPtsTracker(16000)
        tracker.accept(0L, 160L)
        expectDiscontinuity { tracker.accept(60_000L, 160L) }
    }

    @Test
    fun acceptsExactToleranceBoundary() {
        val tracker = NativeSpeechPtsTracker(16000)
        tracker.accept(0L, 160L)
        tracker.accept(12_000L, 160L) // Expected 10ms, exactly +2ms.
    }

    private fun expectDiscontinuity(block: () -> Unit) {
        try {
            block()
            throw AssertionError("timestamp discontinuity must be rejected")
        } catch (expected: IllegalArgumentException) {
            // Expected.
        }
    }
}
