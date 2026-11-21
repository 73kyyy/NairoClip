package com.nairoclip.app

import android.content.Context
import android.net.Uri
import org.json.JSONObject
import java.nio.ByteBuffer
import java.util.concurrent.CompletableFuture
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ScheduledExecutorService
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean

/**
 * Owns one independent Whisper session and serializes every operation against its lifetime.
 *
 * The native recognizer is synchronous, so inference always runs on [worker], never the UI
 * thread. Status polling uses a separate executor because the inference call occupies worker.
 * close() queues native destruction behind any active operation; it never destroys a handle
 * while native code may still be using it.
 *
 * Model buffers passed to load() are treated as sensitive plaintext and zeroed (best effort)
 * after the native call returns. The caller remains responsible for its original buffers if
 * it passed slices/duplicates that share different backing storage.
 */
class NativeSpeechSessionWorker(
    private val bridge: NativeEngineBridge,
    private val listener: Listener? = null,
    private val pollIntervalMs: Long = 250L
) : AutoCloseable {

    interface Listener {
        /** Called on the status-poller thread. Dispatch to the main thread before touching UI. */
        fun onStatus(state: Int, progress: Double, message: String)
    }

    private val worker: ExecutorService =
        Executors.newSingleThreadExecutor { r -> Thread(r, "nc-whisper-worker").apply { isDaemon = true } }
    private val poller: ScheduledExecutorService =
        Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "nc-whisper-status").apply { isDaemon = true } }
    private val lifecycleLock = Any()
    private var session: Long = 0L
    private val closing = AtomicBoolean(false)
    private val closed = AtomicBoolean(false)
    private val busy = AtomicBoolean(false)
    private val cancellationRequested = AtomicBoolean(false)

    init {
        require(pollIntervalMs in 100L..5000L) { "状态轮询间隔必须在 100–5000ms" }
        session = bridge.createSpeechSession()
        require(session != 0L) { "无法创建 Whisper 会话" }
        poller.scheduleWithFixedDelay({
            if (busy.get()) publishStatus()
        }, 0L, pollIntervalMs, TimeUnit.MILLISECONDS)
    }

    /** Loads decrypted model bytes. All three buffers must be direct buffers. */
    fun load(
        encoder: ByteBuffer,
        decoder: ByteBuffer,
        tokenizer: ByteBuffer
    ): CompletableFuture<JSONObject> = submit {
        try {
            require(encoder.isDirect && decoder.isDirect && tokenizer.isDirect) {
                "模型缓冲区必须是 direct ByteBuffer"
            }
            JSONObject(bridge.loadSpeechSession(session, encoder, decoder, tokenizer)).also {
                check(it.optBoolean("ok", false)) {
                    it.optString("message").ifBlank { "Whisper 模型加载失败" }
                }
            }
        } finally {
            zeroRemaining(encoder)
            zeroRemaining(decoder)
            zeroRemaining(tokenizer)
        }
    }

    /** Runs mono float32 PCM inference. Input is cleared best-effort after native returns. */
    fun transcribe(
        pcm: ByteBuffer,
        frameCount: Long,
        sampleRate: Int = 16000,
        language: Int = 0,
        clearPcmAfterUse: Boolean = true
    ): CompletableFuture<JSONObject> {
        return submit {
            try {
                require(pcm.isDirect) { "PCM 必须是 direct ByteBuffer" }
                require(frameCount > 0L && sampleRate > 0) { "PCM 帧数或采样率无效" }
                require(frameCount <= pcm.remaining().toLong() / Float.SIZE_BYTES) {
                    "PCM 剩余容量不足"
                }
                JSONObject(bridge.transcribeSpeechSession(session, pcm, frameCount, sampleRate, language)).also {
                    check(it.optBoolean("ok", false)) {
                        it.optString("message").ifBlank { "语音识别失败" }
                    }
                }
            } finally {
                if (clearPcmAfterUse) zeroRemaining(pcm)
            }
        }
    }

    /** Decode selected media, transcribe it, and insert all recognized cues as one undo step. */
    fun decodeTranscribeAndInsert(
        context: Context,
        uri: Uri,
        timelineStartUs: Long,
        adapter: NativeEditorCommandAdapter,
        language: Int = 0
    ): CompletableFuture<NativeEditorCommandAdapter.EditorStateSnapshot> {
        require(timelineStartUs >= 0L) { "字幕起始时间不能为负数" }
        return submit {
            val decoded = AndroidAudioPcmDecoder.decodeToMono16k(context.applicationContext, uri, cancellationRequested)
            try {
                check(!cancellationRequested.get()) { "语音识别已取消" }
                val response = JSONObject(bridge.transcribeSpeechSession(
                    session, decoded.samples, decoded.frameCount, decoded.sampleRate, language
                ))
                check(response.optBoolean("ok", false)) {
                    response.optString("message").ifBlank { "语音识别失败" }
                }
                val segments = response.optJSONArray("segments") ?: throw IllegalStateException("识别结果缺少 segments")
                val audioDurationSeconds = decoded.frameCount.toDouble() / decoded.sampleRate.toDouble()
                require(audioDurationSeconds.isFinite() && audioDurationSeconds > 0.0)
                require(segments.length() in 1..10000) { "识别结果没有有效字幕或数量超限" }
                val cues = ArrayList<NativeEditorCommandAdapter.SubtitleCueInput>(segments.length())
                for (index in 0 until segments.length()) {
                    val segment = segments.optJSONObject(index) ?: throw IllegalArgumentException("识别片段格式无效")
                    val start = segment.optDouble("start", Double.NaN)
                    val end = segment.optDouble("end", Double.NaN)
                    val text = segment.optString("text").trim()
                    require(start.isFinite() && end.isFinite() && start >= 0.0 && end > start &&
                        start < audioDurationSeconds && end <= audioDurationSeconds + 0.05 && text.isNotEmpty()) {
                        "识别片段时间超出音频长度或文本无效"
                    }
                    val boundedEnd = minOf(end, audioDurationSeconds)
                    val startDelta = (start * 1_000_000.0).toLong()
                    val endDelta = (boundedEnd * 1_000_000.0).toLong()
                    require(startDelta >= 0L && endDelta > startDelta && timelineStartUs <= Long.MAX_VALUE - endDelta) {
                        "识别片段时间溢出"
                    }
                    cues.add(NativeEditorCommandAdapter.SubtitleCueInput(
                        timelineStartUs + startDelta, endDelta - startDelta, text
                    ))
                }
                val inserted = synchronized(lifecycleLock) {
                    check(!closing.get() && !cancellationRequested.get()) { "字幕写入前任务已取消或会话正在关闭" }
                    adapter.addSubtitlesBatchAndReadEditorState(cues)
                }
                check(inserted.ok) { inserted.error ?: "自动字幕写入失败" }
                inserted.value ?: throw IllegalStateException("自动字幕写入后未返回工程快照")
            } finally {
                zeroRemaining(decoded.samples)
            }
        }
    }

    /** Cooperative cancellation; returns false when native inference is not cancellable. */
    fun cancel(): Boolean = synchronized(lifecycleLock) {
        if (closing.get()) return@synchronized false
        cancellationRequested.set(true)
        try {
            bridge.cancelSpeechSession(session)
        } catch (_: Throwable) {
            false
        }
    }

    private fun <T> submit(operation: () -> T): CompletableFuture<T> {
        val result = CompletableFuture<T>()
        synchronized(lifecycleLock) {
            if (closing.get()) {
                result.completeExceptionally(IllegalStateException("Whisper 会话正在关闭"))
                return result
            }
            if (!busy.compareAndSet(false, true)) {
                result.completeExceptionally(IllegalStateException("Whisper 会话已有任务运行"))
                return result
            }
            cancellationRequested.set(false)
            try {
                worker.execute {
                    try {
                        check(!closing.get()) { "Whisper 会话正在关闭" }
                        val value = operation()
                        busy.set(false)
                        publishStatus()
                        result.complete(value)
                    } catch (t: Throwable) {
                        busy.set(false)
                        publishStatus()
                        result.completeExceptionally(t)
                    }
                }
            } catch (t: Throwable) {
                busy.set(false)
                result.completeExceptionally(t)
            }
        }
        return result
    }

    private fun publishStatus() {
        try {
            val raw = synchronized(lifecycleLock) {
                if (closing.get() || closed.get()) return
                bridge.speechSessionStatus(session)
            }
            val json = JSONObject(raw)
            if (!json.optBoolean("ok", false)) return
            val state = json.optInt("state", 0)
            listener?.onStatus(
                state,
                json.optDouble("progress", 0.0).coerceIn(0.0, 1.0),
                json.optString("message")
            )
        } catch (_: Throwable) {
            // A transient status read must not interrupt inference or tear down the session.
        }
    }

    private fun zeroRemaining(buffer: ByteBuffer) {
        if (!buffer.isDirect || buffer.isReadOnly) return
        try {
            val view = buffer.duplicate()
            while (view.hasRemaining()) view.put(0)
        } catch (_: Throwable) {
            // Best effort only; JVM/ORT/native copies and allocator remnants cannot be erased here.
        }
    }

    /**
     * Cancels cooperatively, then destroys the native handle only after queued/in-flight work
     * has left JNI. If inference ignores cancellation, destruction safely waits rather than
     * racing a live native call.
     */
    override fun close() {
        synchronized(lifecycleLock) {
            if (!closing.compareAndSet(false, true)) return
            cancellationRequested.set(true)
            try { bridge.cancelSpeechSession(session) } catch (_: Throwable) {}
            poller.shutdownNow()
            try {
                worker.execute {
                    try {
                        if (closed.compareAndSet(false, true)) bridge.destroySpeechSession(session)
                    } finally {
                        worker.shutdown()
                    }
                }
            } catch (_: Throwable) {
                // Never destroy concurrently with a possible native operation.
            }
        }
    }
}
