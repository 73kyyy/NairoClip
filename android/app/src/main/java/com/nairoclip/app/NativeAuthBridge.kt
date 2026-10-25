package com.nairoclip.app

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.util.Log
import android.webkit.JavascriptInterface
import android.webkit.WebView
import com.tencent.mm.opensdk.modelmsg.SendAuth
import com.tencent.mm.opensdk.openapi.IWXAPI
import com.tencent.mm.opensdk.openapi.WXAPIFactory
import com.tencent.tauth.IUiListener
import com.tencent.tauth.Tencent
import com.tencent.tauth.UiError
import org.json.JSONObject

/**
 * NairoClip · 第三方登录原生桥（Android）
 * ---------------------------------------------------------------------------
 * 注入方式（在 Activity.onCreate 里）：
 *
 *   webView.settings.javaScriptEnabled = true
 *   webView.addJavascriptInterface(NativeAuthBridge(this, webView), "NCAndroid")
 *
 * 前端调用（assets/auth.js）：
 *   window.NCAndroid.authorize('wechat' | 'qq' | 'apple')
 *
 * 回调前端：
 *   window.NC_AUTH_DONE('<provider>', {ok:true, uid, nickname, avatar})  成功
 *   window.NC_AUTH_DONE('<provider>', {ok:false, error:'用户取消'})      失败/取消
 *
 * 关于 Apple：Android 平台**没有**「通过 Apple 登录」。前端已按平台移除该入口，
 * 这里仍保留分支以明确拒绝，避免被直开的页面绕过。
 * ---------------------------------------------------------------------------
 * 需要在 build.gradle 声明的依赖（SDK 已随仓库提供，见 native/libs/）：
 *   implementation files('libs/wechat-sdk-6.8.40.aar')
 *   implementation files('libs/open_sdk_3.5.19_r9483ffc7_lite.jar')
 *
 * AndroidManifest.xml 必须声明（微信要求固定目录，否则回调收不到）：
 *   <activity
 *       android:name=".wxapi.WXEntryActivity"
 *       android:exported="true"
 *       android:taskAffinity="你的包名"
 *       android:launchMode="singleTask" />
 */
class NativeAuthBridge(
    private val activity: Activity,
    private val webView: WebView
) {
    private val tag = "NcAuthBridge"

    /** 微信开放平台 AppID（与包名+签名绑定） */
    private val wxAppId: String = BuildConfig.WECHAT_APP_ID

    /** QQ 互联 AppID（纯数字） */
    private val qqAppId: String = BuildConfig.QQ_APP_ID

    private val wxApi: IWXAPI by lazy {
        WXAPIFactory.createWXAPI(activity, wxAppId, true).also { it.registerApp(wxAppId) }
    }

    private val qqTencent: Tencent by lazy {
        Tencent.createInstance(qqAppId, activity.applicationContext)
    }

    /** 供 WXEntryActivity 回调使用（单例持有，进程内唯一） */
    companion object {
        @Volatile
        var pendingWeChatAuth: ((Result) -> Unit)? = null

        @Volatile
        var qqListener: IUiListener? = null
    }

    data class Result(
        val ok: Boolean,
        val uid: String? = null,
        val nickname: String? = null,
        val avatar: String? = null,
        val error: String? = null
    )

    /**
     * 前端入口。同步返回接受与否，真实结果走 JavaScript 回调。
     * 这里必须切回主线程：JavascriptInterface 方法运行在 WebView 的 JavaBridge 线程。
     */
    @JavascriptInterface
    fun authorize(provider: String) {
        activity.runOnUiThread {
            when (provider.lowercase()) {
                "wechat" -> startWeChat()
                "qq" -> startQQ()
                "apple" -> finish("apple", Result(ok = false, error = "Apple 登录仅在 iOS 设备可用"))
                else -> finish(provider, Result(ok = false, error = "未知登录方式"))
            }
        }
    }

    // ---------------------------------------------------------------- 微信
    private fun startWeChat() {
        if (!wxApi.isWXAppInstalled) {
            finish("wechat", Result(ok = false, error = "未安装微信客户端"))
            return
        }
        if (!wxApi.isWXAppSupportAPI) {
            finish("wechat", Result(ok = false, error = "当前微信版本不支持登录"))
            return
        }

        pendingWeChatAuth = { r -> finish("wechat", r) }

        val req = SendAuth.Req().apply {
            scope = "snsapi_userinfo"
            state = System.currentTimeMillis().toString()   // 防 CSRF，回调需校验
        }
        val sent = wxApi.sendReq(req)
        if (!sent) {
            pendingWeChatAuth = null
            finish("wechat", Result(ok = false, error = "调起微信失败"))
        }
    }

    // ---------------------------------------------------------------- QQ
    private fun startQQ() {
        qqListener = object : IUiListener {
            override fun onComplete(response: Any?) {
                val json = runCatching { JSONObject(response.toString()) }.getOrNull()
                if (json == null) {
                    finish("qq", Result(ok = false, error = "QQ 返回数据无法解析"))
                    return
                }
                // 客户端授权只拿到 openid + access_token；
                // 昵称/头像需再用 access_token 调 OpenAPI（见 fetchQQProfile）。
                val openid = json.optString("openid").ifBlank { null }
                val token = json.optString("access_token").ifBlank { null }
                if (openid == null || token == null) {
                    finish("qq", Result(ok = false, error = "QQ 授权信息不完整"))
                    return
                }
                fetchQQProfile(openid, token)
            }

            override fun onError(err: UiError?) {
                finish("qq", Result(
                    ok = false,
                    error = err?.errorMessage ?: "QQ 授权失败（错误码 ${err?.errorCode}）"
                ))
            }

            override fun onCancel() {
                finish("qq", Result(ok = false, error = "用户取消"))
            }
        }
        qqTencent.login(activity, "get_user_info", qqListener)
    }

    /**
     * 用 access_token 拉 QQ 用户资料。
     * 注意：这是腾讯 OpenAPI 的 HTTPS 调用，与「引擎不提供 HTTP 接口」无关——
     * 文档说的是**本地剪辑引擎**对平台层不要走 HTTP（应 JNI 直调 C ABI），
     * 第三方登录本身就必须走对方服务器的 HTTPS。
     */
    private fun fetchQQProfile(openid: String, token: String) {
        Thread {
            val url = "https://graph.qq.com/user/get_user_info" +
                    "?access_token=$token&oauth_consumer_key=$qqAppId&openid=$openid"
            val body = runCatching {
                (java.net.URL(url).openConnection() as java.net.HttpURLConnection).run {
                    connectTimeout = 8000; readTimeout = 8000
                    inputStream.bufferedReader().use { it.readText() }
                }
            }.getOrElse { null }

            val r = if (body == null) {
                Result(ok = false, error = "获取 QQ 资料失败")
            } else {
                val j = runCatching { JSONObject(body) }.getOrNull()
                if (j?.optInt("ret", -1) == 0) {
                    Result(
                        ok = true,
                        uid = openid,
                        nickname = j.optString("nickname").ifBlank { null },
                        avatar = j.optString("figureurl_qq_2").ifBlank { null }
                    )
                } else {
                    // 拿到 openid 但资料失败：仍视为登录成功，只是没有昵称头像
                    Result(ok = true, uid = openid)
                }
            }
            activity.runOnUiThread { finish("qq", r) }
        }.start()
    }

    // ---------------------------------------------------------------- 回调前端
    private fun finish(provider: String, r: Result) {
        val payload = JSONObject().apply {
            put("ok", r.ok)
            r.uid?.let { put("uid", it) }
            r.nickname?.let { put("nickname", it) }
            r.avatar?.let { put("avatar", it) }
            r.error?.let { put("error", it) }
        }
        val js = "window.NC_AUTH_DONE && window.NC_AUTH_DONE(" +
                "'${provider.replace("'", "")}', $payload);"
        Log.i(tag, "auth result: $provider ok=${r.ok} ${r.error ?: ""}")
        activity.runOnUiThread { webView.evaluateJavascript(js, null) }
    }
}
