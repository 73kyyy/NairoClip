package com.nairoclip.app

import java.math.BigInteger
import kotlin.math.roundToLong

/**
 * Authoritative source/timeline mapping for one selected native timeline clip.
 *
 * ASR currently supports only forward, constant-speed clips. Reversed, frozen,
 * and speed-curve clips must be rejected until their audio transformation is
 * implemented; silently treating them as 1x would place captions incorrectly.
 */
data class NativeSpeechSourceRange(
    val timelineStartUs: Long,
    val timelineDurationUs: Long,
    val sourceInUs: Long,
    val sourceOutUs: Long,
    val speedNumerator: Long,
    val speedDenominator: Long,
    val reversed: Boolean = false,
    val frozen: Boolean = false,
    val hasSpeedCurve: Boolean = false
) {
    fun validationError(): String? {
        if (timelineStartUs < 0L || timelineDurationUs <= 0L || sourceInUs < 0L) {
            return "自动字幕源区间无效"
        }
        if (timelineStartUs > Long.MAX_VALUE - timelineDurationUs) {
            return "自动字幕时间轴区间溢出"
        }
        if (speedNumerator <= 0L || speedDenominator <= 0L) return "片段速度参数无效"
        val speed = speedNumerator.toDouble() / speedDenominator.toDouble()
        if (!speed.isFinite() || speed !in 0.1..100.0) return "片段速度超出自动字幕支持范围"
        if (reversed || frozen || hasSpeedCurve) {
            return "自动字幕暂不支持倒放、定格或变速曲线片段"
        }
        if (sourceOutUs <= sourceInUs) return "自动字幕源区间无效"
        val expectedSourceSpan = roundedRatio(
            BigInteger.valueOf(timelineDurationUs).multiply(BigInteger.valueOf(speedNumerator)),
            BigInteger.valueOf(speedDenominator)
        ) ?: return "自动字幕源区间计算溢出"
        val actualSourceSpan = sourceOutUs - sourceInUs
        if (absDifference(expectedSourceSpan, actualSourceSpan) > 2L) {
            return "片段源区间与时间轴时长/速度不一致"
        }
        return null
    }

    /**
     * Maps a Whisper segment relative to the selected source interval into
     * absolute project timeline microseconds. Returns null for invalid ranges.
     */
    fun mapSegment(
        startSeconds: Double,
        endSeconds: Double,
        audioDurationUs: Long
    ): Pair<Long, Long>? {
        if (validationError() != null || !startSeconds.isFinite() || !endSeconds.isFinite() ||
            audioDurationUs <= 0L || startSeconds < 0.0 || endSeconds <= startSeconds
        ) return null
        val startSourceDelta = (startSeconds * 1_000_000.0).takeIf { it.isFinite() }?.roundToLong()
            ?: return null
        val endSourceDelta = (endSeconds * 1_000_000.0).takeIf { it.isFinite() }?.roundToLong()
            ?: return null
        val sourceSpan = sourceOutUs - sourceInUs
        val availableSourceUs = minOf(audioDurationUs, sourceSpan)
        if (startSourceDelta < 0L || endSourceDelta <= startSourceDelta ||
            startSourceDelta >= sourceSpan ||
            (endSourceDelta > availableSourceUs && endSourceDelta - availableSourceUs > 1L)
        ) return null

        val timelineStartDelta = roundedRatio(
            BigInteger.valueOf(startSourceDelta).multiply(BigInteger.valueOf(speedDenominator)),
            BigInteger.valueOf(speedNumerator)
        ) ?: return null
        val timelineDuration = roundedRatio(
            BigInteger.valueOf(endSourceDelta - startSourceDelta)
                .multiply(BigInteger.valueOf(speedDenominator)),
            BigInteger.valueOf(speedNumerator)
        ) ?: return null
        if (timelineStartDelta < 0L || timelineDuration <= 0L ||
            timelineStartDelta >= timelineDurationUs
        ) return null
        val boundedDuration = minOf(timelineDuration, timelineDurationUs - timelineStartDelta)
        if (boundedDuration <= 0L || timelineStartUs > Long.MAX_VALUE - timelineStartDelta) return null
        return (timelineStartUs + timelineStartDelta) to boundedDuration
    }

    private fun roundedRatio(numerator: BigInteger, denominator: BigInteger): Long? {
        if (denominator.signum() <= 0 || numerator.signum() < 0) return null
        return try {
            numerator.add(denominator.shiftRight(1)).divide(denominator).longValueExact()
        } catch (_: ArithmeticException) {
            null
        }
    }

    private fun absDifference(a: Long, b: Long): Long {
        if (a >= b) return if (a - b < 0L) Long.MAX_VALUE else a - b
        return if (b - a < 0L) Long.MAX_VALUE else b - a
    }
}
