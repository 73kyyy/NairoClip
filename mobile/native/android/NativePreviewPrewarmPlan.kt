package com.nairoclip.app

/**
 * Deterministic, overflow-safe sampling plan for preview-frame prewarming.
 *
 * Samples are spaced across the project duration at approximately 10 fps,
 * capped at 240 timestamps. This is frame-cache sampling, not a promise that
 * every source clip's decoder cache is populated.
 */
object NativePreviewPrewarmPlan {
    private const val FRAME_INTERVAL_US = 100_000L
    private const val MAX_FRAMES = 240

    fun samplesUs(durationUs: Long, playheadUs: Long): LongArray {
        if (durationUs <= 0L) return longArrayOf()

        val wholeIntervals = durationUs / FRAME_INTERVAL_US
        val remainderUs = durationUs % FRAME_INTERVAL_US
        val roundedFrames = wholeIntervals + if (remainderUs >= FRAME_INTERVAL_US / 2L) 1L else 0L
        val total = roundedFrames.coerceIn(1L, MAX_FRAMES.toLong()).toInt()
        val startUs = playheadUs.coerceAtLeast(0L) % durationUs
        val samples = LongArray(total)

        for (index in 0 until total) {
            // Compute floor(duration * index / total) without multiplying the
            // potentially Long-sized duration by index.
            val divisor = total.toLong()
            val offsetUs = (durationUs / divisor) * index +
                ((durationUs % divisor) * index) / divisor

            // Modular addition without overflowing startUs + offsetUs.
            val untilWrap = durationUs - startUs
            samples[index] = if (offsetUs >= untilWrap) {
                offsetUs - untilWrap
            } else {
                startUs + offsetUs
            }
        }
        return samples
    }
}
