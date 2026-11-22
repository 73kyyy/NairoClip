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
 * close() queues native destruction after all accepted worker operations, then waits for a
 * bounded interval. If inference ignores cancellation, destruction remains queued and runs
 * only after JNI returns, avoiding both use-after-free and a permanent timeout-only leak.
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
            // The task never took ownership of these plaintext buffers.
            // Still clear them because the controller has rejected this load request.
            zeroBuffer(encoder)
            zeroBuffer(decoder)
            zeroBuffer(tokenizerJson)
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
            // A rejected request will never reach JNI; do not leave caller PCM plaintext.
            zeroBuffer(pcm)
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
        // Serialize close against load()/transcribe() submission. Without this lock,
        // close could enqueue destruction between a caller's state check and execute(),
        // allowing that accepted JNI task to run after the native handle was destroyed.
        val handle = synchronized(lock) {
            if (!closed.compareAndSet(false, true)) return
            val current = session
            if (current == 0L) return
            if (inferenceActive) {
                try { bridge.cancelSpeechSession(current) } catch (_: Throwable) {}
            }
            try {
                worker.execute {
                    try {
                        bridge.destroySpeechSession(current)
                    } catch (error: Throwable) {
                        Log.e(TAG, "销毁语音会话失败", error)
                    } finally {
                        session = 0L
                        loaded = false
                    }
                }
                worker.shutdown()
            } catch (error: Throwable) {
                // A rejected finalizer means we cannot prove the handle is no longer in use.
                Log.e(TAG, "无法排队销毁 Whisper 会话；保留 native handle 以避免 use-after-free", error)
                worker.shutdown()
                return
            }
            current
        }
        try {
            if (!worker.awaitTermination(CLOSE_WAIT_SECONDS, TimeUnit.SECONDS)) {
                // The queued finalizer will still run after an in-flight JNI call returns.
                Log.w(TAG, "ASR 关闭等待超时；销毁操作仍排队等待 JNI 安全退出")
            }
        } catch (_: InterruptedException) {
            Thread.currentThread().interrupt()
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
