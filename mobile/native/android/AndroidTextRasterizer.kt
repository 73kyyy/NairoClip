package com.nairoclip.app

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.text.Layout
import android.text.StaticLayout
import android.text.TextPaint
import java.nio.ByteBuffer

/**
 * 文本栅格化（§4.6 + §4.10）。
 *
 * 定案：**栅格化走平台侧**，用 Android 的 Canvas / StaticLayout。
 * 不用 stb_truetype 之类的内置软渲染，原因：
 *   · 系统字体引擎有完整的 hinting / 字距 / 连字 / emoji 回退，
 *     质量明显高于任何手写软渲染器；
 *   · 「三端像素一致」这个理由不成立 —— 预览本来就按代理分辨率渲染，
 *     像素级差异是预期行为（见 PERF_XFORM_PLAN.md §4.10）。
 *
 * 关键决定：用 StaticLayout 而不是自己算字形。
 * 它负责换行、行距、字距、emoji 回退 —— 这些手写必然出错。
 */
object AndroidTextRasterizer {

    /**
     * 栅格化一段文本。
     * @return 打包格式：w:int32 + h:int32 + RGBA8 字节流；失败返回 null
     */
    @JvmStatic
    fun rasterize(
        text: String,
        fontFamily: String,
        fontSize: Float,
        color: String,
        outlineColor: String,
        outlineWidth: Float,
        bgColor: String,
        bgPadding: Float,
        alignment: Int,
        maxWidth: Int,
        maxHeight: Int
    ): ByteArray? {
        if (text.isEmpty()) return null

        val paint = TextPaint(Paint.ANTI_ALIAS_FLAG).apply {
            this.textSize = if (fontSize > 0f) fontSize else 48f
            this.color = parseColor(color, Color.WHITE)
            this.isAntiAlias = true
            this.isSubpixelText = true
            this.typeface = if (fontFamily.isEmpty() || fontFamily == "默认") {
                Typeface.DEFAULT
            } else {
                try {
                    Typeface.create(fontFamily, Typeface.NORMAL)
                } catch (e: Throwable) {
                    Typeface.DEFAULT
                }
            }
        }

        // 描边靠 Stroke 样式 + 先画粗后画细
        val strokePaint = if (outlineWidth > 0f) {
            TextPaint(paint).apply {
                this.style = Paint.Style.STROKE
                this.strokeWidth = outlineWidth * 2f   // 描边向外扩展，宽度减半才对
                this.strokeJoin = Paint.Join.ROUND
                this.color = parseColor(outlineColor, Color.BLACK)
            }
        } else null

        val width = if (maxWidth > 0) maxWidth else {
            StaticLayout.Builder
                .obtain(text, 0, text.length, paint, Int.MAX_VALUE)
                .build()
                .width
                .coerceAtMost(4096)
        }

        val align = when (alignment) {
            0 -> Layout.Alignment.ALIGN_NORMAL
            2 -> Layout.Alignment.ALIGN_OPPOSITE
            else -> Layout.Alignment.ALIGN_CENTER
        }

        val layout = try {
            StaticLayout.Builder
                .obtain(text, 0, text.length, paint, width)
                .setAlignment(align)
                .setIncludePad(false)
                .setLineSpacing(0f, 1.0f)
                .build()
        } catch (e: Throwable) {
            return null
        }

        val w = layout.width.coerceAtLeast(1)
        val h = layout.height.coerceAtLeast(1)
        // 超高时不静默裁切：如实返回，由内核决定缩放或拒绝。
        // 裁切会让用户看到「文字被切掉一半」却不知道为什么。

        val pad = bgPadding.toInt().coerceAtLeast(0)
        val totalW = w + pad * 2
        val totalH = h + pad * 2

        val bmp = try {
            Bitmap.createBitmap(totalW, totalH, Bitmap.Config.ARGB_8888)
        } catch (e: Throwable) {
            return null
        }

        try {
            val canvas = Canvas(bmp)
            /* 背景必须全透明（alpha=0）—— 否则文本片段会盖住下方画面。 */
            val bg = parseColor(bgColor, Color.TRANSPARENT)
            if (Color.alpha(bg) > 0) {
                canvas.drawColor(bg)
            }

            canvas.translate(pad.toFloat(), pad.toFloat())
            // 先描边再填充：顺序错了描边会盖住字芯
            if (strokePaint != null) {
                val saved = canvas.save()
                layout.draw(canvas)
                canvas.restoreToCount(saved)
            }
            layout.draw(canvas)
        } catch (e: Throwable) {
            bmp.recycle()
            return null
        }

        /* 导出：ARGB_8888 的内存字节序是 B,G,R,A（小端），
           内核要 RGBA8 —— 这里必须换序，不能直接拷。
           （同样的坑在 nativeFrame 里踩过，见 NativeEngineBridge） */
        val out = try {
            val pixels = IntArray(totalW * totalH)
            bmp.getPixels(pixels, 0, totalW, 0, 0, totalW, totalH)
            val bytes = ByteBuffer.allocate(8 + totalW * totalH * 4)
            bytes.putInt(totalW)
            bytes.putInt(totalH)
            for (p in pixels) {
                bytes.put(((p shr 16) and 0xFF).toByte())  // R
                bytes.put(((p shr 8) and 0xFF).toByte())   // G
                bytes.put((p and 0xFF).toByte())           // B
                bytes.put(((p ushr 24) and 0xFF).toByte()) // A
            }
            bytes.array()
        } catch (e: Throwable) {
            null
        } finally {
            bmp.recycle()
        }
        return out
    }

    /** 解析 "#RRGGBB" / "#AARRGGBB" / "#RGB"。失败返回 fallback。 */
    private fun parseColor(s: String, fallback: Int): Int {
        if (s.isEmpty()) return fallback
        return try {
            Color.parseColor(s)
        } catch (e: Throwable) {
            fallback
        }
    }
}
