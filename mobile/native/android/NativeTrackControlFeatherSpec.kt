package com.nairoclip.app

/**
 * Alpha ramp used to feather clip content beneath the fixed track controls.
 * This is a visibility fade, not a Gaussian blur; control glyphs are drawn above it.
 */
internal object NativeTrackControlFeatherSpec {
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
