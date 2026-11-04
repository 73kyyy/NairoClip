package com.nairoclip.app

import android.content.Context
import android.graphics.PixelFormat
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView

/**
 * 预览用 SurfaceView（P6 零拷贝）。
 *
 * ## 为什么需要它
 *
 * 当前链路：`C++ RGBA → JNI byte[] → Kotlin Bitmap → PNG → Base64 → <img>`
 *
 * 1080p 25fps 时 Base64 字符串每秒约 65MB，WebView 光解析就吃满主线程 ——
 * 这是「预览卡顿」的主因，不是渲染慢。
 *
 * 改成：`C++ RGBA → ANativeWindow_lock 直接写显示缓冲 → unlockAndPost`
 * 省掉 PNG 编码、Base64 编码、JNI 大数组拷贝、WebView 解码四步。
 *
 * ## 布局要点
 *
 * SurfaceView 必须放在 WebView **下面**（Z-order 更低），
 * WebView 的预览区背景设为透明，这样看到的画面来自 Surface，
 * 而 UI（工具栏、面板）仍由 WebView 绘制在上面。
 *
 * ## 风险（文档明确标注「唯一需要动 UI 层级的一步」）
 *
 * · Z-order：SurfaceView 是独立窗口，不能夹在 WebView 的两个图层之间。
 *   只能放最下层 + WebView 透明。
 * · 圆角裁剪：SurfaceView 不支持 border-radius，圆角靠上层 WebView 画遮罩。
 * · 面板弹出遮挡：面板若用独立窗口会盖住 Surface，需确保面板在 WebView 内。
 * · 折叠屏/键盘：坐标要跟随重新上报（[setPreviewRect]）。
 */
class PreviewSurfaceView(context: Context) : SurfaceView(context), SurfaceHolder.Callback {

    private var currentSurface: Surface? = null
    private var attached = false

    /** 预览区在父容器坐标系中的位置（由 JS 上报）。 */
    @Volatile private var rectLeft = 0
    @Volatile private var rectTop = 0
    @Volatile private var rectWidth = 0
    @Volatile private var rectHeight = 0

    /** 原生附加/分离回调（由 NativeEngineBridge 注入）。 */
    var onSurfaceAvailable: ((Surface) -> Unit)? = null
    var onSurfaceDestroyed: (() -> Unit)? = null

    init {
        holder.addCallback(this)
        /* 半透明：SurfaceView 默认不透明且在最上层，
           必须设成半透明才能让 WebView 盖在它上面。 */
        holder.setFormat(PixelFormat.TRANSLUCENT)
        setZOrderOnTop(false)
        setZOrderMediaOverlay(false)
    }

    override fun surfaceCreated(holder: SurfaceHolder) {
        currentSurface = holder.surface
        if (!attached) {
            attached = true
            onSurfaceAvailable?.invoke(holder.surface)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        /* 尺寸变化（旋转/折叠屏展开/分屏）必须重新上报 ——
           ANativeWindow 的 buffer 尺寸在创建时固定，
           不通知会按旧尺寸写，画面被拉伸或裁切。 */
        if (width > 0 && height > 0) {
            rectWidth = width
            rectHeight = height
        }
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        currentSurface = null
        if (attached) {
            attached = false
            onSurfaceDestroyed?.invoke()
        }
    }

    /** JS 上报预览区矩形（像素，相对父容器）。 */
    fun setPreviewRect(left: Int, top: Int, width: Int, height: Int) {
        rectLeft = left
        rectTop = top
        rectWidth = width
        rectHeight = height
        // 位置变化不需要重建 Surface，只调整 View 位置与大小
        layout(left, top, left + width, top + height)
    }

    /** 当前是否可用于直写。 */
    fun isReady(): Boolean = currentSurface?.isValid == true
}
