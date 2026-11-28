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
        assertEquals(40, NativeEditorLayoutSpec.TRACK_CONTROL_BUTTON_WIDTH_DP)
        assertEquals(120, NativeEditorLayoutSpec.TRACK_CONTROL_WIDTH_DP)
        assertEquals(3, NativeEditorLayoutSpec.TRACK_CONTROL_BUTTON_COUNT)
    }

    @Test
    fun featherShaderEndsAtTheVisibleViewportEdge() {
        assertEquals(80f, NativeTrackControlFeatherSpec.shaderWidth(80f, 120f), 0f)
        assertEquals(120f, NativeTrackControlFeatherSpec.shaderWidth(320f, 120f), 0f)
        assertEquals(0f, NativeTrackControlFeatherSpec.shaderWidth(-1f, 120f), 0f)
        assertEquals(0f, NativeTrackControlFeatherSpec.shaderWidth(Float.NaN, 120f), 0f)
    }

    @Test
    fun malformedPositionIsBoundedToTheStrongEdge() {
        assertEquals(0.88f, NativeTrackControlFeatherSpec.alphaAt(Float.NaN), 0.0001f)
        assertEquals(0f, NativeTrackControlFeatherSpec.alphaAt(Float.POSITIVE_INFINITY), 0.0001f)
        assertEquals(0.88f, NativeTrackControlFeatherSpec.alphaAt(Float.NEGATIVE_INFINITY), 0.0001f)
    }
}
