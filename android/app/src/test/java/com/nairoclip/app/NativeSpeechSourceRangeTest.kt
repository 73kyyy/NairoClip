package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeSpeechSourceRangeTest {
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
}
