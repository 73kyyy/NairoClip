package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativePreviewPrewarmPlanTest {
    @Test
    fun emptyOrNegativeDurationProducesNoSamples() {
        assertEquals(0, NativePreviewPrewarmPlan.samplesUs(0L, 0L).size)
        assertEquals(0, NativePreviewPrewarmPlan.samplesUs(-1L, 0L).size)
    }

    @Test
    fun oneSecondUsesTenSamples() {
        val samples = NativePreviewPrewarmPlan.samplesUs(1_000_000L, 0L)
        assertEquals(10, samples.size)
        assertEquals(0L, samples.first())
        assertEquals(900_000L, samples.last())
    }

    @Test
    fun thirtySecondsIsCappedAtTwoHundredFortySamples() {
        assertEquals(240, NativePreviewPrewarmPlan.samplesUs(30_000_000L, 0L).size)
    }

    @Test
    fun samplingStartsAtPlayheadAndWrapsAcrossTimelineEnd() {
        val samples = NativePreviewPrewarmPlan.samplesUs(1_000_000L, 900_000L)
        assertEquals(10, samples.size)
        assertEquals(900_000L, samples[0])
        assertEquals(0L, samples[1])
        assertEquals(800_000L, samples.last())
    }

    @Test
    fun negativePlayheadIsClampedToTimelineStart() {
        val samples = NativePreviewPrewarmPlan.samplesUs(1_000_000L, -50L)
        assertEquals(0L, samples.first())
    }

    @Test
    fun maximumLongDurationNeverOverflowsOrEscapesTimeline() {
        val duration = Long.MAX_VALUE
        val samples = NativePreviewPrewarmPlan.samplesUs(duration, duration - 1L)
        assertEquals(240, samples.size)
        assertTrue(samples.all { it >= 0L && it < duration })
        assertEquals(duration - 1L, samples.first())
        assertEquals(samples.size, samples.distinct().size)
    }
}
