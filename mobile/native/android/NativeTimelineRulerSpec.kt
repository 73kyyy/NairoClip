package com.nairoclip.app

import java.util.Locale
import kotlin.math.floor
import kotlin.math.round

/**
 * Timeline coordinates and ruler labels are ported from the existing editor.js
 * contract: project time zero sits at viewport center, and ruler density adapts
 * to zoom while the playhead readout remains stable MM:SS.
 */
internal object NativeTimelineViewportSpec {
    fun originX(widthPx: Float): Float = widthPx.coerceAtLeast(0f) / 2f

    fun xForTime(widthPx: Float, seconds: Double, pixelsPerSecond: Double, scrollOffsetPx: Double): Double =
        originX(widthPx).toDouble() + seconds * pixelsPerSecond - scrollOffsetPx

    fun timeAtX(xPx: Float, widthPx: Float, pixelsPerSecond: Double, scrollOffsetPx: Double): Double {
        if (!pixelsPerSecond.isFinite() || pixelsPerSecond <= 0.0) return 0.0
        return (scrollOffsetPx + xPx.toDouble() - originX(widthPx).toDouble()) / pixelsPerSecond
    }

    fun maxScrollOffset(durationUs: Long, pixelsPerSecond: Double): Double {
        if (durationUs <= 0L || !pixelsPerSecond.isFinite() || pixelsPerSecond <= 0.0) return 0.0
        return durationUs.toDouble() / 1_000_000.0 * pixelsPerSecond
    }

    fun fitPixelsPerSecond(widthPx: Float, density: Float, durationUs: Long): Float {
        if (widthPx <= 0f || !widthPx.isFinite() || density <= 0f || !density.isFinite() ||
            durationUs <= 0L
        ) return 42f * density.coerceAtLeast(0f)
        val halfViewport = maxOf(60f * density, widthPx / 2f - 24f * density)
        val durationSeconds = durationUs.toDouble() / 1_000_000.0
        return (halfViewport.toDouble() / durationSeconds)
            .toFloat()
            .coerceIn(4f * density, 400f * density)
    }
}

internal object NativeTimelineRulerSpec {
    private val majorStepsSeconds = listOf(
        0.1, 0.2, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0
    )

    fun majorStepSeconds(pixelsPerSecond: Double): Double {
        if (!pixelsPerSecond.isFinite() || pixelsPerSecond <= 0.0) return 600.0
        return majorStepsSeconds.firstOrNull { it * pixelsPerSecond >= 56.0 } ?: 600.0
    }

    fun formatLabel(seconds: Double, stepSeconds: Double): String {
        val safe = if (seconds.isFinite()) seconds.coerceAtLeast(0.0) else 0.0
        val minutes = floor(safe / 60.0).toLong()
        val needMinute = stepSeconds >= 60.0 || safe >= 60.0 || minutes > 0L
        if (!needMinute) {
            return if (stepSeconds < 1.0) {
                val decimals = if (stepSeconds < 0.15) 2 else 1
                String.format(Locale.US, "%.${decimals}f", safe % 60.0) + "s"
            } else {
                "${round(safe % 60.0).toLong()}s"
            }
        }
        return if (stepSeconds < 1.0) {
            val secondsText = String.format(Locale.US, "%.1f", safe % 60.0).padStart(4, '0')
            "${minutes.toString().padStart(2, '0')}:$secondsText"
        } else {
            val secondsText = floor(safe % 60.0).toLong().toString().padStart(2, '0')
            "${minutes.toString().padStart(2, '0')}:$secondsText"
        }
    }
}
