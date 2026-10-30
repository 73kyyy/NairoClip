package com.nairoclip.app

import android.os.Bundle
import android.webkit.WebView
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

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // libnc_jni.so：NativeEngineBridge 里 external 方法的实现
        NativeEngineBridge.loadLibrary()

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

        webView.webViewClient = object : WebViewClientCompat() {
            override fun shouldInterceptRequest(
                view: WebView,
                request: android.webkit.WebResourceRequest
            ) = assetLoader.shouldInterceptRequest(request.url)
        }

        val eng = NativeEngineBridge(applicationContext)
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
