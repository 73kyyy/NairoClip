package com.nairoclip.app

import android.graphics.Bitmap
import android.util.Base64
import android.util.AtomicFile
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
    @Volatile private var lastPreviewError: String = ""
    /** 当前编辑会话对应的本地草稿文件名；只允许应用私有目录内的安全文件名。 */
    private var activeProjectFile: String? = null
    private val maxProjectFileBytes = 64L * 1024L * 1024L

    private fun validProjectFile(name: String): Boolean =
        name.matches(Regex("^[A-Za-z0-9_-]{1,80}\\.nclip$"))

    private fun projectFile(name: String): java.io.File? {
        if (!validProjectFile(name)) return null
        val root = appContext?.filesDir ?: return null
        val file = java.io.File(root, name)
        return if (file.canonicalFile.parentFile == root.canonicalFile) file else null
    }

    private fun recoveryProjectDirectory(name: String): java.io.File? {
        if (!validProjectFile(name)) return null
        val base = appContext?.filesDir ?: return null
        val root = java.io.File(base, "recovery").canonicalFile
        val directory = java.io.File(root, name.removeSuffix(".nclip")).canonicalFile
        return if (directory.parentFile == root) directory else null
    }

    private fun configureRecovery(handle: Long, projectName: String): Int {
        val directory = recoveryProjectDirectory(projectName) ?: return -1003
        return try {
            nativeRecoveryConfigure(handle, directory.absolutePath)
        } catch (_: Throwable) {
            -1002
        }
    }

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
        /* M-1：构造函数阶段对象尚未发布给其它线程，因此**严格来说**
           这里的写入不需要加锁（JMM：构造函数内的动作 happens-before
           任何"正确发布后"读取该对象的动作）。
           但下面 scheduleWithFixedDelay 会把 `this` 逃逸到看门狗线程，
           而看门狗随后会读写 editor —— 为了让"初始写入"与"看门狗读取"
           由**同一监视器**排序（而不是依赖执行器内部的 happens-before
           细节），这里也在 stateLock 内完成创建。
           这是一次非嵌套、构造期无竞争的单次加锁，不构成多余嵌套。 */
        synchronized(stateLock) {
            editor = nativeCreate()
        }
        taskWatchdog.scheduleWithFixedDelay({
            try {
                /* M-1：看门狗必须与其它路径使用**同一把** stateLock。
                   此前用 synchronized(this) —— 那是实例监视器，
                   与 stateLock 不是同一把锁，无法与 release() 互斥。 */
                synchronized(stateLock) {
                    if (editor != 0L) {
                        val now = android.os.SystemClock.elapsedRealtime()
                        nativeTaskTick(editor, now)
                        if (activeProjectFile != null) nativeRecoveryTick(editor, now)
                    }
                }
            } catch (_: Throwable) {
                // 看门狗自身不能把进程带崩，也不能吞掉引擎状态。
            }
        }, 1000L, 1000L, java.util.concurrent.TimeUnit.MILLISECONDS)
    }

    /** 由 Activity.onDestroy 调用：先停止并等待看门狗退出，再销毁引擎。 */
    fun release() {
        taskWatchdog.shutdownNow()
        try {
            if (!taskWatchdog.awaitTermination(5, java.util.concurrent.TimeUnit.SECONDS)) {
                android.util.Log.e("NcEngine", "任务/恢复看门狗未在 5 秒内退出")
            }
        } catch (e: InterruptedException) {
            Thread.currentThread().interrupt()
        }

        synchronized(stateLock) {
            if (editor != 0L) {
                nativeDestroy(editor)
                editor = 0L
            }
            /* Fix 1（Activity 重建）：本实例销毁后必须解除全局绑定，
               否则 Surface / 内存回调不得再访问已销毁实例。 */
            unbindIfCurrent(this)
        }
    }

        private fun ensure(): Long {
            synchronized(stateLock) {
                if (editor == 0L) {
                    val h = nativeCreate()
                    if (h == 0L) {
                        /* F-1（严格单所有者）：已有其它实例持有 Engine 时，
                           nativeCreate 明确返回 0。本实例必须保持"未创建"，
                           绝不写入无效句柄，也绝不销毁他人 Engine。 */
                        android.util.Log.w(
                            "NcEngine", "引擎创建被拒绝（已有实例持有），本实例保持未创建")
                    } else {
                        editor = h
                    }
                }
                return editor
            }
        }

    /**
     * 任意 C ABI 函数。fn 必须是 nc_ 开头的函数名，argsJson 为参数对象。
     * 失败也返回结构化 JSON，绝不把异常抛给 JS。
     */
    @JavascriptInterface
        fun call(fn: String, argsJson: String): String {
        synchronized(stateLock) {
        if (fn.isEmpty() || !fn.startsWith("nc_")) {
            return err("只接受 nc_ 开头的函数名")
        }
        if (fn == "nc_import_asset") {
            return err("素材必须通过系统文件选择器导入", -1001)
        }
        return try {
            nativeCall(ensure(), fn, argsJson.ifBlank { "{}" }, android.os.SystemClock.elapsedRealtime())
        } catch (e: Throwable) {
            err("引擎调用失败：${e.message}")
        }
    
        }}

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
        fun applyCommand(requestJson: String): String = try {
        synchronized(stateLock) {
        nativeApplyCommand(ensure(), requestJson.ifBlank { "{}" }, android.os.SystemClock.elapsedRealtime())
            ?: err("引擎返回空结果")
    
        }} catch (e: Throwable) {
        err("命令执行失败：${e.message}")
    }

    /**
     * 通用任务契约（T2.2）。
     *
     * 时间基准由平台层提供（SystemClock.elapsedRealtime 单调时钟），核心层不读系统时钟，
     * 也不起后台线程 —— 超时由 [taskTick] 驱动看门狗判定。
     *
     * 与 applyCommand 一样，这是 typed 入口，不走 call() 的函数名分发。
     */
    @JavascriptInterface
        fun taskBegin(
        type: String,
        targetId: Long,
        heartbeatTimeoutMs: Int,
        totalTimeoutMs: Long
    ): String = try {
        synchronized(stateLock) {
        nativeTaskBegin(
            ensure(), type, targetId, heartbeatTimeoutMs, totalTimeoutMs,
            android.os.SystemClock.elapsedRealtime()
        )
    
        }} catch (e: Throwable) {
        err("任务创建失败：${e.message}")
    }

    @JavascriptInterface
        fun taskHeartbeat(taskId: String, progress: Double, stageNote: String): Int = try {
        synchronized(stateLock) {
        nativeTaskHeartbeat(
            ensure(), taskId, progress, stageNote, android.os.SystemClock.elapsedRealtime()
        )
    
        }} catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
        fun taskComplete(taskId: String, resultType: String, resultJson: String): Int = try {
        synchronized(stateLock) {
        nativeTaskComplete(
            ensure(), taskId, resultType, resultJson, android.os.SystemClock.elapsedRealtime()
        )
    
        }} catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
        fun taskFail(taskId: String, error: String, retryable: Boolean): Int = try {
        synchronized(stateLock) {
        nativeTaskFail(ensure(), taskId, error, if (retryable) 1 else 0)
    
        }} catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
        fun taskCancel(taskId: String): Int = try {
        synchronized(stateLock) {
        nativeTaskCancel(ensure(), taskId)
    
        }} catch (e: Throwable) {
        -1
    }

    /** 执行器确认已停止（配合 taskCancel），状态才进入 cancelled。 */
    @JavascriptInterface
        fun taskCancelAck(taskId: String): Int = try {
        synchronized(stateLock) {
        nativeTaskCancelAck(ensure(), taskId)
    
        }} catch (e: Throwable) {
        -1
    }

    /** 释放结果载荷（保留任务记录）。 */
    @JavascriptInterface
        fun taskClearResult(taskId: String): Int = try {
        synchronized(stateLock) {
        nativeTaskClearResult(ensure(), taskId)
    
        }} catch (e: Throwable) {
        -1
    }

    @JavascriptInterface
        fun taskQuery(taskId: String): String = try {
        synchronized(stateLock) {
        nativeTaskQuery(ensure(), taskId)
    
        }} catch (e: Throwable) {
        """{"ok":false,"message":"查询失败"}"""
    }

    /** 推进看门狗时钟；返回本次被判超时的任务数。 */
    @JavascriptInterface
        fun taskTick(): Int = try {
        synchronized(stateLock) {
        nativeTaskTick(ensure(), android.os.SystemClock.elapsedRealtime())
    
        }} catch (e: Throwable) {
        0
    }

    /**
     * 一次性取出时间轴快照（工程/轨道/片段/素材/字幕/标记），
     * 由 native 侧直接调 nc_project_*、nc_track_*、nc_clip_info、nc_asset_*
     * 组装，避免 JS 侧发几十次调用。
     */
    @JavascriptInterface
        fun state(): String = try {
        synchronized(stateLock) {
        // 顺带推进任务看门狗：UI 每次刷新都是一次时钟推进，
        // 卡死的任务（长时间无真实进度）会在这里被判超时，
        // 避免用户看到永远 0% 的进度条。
        nativeTaskTick(ensure(), android.os.SystemClock.elapsedRealtime())
        nativeState(ensure())
    
        }} catch (e: Throwable) {
        err("读取工程状态失败：${e.message}")
    }

    /** 预览帧。返回 data:URL，前端直接挂 <img>。失败返回空串。 */
    @JavascriptInterface
        fun frameDataUrl(timeUs: Long, width: Int): String =
        renderDataUrl(timeUs, width, isThumb = false, thumbIndex = 0)

    /** 最近一次主预览解码/渲染失败原因；没有可预览片段时为空。 */
    @JavascriptInterface
    fun previewError(): String = lastPreviewError

    /** 素材缩略图。 */
    @JavascriptInterface
        fun thumbDataUrl(assetId: Long, index: Int, width: Int): String =
        renderDataUrl(assetId, width, isThumb = true, thumbIndex = index)

    /** 音频波形。 */
    @JavascriptInterface
        fun waveform(assetId: Long): String = try {
        synchronized(stateLock) {
        nativeWaveform(ensure(), assetId)
    
        }} catch (e: Throwable) {
        """{"buckets":[]}"""
    }

    /**
     * 预设缩略帧（滤镜/特效/蒙版宫格用）。桥内临时套预设渲染后立即还原，
     * 不改变工程状态。返回 data:URL；失败返回空串。
     */
    @JavascriptInterface
        fun presetFrame(kind: String, style: Int, timeUs: Long, width: Int): String {
        synchronized(stateLock) {
        val raw: ByteArray = try {
            nativePresetFrame(ensure(), kind, style, timeUs, width.coerceIn(16, 1024))
        } catch (e: Throwable) {
            ByteArray(0)
        }
        return rgbaToDataUrl(raw)
    
        }}

    /**
     * 草稿列表。C ABI 没有工程目录接口，草稿扫描是平台层职责：
     * 扫描应用私有目录下的 .nclip，返回 {"projects":[...]}。
     */
    @JavascriptInterface
    fun listProjects(): String = synchronized(stateLock) {
        val dir = appContext?.filesDir
            ?: return@synchronized JSONObject().put("ok", false)
                .put("message", "应用私有目录不可用").put("projects", org.json.JSONArray()).toString()
        val files = try {
            dir.listFiles() ?: return@synchronized JSONObject().put("ok", false)
                .put("message", "无法读取草稿目录").put("projects", org.json.JSONArray()).toString()
        } catch (e: Throwable) {
            return@synchronized JSONObject().put("ok", false)
                .put("message", "读取草稿目录失败：${e.message}")
                .put("projects", org.json.JSONArray()).toString()
        }

        // AtomicFile 可能在进程中断时留下 .bak；openRead() 会恢复最后一次完整提交。
        for (backup in files) {
            if (!backup.isFile || !backup.name.endsWith(".nclip.bak")) continue
            val base = java.io.File(dir, backup.name.removeSuffix(".bak"))
            try {
                AtomicFile(base).openRead().use { /* 触发备份恢复并关闭文件 */ }
            } catch (e: Throwable) {
                return@synchronized JSONObject().put("ok", false)
                    .put("message", "草稿恢复失败（${backup.name}）：${e.message}")
                    .put("projects", org.json.JSONArray()).toString()
            }
        }

        val refreshed = try {
            dir.listFiles() ?: return@synchronized JSONObject().put("ok", false)
                .put("message", "恢复后无法读取草稿目录").put("projects", org.json.JSONArray()).toString()
        } catch (e: Throwable) {
            return@synchronized JSONObject().put("ok", false)
                .put("message", "恢复后读取草稿目录失败：${e.message}")
                .put("projects", org.json.JSONArray()).toString()
        }
        val out = org.json.JSONArray()
        refreshed.filter { it.isFile && validProjectFile(it.name) }
            .sortedByDescending { it.lastModified() }
            .forEach { file ->
                out.put(JSONObject().apply {
                    put("name", file.name.removeSuffix(".nclip"))
                    put("file", file.name)
                    put("bytes", file.length())
                    put("updated", java.text.SimpleDateFormat(
                        "yyyy-MM-dd'T'HH:mm:ssXXX", java.util.Locale.US
                    ).format(java.util.Date(file.lastModified())))
                })
            }
        JSONObject().put("ok", true).put("projects", out).toString()
    }

    /**
     * 只导入由 MainActivity 系统文件选择器复制到 filesDir/imports 的素材。
     * storageName 是 FileProvider 返回给 WebView 的唯一文件名，不接受路径。
     */
    @JavascriptInterface
    fun importPickedMedia(storageName: String, type: Int, durationUs: Long): String =
        synchronized(stateLock) {
            val context = appContext ?: return@synchronized err("应用私有目录不可用", -1002)
            if (!storageName.matches(Regex("^[0-9a-f]{32}__[\\p{L}\\p{N}._-]{1,120}$"))) {
                return@synchronized err("素材文件标识非法", -1003)
            }
            if (type !in 0..2) {
                return@synchronized err("素材类型不受支持", -1004)
            }
            if (durationUs <= 0L) {
                return@synchronized err("素材时长非法", -1005)
            }
            val root = try {
                java.io.File(context.filesDir, "imports").canonicalFile
            } catch (e: Throwable) {
                return@synchronized err("素材目录不可用：${e.message}", -1002)
            }
            val file = try {
                java.io.File(root, storageName).canonicalFile
            } catch (e: Throwable) {
                return@synchronized err("素材路径解析失败：${e.message}", -1003)
            }
            if (file.parentFile != root || !file.isFile || file.length() <= 0L) {
                return@synchronized err("素材文件不存在、为空或路径越界", -1006)
            }
            val displayName = storageName.substringAfter("__")
            val args = JSONObject().apply {
                put("name", displayName)
                put("path", file.absolutePath)
                put("type", type)
                put("duration_us", durationUs)
            }
            try {
                val handle = ensure()
                if (handle == 0L) return@synchronized err("引擎未初始化", -1007)
                JSONObject(nativeCall(handle, "nc_import_asset", args.toString(), android.os.SystemClock.elapsedRealtime())).toString()
            } catch (e: Throwable) {
                err("素材导入失败：${e.message}", -1008)
            }
        }

    /**
     * 使用 Android MediaExtractor + MediaMuxer 从已选择的私有视频中流复制音轨。
     * 不调用 FFmpeg 软件解码/编码，也不接受 JS 传入任意输出路径。
     */
    @JavascriptInterface
    fun extractAudioFromPickedMedia(storageName: String, durationUs: Long): String =
        synchronized(stateLock) {
            val context = appContext ?: return@synchronized err("应用私有目录不可用", -1002)
            if (!storageName.matches(Regex("^[0-9a-f]{32}__[\\p{L}\\p{N}._-]{1,120}$"))) {
                return@synchronized err("源素材标识非法", -1003)
            }
            if (durationUs <= 0L) return@synchronized err("源素材时长非法", -1005)
            val root = try {
                java.io.File(context.filesDir, "imports").canonicalFile
            } catch (e: Throwable) {
                return@synchronized err("素材目录不可用：${e.message}", -1002)
            }
            val source = try {
                java.io.File(root, storageName).canonicalFile
            } catch (e: Throwable) {
                return@synchronized err("源素材路径解析失败：${e.message}", -1003)
            }
            if (source.parentFile != root || !source.isFile || source.length() <= 0L) {
                return@synchronized err("源素材不存在或路径越界", -1006)
            }
            val baseName = storageName.substringAfter("__").substringBeforeLast('.', "audio")
            val outputName = java.util.UUID.randomUUID().toString().replace("-", "") +
                "__" + baseName.take(80) + ".m4a"
            val output = java.io.File(root, outputName)
            if (output.canonicalFile.parentFile != root) {
                return@synchronized err("音频输出路径非法", -1003)
            }

            val extractor = android.media.MediaExtractor()
            var muxer: android.media.MediaMuxer? = null
            var muxerStarted = false
            var sampleCount = 0L
            try {
                extractor.setDataSource(source.absolutePath)
                var sourceTrack = -1
                var sourceFormat: android.media.MediaFormat? = null
                for (i in 0 until extractor.trackCount) {
                    val format = extractor.getTrackFormat(i)
                    val mime = format.getString(android.media.MediaFormat.KEY_MIME) ?: continue
                    if (mime.startsWith("audio/")) {
                        sourceTrack = i
                        sourceFormat = format
                        break
                    }
                }
                if (sourceTrack < 0) {
                    throw java.io.IOException("所选视频没有可提取的音频轨")
                }
                val audioFormat = sourceFormat
                    ?: throw java.io.IOException("无法读取音频轨格式")
                extractor.selectTrack(sourceTrack)
                muxer = android.media.MediaMuxer(
                    output.absolutePath,
                    android.media.MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4
                )
                val outputTrack = muxer.addTrack(audioFormat)
                muxer.start()
                muxerStarted = true

                val declaredBuffer = if (sourceFormat.containsKey(android.media.MediaFormat.KEY_MAX_INPUT_SIZE)) {
                    sourceFormat.getInteger(android.media.MediaFormat.KEY_MAX_INPUT_SIZE)
                } else {
                    4 * 1024 * 1024
                }
                if (declaredBuffer > 8 * 1024 * 1024) {
                    throw java.io.IOException("音频轨声明的样本缓冲区超过 8 MiB 安全上限")
                }
                val bufferSize = declaredBuffer.coerceAtLeast(64 * 1024)
                val buffer = java.nio.ByteBuffer.allocateDirect(bufferSize)
                val info = android.media.MediaCodec.BufferInfo()
                while (true) {
                    buffer.clear()
                    val size = extractor.readSampleData(buffer, 0)
                    if (size < 0) break
                    if (size > buffer.capacity()) {
                        throw java.io.IOException("音频样本超过系统声明的输入缓冲区")
                    }
                    info.set(0, size, extractor.sampleTime, extractor.sampleFlags)
                    muxer.writeSampleData(outputTrack, buffer, info)
                    sampleCount++
                    if (!extractor.advance()) break
                }
                if (sampleCount == 0L) throw java.io.IOException("音频轨没有可写入的样本")
                muxer.stop()
                muxerStarted = false
                muxer.release()
                muxer = null

                if (!output.isFile || output.length() <= 0L) {
                    throw java.io.IOException("系统封装器未生成有效音频文件")
                }
                val handle = ensure()
                if (handle == 0L) throw java.io.IOException("引擎未初始化")
                val importArgs = JSONObject().apply {
                    put("name", outputName.substringAfter("__"))
                    put("path", output.absolutePath)
                    put("type", 1)
                    put("duration_us", durationUs)
                }
                val imported = JSONObject(nativeCall(
                    handle, "nc_import_asset", importArgs.toString(),
                    android.os.SystemClock.elapsedRealtime()
                ))
                if (!imported.optBoolean("ok", false)) {
                    throw java.io.IOException(imported.optString("message", "音频素材登记失败"))
                }
                nativeRecoveryMarkDirty(handle, android.os.SystemClock.elapsedRealtime())
                imported.toString()
            } catch (e: Throwable) {
                output.delete()
                err("音频提取失败：${e.message ?: "系统媒体封装失败"}", -1011)
            } finally {
                if (muxerStarted) {
                    try { muxer?.stop() } catch (_: Throwable) {}
                }
                try { muxer?.release() } catch (_: Throwable) {}
                try { extractor.release() } catch (_: Throwable) {}
            }
        }

    /**
     * 从应用私有目录原子保存当前工程。空文件名表示当前编辑会话草稿；
     * 首次保存时生成不可预测的文件名，后续复用该文件。
     */
    @JavascriptInterface
    fun saveProjectFile(): String = synchronized(stateLock) {
        if (appContext?.filesDir == null) return@synchronized err("应用私有目录不可用")
        val handle = ensure()
        if (handle == 0L) return@synchronized err("引擎未初始化")
        val name = activeProjectFile ?: "project-${java.util.UUID.randomUUID()}.nclip"
        val target = projectFile(name) ?: return@synchronized err("草稿文件名非法")
        val serialized = try {
            nativeProjectSave(handle) ?: return@synchronized err("工程序列化失败")
        } catch (e: Throwable) {
            return@synchronized err("工程序列化失败：${e.message}")
        }
        if (serialized.isBlank()) return@synchronized err("工程内容为空")
        val serializedBytes = serialized.toByteArray(Charsets.UTF_8)
        if (serializedBytes.size.toLong() > maxProjectFileBytes) {
            return@synchronized err("工程文件超过 64 MiB 安全上限")
        }

        val atomic = AtomicFile(target)
        var stream: java.io.FileOutputStream? = null
        try {
            val output = atomic.startWrite()
            stream = output
            output.write(serializedBytes)
            output.fd.sync()
            atomic.finishWrite(output)
            stream = null
            if (activeProjectFile != name) {
                // 工程文件已完成 AtomicFile 提交；即使恢复子系统初始化失败，
                // 也必须保留当前草稿身份，避免下一次保存生成第二份孤儿草稿。
                activeProjectFile = name
                val recoveryRc = configureRecovery(handle, name)
                if (recoveryRc != 0) {
                    return@synchronized err("工程已保存，但恢复目录初始化失败（$recoveryRc）", recoveryRc)
                }
            }
            val cleanRc = nativeRecoveryMarkClean(handle)
            if (cleanRc != 0) {
                return@synchronized err("工程已保存，但恢复快照清理失败（$cleanRc）", cleanRc)
            }
            JSONObject().apply {
                put("ok", true); put("code", 0); put("message", "草稿已保存")
                put("result", JSONObject().put("file", name).put("bytes", target.length()))
            }.toString()
        } catch (e: Throwable) {
            stream?.let { atomic.failWrite(it) }
            err("草稿保存失败：${e.message}")
        }
    }

    /** 载入指定草稿；路径只能是 filesDir 根目录中的安全 .nclip 文件名。 */
    @JavascriptInterface
    fun loadProjectFile(fileName: String): String = synchronized(stateLock) {
        val target = projectFile(fileName) ?: return@synchronized err("草稿文件名非法")
        val backup = java.io.File(target.path + ".bak")
        if (!target.isFile && !backup.isFile) return@synchronized err("草稿文件不存在")
        val storedLength = if (target.isFile) target.length() else backup.length()
        if (storedLength <= 0L) return@synchronized err("草稿文件为空")
        if (storedLength > maxProjectFileBytes) {
            return@synchronized err("草稿文件超过 64 MiB 安全上限")
        }
        val text = try {
            AtomicFile(target).openRead().bufferedReader(Charsets.UTF_8).use { it.readText() }
        } catch (e: Throwable) {
            return@synchronized err("读取草稿失败：${e.message}")
        }
        if (text.isBlank()) return@synchronized err("草稿文件为空")
        val raw = try {
            nativeProjectLoad(ensure(), text)
        } catch (e: Throwable) {
            return@synchronized err("工程载入失败：${e.message}")
        }
        val result = try { JSONObject(raw) } catch (_: Throwable) {
            return@synchronized err("引擎返回了无效载入结果")
        }
        if (result.optBoolean("ok", false)) {
            activeProjectFile = fileName
            val recoveryRc = configureRecovery(ensure(), fileName)
            if (recoveryRc != 0) {
                return@synchronized err("工程已载入，但恢复目录初始化失败（$recoveryRc）", recoveryRc)
            }
        }
        result.toString()
    }

    /** 编辑器成功执行工程修改后调用，时间来自 Android 单调时钟。 */
    @JavascriptInterface
    fun markProjectDirty(): Int = synchronized(stateLock) {
        val handle = ensure()
        if (handle == 0L || activeProjectFile == null) return@synchronized -1007
        nativeRecoveryMarkDirty(handle, android.os.SystemClock.elapsedRealtime())
    }

    @JavascriptInterface
    fun recoveryAvailable(): Boolean = synchronized(stateLock) {
        val handle = ensure()
        handle != 0L && activeProjectFile != null && nativeRecoveryHasSnapshot(handle) > 0
    }

    /** 仅在用户明确确认后调用；恢复成功后重新标脏，等待正常工程保存。 */
    @JavascriptInterface
    fun restoreRecovery(): String = synchronized(stateLock) {
        val handle = ensure()
        if (handle == 0L || activeProjectFile == null) {
            return@synchronized err("当前工程尚未就绪", -1007)
        }
        val snapshot = nativeRecoveryReadLatest(handle)
            ?: return@synchronized err("没有可恢复的有效快照", -1009)
        val loaded = try {
            JSONObject(nativeProjectLoad(handle, snapshot))
        } catch (e: Throwable) {
            return@synchronized err("恢复快照载入失败：${e.message}", -1010)
        }
        if (loaded.optBoolean("ok", false)) {
            nativeRecoveryMarkDirty(handle, android.os.SystemClock.elapsedRealtime())
        }
        loaded.toString()
    }

    /** 用户拒绝恢复时清理该工程的恢复快照。 */
    @JavascriptInterface
    fun discardRecovery(): Int = synchronized(stateLock) {
        val handle = ensure()
        if (handle == 0L) return@synchronized -1007
        nativeRecoveryMarkClean(handle)
    }

    /** 永久删除指定草稿；只允许删除受控目录内的 .nclip 文件。 */
    @JavascriptInterface
    fun deleteProject(fileName: String): String = synchronized(stateLock) {
        val target = projectFile(fileName) ?: return@synchronized err("草稿文件名非法")
        val backup = java.io.File(target.path + ".bak")
        val pending = java.io.File(target.path + ".new")
        if (!target.exists() && !backup.exists() && !pending.exists()) {
            return@synchronized err("草稿不存在")
        }
        val deleted = try {
            AtomicFile(target).delete()
            !target.exists() && !backup.exists() && !pending.exists()
        } catch (_: Throwable) { false }
        if (!deleted) return@synchronized err("草稿删除失败")
        if (activeProjectFile == fileName) {
            val handle = ensure()
            if (handle != 0L) nativeRecoveryMarkClean(handle)
            activeProjectFile = null
        }
        recoveryProjectDirectory(fileName)?.let { directory ->
            if (directory.exists()) directory.deleteRecursively()
        }
        JSONObject().apply {
            put("ok", true); put("code", 0); put("message", "草稿已删除")
            put("result", JSONObject().put("file", fileName))
        }.toString()
    }

    /**
     * 安装模型：读入文件字节 → 算 SHA-256 → 交给 core 校验并记录。
     * core 不碰文件系统，读写都由平台层完成。
     * 返回 {"installed":1|0,"message":"…"}。
     */
    @JavascriptInterface
        fun installModel(modelId: String, filePath: String): String {
        synchronized(stateLock) {
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
                               android.os.SystemClock.elapsedRealtime() / 1000)
        } catch (e: Throwable) {
            err("安装调用失败：${e.message}")
        }
    
        }}

    /** 模型文件应放的目录（应用私有）。 */
    @JavascriptInterface
    fun modelsDir(): String = modelRoot()?.absolutePath ?: ""

    private fun modelRoot(): java.io.File? {
        val base = appContext?.filesDir ?: return null
        return java.io.File(base, "models").also { it.mkdirs() }
    }

    /** AI 模型目录与状态（nc_model_catalog / nc_model_info / nc_model_status）。 */
    @JavascriptInterface
        fun models(): String = try {
        synchronized(stateLock) {
        nativeModels(ensure())
    
        }} catch (e: Throwable) {
        """{"models":[]}"""
    }

    private fun renderDataUrl(
        a: Long, width: Int, isThumb: Boolean, thumbIndex: Int
    ): String {
        val handle = ensure()
        val raw: ByteArray = try {
            if (isThumb) nativeThumb(handle, a, thumbIndex, width.coerceIn(16, 1024))
            else nativeFrame(handle, a, width.coerceIn(16, 2048))
        } catch (e: Throwable) {
            if (!isThumb) lastPreviewError = "预览调用失败：${e.message ?: e.javaClass.simpleName}"
            ByteArray(0)
        }
        if (!isThumb) {
            if (raw.size < 12) {
                val error = try { JSONObject(nativeLastError(handle)) } catch (_: Throwable) { JSONObject() }
                val code = error.optInt("code", 0)
                val message = error.optString("message", "")
                // NC_ERROR_NOT_FOUND(2) 表示当前时间点没有活动片段，不应误报为解码故障。
                lastPreviewError = if (code == 2) "" else
                    message.ifBlank { "媒体解码或预览渲染失败（错误码 $code）" }
            } else {
                lastPreviewError = ""
            }
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

    private fun err(msg: String, code: Int = -1): String =
        JSONObject().apply {
            put("ok", false); put("code", code); put("message", msg)
            put("result", JSONObject())
        }.toString()

    private external fun nativeCreate(): Long
    private external fun nativeDestroy(handle: Long)
    private external fun nativeCall(handle: Long, fn: String, argsJson: String, nowMs: Long): String
    private external fun nativeProjectSave(handle: Long): String?
    private external fun nativeProjectLoad(handle: Long, projectText: String): String
    private external fun nativeRecoveryConfigure(handle: Long, directory: String): Int
    private external fun nativeRecoveryMarkDirty(handle: Long, nowMs: Long): Int
    private external fun nativeRecoveryTick(handle: Long, nowMs: Long): Int
    private external fun nativeRecoveryHasSnapshot(handle: Long): Int
    private external fun nativeRecoveryReadLatest(handle: Long): String?
    private external fun nativeRecoveryMarkClean(handle: Long): Int
    private external fun nativeApplyCommand(handle: Long, requestJson: String, nowMs: Long): String?
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
    private external fun nativeLastError(handle: Long): String
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
            /* M-1：instanceForSurface 的读取与 pendingRasterizerRegister
               的读写必须在 stateLock 内完成 —— 否则"读实例→写标记"之间
               可能插入 bindInstance()，导致标记被覆盖或重复注册。 */
            synchronized(stateLock) {
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
        }

        fun unregisterTextRasterizer() {
            synchronized(stateLock) {
                pendingRasterizerRegister = false
                val inst = instanceForSurface ?: return
                try {
                    inst.nativeRegisterTextRasterizer(null)
                } catch (e: Throwable) {
                }
            }
        }

        /* ---- SurfaceView 零拷贝（P6）----
           这些是 companion 方法，供 MainActivity 调用；
           内部转发到实例 external（external 必须是非静态的，
           因为 JNI 签名带 jobject self）。 */

        /* Fix 1：跨线程可见性。读写都不再依赖 Companion 监视器
           （Companion 监视器与实例监视器是**两把不同的锁**，
            这正是上一版修复无效的根因）。 */
        /* S1-1 · F-3：**唯一**的 Engine 生命周期监视器。
           所有访问 engine 的路径（实例方法与 companion 方法）都必须持有它。
           不再使用 @Synchronized —— 它在实例方法上锁 this、在 companion 方法上
           锁 Companion，是两把不同的锁，正是此前同步失效的根因。 */
        private val stateLock = Any()

        @Volatile
        private var instanceForSurface: NativeEngineBridge? = null

        /** 引擎实例尚未创建时收到的「注册文本栅格化器」请求。 */
        private var pendingRasterizerRegister = false

        /** 当前绑定实例（MainActivity 在内存回调里要用）。
            M-1：读取也走 stateLock，与所有写入点使用同一监视器，
            避免"读到正在被替换的绑定"。 */
        val instance: NativeEngineBridge?
            get() = synchronized(stateLock) { instanceForSurface }

        /**
         * 解除全局绑定（S1-1 Fix 1）。
         *
         * 由实例的 release() 调用：本实例销毁后，companion 的 Surface /
         * 内存回调不得再把调用转发到它。在实例监视器内完成，与
         * release() 的 @Synchronized 及各回调的 synchronized(stateLock) 同一把锁
         * （Java 监视器可重入，故在 release() 内调用不会自死锁）。
         */
        fun unbindIfCurrent(inst: NativeEngineBridge) {
            synchronized(stateLock) {
                if (instanceForSurface === inst) {
                    instanceForSurface = null
                }
            }
        }

        fun attachSurface(surface: android.view.Surface?, width: Int, height: Int): Boolean {
            return synchronized(stateLock) {
                val inst = instanceForSurface ?: return false
                try {
                    inst.nativeAttachSurface(surface, width, height)
                } catch (e: Throwable) {
                    false
                }
            }
        }

        fun detachSurface() {
            synchronized(stateLock) {
                val inst = instanceForSurface ?: return
                try {
                    inst.nativeDetachSurface()
                } catch (e: Throwable) {
                }
            }
        }

        fun blitFrame(handle: Long, timeUs: Long): Boolean {
            return synchronized(stateLock) {
                val inst = instanceForSurface ?: return false
                try {
                    inst.nativeBlitFrame(handle, timeUs)
                } catch (e: Throwable) {
                    false
                }
            }
        }

        /** 安装崩溃处理器（§8.1）。用单例实例转发。 */
        fun installCrashHandler(dir: String): Boolean {
            return synchronized(stateLock) {
                val inst = instanceForSurface ?: return false
                try {
                    inst.nativeInstallCrashHandler(dir)
                } catch (e: Throwable) {
                    false
                }
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
            return synchronized(stateLock) {
                val inst = instanceForSurface ?: return false
                try {
                    inst.nativePreviewSetCacheBudget(bytes) == 0
                } catch (e: Throwable) {
                    false
                }
            }
        }

        /** 读取引擎当前的帧缓存预算（字节；引擎未就绪返回 -1）。 */
        fun previewCacheBudget(): Long {
            return synchronized(stateLock) {
                val inst = instanceForSurface ?: return -1L
                try {
                    inst.nativePreviewGetCacheBudget()
                } catch (e: Throwable) {
                    -1L
                }
            }
        }

        /** 释放内存（§8.2 第 11 条）。返回估算释放字节数。 */
        fun releaseMemory(level: Int): Long {
            return synchronized(stateLock) {
                val inst = instanceForSurface ?: return 0L
                try {
                    inst.nativeReleaseMemory(level)
                } catch (e: Throwable) {
                    0L
                }
            }
        }

        fun surfaceReady(): Boolean {
            return synchronized(stateLock) {
                val inst = instanceForSurface ?: return false
                try {
                    inst.nativeSurfaceReady()
                } catch (e: Throwable) {
                    false
                }
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
            /* Fix 1：绑定本身也必须在实例监视器内完成，
               否则 release() 可能正好在"已绑定但预算未设"的窗口执行，
               留下"绑定了已销毁实例"的状态。 */
            synchronized(stateLock) {
                /* F-1：只接受"确实持有 Engine"的实例。创建被拒绝的实例
                   （editor == 0）不得成为全局绑定对象，否则 Surface / 内存
                   回调会打到没有引擎的实例上。 */
                if (inst.editor == 0L) {
                    android.util.Log.w("NcEngine", "bindInstance 拒绝：该实例未持有 Engine")
                    return
                }
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
}
