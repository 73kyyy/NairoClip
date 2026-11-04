package com.nairoclip.app

import android.app.Activity
import android.webkit.JavascriptInterface
import android.webkit.WebView

/**
 * 预览区坐标桥（P6 SurfaceView 零拷贝）。
 *
 * ## 为什么需要单独一个桥
 *
 * SurfaceView 是**独立窗口**，它不知道 WebView 里预览区在哪。
 * 位置只能由 JS 上报（预览区尺寸由 CSS 决定，随比例/面板展开变化）。
 *
 * 为什么不做自动测量：原生侧无法可靠地知道「WebView 里哪个 DOM 元素
 * 是预览区」—— 要么改页面加标记再解析 HTML（脆弱），要么让 JS 主动
 * 上报（明确）。选后者。
 *
 * ## 坐标系
 *
 * 上报的是 **CSS 像素**（WebView 逻辑坐标），需按 displayMetrics.density
 * 换算成物理像素再交给 SurfaceView。不换算会导致高 DPR 设备上
 * 预览区只占屏幕左上角一小块。
 */
class PreviewBridge(
    private val activity: Activity,
    private val webView: WebView
) {
    private val tag = "NcPreviewBridge"

    /** 是否已启用 Surface 直写（由页面决定：Web 预览时保持 false）。 */
    @Volatile
    private var surfaceEnabled = false

    /** 取 MainActivity 的 previewSurface（反射解耦，便于测试/换宿主）。 */
    private fun surfaceView(): PreviewSurfaceView? = try {
        val field = activity.javaClass.getDeclaredField("previewSurface")
        field.isAccessible = true
        field.get(activity) as? PreviewSurfaceView
    } catch (e: Throwable) {
        null
    }

    /**
     * JS 上报预览区矩形（CSS 像素，相对视口左上角）。
     *
     * 调用时机：页面加载完成 / 画布比例切换 / 旋转 / 面板展开收起。
     *
     * 注意：JavascriptInterface 运行在 JavaBridge 线程，
     * 所有 View 操作必须切回主线程。
     */
    @JavascriptInterface
    fun setPreviewRect(left: Int, top: Int, width: Int, height: Int) {
        if (width <= 0 || height <= 0) return

        activity.runOnUiThread {
            try {
                val density = activity.resources.displayMetrics.density
                val l = (left * density).toInt()
                val t = (top * density).toInt()
                val w = (width * density).toInt()
                val h = (height * density).toInt()

                val view = surfaceView() ?: return@runOnUiThread
                view.setPreviewRect(l, t, w, h)
                surfaceEnabled = true

                /* 位置/尺寸变了要重新附加 —— ANativeWindow 的 buffer 尺寸
                   在附加时确定，不重新 attached 会按旧尺寸写，画面被拉伸。 */
                if (view.isReady()) {
                    NativeEngineBridge.attachSurface(view.holder.surface, w, h)
                }
            } catch (e: Throwable) {
                android.util.Log.w(tag, "setPreviewRect 失败（走 data:URL 回退）", e)
            }
        }
    }

    /** JS 查询是否启用 Surface 直写。 */
    @JavascriptInterface
    fun isSurfaceEnabled(): Boolean = surfaceEnabled

    /**
     * 隐藏/显示 Surface。
     *
     * 什么时候需要：切到非剪辑页（首页/我的）时要隐藏，
     * 否则 Surface 是独立窗口，会一直显示上一帧、盖在新页面上。
     */
    @JavascriptInterface
    fun setSurfaceVisible(visible: Boolean) {
        activity.runOnUiThread {
            try {
                surfaceView()?.visibility =
                    if (visible) android.view.View.VISIBLE else android.view.View.GONE
            } catch (e: Throwable) {
                // 拿不到就忽略 —— 纯优化项
            }
        }
    }
}
