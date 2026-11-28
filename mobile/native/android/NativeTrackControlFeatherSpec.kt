package com.nairoclip.app

/**
 * Shared blur and alpha-mask parameters for clip content passing beneath the
 * fixed track controls. The Canvas draws sharp control glyphs after compositing.
 */
internal object NativeTrackControlFeatherSpec {
    const val BLUR_RADIUS_DP = 3f
    const val START_ALPHA = 0.88f
    const val END_ALPHA = 0f

    fun alphaAt(position: Float): Float {
        val progress = when {
            position.isNaN() -> 0f
            position == Float.POSITIVE_INFINITY -> 1f
            position == Float.NEGATIVE_INFINITY -> 0f
            else -> position.coerceIn(0f, 1f)
        }
        return START_ALPHA + (END_ALPHA - START_ALPHA) * progress
    }
}
