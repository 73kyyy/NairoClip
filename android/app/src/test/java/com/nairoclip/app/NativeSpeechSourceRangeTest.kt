package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeSpeechSourceRangeTest {
    @Test
    fun buildsOnlyFromCompleteNativeClipSourceMetadata() {
        val clip = NativeTimelineClip(
            id = 7L,
            startUs = 2_000_000L,
            durationUs = 2_000_000L,
            label = "素材",
            speedNumerator = 2L,
            speedDenominator = 1L,
            sourceInUs = 1_000_000L,
            sourceOutUs = 5_000_000L,
            sourceRangeKnown = true
        )
        val range = NativeSpeechSourceRange.fromTimelineClip(clip.startUs, clip)
        assertNotNull(range)
        assertNull(range!!.validationError())
        assertNull(
            NativeSpeechSourceRange.fromTimelineClip(
                clip.startUs,
                clip.copy(sourceRangeKnown = false)
            )
        )
    }

    @Test
    fun mapsTwoTimesSourceAudioBackToHalfSpeedTimelineDuration() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = 3_000_000L,
            timelineDurationUs = 2_000_000L,
            sourceInUs = 1_000_000L,
            sourceOutUs = 5_000_000L,
            speedNumerator = 2L,
            speedDenominator = 1L
        )
        assertNull(range.validationError())
        val mapped = range.mapSegment(0.5, 1.5, 4_000_000L)
        assertNotNull(mapped)
        assertEquals(3_250_000L, mapped!!.first)
        assertEquals(500_000L, mapped.second)
    }

    @Test
    fun mapsHalfSpeedSourceAudioToDoubleTimelineDuration() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = 0L,
            timelineDurationUs = 4_000_000L,
            sourceInUs = 2_000_000L,
            sourceOutUs = 4_000_000L,
            speedNumerator = 1L,
            speedDenominator = 2L
        )
        assertNull(range.validationError())
        val mapped = range.mapSegment(0.25, 0.75, 2_000_000L)
        assertNotNull(mapped)
        assertEquals(500_000L, mapped!!.first)
        assertEquals(1_000_000L, mapped.second)
    }

    @Test
    fun rejectsUnsupportedNonLinearClipModes() {
        val base = NativeSpeechSourceRange(
            timelineStartUs = 0L,
            timelineDurationUs = 1_000_000L,
            sourceInUs = 0L,
            sourceOutUs = 1_000_000L,
            speedNumerator = 1L,
            speedDenominator = 1L
        )
        assertFalse(base.copy(reversed = true).validationError().isNullOrBlank())
        assertFalse(base.copy(frozen = true).validationError().isNullOrBlank())
        assertFalse(base.copy(hasSpeedCurve = true).validationError().isNullOrBlank())
    }

    @Test
    fun rejectsMismatchedSourceSpanAndOutOfRangeSegments() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = 0L,
            timelineDurationUs = 2_000_000L,
            sourceInUs = 0L,
            sourceOutUs = 1_000_000L,
            speedNumerator = 1L,
            speedDenominator = 1L
        )
        assertNotNull(range.validationError())
        assertNull(range.mapSegment(0.0, 0.5, 1_000_000L))
    }

    @Test
    fun clampsFinalPcmFrameRoundingTailToClipOutPoint() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = 2_000_000L,
            timelineDurationUs = 500_000L,
            sourceInUs = 250_030L,
            sourceOutUs = 750_030L,
            speedNumerator = 1L,
            speedDenominator = 1L
        )
        val mapped = range.mapSegment(
            startSeconds = 0.0,
            endSeconds = 0.5,
            audioDurationUs = 500_000L,
            audioStartOffsetUs = 33L
        )
        assertEquals(2_000_033L to 499_967L, mapped)
    }

    @Test
    fun rejectsAudioMappingThatExceedsClipByMoreThanOnePcmFrame() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = 2_000_000L,
            timelineDurationUs = 500_000L,
            sourceInUs = 250_030L,
            sourceOutUs = 750_030L,
            speedNumerator = 1L,
            speedDenominator = 1L
        )
        assertNull(
            range.mapSegment(
                startSeconds = 0.0,
                endSeconds = 0.5,
                audioDurationUs = 500_000L,
                audioStartOffsetUs = 100L
            )
        )
    }

    @Test
    fun mapsExactClipBoundariesWithoutMovingCaptionsOutsideTheClip() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = 7_000_000L,
            timelineDurationUs = 1_000_000L,
            sourceInUs = 3_000_000L,
            sourceOutUs = 4_000_000L,
            speedNumerator = 1L,
            speedDenominator = 1L
        )
        assertEquals(7_000_000L to 1_000_000L, range.mapSegment(0.0, 1.0, 1_000_000L))
        assertNull(range.mapSegment(1.0, 1.1, 1_000_000L))
        // A sub-frame timestamp tail is quantization, not extra clip duration.
        assertEquals(7_000_000L to 1_000_000L, range.mapSegment(0.0, 1.000003, 1_000_000L))
    }

    @Test
    fun rejectsTimelineEndOverflowBeforeMapping() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = Long.MAX_VALUE - 99L,
            timelineDurationUs = 100L,
            sourceInUs = 0L,
            sourceOutUs = 100L,
            speedNumerator = 1L,
            speedDenominator = 1L
        )
        assertNotNull(range.validationError())
        assertNull(range.mapSegment(0.0, 0.00005, 100L))
    }

    @Test
    fun safelyMapsLargeValidMicrosecondRanges() {
        val range = NativeSpeechSourceRange(
            timelineStartUs = Long.MAX_VALUE - 1_000_000L,
            timelineDurationUs = 1_000_000L,
            sourceInUs = 0L,
            sourceOutUs = 1_000_000L,
            speedNumerator = 1L,
            speedDenominator = 1L
        )
        assertNull(range.validationError())
        assertEquals(
            (Long.MAX_VALUE - 500_000L) to 250_000L,
            range.mapSegment(0.5, 0.75, 1_000_000L)
        )
    }
}
