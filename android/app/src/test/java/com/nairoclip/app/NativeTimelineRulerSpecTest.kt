package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Test

class NativeTimelineRulerSpecTest {
    @Test
    fun timelineZeroIsCenteredAndCoordinatesStayBidirectional() {
        assertEquals(300f, NativeTimelineViewportSpec.originX(600f), 0f)
        assertEquals(
            300.0,
            NativeTimelineViewportSpec.xForTime(600f, 0.0, 40.0, 0.0),
            0.0
        )
        assertEquals(
            500.0,
            NativeTimelineViewportSpec.xForTime(600f, 5.0, 40.0, 0.0),
            0.0
        )
        assertEquals(
            0.0,
            NativeTimelineViewportSpec.timeAtX(300f, 600f, 40.0, 0.0),
            0.0
        )
        assertEquals(
            5.0,
            NativeTimelineViewportSpec.timeAtX(500f, 600f, 40.0, 0.0),
            0.0
        )
        // When 5s is the playhead/viewport-center time, the playhead stays at x=300.
        assertEquals(
            300.0,
            NativeTimelineViewportSpec.xForTime(600f, 5.0, 40.0, 200.0),
            0.0
        )
        assertEquals(
            5.0,
            NativeTimelineViewportSpec.timeAtX(300f, 600f, 40.0, 200.0),
            0.0
        )
        assertEquals(1200.0, NativeTimelineViewportSpec.maxScrollOffset(30_000_000L, 40.0), 0.0)
    }

    @Test
    fun trackControlFeatherFadesBlurFromLeftToRightWithoutChangingControlStates() {
        assertEquals(0.88f, NativeTrackControlFeatherSpec.alphaAt(0f), 0.0001f)
        assertEquals(0.44f, NativeTrackControlFeatherSpec.alphaAt(0.5f), 0.0001f)
        assertEquals(0f, NativeTrackControlFeatherSpec.alphaAt(1f), 0.0001f)
        assertEquals(0.88f, NativeTrackControlFeatherSpec.alphaAt(-1f), 0.0001f)
        assertEquals(0f, NativeTrackControlFeatherSpec.alphaAt(2f), 0.0001f)
        assertEquals(3f, NativeTrackControlFeatherSpec.BLUR_RADIUS_DP, 0f)
    }

    @Test
    fun rulerMajorStepAdaptsToPixelDensity() {
        assertEquals(5.0, NativeTimelineRulerSpec.majorStepSeconds(18.0), 0.0)
        assertEquals(2.0, NativeTimelineRulerSpec.majorStepSeconds(42.0), 0.0)
        assertEquals(1.0, NativeTimelineRulerSpec.majorStepSeconds(63.0), 0.0)
        assertEquals(0.5, NativeTimelineRulerSpec.majorStepSeconds(270.0), 0.0)
    }

    @Test
    fun rulerLabelsUseSecondsAndMinuteFormatsLikeTheExistingEditor() {
        assertEquals("5s", NativeTimelineRulerSpec.formatLabel(5.0, 5.0))
        assertEquals("3.5s", NativeTimelineRulerSpec.formatLabel(3.5, 0.5))
        assertEquals("3.55s", NativeTimelineRulerSpec.formatLabel(3.55, 0.1))
        assertEquals("01:03", NativeTimelineRulerSpec.formatLabel(63.0, 5.0))
        assertEquals("01:03.5", NativeTimelineRulerSpec.formatLabel(63.5, 0.5))
        assertEquals("0s", NativeTimelineRulerSpec.formatLabel(-1.0, 5.0))
    }

    @Test
    fun initialTimelineZoomFitsTheSequenceAroundTheCenteredPlayhead() {
        val cssPixelScale = NativeTimelineViewportSpec.fitPixelsPerSecond(
            widthPx = 692f,
            density = 1f,
            durationUs = 12_000_000L
        )
        assertEquals(322f / 12f, cssPixelScale, 0.001f)

        val highDensity = NativeTimelineViewportSpec.fitPixelsPerSecond(
            widthPx = 1038f,
            density = 1.5f,
            durationUs = 12_000_000L
        )
        assertEquals(322f / 12f * 1.5f, highDensity, 0.001f)

        val minimumZoom = NativeTimelineViewportSpec.fitPixelsPerSecond(
            widthPx = 400f,
            density = 1f,
            durationUs = 1_000_000_000L
        )
        assertEquals(4f, minimumZoom, 0f)
    }
}
