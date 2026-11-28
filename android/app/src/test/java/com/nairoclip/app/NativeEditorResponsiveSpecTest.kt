package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorResponsiveSpecTest {
    @Test
    fun compactWindowsReduceTimelineAndPanelFootprint() {
        val compact = NativeEditorResponsiveSpec.forHeight(480f)
        val medium = NativeEditorResponsiveSpec.forHeight(700f)
        val regular = NativeEditorResponsiveSpec.forHeight(900f)

        assertEquals(112, compact.timelineHeightDp)
        assertEquals(88, compact.parameterPanelMaxHeightDp)
        assertEquals(32, compact.previewMinHeightDp)

        assertEquals(168, medium.timelineHeightDp)
        assertEquals(128, medium.parameterPanelMaxHeightDp)

        assertEquals(NativeEditorLayoutSpec.TIMELINE_HEIGHT_DP, regular.timelineHeightDp)
        assertTrue(compact.timelineHeightDp < medium.timelineHeightDp)
        assertTrue(medium.timelineHeightDp < regular.timelineHeightDp)
    }

    @Test
    fun minimumRegionsFitWithinEachSupportedHeightClass() {
        listOf(480f, 600f, 700f, 800f, 900f).forEach { height ->
            val metrics = NativeEditorResponsiveSpec.forHeight(height)
            val minimumRequired = NativeEditorLayoutSpec.TOP_BAR_HEIGHT_DP +
                NativeEditorLayoutSpec.QUICK_ACTION_ROW_HEIGHT_DP +
                NativeEditorLayoutSpec.TRANSPORT_ROW_HEIGHT_DP +
                NativeEditorLayoutSpec.BOTTOM_TOOLBAR_HEIGHT_DP +
                metrics.timelineHeightDp +
                metrics.parameterPanelMaxHeightDp +
                metrics.previewMinHeightDp
            assertTrue("layout exceeds $height dp: $minimumRequired", minimumRequired <= height)
        }
    }

    @Test
    fun nonFiniteHeightUsesSafeCompactPolicy() {
        assertEquals(
            NativeEditorResponsiveSpec.forHeight(0f),
            NativeEditorResponsiveSpec.forHeight(Float.NaN)
        )
        assertEquals(
            NativeEditorResponsiveSpec.forHeight(0f),
            NativeEditorResponsiveSpec.forHeight(Float.POSITIVE_INFINITY)
        )
    }
}
