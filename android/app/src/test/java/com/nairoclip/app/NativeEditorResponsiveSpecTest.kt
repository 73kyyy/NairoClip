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
