package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.nio.ByteOrder

class NativeSpeechResamplerTest {
    @Test
    fun upsamplesEightKhzAndFlushesTheFinalHalfFrame() {
        val resampler = NativeSpeechResampler(8000)
        resampler.push(floatArrayOf(0.0f, 1.0f))
        resampler.finish()

        assertEquals(4, resampler.outputSize)
        val output = resampler.toDirectBuffer().order(ByteOrder.nativeOrder()).asFloatBuffer()
        assertEquals(0.0f, output.get(0), 0.000001f)
        assertEquals(0.5f, output.get(1), 0.000001f)
        assertEquals(1.0f, output.get(2), 0.000001f)
        assertEquals(1.0f, output.get(3), 0.000001f)
    }

    @Test
    fun oneSecondAtEightKhzProducesExactlySixteenThousandFrames() {
        val resampler = NativeSpeechResampler(8000)
        resampler.push(FloatArray(8000) { 0.25f })
        resampler.finish()
        assertEquals(16000, resampler.outputSize)
        assertEquals(16000, resampler.toDirectBuffer().remaining() / Float.SIZE_BYTES)
    }

    @Test
    fun chunkBoundariesDoNotChangeResampledOutput() {
        val input = FloatArray(4410) { index -> (index % 100) / 100.0f }
        val whole = NativeSpeechResampler(44100)
        whole.push(input)
        whole.finish()

        val chunked = NativeSpeechResampler(44100)
        chunked.push(input.copyOfRange(0, 1))
        chunked.push(input.copyOfRange(1, 997))
        chunked.push(input.copyOfRange(997, 3001))
        chunked.push(input.copyOfRange(3001, input.size))
        chunked.finish()

        assertEquals(whole.outputSize, chunked.outputSize)
        val expected = whole.toDirectBuffer().order(ByteOrder.nativeOrder()).asFloatBuffer()
        val actual = chunked.toDirectBuffer().order(ByteOrder.nativeOrder()).asFloatBuffer()
        while (expected.hasRemaining()) {
            assertEquals(expected.get(), actual.get(), 0.000001f)
        }
        assertTrue(!actual.hasRemaining())
    }

    @Test
    fun downsamplingFortyEightKhzPreservesExactOneSecondFrameCount() {
        val resampler = NativeSpeechResampler(48000)
        resampler.push(FloatArray(48000) { 0.1f })
        resampler.finish()
        assertEquals(16000, resampler.outputSize)
    }
}
