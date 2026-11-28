package com.nairoclip.app

import java.nio.ByteBuffer
import java.nio.ByteOrder
import kotlin.math.max

/**
 * Streaming mono linear resampler. Output timestamps cover the full input-frame
 * duration [0, inputFrameCount / inputRate), including a final edge-held sample
 * when the last target sample falls between the final input sample and EOF.
 */
internal class NativeSpeechResampler(
    val inputRate: Int,
    private val outputRate: Int = 16000,
    maxOutputDurationMs: Long = 605_000L
) {
    private val step: Double
    private val maxOutputFrames: Long
    private var sourceIndex = 0L
    private var nextOutputPosition = 0.0
    private var previous: Float? = null
    private var finished = false
    private var values = ByteBuffer.allocateDirect(4096 * Float.SIZE_BYTES)
        .order(ByteOrder.nativeOrder())

    var outputSize: Int = 0
        private set

    init {
        require(inputRate in 8000..192000) { "输入采样率无效" }
        require(outputRate in 8000..192000) { "输出采样率无效" }
        require(maxOutputDurationMs in 1L..605_000L) { "重采样时长上限无效" }
        step = inputRate.toDouble() / outputRate.toDouble()
        maxOutputFrames = (outputRate.toLong() * maxOutputDurationMs + 999L) / 1000L
    }

    fun push(input: FloatArray) {
        check(!finished) { "重采样已结束，不能继续输入" }
        for (sample in input) {
            val safeSample = if (sample.isFinite()) sample.coerceIn(-1.0f, 1.0f) else 0.0f
            val prev = previous
            if (prev == null) {
                previous = safeSample
                append(safeSample)
                nextOutputPosition += step
                continue
            }
            val currentIndex = sourceIndex + 1L
            while (nextOutputPosition <= currentIndex.toDouble() + 1e-9) {
                val fraction = (nextOutputPosition - sourceIndex.toDouble()).coerceIn(0.0, 1.0)
                append((prev + (safeSample - prev) * fraction.toFloat()).coerceIn(-1.0f, 1.0f))
                nextOutputPosition += step
            }
            previous = safeSample
            sourceIndex = currentIndex
        }
    }

    /**
     * Completes the half-open input duration. When the next output instant lies
     * after the last input sample but before EOF, hold the last sample rather
     * than silently shortening the decoded audio by one target frame.
     */
    fun finish() {
        if (finished) return
        val last = previous
        if (last != null) {
            val inputDurationFrames = sourceIndex.toDouble() + 1.0
            while (nextOutputPosition < inputDurationFrames - 1e-9) {
                append(last)
                nextOutputPosition += step
            }
        }
        finished = true
    }

    private fun append(value: Float) {
        check(outputSize.toLong() < maxOutputFrames) {
            "重采样输出超过允许的音频时长上限"
        }
        if (values.remaining() < Float.SIZE_BYTES) {
            val expanded = ByteBuffer.allocateDirect(
                max(values.capacity() * 2, (outputSize + 1) * Float.SIZE_BYTES)
            ).order(ByteOrder.nativeOrder())
            val previousBuffer = values
            previousBuffer.flip()
            expanded.put(previousBuffer)
            // Direct buffers can contain audio plaintext. Clear the old allocation
            // before dropping the final reference after capacity growth.
            wipe(previousBuffer)
            values = expanded
        }
        values.putFloat(value)
        outputSize++
    }

    fun toDirectBuffer(): ByteBuffer {
        check(finished) { "必须先结束重采样再读取输出" }
        return values.duplicate().apply { flip() }
    }

    private fun wipe(buffer: ByteBuffer) {
        try {
            val view = buffer.duplicate()
            view.clear()
            while (view.hasRemaining()) view.put(0)
        } catch (_: Throwable) {}
    }

    fun clear() {
        wipe(values)
        outputSize = 0
        previous = null
        sourceIndex = 0L
        nextOutputPosition = 0.0
        finished = true
    }
}
