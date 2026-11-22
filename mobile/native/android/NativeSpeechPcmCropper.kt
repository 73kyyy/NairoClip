package com.nairoclip.app

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.ceil

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

        val oneFrameUs = 1_000_000.0 / decoded.sampleRate.toDouble()
        require(
            decoded.startTimeUs <= sourceRange.sourceInUs ||
                decoded.startTimeUs - sourceRange.sourceInUs <= oneFrameUs.toLong()
        ) {
            "系统解码器跳过了所选片段源入点，不能安全生成字幕"
        }
        val startDeltaUs = (sourceRange.sourceInUs - decoded.startTimeUs).coerceAtLeast(0L)
        val endDeltaUs = sourceRange.sourceOutUs - decoded.startTimeUs
        require(endDeltaUs > startDeltaUs) { "解码音频未覆盖所选源区间" }
        val startFrame = ceil(startDeltaUs.toDouble() * decoded.sampleRate / 1_000_000.0).toLong()
        val endFrame = ceil(endDeltaUs.toDouble() * decoded.sampleRate / 1_000_000.0).toLong()
        val totalFrames = decoded.frameCount
        require(startFrame in 0 until totalFrames) { "所选源入点超出解码音频范围" }
        require(endFrame <= totalFrames + 1L) { "系统解码器未覆盖所选片段源出点" }
        val boundedEndFrame = minOf(endFrame, totalFrames)
        val frameCount = boundedEndFrame - startFrame
        require(frameCount > 0L) { "所选源区间没有有效音频帧" }

        val sourceFloats = decoded.samples.duplicate()
            .order(ByteOrder.nativeOrder())
            .asFloatBuffer()
        sourceFloats.position(startFrame.toInt())
        sourceFloats.limit(boundedEndFrame.toInt())
        val output = ByteBuffer.allocateDirect(frameCount.toInt() * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
        output.asFloatBuffer().put(sourceFloats)
        return AndroidSpeechPcm(
            samples = output,
            frameCount = frameCount,
            sampleRate = decoded.sampleRate,
            startTimeUs = sourceRange.sourceInUs
        )
    }
}
