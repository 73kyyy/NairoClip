package com.nairoclip.app.wxapi

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.util.Log
import com.nairoclip.app.auth.NativeAuthBridge
import com.tencent.mm.opensdk.modelbase.BaseReq
import com.tencent.mm.opensdk.modelbase.BaseResp
import com.tencent.mm.opensdk.modelmsg.SendAuth
import com.tencent.mm.opensdk.openapi.IWXAPI
import com.tencent.mm.opensdk.openapi.IWXAPIEventHandler
import com.tencent.mm.opensdk.openapi.WXAPIFactory

/**
 * 微信回调入口
 * ---------------------------------------------------------------------------
 * **必须**放在 `<包名>.wxapi` 这个固定目录下、且类名必须叫 WXEntryActivity，
 * 否则微信回来后找不到入口，表现为「授权完成但 App 没反应」。
 *
 * AndroidManifest.xml：
 *   <activity
 *       android:name=".wxapi.WXEntryActivity"
 *       android:exported="true"
 *       android:taskAffinity="com.nairoclip.app"
 *       android:launchMode="singleTask"
 *       android:theme="@android:style/Theme.Translucent.NoTitleBar" />
 *
 * 微信登录走 code 换 token 的流程：
 *   1. 这里拿到 code（不是 access_token）
 *   2. 把 code 交给**你自己的服务端**换 access_token + openid
 *      （appsecret 绝不能放在客户端）
 *   3. 服务端返回用户资料，再回调 NC_AUTH_DONE
 *
 * 下方 fetchViaBackend 是这条链路的占位实现，接口约定见函数注释。
 * ---------------------------------------------------------------------------
 */
class WXEntryActivity : Activity(), IWXAPIEventHandler {

    private lateinit var api: IWXAPI

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        api = WXAPIFactory.createWXAPI(this, BuildConfig.WECHAT_APP_ID, false)
        api.handleIntent(intent, this)
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        api.handleIntent(intent, this)
    }

    override fun onReq(req: BaseReq?) {
        // 本应用只处理登录回调，不处理微信主动请求
    }

    override fun onResp(resp: BaseResp?) {
        val callback = NativeAuthBridge.pendingWeChatAuth
        try {
            if (resp is SendAuth.Resp) {
                when (resp.errCode) {
                    BaseResp.ErrCode.ERR_OK -> {
                        val code = resp.code
                        val state = resp.state
                        if (code.isNullOrBlank()) {
                            callback?.invoke(NativeAuthBridge.Result(
                                ok = false, error = "微信未返回授权码"
                            ))
                        } else {
                            // state 必须与发起时一致，防 CSRF
                            fetchViaBackend(code, state, callback)
                        }
                    }
                    BaseResp.ErrCode.ERR_USER_CANCEL ->
                        callback?.invoke(NativeAuthBridge.Result(ok = false, error = "用户取消"))
                    BaseResp.ErrCode.ERR_AUTH_DENIED ->
                        callback?.invoke(NativeAuthBridge.Result(ok = false, error = "用户拒绝授权"))
                    else ->
                        callback?.invoke(NativeAuthBridge.Result(
                            ok = false, error = "微信授权失败（${resp.errCode}: ${resp.errStr}）"
                        ))
                }
            }
        } finally {
            NativeAuthBridge.pendingWeChatAuth = null
            finish()
        }
    }

    /**
     * 用 code 换用户信息。
     *
     * 这是**唯一**必须由服务端完成的一步：微信要求 appsecret 参与换 token，
     * 客户端无法安全持有。发布前把 NC_AUTH_BACKEND 指向真实服务。
     *
     * 约定的服务端接口（POST JSON）：
     *   POST {NC_AUTH_BACKEND}/auth/wechat
     *   { "code": "...", "state": "..." }
     *   -> 200 { "ok": true, "uid": "...", "nickname": "...", "avatar": "..." }
     *   -> 4xx { "ok": false, "error": "..." }
     *
     * 该接口地址未配置时，如实返回「后端未配置」，不伪造登录成功。
     */
    private fun fetchViaBackend(
        code: String,
        state: String?,
        callback: ((NativeAuthBridge.Result) -> Unit)?
    ) {
        val backend = BuildConfig.AUTH_BACKEND
        if (backend.isBlank()) {
            Log.w("WXEntry", "AUTH_BACKEND 未配置，无法用 code 换 token")
            callback?.invoke(NativeAuthBridge.Result(
                ok = false,
                error = "登录服务未配置（需服务端用 code 换 access_token）"
            ))
            return
        }

        Thread {
            val r = runCatching {
                val conn = java.net.URL("$backend/auth/wechat").openConnection()
                        as java.net.HttpURLConnection
                conn.requestMethod = "POST"
                conn.doOutput = true
                conn.setRequestProperty("Content-Type", "application/json")
                conn.connectTimeout = 10000
                conn.readTimeout = 10000
                val body = org.json.JSONObject()
                    .put("code", code)
                    .put("state", state ?: "")
                    .toString()
                conn.outputStream.use { it.write(body.toByteArray()) }

                val text = conn.inputStream.bufferedReader().use { it.readText() }
                val j = org.json.JSONObject(text)
                if (j.optBoolean("ok", false)) {
                    NativeAuthBridge.Result(
                        ok = true,
                        uid = j.optString("uid").ifBlank { null },
                        nickname = j.optString("nickname").ifBlank { null },
                        avatar = j.optString("avatar").ifBlank { null }
                    )
                } else {
                    NativeAuthBridge.Result(
                        ok = false, error = j.optString("error").ifBlank { "服务端拒绝" }
                    )
                }
            }.getOrElse { e ->
                NativeAuthBridge.Result(ok = false, error = "换取登录态失败：${e.message}")
            }
            callback?.invoke(r)
        }.start()
    }
}
