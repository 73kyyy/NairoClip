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

        webView.loadUrl("https://appassets.androidplatform.net/assets/nairoclip-home.html")

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
