package com.nairoclip.app

import android.util.Log
import org.json.JSONObject
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Owns one independent Whisper session and serializes model loading / inference.
 *
 * All native inference and model loading happen on a dedicated worker, never on
 * the UI thread or under the editor state lock. The caller must keep direct
 * model/PCM buffers valid until the corresponding completion callback.
 *
 * close() requests cooperative cancellation and joins the worker before
 * destroying the native handle. If inference does not stop within the bounded
 * wait, the handle is deliberately retained rather than risking native
 * use-after-free; the worker is daemonized so it cannot keep the process alive.
 */
class NativeSpeechSessionController(
    private val bridge: NativeEngineBridge,
    private val listener: Listener,
    private val workerName: String = "NairoClip-ASR"
) : AutoCloseable {

    data class Status(
        val state: Int,
        val progress: Double,
        val message: String
    ) {
        val isRunning: Boolean get() = state == STATE_RUNNING
        val isLoaded: Boolean get() = state == STATE_LOADED ||
            state == STATE_RUNNING || state == STATE_SUCCEEDED
    }

    interface Listener {
        /** Invoked on the ASR worker thread; marshal to Main when updating Compose. */
        fun onStatus(status: Status) {}
        fun onLoaded() {}
        fun onResult(segmentsJson: String) {}
        fun onFailure(message: String, retryable: Boolean) {}
        fun onCancelled() {}
    }

    private val lock = Any()
    private val closed = AtomicBoolean(false)
    private val worker: ExecutorService = Executors.newSingleThreadExecutor { task ->
        Thread(task, workerName).apply { isDaemon = true }
    }
    @Volatile private var session: Long = bridge.createSpeechSession()
    @Volatile private var inferenceActive = false
    @Volatile private var loaded = false

    init {
        check(session != 0L) { "无法创建独立语音识别会话" }
    }

    /** Queue model loading. Buffers must be direct and remain valid until callback. */
    fun load(
        encoder: ByteBuffer,
        decoder: ByteBuffer,
        tokenizerJson: ByteBuffer
    ): Boolean = synchronized(lock) {
        if (closed.get() || session == 0L || inferenceActive) return false
        if (!encoder.isDirect || !decoder.isDirect || !tokenizerJson.isDirect ||
            encoder.isReadOnly || decoder.isReadOnly || tokenizerJson.isReadOnly ||
            !encoder.hasRemaining() || !decoder.hasRemaining() || !tokenizerJson.hasRemaining()) return false
        inferenceActive = true
        val encoderView = encoder.slice()
        val decoderView = decoder.slice()
        val tokenizerView = tokenizerJson.slice()
        try {
            worker.execute {
                try {
                    val response = JSONObject(
                        bridge.loadSpeechSession(session, encoderView, decoderView, tokenizerView)
                    )
                    if (response.optBoolean("ok", false)) {
                        loaded = true
                        if (!closed.get()) listener.onLoaded()
                        publishStatus()
                    } else {
                        loaded = false
                        if (!closed.get()) {
                            listener.onFailure(response.optString("message", "Whisper 模型加载失败"), true)
                        }
                        publishStatus()
                    }
                } catch (error: Throwable) {
                    loaded = false
                    if (!closed.get()) listener.onFailure(error.message ?: "Whisper 模型加载异常", true)
                } finally {
                    zeroBuffer(encoder)
                    zeroBuffer(decoder)
                    zeroBuffer(tokenizerJson)
                    inferenceActive = false
                }
            }
            true
        } catch (_: java.util.concurrent.RejectedExecutionException) {
            inferenceActive = false
            false
        }
    }

    /** Queue one mono float32 PCM inference; PCM is normally 16 kHz, mono, [-1,1]. */
    fun transcribe(
        pcm: ByteBuffer,
        frameCount: Long,
        sampleRate: Int = 16000,
        language: Int = LANGUAGE_AUTO
    ): Boolean = synchronized(lock) {
        if (closed.get() || session == 0L || inferenceActive || !loaded) return false
        if (!pcm.isDirect || pcm.isReadOnly || frameCount <= 0L || sampleRate != 16000 ||
            frameCount > pcm.remaining().toLong() / Float.SIZE_BYTES ||
            language !in LANGUAGE_AUTO..LANGUAGE_KOREAN) return false
        inferenceActive = true
        val pcmView = pcm.slice().order(java.nio.ByteOrder.nativeOrder())
        try {
            worker.execute {
                try {
                    val response = JSONObject(
                        bridge.transcribeSpeechSession(session, pcmView, frameCount, sampleRate, language)
                    )
                    if (response.optBoolean("ok", false)) {
                        if (!closed.get()) listener.onResult(response.optJSONArray("segments")?.toString() ?: "[]")
                    } else {
                        val state = readStatus()
                        if (!closed.get() && state.state == STATE_CANCELLED) listener.onCancelled()
                        else if (!closed.get()) listener.onFailure(
                            response.optString("message", "语音识别失败"),
                            retryable = state.state != STATE_CANCELLED
                        )
                    }
                    publishStatus()
                } catch (error: Throwable) {
                    if (!closed.get()) listener.onFailure(error.message ?: "语音识别异常", true)
                } finally {
                    zeroBuffer(pcm)
                    inferenceActive = false
                }
            }
            true
        } catch (_: java.util.concurrent.RejectedExecutionException) {
            inferenceActive = false
            false
        }
    }

    /** Best-effort overwrite of the complete caller-owned direct-buffer view. */
    private fun zeroBuffer(buffer: ByteBuffer) {
        if (!buffer.isDirect || buffer.isReadOnly) return
        try {
            val view = buffer.duplicate()
            view.clear()
            while (view.hasRemaining()) view.put(0)
        } catch (_: Throwable) {
            // Native/runtime copies and allocator remnants are outside this buffer's ownership.
        }
    }

    /** Cooperative cancellation; completion is reported separately. */
    fun cancel(): Boolean {
        if (closed.get() || !inferenceActive || session == 0L) return false
        return try {
            bridge.cancelSpeechSession(session)
        } catch (_: Throwable) {
            false
        }
    }

    /** Safe to poll from UI; native status polling does not wait for inference. */
    fun status(): Status = readStatus()

    private fun publishStatus() {
        if (!closed.get()) listener.onStatus(readStatus())
    }

    private fun readStatus(): Status {
        val handle = session
        if (handle == 0L) return Status(STATE_UNLOADED, 0.0, "会话已关闭")
        return try {
            val json = JSONObject(bridge.speechSessionStatus(handle))
            Status(
                state = json.optInt("state", STATE_UNLOADED),
                progress = json.optDouble("progress", 0.0).coerceIn(0.0, 1.0),
                message = json.optString("message", "")
            )
        } catch (error: Throwable) {
            Status(STATE_UNLOADED, 0.0, error.message ?: "状态读取失败")
        }
    }

    /**
     * Must be called from a lifecycle owner, not the inference worker itself.
     * Native destruction is only legal after the worker has fully terminated.
     */
    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        val handle = session
        if (handle == 0L) return
        if (inferenceActive) {
            try { bridge.cancelSpeechSession(handle) } catch (_: Throwable) {}
        }
        worker.shutdown()
        var terminated = false
        try {
            terminated = worker.awaitTermination(CLOSE_WAIT_SECONDS, TimeUnit.SECONDS)
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
        }
        if (terminated) {
            session = 0L
            try {
                bridge.destroySpeechSession(handle)
            } catch (error: Throwable) {
                Log.e(TAG, "销毁语音会话失败", error)
            }
        } else {
            // Intentional bounded leak: destroying a handle still used by JNI is unsafe.
            Log.e(TAG, "ASR 未在关闭期限内退出；保留 native session 以避免 use-after-free")
        }
    }

    companion object {
        const val STATE_UNLOADED = 0
        const val STATE_LOADED = 1
        const val STATE_RUNNING = 2
        const val STATE_SUCCEEDED = 3
        const val STATE_FAILED = 4
        const val STATE_CANCELLED = 5

        const val LANGUAGE_AUTO = 0
        const val LANGUAGE_CHINESE = 1
        const val LANGUAGE_ENGLISH = 2
        const val LANGUAGE_JAPANESE = 3
        const val LANGUAGE_KOREAN = 4

        private const val CLOSE_WAIT_SECONDS = 30L
        private const val TAG = "NcSpeechSession"
    }
}
