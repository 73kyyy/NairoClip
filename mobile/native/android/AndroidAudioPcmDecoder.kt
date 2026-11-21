package com.nairoclip.app

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import android.os.SystemClock
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.CancellationException
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.max

/** Decoded mono 16 kHz float32 PCM. The direct buffer is owned by the caller. */
data class AndroidSpeechPcm(val samples: ByteBuffer, val frameCount: Long, val sampleRate: Int = 16000)

/**
 * Uses Android's installed audio decoder only (MediaExtractor + MediaCodec).
 * Downmixes interleaved PCM and performs streaming linear resampling to 16 kHz.
 * Output is bounded to prevent unbounded allocations for unusually long recordings.
 */
object AndroidAudioPcmDecoder {
    private const val TARGET_RATE = 16000
    private const val MAX_DURATION_MS = 10 * 60 * 1000L
    private const val WALL_TIMEOUT_MS = 10 * 60 * 1000L
    private const val MAX_CHANNELS = 8

    fun decodeToMono16k(
        context: Context,
        uri: Uri,
        cancelled: AtomicBoolean = AtomicBoolean(false),
        maxDurationMs: Long = MAX_DURATION_MS
    ): AndroidSpeechPcm {
        require(maxDurationMs in 1L..MAX_DURATION_MS) { "音频时长上限必须在 1ms–10min" }
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        val startedAt = SystemClock.elapsedRealtime()
        try {
            extractor.setDataSource(context, uri, null)
            var audioTrack = -1
            var mime: String? = null
            for (index in 0 until extractor.trackCount) {
                val format = extractor.getTrackFormat(index)
                val candidate = format.getString(MediaFormat.KEY_MIME)
                if (candidate?.startsWith("audio/") == true) {
                    audioTrack = index
                    mime = candidate
                    break
                }
            }
            require(audioTrack >= 0 && mime != null) { "所选媒体没有可解码的音轨" }
            extractor.selectTrack(audioTrack)
            val inputFormat = extractor.getTrackFormat(audioTrack)
            val declaredRate = inputFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
            val declaredChannels = inputFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
            require(declaredRate in 8000..192000) { "音频采样率超出支持范围" }
            require(declaredChannels in 1..MAX_CHANNELS) { "音频声道数超出支持范围" }

            codec = MediaCodec.createDecoderByType(mime!!)
            codec.configure(inputFormat, null, null, 0)
            codec.start()

            var inputEos = false
            var outputEos = false
            var outputRate = declaredRate
            var outputChannels = declaredChannels
            var pcmEncoding = AudioFormat.ENCODING_PCM_16BIT
            var resampler: StreamingMonoResampler? = null
            val info = MediaCodec.BufferInfo()

            while (!outputEos) {
                if (cancelled.get()) throw CancellationException("音频解码已取消")
                if (SystemClock.elapsedRealtime() - startedAt > WALL_TIMEOUT_MS) {
                    throw IllegalStateException("音频解码超时")
                }
                if (!inputEos) {
                    val inputIndex = codec.dequeueInputBuffer(10_000L)
                    if (inputIndex >= 0) {
                        val input = codec.getInputBuffer(inputIndex)
                            ?: throw IllegalStateException("解码器输入缓冲区不可用")
                        input.clear()
                        val size = extractor.readSampleData(input, 0)
                        val timeUs = extractor.sampleTime
                        if (size < 0 || timeUs < 0L) {
                            codec.queueInputBuffer(inputIndex, 0, 0, 0L, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inputEos = true
                        } else {
                            if (timeUs > maxDurationMs * 1000L) {
                                throw IllegalArgumentException("音频超过${maxDurationMs / 1000}秒时长上限")
                            }
                            codec.queueInputBuffer(inputIndex, 0, size, timeUs, 0)
                            extractor.advance()
                        }
                    }
                }

                val outputIndex = codec.dequeueOutputBuffer(info, 10_000L)
                when {
                    outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val format = codec.outputFormat
                        outputRate = format.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        outputChannels = format.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        pcmEncoding = if (format.containsKey(MediaFormat.KEY_PCM_ENCODING)) {
                            format.getInteger(MediaFormat.KEY_PCM_ENCODING)
                        } else AudioFormat.ENCODING_PCM_16BIT
                        require(outputRate in 8000..192000) { "解码输出采样率无效" }
                        require(outputChannels in 1..MAX_CHANNELS) { "解码输出声道数无效" }
                        require(
                            pcmEncoding == AudioFormat.ENCODING_PCM_16BIT ||
                                pcmEncoding == AudioFormat.ENCODING_PCM_FLOAT ||
                                pcmEncoding == AudioFormat.ENCODING_PCM_8BIT
                        ) { "当前系统解码器输出了不支持的 PCM 位宽" }
                        if (resampler == null) resampler = StreamingMonoResampler(outputRate, TARGET_RATE)
                        else require(resampler!!.inputRate == outputRate) {
                            "解码期间音频采样率发生变化，无法安全重采样"
                        }
                    }
                    outputIndex >= 0 -> {
                        val output = codec.getOutputBuffer(outputIndex)
                        if (output != null && info.size > 0) {
                            val bytesPerSample = when (pcmEncoding) {
                                AudioFormat.ENCODING_PCM_FLOAT -> 4
                                AudioFormat.ENCODING_PCM_16BIT -> 2
                                AudioFormat.ENCODING_PCM_8BIT -> 1
                                else -> error("unsupported PCM")
                            }
                            val frameBytes = bytesPerSample * outputChannels
                            require(info.offset >= 0 && info.size >= 0 && info.size % frameBytes == 0) {
                                "解码器返回了不完整的 PCM 帧"
                            }
                            val view = output.duplicate().order(ByteOrder.LITTLE_ENDIAN)
                            view.position(info.offset)
                            view.limit(info.offset + info.size)
                            val mono = FloatArray(info.size / frameBytes)
                            for (frame in mono.indices) {
                                var sum = 0.0
                                repeat(outputChannels) {
                                    val sample = when (pcmEncoding) {
                                        AudioFormat.ENCODING_PCM_FLOAT -> view.float
                                        AudioFormat.ENCODING_PCM_16BIT -> view.short / 32768.0f
                                        AudioFormat.ENCODING_PCM_8BIT -> ((view.get().toInt() and 0xff) - 128) / 128.0f
                                        else -> 0.0f
                                    }
                                    sum += sample.toDouble()
                                }
                                mono[frame] = (sum / outputChannels).toFloat().coerceIn(-1.0f, 1.0f)
                            }
                            if (resampler == null) resampler = StreamingMonoResampler(outputRate, TARGET_RATE)
                            resampler!!.push(mono)
                            val maxOutputFrames = (TARGET_RATE.toLong() * maxDurationMs / 1000L).coerceAtLeast(1L)
                            require(resampler!!.outputSize.toLong() <= maxOutputFrames) {
                                "重采样结果超过音频时长上限"
                            }
                        }
                        if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outputEos = true
                        codec.releaseOutputBuffer(outputIndex, false)
                    }
                }
            }

            val samples = resampler?.toDirectBuffer() ?: throw IllegalStateException("解码器没有输出音频样本")
            val frames = samples.remaining() / Float.SIZE_BYTES
            require(frames > 0) { "解码后的音频为空" }
            return AndroidSpeechPcm(samples, frames.toLong())
        } finally {
            try { codec?.stop() } catch (_: Throwable) {}
            try { codec?.release() } catch (_: Throwable) {}
            try { extractor.release() } catch (_: Throwable) {}
        }
    }

    private class StreamingMonoResampler(val inputRate: Int, private val outputRate: Int) {
        private val step = inputRate.toDouble() / outputRate.toDouble()
        private var sourceIndex = 0L
        private var nextOutputPosition = 0.0
        private var previous: Float? = null
        private var values = ByteBuffer.allocateDirect(4096 * Float.SIZE_BYTES).order(ByteOrder.nativeOrder())
        var outputSize: Int = 0
            private set

        fun push(input: FloatArray) {
            for (sample in input) {
                val prev = previous
                if (prev == null) {
                    previous = sample
                    append(sample)
                    nextOutputPosition += step
                    continue
                }
                val currentIndex = sourceIndex + 1L
                while (nextOutputPosition <= currentIndex.toDouble() + 1e-9) {
                    val fraction = (nextOutputPosition - sourceIndex.toDouble()).coerceIn(0.0, 1.0)
                    append((prev + (sample - prev) * fraction.toFloat()).coerceIn(-1.0f, 1.0f))
                    nextOutputPosition += step
                }
                previous = sample
                sourceIndex = currentIndex
            }
        }

        private fun append(value: Float) {
            check(outputSize < outputRate * 600) { "重采样输出超过 10 分钟安全上限" }
            if (values.remaining() < Float.SIZE_BYTES) {
                val expanded = ByteBuffer.allocateDirect(max(values.capacity() * 2, (outputSize + 1) * Float.SIZE_BYTES))
                    .order(ByteOrder.nativeOrder())
                values.flip()
                expanded.put(values)
                values = expanded
            }
            values.putFloat(value)
            outputSize++
        }

        fun toDirectBuffer(): ByteBuffer {
            values.flip()
            return values
        }
    }
}
