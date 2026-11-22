package com.nairoclip.app

/**
 * Validates decoded PCM buffer timestamps against the cumulative sample clock.
 *
 * The expected clock is anchored to the first output PTS and total decoded
 * frames, not the previous buffer's reported PTS. Small codec timestamp
 * quantization is tolerated, but a real gap/overlap must not be silently
 * concatenated because doing so shifts every following subtitle timestamp.
 */
internal class NativeSpeechPtsTracker(
    private val sampleRate: Int,
    private val toleranceUs: Long = DEFAULT_TOLERANCE_US
) {
    private var firstPtsUs: Long? = null
    private var decodedFrames = 0L

    init {
        require(sampleRate in 8000..192000)
        require(toleranceUs >= 0L)
    }

    fun accept(presentationTimeUs: Long, frameCount: Long) {
        require(presentationTimeUs >= 0L) { "解码器返回了负数音频时间戳" }
        require(frameCount > 0L) { "解码器返回了空 PCM 帧" }
        val first = firstPtsUs
        if (first == null) {
            firstPtsUs = presentationTimeUs
        } else {
            val elapsedUs = try {
                Math.multiplyExact(decodedFrames, 1_000_000L) / sampleRate.toLong()
            } catch (_: ArithmeticException) {
                throw IllegalStateException("解码音频累计帧数溢出")
            }
            val expectedPtsUs = try {
                Math.addExact(first, elapsedUs)
            } catch (_: ArithmeticException) {
                throw IllegalStateException("解码音频时间戳溢出")
            }
            val deltaUs = try {
                Math.subtractExact(presentationTimeUs, expectedPtsUs)
            } catch (_: ArithmeticException) {
                throw IllegalStateException("解码音频时间戳差值溢出")
            }
            require(deltaUs in -toleranceUs..toleranceUs) {
                "解码音频时间戳不连续（偏差 ${deltaUs}us），不能安全映射字幕时间"
            }
        }
        decodedFrames = try {
            Math.addExact(decodedFrames, frameCount)
        } catch (_: ArithmeticException) {
            throw IllegalStateException("解码音频累计帧数溢出")
        }
    }

    fun firstPresentationTimeUs(): Long =
        firstPtsUs ?: throw IllegalStateException("解码器没有输出音频时间戳")

    companion object {
        /** Covers codec timestamp rounding without hiding audible discontinuities. */
        const val DEFAULT_TOLERANCE_US = 2_000L
    }
}
