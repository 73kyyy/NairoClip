package com.nairoclip.app

import android.app.ActivityManager
import android.content.ComponentCallbacks2
import android.content.Context
import android.util.Log

/**
 * 内存压力响应（§8.2 第 11 条）。
 *
 * 为什么必须做：`onTrimMemory` 是 Android 在**杀进程之前**给的
 * 最后一次机会。不响应就是「闪退」；响应了最多是「重算一帧」，
 * 用户几乎无感。实测现状：代码里完全没监听这个回调。
 *
 * 分级映射：
 *   RUNNING_LOW(10)      前台但内存偏低  → level 0 清帧缓存
 *   RUNNING_CRITICAL(15) 前台且严重不足  → level 1 连解码器一起释放
 *   UI_HIDDEN(20)        用户按了 HOME   → level 1 中等清理
 *   COMPLETE/MODERATE    后台即将被杀    → level 2 全部可重建缓存
 *
 * 关键判断：前台才克制清理；后台用户看不到画面，清得越干净越好。
 */
class MemoryGuard(
    private val context: Context,
    private val onRelease: (Int) -> Long
) : ComponentCallbacks2 {

    private val tag = "NcMemoryGuard"

    @Volatile
    var totalReleasedBytes: Long = 0L
        private set

    @Volatile
    var lastLevel: Int = -1
        private set

    override fun onTrimMemory(level: Int) {
        val ourLevel = when {
            level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_CRITICAL -> 1
            level >= ComponentCallbacks2.TRIM_MEMORY_RUNNING_LOW -> 0
            level >= ComponentCallbacks2.TRIM_MEMORY_COMPLETE -> 2
            level >= ComponentCallbacks2.TRIM_MEMORY_MODERATE -> 2
            level >= ComponentCallbacks2.TRIM_MEMORY_UI_HIDDEN -> 1
            else -> -1
        }
        if (ourLevel < 0) return

        lastLevel = ourLevel
        try {
            val freed = onRelease(ourLevel)
            if (freed > 0) {
                totalReleasedBytes += freed
                Log.i(tag, "onTrimMemory($level) -> level $ourLevel, released " +
                    "${freed / 1024 / 1024}MB (total ${totalReleasedBytes / 1024 / 1024}MB)")
            }
        } catch (e: Throwable) {
            Log.w(tag, "release failed (ignored)", e)
        }
    }

    override fun onConfigurationChanged(newConfig: android.content.res.Configuration) {
        // 配置变化（旋转/语言）与内存无关
    }

    override fun onLowMemory() {
        // 比 onTrimMemory 更严重，不再分级
        lastLevel = 2
        try {
            val freed = onRelease(2)
            if (freed > 0) totalReleasedBytes += freed
            Log.w(tag, "onLowMemory -> released ${freed / 1024 / 1024}MB")
        } catch (e: Throwable) {
        }
    }

    companion object {
        /**
         * 按设备内存算帧缓存预算（§8.2 第 9 条）。
         *
         * 关键：读 memoryClass（本 App 的堆限制），不是手机总内存。
         * 24GB 的旗舰机，单 App 堆限制通常仍是 256-512MB。
         *
         * 取堆的约 30%，因为帧缓存之外还有：解码器缓冲、Bitmap、
         * WebView 自身的堆、代理渲染的临时缓冲。
         *
         * 上限 128MB —— OOM 是断崖式的（不是变慢，是进程被杀）。
         */
        fun frameCacheBudgetBytes(context: Context): Long {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
                ?: return 64L * 1024 * 1024
            val memClassMb = am.memoryClass.toLong()
            val budgetMb = (memClassMb * 30L) / 100L
            return budgetMb.coerceIn(48L, 128L) * 1024L * 1024L
        }

        fun deviceTier(context: Context): String {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
                ?: return "未知"
            return when {
                am.memoryClass >= 384 -> "旗舰"
                am.memoryClass >= 256 -> "高端"
                am.memoryClass >= 192 -> "中端"
                else -> "低端"
            }
        }
    }
}
