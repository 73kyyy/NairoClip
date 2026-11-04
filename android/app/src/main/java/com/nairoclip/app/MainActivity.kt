package com.nairoclip.app

import android.os.Bundle
import android.webkit.WebView
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AppCompatActivity
import androidx.webkit.WebViewAssetLoader
import androidx.webkit.WebViewClientCompat

/**
 * NairoClip 主界面：全屏 WebView + 两个原生桥。
 *
 * 页面资源经 WebViewAssetLoader 以
 * https://appassets.androidplatform.net/assets/ 提供。
 * 不用 file://，否则跨页 location.href 与相对路径都不对。
 *
 * 引擎桥：window.NCEngine  -> JNI 直调 nc_*（不走 HTTP）
 * 登录桥：window.NCAndroid -> 微信 / QQ 授权
 */
class MainActivity : AppCompatActivity() {

    private lateinit var webView: WebView
    private var engine: NativeEngineBridge? = null

    /**
     * 面板层级（由 JS 通过 NCEngine.setPanelDepth 推送）。
     * 0 = 没有面板；>0 = 面板开着，返回键应先交给页面处理。
     *
     * 为什么用「JS 推」而不是「原生问 JS」：
     * evaluateJavascript 是异步的，onBackPressed 里等不到结果；
     * 而返回值必须同步决定（消费掉还是交给系统）。
     */
    @Volatile
    private var panelDepth: Int = 0

    fun updatePanelDepth(depth: Int) {
        panelDepth = depth.coerceAtLeast(0)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // libnc_jni.so：NativeEngineBridge 里 external 方法的实现
        NativeEngineBridge.loadLibrary()
        /* 注册文本栅格化器（§4.6）：之后文本片段才能渲染。
           放在 loadLibrary 之后、WebView 加载之前 ——
           晚于这个时机会出现「首次预览没有文字」。
           失败不影响启动（文本不显示，其余功能正常）。 */
        NativeEngineBridge.registerTextRasterizer()

        val assetLoader = WebViewAssetLoader.Builder()
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .setDomain("appassets.androidplatform.net")
            .build()

        webView = WebView(this)
        webView.settings.javaScriptEnabled = true
        webView.settings.domStorageEnabled = true
        webView.settings.allowFileAccess = false
        webView.settings.allowContentAccess = true
        webView.settings.mediaPlaybackRequiresUserGesture = false

        /* ---- 渲染性能（"更接近原生"的关键几项）----
         *
         * 1) HARDWARE 层：让整棵 DOM 走 GPU 合成，滚动/位移不再走软件绘制。
         *    剪辑页有大量绝对定位元素（时间轴、轨道行、播放头），
         *    软件层下每次更新都要重绘整块，这是"拖起来发涩"的主因之一。
         *
         * 2) rendererPriorityPolicy：把渲染进程优先级提到 FOREGROUND，
         *    避免系统在内存压力下先降 WebView 渲染进程的频。
         *
         * 3) setBackgroundColor 黑：WebView 默认白底，首帧到达前会闪白。
         *    剪辑页底色是 #000，直接对齐，省掉一次整屏重绘。
         *
         * 4) overScrollMode = NEVER：关掉边缘拉伸光效。
         *    时间轴是横向可滚的，越界回弹会额外触发一次合成。
         */
        webView.setLayerType(android.view.View.LAYER_TYPE_HARDWARE, null)
        webView.setBackgroundColor(android.graphics.Color.BLACK)
        webView.overScrollMode = android.view.View.OVER_SCROLL_NEVER

        /* 渲染进程优先级：API 33 引入 rendererPriorityPolicy，
           **API 34 已移除**（compileSdk=34 的 android.jar 里没有这个符号，
           Kotlin 编译期直接报 Unresolved reference）。
           所以不能直接引用，只能用反射在 API 33 上尝试；
           拿不到就跳过 —— 这是纯优化项，缺失不影响功能。 */
        if (android.os.Build.VERSION.SDK_INT == android.os.Build.VERSION_CODES.TIRAMISU) {
            try {
                val policyClass =
                    Class.forName("android.webkit.WebSettings\$RendererPriorityPolicy")
                val important = policyClass.getField("RENDERER_PRIORITY_IMPORTANT").get(null)
                val m = android.webkit.WebSettings::class.java
                    .getMethod("setRendererPriorityPolicy", Int::class.javaPrimitiveType!!, Boolean::class.javaPrimitiveType!!)
                m.invoke(webView.settings, important, false)
            } catch (ignored: Throwable) {
                // 反射失败（厂商 ROM 裁剪等）不影响主流程
            }
        }

        /* 长按不再触发系统的文本选择/放大镜 —— 时间轴长按是"拖动排序"，
           系统选择菜单会抢走触摸，表现为"按下去没反应"。 */
        webView.isLongClickable = false
        webView.setOnLongClickListener { true }
        webView.settings.setSupportZoom(false)
        webView.settings.builtInZoomControls = false

        webView.webViewClient = object : WebViewClientCompat() {
            override fun shouldInterceptRequest(
                view: WebView,
                request: android.webkit.WebResourceRequest
            ) = assetLoader.shouldInterceptRequest(request.url)
        }

        val eng = NativeEngineBridge(applicationContext, ::updatePanelDepth)
        engine = eng
        webView.addJavascriptInterface(eng, "NCEngine")
        webView.addJavascriptInterface(NativeAuthBridge(this, webView), "NCAndroid")

        /* 入口用**页面版**而不是静态版。
           assets/nairoclip-home.html 是 make_static.py / make_pages_static.py
           导出的单文件快照（数据在构建时冻结，engine_static.js 只读）。
           页面版 pages/01-home.html 走 engine.js → window.NCEngine，
           才有真实的编辑/预览/导出能力。
           两者都在 assets 里：pages/ 由 Gradle 的 syncMobileUi 从 mobile/ 同步。 */
        webView.loadUrl("https://appassets.androidplatform.net/assets/pages/01-home.html")

        /* 返回键 / 侧滑返回。
           语义：面板开着时先关面板（退一层），关到没有面板才让系统退出。
           侧滑手势与导航返回键在 Android 上都汇聚到 OnBackPressedCallback，
           所以这里一处拦截即可覆盖两种入口。 */
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                /* 按层级由内到外退：三级面板 → 二级工具栏 → 系统退出。
                   一次只退一层，符合用户对「返回上一步」的预期。 */
                if (panelDepth > 0) {
                    // 面板自己退一层；退完它会重新推送新的 depth
                    webView.evaluateJavascript("window.NCL3BACK && window.NCL3BACK()", null)
                    return
                }
                /* 面板已关：可能是二级工具栏。用返回字符串判断
                   （evaluateJavascript 异步，但这里只需要「稍后」执行一次，
                   不必等它的结果来决定是否退出——若它处理了，
                   下一次按返回时 panelDepth 仍为 0，会再走这条分支，
                   NCBARBACK 返回 false 后自然落到退出）。 */
                webView.evaluateJavascript(
                    "window.NCBARBACK ? window.NCBARBACK() : false"
                ) { result ->
                    if (result != "true") {
                        isEnabled = false
                        onBackPressedDispatcher.onBackPressed()
                        isEnabled = true
                    }
                }
            }
        })

        setContentView(webView)
    }

    override fun onDestroy() {
        engine?.release()
        engine = null
        if (this::webView.isInitialized) {
            webView.removeJavascriptInterface("NCEngine")
            webView.removeJavascriptInterface("NCAndroid")
            webView.destroy()
        }
        super.onDestroy()
    }
}
