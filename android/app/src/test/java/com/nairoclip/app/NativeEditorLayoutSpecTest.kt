package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorLayoutSpecTest {
    @Test
    fun screenshotReferenceGeometryRemainsLocked() {
        assertEquals(692, NativeEditorLayoutSpec.REFERENCE_WIDTH_PX)
        assertEquals(1536, NativeEditorLayoutSpec.REFERENCE_HEIGHT_PX)
        assertEquals(9f / 16f, NativeEditorLayoutSpec.PREVIEW_ASPECT_WIDTH.toFloat() /
            NativeEditorLayoutSpec.PREVIEW_ASPECT_HEIGHT.toFloat(), 0.0001f)
        assertEquals(44, NativeEditorLayoutSpec.TOP_BAR_HEIGHT_DP)
        assertEquals(46, NativeEditorLayoutSpec.QUICK_ACTION_ROW_HEIGHT_DP)
        assertEquals(282, NativeEditorLayoutSpec.TIMELINE_HEIGHT_DP)
        assertEquals(56, NativeEditorLayoutSpec.TRANSPORT_ROW_HEIGHT_DP)
        assertEquals(104, NativeEditorLayoutSpec.BOTTOM_TOOLBAR_HEIGHT_DP)
    }

    @Test
    fun quickActionVisualCirclesStayEqualAndTouchTargetsRemainLarger() {
        assertEquals(11, NativeEditorLayoutSpec.QUICK_ACTION_COUNT)
        assertEquals(30, NativeEditorLayoutSpec.QUICK_ACTION_DIAMETER_DP)
        assertEquals(48, NativeEditorLayoutSpec.QUICK_ACTION_TOUCH_TARGET_DP)
        assertTrue(
            NativeEditorLayoutSpec.QUICK_ACTION_TOUCH_TARGET_DP >
                NativeEditorLayoutSpec.QUICK_ACTION_DIAMETER_DP
        )
    }

    @Test
    fun previewExpandAndTimelineRulerKeepReferenceGeometry() {
        assertEquals(36, NativeEditorLayoutSpec.PREVIEW_EXPAND_DIAMETER_DP)
        assertEquals(27, NativeEditorLayoutSpec.RULER_HEIGHT_DP)
        assertEquals(3, NativeEditorLayoutSpec.RULER_TICK_HEIGHT_DP)
        assertEquals(4, NativeEditorLayoutSpec.RULER_MAJOR_TICK_HEIGHT_DP)
        assertEquals(3, NativeEditorLayoutSpec.TRANSPORT_ACTION_COUNT)
    }

    @Test
    fun previewSizingPreservesNineBySixteenWhenWidthOrHeightIsConstrained() {
        val heightConstrained = NativeEditorLayoutSpec.previewSize(420f, 720f)
        assertEquals(405f, heightConstrained.widthDp, 0.001f)
        assertEquals(720f, heightConstrained.heightDp, 0.001f)

        val widthConstrained = NativeEditorLayoutSpec.previewSize(200f, 720f)
        assertEquals(200f, widthConstrained.widthDp, 0.001f)
        assertEquals(200f * 16f / 9f, widthConstrained.heightDp, 0.001f)

        val zeroWidth = NativeEditorLayoutSpec.previewSize(0f, 720f)
        assertEquals(0f, zeroWidth.widthDp, 0f)
        assertEquals(0f, zeroWidth.heightDp, 0f)
    }
}
