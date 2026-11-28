package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeTrackControlFeatherSpecTest {
    @Test
    fun featherStartsStrongAndClearsAtTheControlStripEdge() {
        assertEquals(0.88f, NativeTrackControlFeatherSpec.alphaAt(0f), 0.0001f)
        assertEquals(0f, NativeTrackControlFeatherSpec.alphaAt(1f), 0.0001f)
    }

    @Test
    fun featherAlphaChangesMonotonicallyAcrossTheControlStrip() {
        val samples = listOf(0f, 0.25f, 0.5f, 0.75f, 1f)
            .map(NativeTrackControlFeatherSpec::alphaAt)
        assertEquals(0.44f, samples[2], 0.0001f)
        assertTrue(samples.zipWithNext().all { (left, right) -> left >= right })
    }


    @Test
    fun featherCoversTheFixedTrackControlStrip() {
        assertEquals(40, NativeEditorLayoutSpec.TRACK_CONTROL_WIDTH_DP)
    }

    @Test
    fun malformedPositionIsBoundedToTheStrongEdge() {
        assertEquals(0.88f, NativeTrackControlFeatherSpec.alphaAt(Float.NaN), 0.0001f)
        assertEquals(0.88f, NativeTrackControlFeatherSpec.alphaAt(Float.POSITIVE_INFINITY), 0.0001f)
    }
}
