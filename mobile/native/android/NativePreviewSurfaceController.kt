package com.nairoclip.app

import android.view.Surface

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
    private var disposed: Boolean = false

    init {
        previewView.onSurfaceAvailable = { surface ->
            if (!disposed) currentSurface = surface
        }
        previewView.onSurfaceSizeChanged = { surface, _, width, height ->
            if (!disposed) {
                currentSurface = surface
                bufferWidth = width
                bufferHeight = height
                attachCurrentSurface()
            }
        }
        previewView.onSurfaceDestroyed = {
            if (!disposed) {
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

    /** Render a single frame at project time after the Surface and engine are ready. */
    fun renderFrameAt(timeUs: Long): Boolean {
        if (disposed || !attached || currentSurface?.isValid != true || timeUs < 0L) {
            return false
        }
        return NativeEngineBridge.renderPreviewFrame(timeUs)
    }

    /** Detaches native rendering and releases all View listener references. */
    fun dispose() {
        if (disposed) return
        disposed = true
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
