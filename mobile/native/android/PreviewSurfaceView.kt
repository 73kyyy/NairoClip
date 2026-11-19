package com.nairoclip.app

import android.content.Context
import android.graphics.PixelFormat
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * Reusable native preview surface.
 *
 * Surface callbacks and listener registration are expected on the Android main
 * thread (SurfaceHolder.Callback is dispatched there). A listener installed
 * after surfaceCreated is replayed immediately, so Compose/host construction
 * order cannot silently lose the initial attach event.
 *
 * The surface size callback reports the buffer dimensions. The preview rectangle
 * is a separate parent-layout concern and is never used as a substitute for the
 * actual Surface buffer size.
 */
class PreviewSurfaceView(context: Context) : SurfaceView(context), SurfaceHolder.Callback {

    private var currentSurface: Surface? = null
    private var currentFormat: Int = PixelFormat.UNKNOWN
    private var bufferWidth: Int = 0
    private var bufferHeight: Int = 0

    private var surfaceAvailableListener: ((Surface) -> Unit)? = null
    private var surfaceDestroyedListener: (() -> Unit)? = null
    private var surfaceSizeChangedListener: ((Surface, Int, Int, Int) -> Unit)? = null

    /** Called once for each valid surface lifetime; late registration is replayed. */
    var onSurfaceAvailable: ((Surface) -> Unit)?
        get() = surfaceAvailableListener
        set(listener) {
            surfaceAvailableListener = listener
            val surface = currentSurface
            if (listener != null && surface?.isValid == true) {
                listener(surface)
            }
        }

    /** Called when the current surface lifetime ends. */
    var onSurfaceDestroyed: (() -> Unit)?
        get() = surfaceDestroyedListener
        set(listener) {
            surfaceDestroyedListener = listener
        }

    /**
     * Called after the buffer size changes. Arguments are surface, pixel format,
     * width and height. Late registration is replayed when a valid size is known.
     */
    var onSurfaceSizeChanged: ((Surface, Int, Int, Int) -> Unit)?
        get() = surfaceSizeChangedListener
        set(listener) {
            surfaceSizeChangedListener = listener
            val surface = currentSurface
            if (listener != null && surface?.isValid == true &&
                bufferWidth > 0 && bufferHeight > 0
            ) {
                listener(surface, currentFormat, bufferWidth, bufferHeight)
            }
        }

    init {
        holder.addCallback(this)
        holder.setFormat(PixelFormat.TRANSLUCENT)
        setZOrderOnTop(false)
        setZOrderMediaOverlay(false)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        val surface = holder.surface
        currentSurface = surface
        if (surface.isValid) {
            surfaceAvailableListener?.invoke(surface)
            if (bufferWidth > 0 && bufferHeight > 0) {
                surfaceSizeChangedListener?.invoke(
                    surface, currentFormat, bufferWidth, bufferHeight
                )
            }
        }
    }

    override fun surfaceChanged(
        holder: SurfaceHolder,
        format: Int,
        width: Int,
        height: Int
    ) {
        if (width <= 0 || height <= 0) return

        currentFormat = format
        bufferWidth = width
        bufferHeight = height

        val surface = currentSurface ?: holder.surface
        if (surface.isValid) {
            currentSurface = surface
            surfaceSizeChangedListener?.invoke(surface, format, width, height)
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        currentSurface = null
        currentFormat = PixelFormat.UNKNOWN
        bufferWidth = 0
        bufferHeight = 0
        surfaceDestroyedListener?.invoke()
    }

    /** Position and size in physical pixels relative to the parent container. */
    fun setPreviewRect(left: Int, top: Int, width: Int, height: Int) {
        if (width <= 0 || height <= 0) {
            layout(left, top, left, top)
            return
        }
        layout(left, top, left + width, top + height)
    }

    /** Current surface is valid and can be passed to the native renderer. */
    fun isReady(): Boolean = currentSurface?.isValid == true
}
