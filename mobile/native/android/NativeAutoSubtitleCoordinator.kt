package com.nairoclip.app

import android.content.Context
import android.net.Uri
import android.os.Handler
import android.os.Looper
import java.util.concurrent.CompletableFuture
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong

/**
 * Lifecycle-neutral host adapter for the native automatic-subtitle panel.
 *
 * The Activity/ViewModel supplies the selected source URI, its timeline offset,
 * and whether the speech session has completed model loading. All state
 * notifications are marshalled onto the main looper; decoding/inference and
 * subtitle insertion remain inside NativeSpeechSessionWorker.
 */
class NativeAutoSubtitleCoordinator(
    context: Context,
    private val worker: NativeSpeechSessionWorker,
    private val adapter: NativeEditorCommandAdapter,
    private val onStateChanged: (NativeAutoSubtitleUiState) -> Unit,
    private val mainHandler: Handler = Handler(Looper.getMainLooper())
) : AutoCloseable {

    private val appContext = context.applicationContext
    private val closed = AtomicBoolean(false)
    private val stateVersion = AtomicLong(0L)
    private val lock = Any()
    private var sourceUri: Uri? = null
    private var timelineStartUs: Long = 0L
    private var sessionReady = false
    private var running = false
    private var progress: Double? = null
    private var statusMessage = "请选择时间轴中的视频或音频素材"
    private var errorMessage: String? = null

    init {
        publish()
    }

    /** Set only from the host's authoritative selected timeline clip. */
    fun setSource(uri: Uri?, clipTimelineStartUs: Long) {
        require(clipTimelineStartUs >= 0L) { "素材时间线起点不能为负数" }
        synchronized(lock) {
            check(!closed.get()) { "自动字幕协调器已关闭" }
            check(!running) { "识别过程中不能切换素材" }
            sourceUri = uri
            timelineStartUs = clipTimelineStartUs
            errorMessage = null
            progress = null
            statusMessage = if (uri == null) "请选择时间轴中的视频或音频素材"
                else if (sessionReady) "素材已就绪，可以开始识别"
                else "素材已选择，等待语音模型加载"
        }
        publish()
    }

    /** Host calls this only after the worker's model-load future succeeds. */
    fun setSessionReady(ready: Boolean, message: String? = null) {
        synchronized(lock) {
            if (closed.get()) return
            sessionReady = ready
            if (!running) {
                statusMessage = message?.takeIf { it.isNotBlank() }
                    ?: if (ready) "语音模型已就绪，可以开始识别"
                    else "语音模型尚未就绪"
                if (!ready) errorMessage = null
            }
        }
        publish()
    }

    fun state(): NativeAutoSubtitleUiState = synchronized(lock) {
        NativeAutoSubtitleUiState(
            available = !closed.get() && sourceUri != null && sessionReady && !running,
            running = running,
            progress = progress,
            statusMessage = statusMessage,
            errorMessage = errorMessage,
            canCancel = running
        )
    }

    fun start(): Boolean {
        val request = synchronized(lock) {
            if (closed.get() || running || !sessionReady) return false
            val uri = sourceUri
            if (uri == null) {
                statusMessage = "请先选择时间轴中的视频或音频素材"
                null
            } else {
                running = true
                // No progress callback is currently exposed by the JNI worker.
                // Use an indeterminate indicator rather than presenting a fake 0%.
                progress = null
                errorMessage = null
                statusMessage = "正在解码音频并准备识别…"
                uri to timelineStartUs
            }
        }
        if (request == null) {
            publish()
            return false
        }
        publish()
        return try {
            worker.decodeTranscribeAndInsert(
                appContext,
                request.first,
                request.second,
                adapter
            ).whenComplete { _, failure ->
                synchronized(lock) {
                    if (closed.get()) return@whenComplete
                    running = false
                    progress = if (failure == null) 1.0 else null
                    if (failure == null) {
                        errorMessage = null
                        statusMessage = "字幕已写入时间轴"
                    } else {
                        val cause = failure.cause ?: failure
                        errorMessage = cause.message?.takeIf { it.isNotBlank() } ?: "自动字幕失败"
                        statusMessage = "识别未完成，工程内容未确认写入"
                    }
                }
                publish()
            }
            true
        } catch (failure: Throwable) {
            synchronized(lock) {
                running = false
                progress = null
                errorMessage = failure.message ?: "无法启动自动字幕"
                statusMessage = "启动失败"
            }
            publish()
            false
        }
    }

    fun cancel(): Boolean {
        val canCancel = synchronized(lock) { !closed.get() && running }
        if (!canCancel) return false
        val accepted = worker.cancel()
        val shouldPublish = synchronized(lock) {
            if (!running || closed.get()) {
                false
            } else {
                statusMessage = if (accepted) "已请求取消，正在等待安全结束…"
                    else "当前阶段无法接受取消请求，正在等待任务结束"
                true
            }
        }
        if (shouldPublish) publish()
        return accepted
    }

    private fun publish() {
        val version = stateVersion.incrementAndGet()
        val snapshot = state()
        val deliver = {
            // Background completions may be queued while a newer main-thread
            // state is already visible. Drop stale snapshots instead of reverting UI.
            if (!closed.get() && stateVersion.get() == version) onStateChanged(snapshot)
        }
        if (Looper.myLooper() == Looper.getMainLooper()) deliver()
        else mainHandler.post { deliver() }
    }

    override fun close() {
        if (!closed.compareAndSet(false, true)) return
        synchronized(lock) {
            running = false
            sourceUri = null
            progress = null
            statusMessage = "自动字幕服务已关闭"
        }
        worker.close()
    }
}
