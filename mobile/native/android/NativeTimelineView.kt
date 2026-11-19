package com.nairoclip.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import kotlin.math.ceil
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min

/**
 * Dedicated Android View for the editor timeline.
 *
 * The timeline is intentionally not composed from hundreds of small Compose
 * nodes. It draws the ruler, track lanes, clips and playhead in one Canvas pass,
 * and owns horizontal pan / pinch zoom at the View level. The first shell uses
 * empty tracks; real project snapshots and edit commands are connected in the
 * following S5 slices after the shared timeline contract is frozen.
 */
data class NativeTimelineClip(
    val id: Long,
    val startUs: Long,
    val durationUs: Long,
    val label: String
)

data class NativeTimelineTrack(
    val id: Long,
    val name: String,
    val clips: List<NativeTimelineClip> = emptyList()
)

class NativeTimelineView(context: Context) : View(context) {

    var onPlayheadChanged: ((Long) -> Unit)? = null

    private val density = resources.displayMetrics.density
    private val rulerHeight = 27f * density
    private val labelWidth = 46f * density
    private val rowHeight = 47f * density
    private val rowGap = 4f * density

    private val backgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF111314.toInt() }
    private val rulerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF151719.toInt() }
    private val lanePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF1D2022.toInt() }
    private val alternateLanePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF202326.toInt() }
    private val labelPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF9B9B9E.toInt()
        textSize = 9f * density
    }
    private val rulerTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF9B9B9E.toInt()
        textSize = 9f * density
    }
    private val tickPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF4B5053.toInt()
        strokeWidth = density
    }
    private val clipPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF454B4F.toInt() }
    private val clipTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFE4E5E6.toInt()
        textSize = 9f * density
    }
    private val playheadPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = android.graphics.Color.WHITE
        strokeWidth = 1.5f * density
    }
    private val playheadCapPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = android.graphics.Color.WHITE
    }

    private var tracks: List<NativeTimelineTrack> = listOf(
        NativeTimelineTrack(1L, "V1"),
        NativeTimelineTrack(2L, "A1"),
        NativeTimelineTrack(3L, "T1")
    )
    private var durationUs: Long = 30_000_000L
    private var playheadUs: Long = 0L
    private var pixelsPerSecond = 42f * density
    private var scrollOffset = 0f
    private var lastTouchX = 0f
    private var moved = false

    private val scaleDetector = ScaleGestureDetector(
        context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScale(detector: ScaleGestureDetector): Boolean {
                val old = pixelsPerSecond
                pixelsPerSecond = (pixelsPerSecond * detector.scaleFactor).coerceIn(
                    12f * density,
                    180f * density
                )
                val focusTime = (scrollOffset + detector.focusX - labelWidth).coerceAtLeast(0f) / old
                scrollOffset = (focusTime * pixelsPerSecond - (detector.focusX - labelWidth))
                    .coerceAtLeast(0f)
                invalidate()
                return true
            }
        }
    )

    init {
        isClickable = true
        contentDescription = "多轨时间轴，可横向拖动和双指缩放"
        setLayerType(View.LAYER_TYPE_HARDWARE, null)
    }

    fun setTimeline(
        newTracks: List<NativeTimelineTrack>,
        totalDurationUs: Long,
        currentPlayheadUs: Long
    ) {
        tracks = newTracks
        durationUs = totalDurationUs.coerceAtLeast(1L)
        playheadUs = currentPlayheadUs.coerceIn(0L, durationUs)
        invalidate()
    }

    fun setPlayhead(timeUs: Long) {
        playheadUs = timeUs.coerceIn(0L, durationUs)
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), backgroundPaint)

        val contentWidth = (width - labelWidth).coerceAtLeast(1f)
        val centerX = labelWidth + contentWidth * 0.46f
        drawRuler(canvas, centerX)
        drawTracks(canvas)

        canvas.drawLine(centerX, 0f, centerX, height.toFloat(), playheadPaint)
        canvas.drawCircle(centerX, 4f * density, 3f * density, playheadCapPaint)
        drawTimeReadout(canvas, centerX)
    }

    private fun drawRuler(canvas: Canvas, playheadX: Float) {
        canvas.drawRect(labelWidth, 0f, width.toFloat(), rulerHeight, rulerPaint)
        val secondsPerTick = 5f
        val first = floor(scrollOffset / pixelsPerSecond / secondsPerTick).toInt()
        val last = ceil((scrollOffset + width - labelWidth) / pixelsPerSecond / secondsPerTick).toInt()
        for (tick in first..last) {
            val seconds = tick * secondsPerTick
            val x = labelWidth + seconds * pixelsPerSecond - scrollOffset
            if (x < labelWidth || x > width) continue
            canvas.drawLine(x, rulerHeight - 7f * density, x, rulerHeight, tickPaint)
            canvas.drawText(formatTime(seconds.toLong()), x + 3f * density, 11f * density, rulerTextPaint)
        }
        canvas.drawLine(labelWidth, rulerHeight, width.toFloat(), rulerHeight, tickPaint)
        canvas.drawText(formatTime(playheadUs / 1_000_000L), playheadX + 5f * density, 23f * density, clipTextPaint)
    }

    private fun drawTracks(canvas: Canvas) {
        tracks.forEachIndexed { index, track ->
            val top = rulerHeight + index * (rowHeight + rowGap)
            val bottom = min(height.toFloat(), top + rowHeight)
            if (top >= height) return@forEachIndexed

            val lane = if (index % 2 == 0) alternateLanePaint else lanePaint
            canvas.drawRect(0f, top, width.toFloat(), bottom, lane)
            canvas.drawText(track.name.take(5), 8f * density, top + rowHeight * 0.58f, labelPaint)

            track.clips.forEach { clip ->
                val left = labelWidth + clip.startUs / 1_000_000f * pixelsPerSecond - scrollOffset
                val right = left + max(clip.durationUs / 1_000_000f * pixelsPerSecond, 5f * density)
                if (right < labelWidth || left > width) return@forEach
                val rect = RectF(
                    max(left, labelWidth),
                    top + 5f * density,
                    min(right, width.toFloat()),
                    bottom - 5f * density
                )
                canvas.drawRoundRect(rect, 3f * density, 3f * density, clipPaint)
                if (rect.width() > 34f * density) {
                    canvas.drawText(clip.label, rect.left + 5f * density, rect.centerY() + 3f * density, clipTextPaint)
                }
            }
        }
    }

    private fun drawTimeReadout(canvas: Canvas, centerX: Float) {
        val text = formatTime(playheadUs / 1_000_000L)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
            color = android.graphics.Color.BLACK
            textSize = 8f * density
            typeface = android.graphics.Typeface.create(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD)
        }
        val width = paint.measureText(text) + 8f * density
        val top = 2f * density
        val rect = RectF(centerX - width / 2f, top, centerX + width / 2f, top + 13f * density)
        canvas.drawRoundRect(rect, 2f * density, 2f * density, playheadCapPaint)
        canvas.drawText(text, rect.left + 4f * density, rect.bottom - 3f * density, paint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        scaleDetector.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastTouchX = event.x
                moved = false
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (!scaleDetector.isInProgress && event.pointerCount == 1) {
                    val dx = event.x - lastTouchX
                    if (kotlin.math.abs(dx) > 1.5f * density) moved = true
                    scrollOffset = (scrollOffset - dx).coerceAtLeast(0f)
                    lastTouchX = event.x
                    updatePlayheadFromCenter()
                    invalidate()
                }
                return true
            }
            MotionEvent.ACTION_UP -> {
                if (!moved && !scaleDetector.isInProgress) {
                    updatePlayheadFromTouch(event.x)
                    performClick()
                }
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun updatePlayheadFromCenter() {
        val centerX = labelWidth + (width - labelWidth).coerceAtLeast(1f) * 0.46f
        playheadUs = (((scrollOffset + centerX - labelWidth) / pixelsPerSecond) * 1_000_000L)
            .toLong().coerceIn(0L, durationUs)
        onPlayheadChanged?.invoke(playheadUs)
    }

    private fun updatePlayheadFromTouch(x: Float) {
        val seconds = (scrollOffset + x - labelWidth).coerceAtLeast(0f) / pixelsPerSecond
        playheadUs = (seconds * 1_000_000L).toLong().coerceIn(0L, durationUs)
        onPlayheadChanged?.invoke(playheadUs)
        invalidate()
    }

    private fun formatTime(seconds: Long): String {
        val safe = seconds.coerceAtLeast(0L)
        val minutes = safe / 60L
        val remaining = safe % 60L
        return "%02d:%02d".format(minutes, remaining)
    }
}
