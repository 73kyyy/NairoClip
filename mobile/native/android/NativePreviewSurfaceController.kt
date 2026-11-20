package com.nairoclip.app

import android.os.Handler
import android.os.Looper
import android.view.Surface
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference

/**
 * Owns the lifecycle bridge between [PreviewSurfaceView] and the existing
 * engine Surface API. This controller is intentionally not installed into
 * MainActivity yet; S5 runtime entry wiring remains gated by the prerequisite
 * acceptance tasks.
 *
 * Surface callbacks can arrive before the engine is bound. In that case the
 * latest valid surface/size is retained and [onEngineReady] retries the attach.
 */
class NativePreviewSurfaceController(
    private val previewView: PreviewSurfaceView
) {
    private var currentSurface: Surface? = null
    private var bufferWidth: Int = 0
    private var bufferHeight: Int = 0
    private var attached: Boolean = false
    @Volatile
    private var disposed: Boolean = false

    private data class RenderRequest(
        val sequence: Long,
        val timeUs: Long,
        val onComplete: ((Long, Boolean) -> Unit)?
    )

    /*
     * Surface rendering is synchronous inside JNI and holds NativeEngineBridge's
     * serialization lock. Never run it from a Compose/UI callback. A single
     * worker plus a one-slot latest-request mailbox bounds queued work while a
     * user scrubs rapidly through the timeline.
     */
    private val renderExecutor = Executors.newSingleThreadExecutor { runnable ->
        Thread(runnable, "nc-preview-render").apply { isDaemon = true }
    }
    private val mainHandler = Handler(Looper.getMainLooper())
    private val pendingRender = AtomicReference<RenderRequest?>(null)
    private val renderSequence = AtomicLong(0L)
    private val renderWorkerScheduled = AtomicBoolean(false)

    init {
        previewView.onSurfaceAvailable = { surface ->
            if (!disposed) {
                if (currentSurface !== surface) invalidatePendingRender()
                currentSurface = surface
            }
        }
        previewView.onSurfaceSizeChanged = { surface, _, width, height ->
            if (!disposed) {
                if (currentSurface !== surface ||
                    bufferWidth != width || bufferHeight != height
                ) {
                    invalidatePendingRender()
                }
                currentSurface = surface
                bufferWidth = width
                bufferHeight = height
                attachCurrentSurface()
            }
        }
        previewView.onSurfaceDestroyed = {
            if (!disposed) {
                invalidatePendingRender()
                currentSurface = null
                bufferWidth = 0
                bufferHeight = 0
                if (attached) NativeEngineBridge.detachSurface()
                attached = false
            }
        }
    }

    /**
     * Call after NativeEngineBridge.bindInstance(). It is safe to call more
     * than once; the current valid Surface and buffer size are re-applied.
     */
    fun onEngineReady(): Boolean {
        if (disposed) return false
        return attachCurrentSurface()
    }

    private fun attachCurrentSurface(): Boolean {
        val surface = currentSurface
        if (disposed || surface?.isValid != true ||
            bufferWidth <= 0 || bufferHeight <= 0
        ) {
            return false
        }

        val success = NativeEngineBridge.attachSurface(surface, bufferWidth, bufferHeight)
        attached = success
        if (!success) {
            // Do not leave a previously attached buffer active after a failed
            // reconfiguration (for example, a surface replacement).
            NativeEngineBridge.detachSurface()
        }
        return success
    }

    /**
     * Queue a scrub/seek preview render without blocking the caller.
     *
     * Requests that have not started are coalesced: only the newest timestamp
     * is rendered. Completion is posted to the main thread and is omitted when
     * a newer request superseded it. This is deliberately not a playback clock.
     *
     * @return false when the request is invalid or the controller is unavailable;
     *         true when the request was accepted for asynchronous processing.
     */
    fun requestRenderFrame(
        timeUs: Long,
        onComplete: ((timeUs: Long, rendered: Boolean) -> Unit)? = null
    ): Boolean {
        if (disposed || !attached || currentSurface?.isValid != true || timeUs < 0L) {
            return false
        }

        val request = RenderRequest(
            sequence = renderSequence.incrementAndGet(),
            timeUs = timeUs,
            onComplete = onComplete
        )
        pendingRender.set(request)
        if (renderWorkerScheduled.compareAndSet(false, true)) {
            try {
                renderExecutor.execute { drainRenderRequests() }
            } catch (_: RuntimeException) {
                renderWorkerScheduled.set(false)
                pendingRender.compareAndSet(request, null)
                return false
            }
        }
        return true
    }

    /**
     * Invalidates queued/in-flight completions when the Surface generation
     * changes. A worker that has dequeued but not started a frame also checks
     * the sequence immediately before entering JNI.
     */
    private fun invalidatePendingRender() {
        renderSequence.incrementAndGet()
        pendingRender.set(null)
    }

    private fun drainRenderRequests() {
        while (!disposed) {
            val request = pendingRender.getAndSet(null)
            if (request == null) {
                renderWorkerScheduled.set(false)
                // Close the race where a producer publishes just as the worker
                // clears the scheduled flag.
                if (pendingRender.get() != null &&
                    renderWorkerScheduled.compareAndSet(false, true)
                ) {
                    continue
                }
                return
            }

            if (disposed || request.sequence != renderSequence.get()) {
                continue
            }

            val rendered = NativeEngineBridge.renderPreviewFrame(request.timeUs) {
                !disposed && request.sequence == renderSequence.get()
            }
            if (!disposed &&
                request.sequence == renderSequence.get() &&
                pendingRender.get() == null
            ) {
                request.onComplete?.let { callback ->
                    mainHandler.post {
                        if (!disposed && request.sequence == renderSequence.get()) {
                            callback(request.timeUs, rendered)
                        }
                    }
                }
            }
        }
        renderWorkerScheduled.set(false)
    }

    /** Detaches native rendering and releases all View listener references. */
    fun dispose() {
        if (disposed) return
        disposed = true
        invalidatePendingRender()
        renderExecutor.shutdownNow()
        previewView.onSurfaceAvailable = null
        previewView.onSurfaceSizeChanged = null
        previewView.onSurfaceDestroyed = null
        if (attached) NativeEngineBridge.detachSurface()
        attached = false
        currentSurface = null
        bufferWidth = 0
        bufferHeight = 0
    }
}
