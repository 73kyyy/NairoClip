package com.nairoclip.app

/**
 * Geometry locked to the saved 692x1536 NairoClip editor reference.
 *
 * Keep visual dimensions separate from touch-target dimensions. These values
 * describe the portrait editor composition; responsive behavior may scroll
 * tool rows, but must not silently resize or distort these controls.
 */
internal object NativeEditorLayoutSpec {
    data class PreviewSize(val widthDp: Float, val heightDp: Float)

    fun previewSize(maxWidthDp: Float, maxHeightDp: Float): PreviewSize {
        val safeWidth = if (maxWidthDp.isFinite()) maxWidthDp.coerceAtLeast(0f) else 0f
        val safeHeight = if (maxHeightDp.isFinite()) maxHeightDp.coerceAtLeast(0f) else 0f
        val width = minOf(
            safeWidth,
            safeHeight * PREVIEW_ASPECT_WIDTH.toFloat() / PREVIEW_ASPECT_HEIGHT.toFloat()
        )
        return PreviewSize(width, width * PREVIEW_ASPECT_HEIGHT.toFloat() / PREVIEW_ASPECT_WIDTH.toFloat())
    }

    const val REFERENCE_WIDTH_PX = 692
    const val REFERENCE_HEIGHT_PX = 1536
    const val PREVIEW_ASPECT_WIDTH = 9
    const val PREVIEW_ASPECT_HEIGHT = 16

    const val TOP_BAR_HEIGHT_DP = 44
    const val PREVIEW_EXPAND_DIAMETER_DP = 36
    const val QUICK_ACTION_ROW_HEIGHT_DP = 50
    const val QUICK_ACTION_DIAMETER_DP = 30
    const val QUICK_ACTION_TOUCH_TARGET_DP = 48
    const val QUICK_ACTION_COUNT = 11

    const val TIMELINE_HEIGHT_DP = 218
    const val RULER_HEIGHT_DP = 27
    const val RULER_TICK_HEIGHT_DP = 3
    const val RULER_MAJOR_TICK_HEIGHT_DP = 4
    const val TRACK_CONTROL_WIDTH_DP = 40
    const val START_BADGE_WIDTH_DP = 32
    const val START_BADGE_HEIGHT_DP = 20
    const val TRANSPORT_ROW_HEIGHT_DP = 52
    const val TRANSPORT_ACTION_COUNT = 3
    const val BOTTOM_TOOLBAR_HEIGHT_DP = 82
}
