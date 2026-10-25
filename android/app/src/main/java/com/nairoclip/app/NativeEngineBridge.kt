package com.nairoclip.app

import android.graphics.Bitmap
import android.util.Base64
import android.webkit.JavascriptInterface
import org.json.JSONObject

/**
 * NairoClip · 引擎桥（JNI 直调 C ABI，不走 HTTP）
 * ---------------------------------------------------------------------------
 * 依据 docs/UI_INTEGRATION.md §7「平台层对接（不要用 HTTP）」：
 *   · Android = Kotlin + JNI 直调 nc_*
 *   · 单个 NcEditor* 不支持多线程并发 → 本类用 @Synchronized 串行化
 *   · char* 用 nc_free_string 释放；uint8_t*/double* 用 nc_free_buffer 释放
 *
 * 注入：
 *   NativeEngineBridge.loadLibrary()
 *   webView.addJavascriptInterface(NativeEngineBridge(), "NCEngine")
 *
 * 前端只认 window.NCEngine，看不到任何 URL 或端口：
 *   NCEngine.call(fn, argsJson)          -> JSON 字符串 {ok,code,message,result}
 *   NCEngine.state()                     -> 工程快照 JSON
 *   NCEngine.frameDataUrl(timeUs, w)     -> "data:image/png;base64,..."
 *   NCEngine.thumbDataUrl(assetId, i, w) -> 同上
 *   NCEngine.waveform(assetId)           -> {"buckets":[...]}
 *
 * JavascriptInterface 方法跑在 WebView 的 JavaBridge 线程，所有 nc_* 调用
 * 都在该线程串行执行，符合「平台层串行化」契约。
 */
class NativeEngineBridge(private val appContext: android.content.Context? = null) {

    /** C ABI 句柄。0 表示尚未创建。 */
    private var editor: Long = 0L

    init {
        editor = nativeCreate()
    }

    /** 由 Activity.onDestroy 调用。 */
    @Synchronized
    fun release() {
        if (editor != 0L) {
            nativeDestroy(editor)
            editor = 0L
        }
    }

    @Synchronized
    private fun ensure(): Long {
        if (editor == 0L) editor = nativeCreate()
        return editor
    }

    /**
     * 任意 C ABI 函数。fn 必须是 nc_ 开头的函数名，argsJson 为参数对象。
     * 失败也返回结构化 JSON，绝不把异常抛给 JS。
     */
    @JavascriptInterface
    @Synchronized
    fun call(fn: String, argsJson: String): String {
        if (fn.isEmpty() || !fn.startsWith("nc_")) {
            return err("只接受 nc_ 开头的函数名")
        }
        return try {
            nativeCall(ensure(), fn, argsJson.ifBlank { "{}" })
        } catch (e: Throwable) {
            err("引擎调用失败：${e.message}")
        }
    }

    /**
     * 一次性取出时间轴快照（工程/轨道/片段/素材/字幕/标记），
     * 由 native 侧直接调 nc_project_*、nc_track_*、nc_clip_info、nc_asset_*
     * 组装，避免 JS 侧发几十次调用。
     */
    @JavascriptInterface
    @Synchronized
    fun state(): String = try {
        nativeState(ensure())
    } catch (e: Throwable) {
        err("读取工程状态失败：${e.message}")
    }

    /** 预览帧。返回 data:URL，前端直接挂 <img>。失败返回空串。 */
    @JavascriptInterface
    @Synchronized
    fun frameDataUrl(timeUs: Long, width: Int): String =
        renderDataUrl(timeUs, width, isThumb = false, thumbIndex = 0)

    /** 素材缩略图。 */
    @JavascriptInterface
    @Synchronized
    fun thumbDataUrl(assetId: Long, index: Int, width: Int): String =
        renderDataUrl(assetId, width, isThumb = true, thumbIndex = index)

    /** 音频波形。 */
    @JavascriptInterface
    @Synchronized
    fun waveform(assetId: Long): String = try {
        nativeWaveform(ensure(), assetId)
    } catch (e: Throwable) {
        """{"buckets":[]}"""
    }

    /**
     * 预设缩略帧（滤镜/特效/蒙版宫格用）。桥内临时套预设渲染后立即还原，
     * 不改变工程状态。返回 data:URL；失败返回空串。
     */
    @JavascriptInterface
    @Synchronized
    fun presetFrame(kind: String, style: Int, timeUs: Long, width: Int): String {
        val raw: ByteArray = try {
            nativePresetFrame(ensure(), kind, style, timeUs, width.coerceIn(16, 1024))
        } catch (e: Throwable) {
            ByteArray(0)
        }
        return rgbaToDataUrl(raw)
    }

    /**
     * 草稿列表。C ABI 没有工程目录接口，草稿扫描是平台层职责：
     * 扫描应用私有目录下的 .nclip，返回 {"projects":[...]}。
     */
    @JavascriptInterface
    fun listProjects(): String {
        val out = org.json.JSONArray()
        try {
            val dir = appContext?.filesDir ?: return """{"projects":[]}"""
            dir.listFiles()?.forEach { f ->
                if (!f.isFile || !f.name.endsWith(".nclip")) return@forEach
                out.put(JSONObject().apply {
                    put("name", f.name.removeSuffix(".nclip"))
                    put("file", f.name)
                    put("bytes", f.length())
                })
            }
        } catch (e: Throwable) {
            return """{"projects":[]}"""
        }
        return JSONObject().put("projects", out).toString()
    }

    /** AI 模型目录与状态（nc_model_catalog / nc_model_info / nc_model_status）。 */
    @JavascriptInterface
    @Synchronized
    fun models(): String = try {
        nativeModels(ensure())
    } catch (e: Throwable) {
        """{"models":[]}"""
    }

    private fun renderDataUrl(
        a: Long, width: Int, isThumb: Boolean, thumbIndex: Int
    ): String {
        val raw: ByteArray = try {
            if (isThumb) nativeThumb(ensure(), a, thumbIndex, width.coerceIn(16, 1024))
            else nativeFrame(ensure(), a, width.coerceIn(16, 2048))
        } catch (e: Throwable) {
            ByteArray(0)
        }
        return rgbaToDataUrl(raw)
    }

    /**
     * native 侧返回 [w:int32][h:int32][RGBA8...] 原始像素——C ABI 的
     * nc_preview_render / nc_thumbnail_generate 输出的是像素而不是 PNG，
     * 这里用 Bitmap 编码成 PNG 再转 data:URL 交给 <img>。
     */
    private fun rgbaToDataUrl(raw: ByteArray): String {
        if (raw.size < 12) return ""
        val w = readIntLE(raw, 0)
        val h = readIntLE(raw, 4)
        if (w <= 0 || h <= 0) return ""
        if (raw.size.toLong() - 8L != w.toLong() * h.toLong() * 4L) return ""

        val png = try {
            val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
            // RGBA8 → ARGB_8888（Android 的 int 像素低字节是 B，需换序）
            val pixels = IntArray(w * h)
            var p = 8
            for (i in pixels.indices) {
                val r = raw[p].toInt() and 0xFF
                val g = raw[p + 1].toInt() and 0xFF
                val b = raw[p + 2].toInt() and 0xFF
                val al = raw[p + 3].toInt() and 0xFF
                pixels[i] = (al shl 24) or (r shl 16) or (g shl 8) or b
                p += 4
            }
            bmp.setPixels(pixels, 0, w, 0, 0, w, h)
            val out = java.io.ByteArrayOutputStream()
            bmp.compress(Bitmap.CompressFormat.PNG, 100, out)
            bmp.recycle()
            out.toByteArray()
        } catch (e: Throwable) {
            ByteArray(0)
        }
        if (png.isEmpty()) return ""
        return "data:image/png;base64," + Base64.encodeToString(png, Base64.NO_WRAP)
    }

    /** 小端读 int32（JNI 侧直接写的是本机字节序）。 */
    private fun readIntLE(b: ByteArray, off: Int): Int =
        (b[off].toInt() and 0xFF) or
        ((b[off + 1].toInt() and 0xFF) shl 8) or
        ((b[off + 2].toInt() and 0xFF) shl 16) or
        ((b[off + 3].toInt() and 0xFF) shl 24)

    private fun err(msg: String): String =
        JSONObject().apply {
            put("ok", false); put("code", -1); put("message", msg)
            put("result", JSONObject())
        }.toString()

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeCall(handle: Long, fn: String, argsJson: String): String
    private external fun nativeState(handle: Long): String
    private external fun nativeFrame(handle: Long, timeUs: Long, width: Int): ByteArray
    private external fun nativeThumb(handle: Long, assetId: Long, index: Int, width: Int): ByteArray
    private external fun nativeWaveform(handle: Long, assetId: Long): String
    private external fun nativeModels(handle: Long): String
    private external fun nativePresetFrame(handle: Long, kind: String, style: Int,
                                          timeUs: Long, width: Int): ByteArray

    companion object {
        @JvmStatic
        fun loadLibrary() {
            System.loadLibrary("nc_jni")
        }
    }
}
