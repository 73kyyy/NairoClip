package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteBuffer
import java.nio.ByteOrder

class NativeSpeechPcmCropperTest {
    private fun pcm(frameCount: Int, startTimeUs: Long): AndroidSpeechPcm {
        val buffer = ByteBuffer.allocateDirect(frameCount * Float.SIZE_BYTES)
            .order(ByteOrder.nativeOrder())
        val floats = buffer.asFloatBuffer()
        repeat(frameCount) { floats.put(it.toFloat() / frameCount.toFloat()) }
        return AndroidSpeechPcm(buffer, frameCount.toLong(), 16000, startTimeUs)
    }

    private fun range() = NativeSpeechSourceRange(
        timelineStartUs = 2_000_000L,
        timelineDurationUs = 500_000L,
        sourceInUs = 250_000L,
        sourceOutUs = 750_000L,
        speedNumerator = 1L,
        speedDenominator = 1L
    )

    @Test
    fun trimsWholeFilePcmToSelectedSourceInterval() {
        val cropped = NativeSpeechPcmCropper.trim(pcm(16_000, 0L), range())
        assertEquals(8_000L, cropped.frameCount)
        assertEquals(250_000L, cropped.startTimeUs)
        val samples = cropped.samples.duplicate().order(ByteOrder.nativeOrder()).asFloatBuffer()
        assertEquals(0.25f, samples.get(0), 0.000001f)
        assertEquals(11_999.0f / 16_000.0f, samples.get(7_999), 0.000001f)
        assertTrue(cropped.samples.isDirect)
    }

    @Test
    fun trimsSeekPrerollUsingFirstOutputPresentationTimestamp() {
        val cropped = NativeSpeechPcmCropper.trim(pcm(16_000, 100_000L), range())
        assertEquals(8_000L, cropped.frameCount)
        val samples = cropped.samples.duplicate().order(ByteOrder.nativeOrder()).asFloatBuffer()
        assertEquals(2_400.0f / 16_000.0f, samples.get(0), 0.000001f)
        assertEquals(10_399.0f / 16_000.0f, samples.get(7_999), 0.000001f)
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsDecoderThatSkippedTheRequestedSourceInPoint() {
        NativeSpeechPcmCropper.trim(pcm(16_000, 300_000L), range())
    }

    @Test(expected = IllegalArgumentException::class)
    fun rejectsDecoderThatDidNotCoverTheRequestedSourceOutPoint() {
        NativeSpeechPcmCropper.trim(pcm(8_000, 0L), range())
    }
}
