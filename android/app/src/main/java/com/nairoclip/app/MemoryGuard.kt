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
         * ## 为什么需要两个指标
         *
         * 帧缓存存的是 `std::vector<uint8_t>`（**native heap**），
         * 不受 Java 堆（`memoryClass`）直接限制 —— 单看 memoryClass 会
         * 让 24GB 手机和 8GB 手机拿到同一个 128MB，毫无区分度。
         *
         * 但也不能只看物理内存：native 与 Java 堆共享同一个进程预算，
         * 24GB 手机如果给 8GB 帧缓存，进程照样会被系统杀掉。
         *
         * 所以两个一起看：
         *   · **物理内存（totalMem）** 决定档位 —— 大内存设备 native 可以多给；
         *   · **memoryClass × 0.4** 作为上界 —— 防止 native 吃光进程预算。
         *
         * ## 分档依据
         *
         * 1080p 单帧 RGBA = 8.3MB（1920×1080×4）。各档能存：
         *   192MB → 23 帧（0.77s @30fps）
         *   128MB → 15 帧（0.50s）
         *    64MB →  7 帧（0.23s）
         *
         * 0.5 秒是「来回拖一下」的合理缓存深度；低于这个值，
         * 用户拖超过半秒就要全部重算，体感明显卡顿。所以 4GB 设备
         * 保底 64MB（0.23s）—— 再少不如不做缓存。
         *
         * ## 与 core 的对齐
         *
         * 算出的值由 MainActivity 通过 `nc_preview_set_cache_budget`
         * 传给引擎。core 侧还会夹在 [24MB, 256MB]（见 preview.cpp），
         * 这里不重复那层保护 —— 单一职责：这里只负责「按设备算」。
         */
        fun frameCacheBudgetBytes(context: Context): Long {
            val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
                ?: return 96L * 1024 * 1024   // 拿不到服务：用 curl 过的中位数，不用最保守的 64

            // 1) 按物理内存分档
            val totalMemBytes = android.app.ActivityManager.MemoryInfo().also {
                am.getMemoryInfo(it)
            }.totalMem
            val totalGb = totalMemBytes / (1024.0 * 1024.0 * 1024.0)
            val tierMb = when {
                totalGb >= 12.0 -> 192L
                totalGb >= 8.0 -> 160L
                totalGb >= 6.0 -> 128L
                totalGb >= 4.0 -> 96L
                else -> 64L
            }

            // 2) 受 memoryClass 约束：native 不能吃光进程预算。
            //    0.4 而不是 0.3 —— 原来 0.3 在 512MB 堆上只给 154MB，
            //    把 12GB 设备压到了 154MB（本该 192MB）。
            //    0.4 在 512MB 堆上给 204MB，不成为瓶颈；在 192MB 堆上
            //    给 77MB，仍能挡住低端机被误配成旗舰值。
            val memClassMb = am.memoryClass.toLong()
            val cappedMb = (memClassMb * 40L) / 100L

            val budgetMb = minOf(tierMb, cappedMb).coerceAtLeast(48L)
            return budgetMb * 1024 * 1024
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
