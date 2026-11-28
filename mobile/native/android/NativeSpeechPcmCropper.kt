package com.nairoclip.app

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ceil
import kotlin.math.roundToLong

/**
 * Crops decoder seek-preroll and samples after the selected clip source out-point.
 * It refuses to guess when the decoder skipped the in-point or failed to cover
 * the out-point, keeping caption timestamps anchored to the actual media PTS.
 */
object NativeSpeechPcmCropper {
    fun trim(
        decoded: AndroidSpeechPcm,
        sourceRange: NativeSpeechSourceRange
    ): AndroidSpeechPcm {
        require(sourceRange.validationError() == null) {
            sourceRange.validationError() ?: "自动字幕源区间无效"
        }
        require(decoded.samples.isDirect && !decoded.samples.isReadOnly)
        require(decoded.sampleRate == 16000 && decoded.frameCount > 0L)
        require(decoded.frameCount <= Int.MAX_VALUE.toLong() / Float.SIZE_BYTES)
        require(decoded.frameCount <= decoded.samples.remaining().toLong() / Float.SIZE_BYTES)
        require(decoded.startTimeUs >= 0L)

        val oneFrameUs = ceil(1_000_000.0 / decoded.sampleRate.toDouble()).toLong()
        require(
            decoded.startTimeUs <= sourceRange.sourceInUs ||
                decoded.startTimeUs - sourceRange.sourceInUs <= oneFrameUs
        ) {
            "系统解码器跳过了所选片段源入点，不能安全生成字幕"
        }
        val startDeltaUs = (sourceRange.sourceInUs - decoded.startTimeUs).coerceAtLeast(0L)
        val endDeltaUs = sourceRange.sourceOutUs - decoded.startTimeUs
        require(endDeltaUs > startDeltaUs) { "解码音频未覆盖所选源区间" }
        // Use integer ceil arithmetic: floating-point rounding near a frame boundary
        // must not allow an out-point that extends beyond the decoded PCM.
        val startFrame = ceilFrameIndex(startDeltaUs, decoded.sampleRate)
        val endFrame = ceilFrameIndex(endDeltaUs, decoded.sampleRate)
        val totalFrames = decoded.frameCount
        require(startFrame in 0 until totalFrames) { "所选源入点超出解码音频范围" }
        require(endFrame <= totalFrames) { "系统解码器未完整覆盖所选片段源出点" }
        val frameCount = endFrame - startFrame
        require(frameCount > 0L) { "所选源区间没有有效音频帧" }

        val sourceFloats = decoded.samples.duplicate()
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
        sourceFloats.position(startFrame.toInt())
        sourceFloats.limit(endFrame.toInt())
        val output = ByteBuffer.allocateDirect(frameCount.toInt() * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
        output.asFloatBuffer().put(sourceFloats)
        output.position(0)
        // ceil() selects the first sample at or after the source in-point.
        // Preserve that sample's actual media timestamp instead of relabelling it
        // as the exact in-point (which can shift captions by almost one PCM frame).
        val croppedStartOffsetUs =
            (startFrame.toDouble() * 1_000_000.0 / decoded.sampleRate.toDouble()).roundToLong()
        require(decoded.startTimeUs <= Long.MAX_VALUE - croppedStartOffsetUs) {
            "裁剪后音频起始时间戳溢出"
        }
        return AndroidSpeechPcm(
            samples = output,
            frameCount = frameCount,
            sampleRate = decoded.sampleRate,
            startTimeUs = decoded.startTimeUs + croppedStartOffsetUs
        )
    }
    private fun ceilFrameIndex(deltaUs: Long, sampleRate: Int): Long {
        require(deltaUs >= 0L && sampleRate > 0)
        val numerator = Math.multiplyExact(deltaUs, sampleRate.toLong())
        return Math.addExact(numerator, 999_999L) / 1_000_000L
    }

}
