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
import kotlin.math.abs

/** Decoded mono 16 kHz float32 PCM. The direct buffer is owned by the caller. */
data class AndroidSpeechPcm(
    val samples: ByteBuffer,
    val frameCount: Long,
    val sampleRate: Int = 16000,
    /** Presentation timestamp of the first decoded PCM frame in source-media time. */
    val startTimeUs: Long = 0L
)

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
        maxDurationMs: Long = MAX_DURATION_MS,
        sourceInUs: Long = 0L,
        sourceOutUs: Long? = null
    ): AndroidSpeechPcm {
        require(maxDurationMs in 1L..MAX_DURATION_MS) { "音频时长上限必须在 1ms–10min" }
        require(sourceInUs >= 0L) { "音频源入点不能为负数" }
        if (sourceOutUs != null) {
            require(sourceOutUs > sourceInUs) { "音频源出点必须晚于源入点" }
            require(sourceOutUs - sourceInUs <= maxDurationMs * 1000L) {
                "所选片段音频超过${maxDurationMs / 1000}秒时长上限"
            }
        }
        val extractor = MediaExtractor()
        var codec: MediaCodec? = null
        var resampler: NativeSpeechResampler? = null
        var handedOff = false
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
            if (sourceInUs > 0L) {
                extractor.seekTo(sourceInUs, MediaExtractor.SEEK_TO_PREVIOUS_SYNC)
            }
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
            var firstOutputPresentationTimeUs: Long? = null
            var expectedNextOutputPresentationTimeUs: Long? = null
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
                        if (size < 0 || timeUs < 0L || (sourceOutUs != null && timeUs >= sourceOutUs)) {
                            codec.queueInputBuffer(inputIndex, 0, 0, maxOf(0L, timeUs), MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inputEos = true
                        } else {
                            if (timeUs >= sourceInUs && timeUs - sourceInUs > maxDurationMs * 1000L) {
                                throw IllegalArgumentException("所选音频超过${maxDurationMs / 1000}秒时长上限")
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
                        if (resampler == null) resampler = NativeSpeechResampler(outputRate, TARGET_RATE)
                        else require(resampler!!.inputRate == outputRate) {
                            "解码期间音频采样率发生变化，无法安全重采样"
                        }
                    }
                    outputIndex >= 0 -> {
                        val output = codec.getOutputBuffer(outputIndex)
                        if (output != null && info.size > 0) {
                            if (firstOutputPresentationTimeUs == null) {
                                firstOutputPresentationTimeUs = info.presentationTimeUs
                                expectedNextOutputPresentationTimeUs = info.presentationTimeUs
                            } else {
                                val expectedPts = expectedNextOutputPresentationTimeUs
                                    ?: throw IllegalStateException("音频时间戳状态丢失")
                                require(abs(info.presentationTimeUs - expectedPts) <= 50_000L) {
                                    "解码音频时间戳不连续，不能安全映射字幕时间"
                                }
                            }
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
                                    val safeSample = if (sample.isFinite()) sample.coerceIn(-1.0f, 1.0f) else 0.0f
                                    sum += safeSample.toDouble()
                                }
                                mono[frame] = (sum / outputChannels).toFloat().coerceIn(-1.0f, 1.0f)
                            }
                            if (resampler == null) resampler = NativeSpeechResampler(outputRate, TARGET_RATE)
                            try {
                                resampler!!.push(mono)
                                expectedNextOutputPresentationTimeUs =
                                    info.presentationTimeUs +
                                        (mono.size.toLong() * 1_000_000L / outputRate.toLong())
                            } finally {
                                mono.fill(0.0f)
                            }
                            // Seeking to the previous sync sample may decode a short preroll
                            // before sourceInUs; allow up to 5 seconds for that data, then the
                            // worker trims by the first output PTS before inference.
                            val maxOutputFrames = (TARGET_RATE.toLong() * (maxDurationMs + 5000L) / 1000L)
                                .coerceAtLeast(1L)
                            require(resampler!!.outputSize.toLong() <= maxOutputFrames) {
                                "重采样结果超过音频时长上限（含 seek preroll）"
                            }
                        }
                        if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) outputEos = true
                        codec.releaseOutputBuffer(outputIndex, false)
                    }
                }
            }

            val completedResampler = resampler ?: throw IllegalStateException("解码器没有输出音频样本")
            completedResampler.finish()
            val samples = completedResampler.toDirectBuffer()
            val frames = samples.remaining() / Float.SIZE_BYTES
            require(frames > 0) { "解码后的音频为空" }
            val firstPts = firstOutputPresentationTimeUs
                ?: throw IllegalStateException("解码器没有提供音频时间戳")
            require(firstPts >= 0L) { "解码器返回了负数音频起始时间戳" }
            handedOff = true
            return AndroidSpeechPcm(samples, frames.toLong(), startTimeUs = firstPts)
        } finally {
            if (!handedOff) resampler?.clear()
            try { codec?.stop() } catch (_: Throwable) {}
            try { codec?.release() } catch (_: Throwable) {}
            try { extractor.release() } catch (_: Throwable) {}
        }
    }

}
