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
 *   · char 指针用 nc_free_string 释放；字节/浮点数组用 nc_free_buffer 释放
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
class NativeEngineBridge(
    private val appContext: android.content.Context? = null,
    /**
     * 面板层级回调。JS 侧 push 当前层级，原生据此决定返回键是否消费。
     * 用函数而不是持有 Activity：桥是长期对象，直接持有会泄漏 Activity。
     */
    private val onPanelDepth: ((Int) -> Unit)? = null
) {

    /** C ABI 句柄。0 表示尚未创建。 */
    private var editor: Long = 0L

    /**
     * 面板层级。0 = 无面板；>0 = 面板开着。
     * 由 JS 主动推送：evaluateJavascript 是异步的，
     * 返回键处理里等不到 JS 的返回值，所以必须提前告知。
     */
    @JavascriptInterface
    fun setPanelDepth(depth: Int) {
        onPanelDepth?.invoke(depth)
    }

    /**
     * 任务看门狗（T2.2 验收补充）。
     *
     * 为什么必须独立：原实现只在 `state()`（UI 刷新）里推进 tick，
     * 一旦界面不刷新（应用进后台、页面暂停、主线程被长任务阻塞），
     * 超时检测就停了 —— 卡死的任务永远不会被判失败。
     *
     * 用单线程调度器按固定节拍推进；与引擎调用共用同一把对象锁，
     * 因为 NcEditor 明确不是线程安全的（见 editor.h 头注释）。
     * 剩余风险：UI 线程若长时间持有该锁，tick 会被延后，
     * 延后量 = 那次引擎调用耗时，不会丢事件。
     */
    private val taskWatchdog: java.util.concurrent.ScheduledExecutorService =
        java.util.concurrent.Executors.newSingleThreadScheduledExecutor { r ->
            Thread(r, "nc-task-watchdog").apply { isDaemon = true }
        }

    init {
        editor = nativeCreate()
        taskWatchdog.scheduleWithFixedDelay({
            try {
                synchronized(this) {
                    if (editor != 0L) {
                        nativeTaskTick(editor, System.currentTimeMillis())
                    }
                }
            } catch (_: Throwable) {
                // 看门狗自身不能把进程带崩，也不能吞掉引擎状态。
            }
        }, 1000L, 1000L, java.util.concurrent.TimeUnit.MILLISECONDS)
    }

    /** 由 Activity.onDestroy 调用。 */
    @Synchronized
    fun release() {
        taskWatchdog.shutdownNow()
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
     * 契约层统一命令入口（P1/P2）。
     * 入参是 CommandRequest 的 JSON（schemaVersion/commandId/projectId/
     * expectedRevision/commandType/payload/origin/transactionId），
     * 返回 CommandResult 的 JSON。
     *
     * 与 call() 的区别：这里**不按函数名分发**，走结构化信封 +
     * 引擎侧校验 + revision 并发控制 + 事件发布。
     * 新增功能一律走这里；call() 只保留给尚未迁移的老路径（P2 逐步清空）。
     */
    @JavascriptInterface
    @Synchronized
    fun applyCommand(requestJson: String): String = try {
        nativeApplyCommand(ensure(), requestJson.ifBlank { "{}" })
            ?: err("引擎返回空结果")
    } catch (e: Throwable) {
        err("命令执行失败：${e.message}")
    }

    /**
     * 通用任务契约（T2.2）。
     *
     * 时间基准由平台层提供（System.currentTimeMillis），核心层不读系统时钟，
     * 也不起后台线程 —— 超时由 [taskTick] 驱动看门狗判定。
     *
     * 与 applyCommand 一样，这是 typed 入口，不走 call() 的函数名分发。
     */
    @JavascriptInterface
    @Synchronized
    fun taskBegin(
        type: String,
        targetId: Long,
        heartbeatTimeoutMs: Int,
        totalTimeoutMs: Long
    ): String = try {
        nativeTaskBegin(
            ensure(), type, targetId, heartbeatTimeoutMs, totalTimeoutMs,
            System.currentTimeMillis()
        )
    } catch (e: Throwable) {
        err("任务创建失败：${e.message}")
    }

    @JavascriptInterface
    @Synchronized
    fun taskHeartbeat(taskId: String, progress: Double, stageNote: String): Int = try {
        nativeTaskHeartbeat(
            ensure(), taskId, progress, stageNote, System.currentTimeMillis()
        )
    } catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
    @Synchronized
    fun taskComplete(taskId: String, resultType: String, resultJson: String): Int = try {
        nativeTaskComplete(
            ensure(), taskId, resultType, resultJson, System.currentTimeMillis()
        )
    } catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
    @Synchronized
    fun taskFail(taskId: String, error: String, retryable: Boolean): Int = try {
        nativeTaskFail(ensure(), taskId, error, if (retryable) 1 else 0)
    } catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
    @Synchronized
    fun taskCancel(taskId: String): Int = try {
        nativeTaskCancel(ensure(), taskId)
    } catch (e: Throwable) {
        -1
    }

    /** 执行器确认已停止（配合 taskCancel），状态才进入 cancelled。 */
    @JavascriptInterface
    @Synchronized
    fun taskCancelAck(taskId: String): Int = try {
        nativeTaskCancelAck(ensure(), taskId)
    } catch (e: Throwable) {
        -1
    }

    /** 释放结果载荷（保留任务记录）。 */
    @JavascriptInterface
    @Synchronized
    fun taskClearResult(taskId: String): Int = try {
        nativeTaskClearResult(ensure(), taskId)
    } catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
    @Synchronized
    fun taskQuery(taskId: String): String = try {
        nativeTaskQuery(ensure(), taskId)
    } catch (e: Throwable) {
        """{"ok":false,"message":"查询失败"}"""
    }

    /** 推进看门狗时钟；返回本次被判超时的任务数。 */
    @JavascriptInterface
    @Synchronized
    fun taskTick(): Int = try {
        nativeTaskTick(ensure(), System.currentTimeMillis())
    } catch (e: Throwable) {
        0
    }

    /**
     * 一次性取出时间轴快照（工程/轨道/片段/素材/字幕/标记），
     * 由 native 侧直接调 nc_project_*、nc_track_*、nc_clip_info、nc_asset_*
     * 组装，避免 JS 侧发几十次调用。
     */
    @JavascriptInterface
    @Synchronized
    fun state(): String = try {
        // 顺带推进任务看门狗：UI 每次刷新都是一次时钟推进，
        // 卡死的任务（长时间无真实进度）会在这里被判超时，
        // 避免用户看到永远 0% 的进度条。
        nativeTaskTick(ensure(), System.currentTimeMillis())
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

    /**
     * 安装模型：读入文件字节 → 算 SHA-256 → 交给 core 校验并记录。
     * core 不碰文件系统，读写都由平台层完成。
     * 返回 {"installed":1|0,"message":"…"}。
     */
    @JavascriptInterface
    @Synchronized
    fun installModel(modelId: String, filePath: String): String {
        val f = java.io.File(filePath)
        if (!f.isFile) return err("文件不存在：$filePath")

        val bytes: ByteArray = try {
            f.readBytes()
        } catch (e: Throwable) {
            return err("读取失败：${e.message}")
        }
        if (bytes.isEmpty()) return err("文件为空")

        val sha = try {
            val md = java.security.MessageDigest.getInstance("SHA-256")
            md.digest(bytes).joinToString("") { "%02x".format(it) }
        } catch (e: Throwable) {
            return err("计算 SHA-256 失败：${e.message}")
        }

        return try {
            nativeInstallModel(ensure(), modelId, bytes, f.absolutePath, sha,
                               System.currentTimeMillis() / 1000)
        } catch (e: Throwable) {
            err("安装调用失败：${e.message}")
        }
    }

    /** 模型文件应放的目录（应用私有）。 */
    @JavascriptInterface
    fun modelsDir(): String = modelRoot()?.absolutePath ?: ""

    private fun modelRoot(): java.io.File? {
        val base = appContext?.filesDir ?: return null
        return java.io.File(base, "models").also { it.mkdirs() }
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
     * native 侧返回「宽:int32 高:int32 RGBA8...」原始像素——C ABI 的
     * nc_preview_render / nc_thumbnail_generate 输出的是像素而不是 PNG，
     * 这里用 Bitmap 编码成 PNG 再转 data:URL 交给 image 标签。
     */
    private fun rgbaToDataUrl(raw: ByteArray): String {
        if (raw.size < 12) return ""
        val w = readIntLE(raw, 0)
        val h = readIntLE(raw, 4)
        if (w <= 0 || h <= 0) return ""
        if (raw.size.toLong() - 8L != w.toLong() * h.toLong() * 4L) return ""

        /* 快速路径：整块拷贝，替代原本的逐像素 Kotlin 循环。
           -----------------------------------------------------------------
           **字节序必须搞对**，这里踩过一次坑：
             · core 输出 RGBA8：偏移 0=R 1=G 2=B 3=A
             · Bitmap.Config.ARGB_8888 的 int 值是 0xAARRGGBB，
               在小端 ARM 上内存字节序是 **B,G,R,A**（低字节在前）
           ⇒ 直接把 RGBA 整块拷进去会 **R/B 互换（红蓝颠倒）**。

           正确做法二选一：
             (a) Kotlin 侧逐像素换序（原实现，200 万次操作，慢）
             (b) **C++ 侧直接输出 BGRA**，Kotlin 整块拷（本方案）

           这里用 (b)：nativeFrame 的 JNI 侧已经把 RGBA 换成 BGRA
           （见 nc_jni.cpp 的 nativeFrame，用 rgbaToBgraInPlace）。
           换序在 C++ 侧做，NEON 一次处理 16 像素，比 Kotlin 的
           boxed Int 循环快一个量级。

           兜底：若字节数不符（异常尺寸），回退到空的 ByteArray，
           下方 png.isEmpty() 会让调用方拿到空串而不是错色画面。 */
        val png = try {
            val bmp = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888)
            val direct = java.nio.ByteBuffer.wrap(raw, 8, w * h * 4)
            bmp.copyPixelsFromBuffer(direct)
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
    private external fun nativeApplyCommand(handle: Long, requestJson: String): String?
    private external fun nativeTaskBegin(handle: Long, type: String, targetId: Long, heartbeatTimeoutMs: Int, totalTimeoutMs: Long, nowMs: Long): String
    private external fun nativeTaskHeartbeat(handle: Long, taskId: String, progress: Double, stageNote: String, nowMs: Long): Int
    private external fun nativeTaskComplete(handle: Long, taskId: String, resultType: String, resultJson: String, nowMs: Long): Int
    private external fun nativeTaskFail(handle: Long, taskId: String, error: String, retryable: Int): Int
    private external fun nativeTaskCancel(handle: Long, taskId: String): Int
    private external fun nativeTaskCancelAck(handle: Long, taskId: String): Int
    private external fun nativeTaskClearResult(handle: Long, taskId: String): Int
    private external fun nativeTaskQuery(handle: Long, taskId: String): String
    private external fun nativeTaskTick(handle: Long, nowMs: Long): Int
    private external fun nativeState(handle: Long): String
    private external fun nativeFrame(handle: Long, timeUs: Long, width: Int): ByteArray
    private external fun nativeThumb(handle: Long, assetId: Long, index: Int, width: Int): ByteArray
    private external fun nativeWaveform(handle: Long, assetId: Long): String
    private external fun nativeModels(handle: Long): String
    /** 注册/注销文本栅格化器（§4.6）。传 null 注销。 */
    private external fun nativeRegisterTextRasterizer(rasterizer: Any?)

    /* ---- SurfaceView 零拷贝（P6）---- */
    /** 附加显示 Surface。返回是否成功。 */
    private external fun nativeAttachSurface(surface: android.view.Surface?, width: Int, height: Int): Boolean
    /** 分离显示 Surface。 */
    private external fun nativeDetachSurface()
    /** 把 timeUs 时刻的帧直接写进 Surface。返回是否写入。 */
    private external fun nativeBlitFrame(handle: Long, timeUs: Long): Boolean
    /** Surface 是否已就绪。 */
    private external fun nativeSurfaceReady(): Boolean

    /* ---- 崩溃捕获与内存释放（§8.1 / §8.2）---- */
    private external fun nativeInstallCrashHandler(dir: String): Boolean
    private external fun nativeReleaseMemory(level: Int): Long
    /* ---- 帧缓存预算分级（§8.2 第 9 条）---- */
    private external fun nativePreviewSetCacheBudget(bytes: Long): Int
    private external fun nativePreviewGetCacheBudget(): Long
    private external fun nativePreviewSetCacheScale(scale: Double): Int
    private external fun nativeInstallModel(handle: Long, modelId: String, data: ByteArray,
                                            path: String, sha256: String,
                                            nowUnix: Long): String
    private external fun nativePresetFrame(handle: Long, kind: String, style: Int,
                                          timeUs: Long, width: Int): ByteArray

    companion object {
        @JvmStatic
        fun loadLibrary() {
            System.loadLibrary("nc_jni")
        }

        /**
         * 注册文本栅格化器（§4.6）。
         *
         * 必须在 loadLibrary() 之后、任何预览渲染之前调用。
         * 不调用也能跑 —— 文本片段不会渲染（显式降级），
         * 其余功能完全正常。这是刻意的：文本不可见比崩溃好。
         *
         * 用 object 实例而不是类引用：JNI 侧要拿它的 jmethodID，
         * object 的方法签名是实例方法 `(…)[B`，与 Kotlin object 的
         * 实际编译结果一致（object 的 @JvmStatic 才是静态方法，
         * 这里刻意不加，避免签名对不上）。
         */
        fun registerTextRasterizer() {
            /* 关键：nativeRegisterTextRasterizer 是**实例** external 成员
               （JNI 签名带 jobject self）。挪进 companion 会让符号变成
               Java_..._NativeEngineBridge_00024Companion_...，
               与 nc_jni.cpp 的实现对不上 —— 编译能过，运行期
               UnsatisfiedLinkError。所以只能经实例转发。

               但 MainActivity 在引擎实例创建**之前**就调本方法，
               此时 instanceForSurface 还是 null。直接 return 会让
               文本栅格化器永远注册不上（文本片段静默不渲染）。
               因此这里记一个待注册标记，由 bindInstance() 补注册。 */
            val inst = instanceForSurface
            if (inst == null) {
                pendingRasterizerRegister = true
                return
            }
            try {
                inst.nativeRegisterTextRasterizer(AndroidTextRasterizer)
            } catch (e: Throwable) {
                // 老 so 可能没有这个符号 —— 不让它影响启动
            }
        }

        fun unregisterTextRasterizer() {
            pendingRasterizerRegister = false
            try {
                instanceForSurface?.nativeRegisterTextRasterizer(null)
            } catch (e: Throwable) {
            }
        }

        /* ---- SurfaceView 零拷贝（P6）----
           这些是 companion 方法，供 MainActivity 调用；
           内部转发到实例 external（external 必须是非静态的，
           因为 JNI 签名带 jobject self）。 */

        private var instanceForSurface: NativeEngineBridge? = null

        /** 引擎实例尚未创建时收到的「注册文本栅格化器」请求。 */
        private var pendingRasterizerRegister = false

        /** 当前绑定实例（MainActivity 在内存回调里要用）。 */
        val instance: NativeEngineBridge?
            get() = instanceForSurface

        fun attachSurface(surface: android.view.Surface?, width: Int, height: Int): Boolean {
            val inst = instanceForSurface ?: return false
            return try {
                inst.nativeAttachSurface(surface, width, height)
            } catch (e: Throwable) {
                false
            }
        }

        fun detachSurface() {
            try {
                instanceForSurface?.nativeDetachSurface()
            } catch (e: Throwable) {
            }
        }

        fun blitFrame(handle: Long, timeUs: Long): Boolean {
            val inst = instanceForSurface ?: return false
            return try {
                inst.nativeBlitFrame(handle, timeUs)
            } catch (e: Throwable) {
                false
            }
        }

        /** 安装崩溃处理器（§8.1）。用单例实例转发。 */
        fun installCrashHandler(dir: String): Boolean {
            val inst = instanceForSurface ?: return false
            return try {
                inst.nativeInstallCrashHandler(dir)
            } catch (e: Throwable) {
                false
            }
        }

        /**
         * 设置帧缓存预算（§8.2 第 9 条：按设备内存分级）。
         *
         * 由 MainActivity 在引擎创建后调用。返回 true 表示生效；
         * 引擎未就绪返回 false（调用方可在引擎就绪后重试）。
         *
         * 为什么不在这里读 Context 自己算：计算逻辑属于平台层策略
         * （MemoryGuard.frameCacheBudgetBytes），而这里只负责跨 JNI 传递。
         * 分开的好处是预算算法可以单测，不必起引擎。
         */
        fun setPreviewCacheBudget(bytes: Long): Boolean {
            val inst = instanceForSurface ?: return false
            return try {
                inst.nativePreviewSetCacheBudget(bytes) == 0
            } catch (e: Throwable) {
                false
            }
        }

        /** 读取引擎当前的帧缓存预算（字节；引擎未就绪返回 -1）。 */
        fun previewCacheBudget(): Long {
            val inst = instanceForSurface ?: return -1L
            return try {
                inst.nativePreviewGetCacheBudget()
            } catch (e: Throwable) {
                -1L
            }
        }

        /** 释放内存（§8.2 第 11 条）。返回估算释放字节数。 */
        fun releaseMemory(level: Int): Long {
            val inst = instanceForSurface ?: return 0L
            return try {
                inst.nativeReleaseMemory(level)
            } catch (e: Throwable) {
                0L
            }
        }

        fun surfaceReady(): Boolean {
            val inst = instanceForSurface ?: return false
            return try {
                inst.nativeSurfaceReady()
            } catch (e: Throwable) {
                false
            }
        }

        /**
         * 由实例构造时登记（Activity 创建后调用一次）。
         *
         * 同时在这里**把帧缓存预算推给引擎**（§8.2 第 9 条）。
         *
         * 为什么放在这里而不是 MainActivity：
         *   · 这是「引擎已就绪」唯一可靠的时机点（nativeCreate 刚跑完）；
         *   · 放在 Activity 里要额外管生命周期，容易漏调或重复调；
         *   · 预算算法在 MemoryGuard（需要 Context），而 bindInstance
         *     拿不到 Context —— 所以由 Activity 把算好的值传进来。
         *
         * 失败不影响任何功能：core 侧有 96MB 的保守默认值兜底。
         */
        fun bindInstance(inst: NativeEngineBridge, cacheBudgetBytes: Long = 0L) {
            instanceForSurface = inst
            /* 引擎就绪 —— 补注册启动阶段排队的文本栅格化器。 */
            if (pendingRasterizerRegister) {
                pendingRasterizerRegister = false
                registerTextRasterizer()
            }
            if (cacheBudgetBytes > 0) {
                try {
                    inst.nativePreviewSetCacheBudget(cacheBudgetBytes)
                    /* 降采样缓存（P2）：与预算配套。
                       预算决定「能存多少字节」，缩放决定「每帧多大」——
                       只有一起调才能真正提高缓存深度：
                         1080p 全尺寸 8.3MB/帧，128MB → 15 帧
                         1080p 半尺寸 2.1MB/帧，128MB → 60 帧
                       代价是命中缓存时放大显示（略糊），暂停后
                       下一帧全质量渲染会替换掉。 */
                    inst.nativePreviewSetCacheScale(0.5)
                } catch (e: Throwable) {
                    android.util.Log.w("NcMemory", "帧缓存策略设置失败（用默认值）", e)
                }
            }
        }
    }
}
