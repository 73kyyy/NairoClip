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
    private val modelLoaded = AtomicBoolean(false)
    private val cancellationRequested = AtomicBoolean(false)
    // Once the engine transaction begins it cannot be cancelled safely; report that boundary honestly.
    private val subtitleCommitStarted = AtomicBoolean(false)

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
    ): CompletableFuture<JSONObject> {
        val future = submit {
            require(
                encoder.isDirect && decoder.isDirect && tokenizer.isDirect &&
                    !encoder.isReadOnly && !decoder.isReadOnly && !tokenizer.isReadOnly &&
                    encoder.hasRemaining() && decoder.hasRemaining() && tokenizer.hasRemaining()
            ) {
                "模型缓冲区必须是可写、非空 direct ByteBuffer"
            }
            // JNI GetDirectBufferAddress points at the buffer view's base address,
            // not its current position. Slice so native sees exactly the remaining bytes.
            modelLoaded.set(false)
            JSONObject(bridge.loadSpeechSession(
                session, encoder.slice(), decoder.slice(), tokenizer.slice()
            )).also {
                check(it.optBoolean("ok", false)) {
                    it.optString("message").ifBlank { "Whisper 模型加载失败" }
                }
                modelLoaded.set(true)
            }
        }
        // Also wipe buffers when submit() rejects the task before its body can run.
        return future.whenComplete { _, _ ->
            zeroRemaining(encoder)
            zeroRemaining(decoder)
            zeroRemaining(tokenizer)
        }
    }

    /** Runs mono float32 PCM inference. Input is cleared best-effort after completion. */
    fun transcribe(
        pcm: ByteBuffer,
        frameCount: Long,
        sampleRate: Int = 16000,
        language: Int = 0,
        clearPcmAfterUse: Boolean = true
    ): CompletableFuture<JSONObject> {
        val future = submit {
            check(modelLoaded.get()) { "Whisper 模型尚未成功加载" }
            require(pcm.isDirect && (!clearPcmAfterUse || !pcm.isReadOnly)) {
                "PCM 必须是 direct ByteBuffer；启用清理时还必须可写"
            }
            require(frameCount > 0L) { "PCM 帧数必须大于 0" }
            require(sampleRate == 16000) { "Whisper 输入必须为 16 kHz 单声道 PCM" }
            require(language in 0..4) { "语音识别语言代码无效" }
            require(frameCount <= pcm.remaining().toLong() / Float.SIZE_BYTES) {
                "PCM 剩余容量不足"
            }
            // JNI GetDirectBufferAddress ignores ByteBuffer.position(); pass a
            // zero-based view whose capacity equals the caller's remaining PCM bytes.
            val pcmView = pcm.slice().order(java.nio.ByteOrder.nativeOrder())
            JSONObject(bridge.transcribeSpeechSession(session, pcmView, frameCount, sampleRate, language)).also {
                check(it.optBoolean("ok", false)) {
                    it.optString("message").ifBlank { "语音识别失败" }
                }
            }
        }
        // The completion hook also runs for busy/closing rejection, where the worker
        // operation body never starts and an in-body finally would not be reached.
        return if (clearPcmAfterUse) {
            future.whenComplete { _, _ -> zeroRemaining(pcm) }
        } else {
            future
        }
    }

    /** Decode selected media, transcribe it, and insert all recognized cues as one undo step. */
    fun decodeTranscribeAndInsert(
        context: Context,
        uri: Uri,
        sourceRange: NativeSpeechSourceRange,
        adapter: NativeEditorCommandAdapter,
        language: Int = 0
    ): CompletableFuture<NativeEditorCommandAdapter.EditorStateSnapshot> {
        require(sourceRange.validationError() == null) {
            sourceRange.validationError() ?: "自动字幕源区间无效"
        }
        require(language in 0..4) { "语音识别语言代码无效" }
        return submit {
            check(modelLoaded.get()) { "Whisper 模型尚未成功加载" }
            val decoded = AndroidAudioPcmDecoder.decodeToMono16k(
                context.applicationContext,
                uri,
                cancellationRequested,
                sourceInUs = sourceRange.sourceInUs,
                sourceOutUs = sourceRange.sourceOutUs
            )
            val pcm = try {
                NativeSpeechPcmCropper.trim(decoded, sourceRange)
            } finally {
                zeroRemaining(decoded.samples)
            }
            try {
                check(!cancellationRequested.get()) { "语音识别已取消" }
                require(pcm.samples.isDirect && !pcm.samples.isReadOnly) {
                    "所选源区间 PCM 必须是可写 direct ByteBuffer"
                }
                require(pcm.sampleRate == 16000 && pcm.frameCount > 0L) {
                    "所选源区间 PCM 必须为非空 16 kHz 单声道音频"
                }
                require(pcm.frameCount <= pcm.samples.remaining().toLong() / Float.SIZE_BYTES) {
                    "所选源区间 PCM 帧数与缓冲区容量不一致"
                }
                // JNI GetDirectBufferAddress uses the view base, not the current
                // position. Keep the same remaining-range contract as transcribe().
                val pcmView = pcm.samples.slice().order(java.nio.ByteOrder.nativeOrder())
                val response = JSONObject(bridge.transcribeSpeechSession(
                    session, pcmView, pcm.frameCount, pcm.sampleRate, language
                ))
                check(response.optBoolean("ok", false)) {
                    response.optString("message").ifBlank { "语音识别失败" }
                }
                val segments = response.optJSONArray("segments") ?: throw IllegalStateException("识别结果缺少 segments")
                val audioDurationSeconds = pcm.frameCount.toDouble() / pcm.sampleRate.toDouble()
                val audioDurationUs = pcm.frameCount * 1_000_000L / pcm.sampleRate.toLong()
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
                        "识别片段时间超出所选音频区间或文本无效"
                    }
                    val audioStartOffsetUs = pcm.startTimeUs - sourceRange.sourceInUs
                    require(audioStartOffsetUs >= 0L) { "裁剪后 PCM 起始时间早于所选源入点" }
                    val mapped = sourceRange.mapSegment(
                        start,
                        minOf(end, audioDurationSeconds),
                        audioDurationUs,
                        audioStartOffsetUs
                    ) ?: throw IllegalArgumentException("识别片段无法映射到所选时间轴区间")
                    cues.add(NativeEditorCommandAdapter.SubtitleCueInput(mapped.first, mapped.second, text))
                }
                val inserted = synchronized(lifecycleLock) {
                    check(!closing.get() && !cancellationRequested.get()) { "字幕写入前任务已取消或会话正在关闭" }
                    // The batch is atomic but not interruptible once dispatched to the engine.
                    subtitleCommitStarted.set(true)
                    adapter.addSubtitlesBatchAndReadEditorState(cues)
                }
                check(inserted.ok) { inserted.error ?: "自动字幕写入失败" }
                inserted.value ?: throw IllegalStateException("自动字幕写入后未返回工程快照")
            } finally {
                zeroRemaining(pcm.samples)
            }
        }
    }

    /** Cooperative cancellation; returns false when native inference is not cancellable. */
    fun cancel(): Boolean = synchronized(lifecycleLock) {
        if (closing.get() || !busy.get() || subtitleCommitStarted.get()) return@synchronized false
        // The cooperative token cancels MediaCodec decoding and prevents subtitle
        // insertion. Once the engine batch starts, cancellation is no longer accepted.
        cancellationRequested.set(true)
        try { bridge.cancelSpeechSession(session) } catch (_: Throwable) {}
        // A request is accepted once the worker has recorded the token. Native
        // cancellation may return false during decode; the worker still observes it.
        true
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
            subtitleCommitStarted.set(false)
            try {
                worker.execute {
                    try {
                        check(!closing.get()) { "Whisper 会话正在关闭" }
                        val value = operation()
                        busy.set(false)
                        subtitleCommitStarted.set(false)
                        publishStatus()
                        result.complete(value)
                    } catch (t: Throwable) {
                        busy.set(false)
                        subtitleCommitStarted.set(false)
                        publishStatus()
                        result.completeExceptionally(t)
                    }
                }
            } catch (t: Throwable) {
                busy.set(false)
                subtitleCommitStarted.set(false)
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
            // Wipe the entire view, not only its current remaining range:
            // JNI/codec code may advance the caller's position before returning.
            val view = buffer.duplicate()
            view.clear()
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
