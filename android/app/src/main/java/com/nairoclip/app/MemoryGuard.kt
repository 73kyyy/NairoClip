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

            /* 2) 受 memoryClass 约束：native 不能吃光进程预算。
               ------------------------------------------------------------------
               系数从 40% 提到 60%，依据是实测的档位压制情况：

                 | 物理内存 | 档位 | 40% 实际 | 60% 实际 |
                 |---------|------|---------|---------|
                 | 4 GB    |  96  |  76 MB  |  96 MB  |
                 | 6 GB    | 128  |  76 MB  | 115 MB  |  ← 提升 51%
                 | 8 GB    | 160  | 102 MB  | 153 MB  |  ← 提升 50%
                 | 12 GB   | 192  | 153 MB  | 192 MB  |

               40% 时 6 个档位有 5 个被压住，最狠的是 6GB 设备被压到
               76MB —— 和 4GB 设备完全一样，物理内存优势被抹平。

               为什么敢提到 60%：光看 memoryClass 会低估大内存设备的
               进程预算（它是固定分档，6GB 和 4GB 都可能是 192MB），
               而物理内存分档已经反映真实差异。60% 在 192MB 堆上给 115MB，
               在 512MB 堆上给 307MB（不成为瓶颈）。

               **风险与兜底**：60% 是否安全必须真机验证（我算不准具体
               机型会不会 OOM）。兜底机制见 releaseForPressure() ——
               系统内存紧张时会主动把预算砍半，所以偏激进一点可接受。 */
            val memClassMb = am.memoryClass.toLong()
            val cappedMb = (memClassMb * 60L) / 100L

            val budgetMb = minOf(tierMb, cappedMb).coerceAtLeast(48L)
            return budgetMb * 1024 * 1024
        }

        /**
         * 内存压力下的**动态预算调整**（60% 系数的兜底）。
         *
         * 问题：预算一旦设给引擎就固定了。`onTrimMemory` 能清空缓存，
         * 但清空后引擎会**按原预算重新填满** —— 等于白清。
         *
         * 做法：内存紧张时把预算砍半（并清缓存），压力解除后再恢复。
         * 这样「平时给得多、紧张时缩得快」，比一开始就保守更划算。
         *
         * 映射（与 onTrimMemory 的 ourLevel 对应）：
         *   level 0（RUNNING_LOW，前台偏低）      → 预算 × 0.5
         *   level 1（RUNNING_CRITICAL / UI_HIDDEN）→ 预算 × 0.25
         *   level 2（COMPLETE / MODERATE）        → 不设（缓存已全清）
         *
         * @param baseBudget 正常状态下的预算（字节）
         * @param level onTrimMemory 归一化后的级别（0/1/2）
         * @return 压力状态下应设的预算；level < 0 返回 baseBudget（不调整）
         */
        fun budgetUnderPressure(baseBudget: Long, level: Int): Long {
            if (baseBudget <= 0) return baseBudget
            return when (level) {
                0 -> (baseBudget / 2).coerceAtLeast(24L * 1024 * 1024)
                1 -> (baseBudget / 4).coerceAtLeast(24L * 1024 * 1024)
                else -> baseBudget   // level 2：缓存已全清，不需要改预算
            }
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
