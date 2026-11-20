package com.nairoclip.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.os.Bundle
import android.view.MotionEvent
import android.view.ScaleGestureDetector
import android.view.View
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.SeekBar
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
    val label: String,
    val enabled: Boolean = true
)

data class NativeTimelineTrack(
    val id: Long,
    val name: String,
    val clips: List<NativeTimelineClip> = emptyList(),
    val visible: Boolean = true,
    val muted: Boolean = false,
    val engineIndex: Int = -1
)

class NativeTimelineView(context: Context) : View(context) {

    var onPlayheadChanged: ((Long) -> Unit)? = null
    var onClipSelected: ((Int, Long) -> Unit)? = null
    var onClipSelectionCleared: (() -> Unit)? = null
    var onTrackVisibilityChanged: ((Int, Boolean) -> Unit)? = null
    var onTrackMuteChanged: ((Int, Boolean) -> Unit)? = null

    private val density = resources.displayMetrics.density
    private val rulerHeight = 27f * density
    private val labelWidth = 120f * density
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
    private val selectedClipPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF777F86.toInt()
        style = Paint.Style.STROKE
        strokeWidth = 2f * density
    }
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
    private val readoutPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = android.graphics.Color.BLACK
        textSize = 8f * density
        typeface = android.graphics.Typeface.create(
            android.graphics.Typeface.DEFAULT,
            android.graphics.Typeface.BOLD
        )
    }
    private val emptyStatePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFB8BEC2.toInt()
        textSize = 13f * density
        textAlign = Paint.Align.CENTER
    }
    private val emptyHintPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF777F84.toInt()
        textSize = 10f * density
        textAlign = Paint.Align.CENTER
    }

    // Never fabricate placeholder lanes before an engine snapshot arrives.
    // The empty-state renderer explains the blank project without implying
    // that video/audio/text tracks already exist in the user's project.
    private var tracks: List<NativeTimelineTrack> = emptyList()
    // Keep a detached source snapshot rather than the caller's list identity.
    // Some hosts reuse and mutate a list in place; identity-only invalidation
    // would then leave stale clips on screen. Track-control gestures only emit
    // commands; the rendered [tracks] snapshot remains engine-authoritative.
    private var sourceTracksSnapshot: List<NativeTimelineTrack>? = null
    private var durationUs: Long = 30_000_000L
    private var playheadUs: Long = 0L
    private var pixelsPerSecond = 42f * density
    private var scrollOffset = 0f
    private var lastTouchX = 0f
    private var lastTouchY = 0f
    private var moved = false
    private var draggingPlayhead = false
    private var draggingTimelineVertically = false
    private var draggingTimelineHorizontally = false
    private var touchStartedInTimelineLane = false
    private var verticalScrollOffset = 0f
    // Once a gesture becomes multi-touch, lifting one finger must not turn the
    // remaining finger-up into a tap, seek, scrub, or one-finger pan.
    private var gestureHadMultiplePointers = false
    private val touchSlop = android.view.ViewConfiguration.get(context).scaledTouchSlop.toFloat()
    private var selectedClip: Pair<Long, Long>? = null

    private val scaleDetector = ScaleGestureDetector(
        context,
        object : ScaleGestureDetector.SimpleOnScaleGestureListener() {
            override fun onScaleBegin(detector: ScaleGestureDetector): Boolean {
                gestureHadMultiplePointers = true
                draggingPlayhead = false
                moved = true
                return true
            }

            override fun onScale(detector: ScaleGestureDetector): Boolean {
                val old = pixelsPerSecond
                val factor = detector.scaleFactor
                if (!old.isFinite() || old <= 0f || !factor.isFinite() || factor <= 0f) {
                    return false
                }
                val focusOffset = detector.focusX.toDouble() - labelWidth.toDouble()
                val focusTimeSeconds = (
                    scrollOffset.toDouble() + focusOffset
                    ).coerceAtLeast(0.0) / old.toDouble()
                pixelsPerSecond = (old * factor).coerceIn(
                    12f * density,
                    180f * density
                )
                // Preserve the timeline time under the pinch focus, subject to
                // the legal scroll range at the new zoom level.
                val requestedOffset = focusTimeSeconds * pixelsPerSecond.toDouble() - focusOffset
                scrollOffset = requestedOffset.coerceIn(0.0, maxScrollOffset().toDouble()).toFloat()
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
        var tracksChanged = false
        if (sourceTracksSnapshot != newTracks) {
            val detachedTracks = newTracks.map { it.copy(clips = it.clips.toList()) }
            sourceTracksSnapshot = detachedTracks
            tracks = detachedTracks
            tracksChanged = true
            if (selectedClip != null && tracks.none { track ->
                    track.id == selectedClip!!.first &&
                        track.visible &&
                        track.clips.any { it.id == selectedClip!!.second && it.enabled }
                }) {
                selectedClip = null
            }
        }

        val safeDurationUs = totalDurationUs.coerceAtLeast(1L)
        val safePlayheadUs = currentPlayheadUs.coerceIn(0L, safeDurationUs)
        val durationChanged = durationUs != safeDurationUs
        val playheadChanged = playheadUs != safePlayheadUs
        if (!tracksChanged && !durationChanged && !playheadChanged) return

        durationUs = safeDurationUs
        playheadUs = safePlayheadUs
        if (tracksChanged || durationChanged) {
            scrollOffset = scrollOffset.coerceIn(0f, maxScrollOffset())
            verticalScrollOffset = verticalScrollOffset.coerceIn(0f, maxVerticalScrollOffset())
        }
        invalidate()
    }

    fun setPlayhead(timeUs: Long) {
        playheadUs = timeUs.coerceIn(0L, durationUs)
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        scrollOffset = scrollOffset.coerceIn(0f, maxScrollOffset())
        verticalScrollOffset = verticalScrollOffset.coerceIn(0f, maxVerticalScrollOffset())
    }

    override fun onInitializeAccessibilityNodeInfo(info: AccessibilityNodeInfo) {
        super.onInitializeAccessibilityNodeInfo(info)
        // Expose the custom-drawn playhead as an adjustable timeline control.
        // Use seconds for RangeInfo: Float cannot represent microsecond timestamps
        // across normal project lengths with useful precision.
        val durationSeconds = (durationUs.toDouble() / 1_000_000.0).toFloat()
        val playheadSeconds = (playheadUs.toDouble() / 1_000_000.0).toFloat()
        info.className = SeekBar::class.java.name
        info.contentDescription =
            "时间轴播放位置，当前 ${formatTime(playheadUs / 1_000_000L)}，" +
                "总长 ${formatTime(durationUs / 1_000_000L)}，包含 ${tracks.size} 条轨道"
        info.isScrollable = true
        info.rangeInfo = AccessibilityNodeInfo.RangeInfo.obtain(
            AccessibilityNodeInfo.RangeInfo.RANGE_TYPE_FLOAT,
            0f,
            durationSeconds.coerceAtLeast(0.001f),
            playheadSeconds.coerceIn(0f, durationSeconds.coerceAtLeast(0.001f))
        )
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD)
        info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD)
    }

    override fun performAccessibilityAction(action: Int, arguments: Bundle?): Boolean {
        val stepUs = 1_000_000L
        val targetUs = when (action) {
            AccessibilityNodeInfo.ACTION_SET_PROGRESS -> {
                val seconds = arguments?.getFloat(
                    AccessibilityNodeInfo.ACTION_ARGUMENT_PROGRESS_VALUE,
                    Float.NaN
                ) ?: Float.NaN
                if (!seconds.isFinite()) return false
                (seconds.toDouble() * 1_000_000.0)
                    .coerceIn(0.0, durationUs.toDouble())
                    .toLong()
            }
            AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ->
                (playheadUs + stepUs).coerceAtMost(durationUs)
            AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD ->
                (playheadUs - stepUs).coerceAtLeast(0L)
            else -> return super.performAccessibilityAction(action, arguments)
        }
        playheadUs = targetUs
        onPlayheadChanged?.invoke(targetUs)
        invalidate()
        sendAccessibilityEvent(android.view.accessibility.AccessibilityEvent.TYPE_VIEW_SELECTED)
        return true
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawRect(0f, 0f, width.toFloat(), height.toFloat(), backgroundPaint)

        // Keep timeline-time arithmetic in Double: Float loses microsecond
        // precision quickly on long projects and can visibly drift the playhead.
        val playheadX = playheadXPosition()
        drawRuler(canvas, playheadX)
        drawTracks(canvas)
        if (tracks.isEmpty()) drawEmptyTimeline(canvas)

        canvas.drawLine(playheadX, 0f, playheadX, height.toFloat(), playheadPaint)
        canvas.drawCircle(playheadX, 4f * density, 3f * density, playheadCapPaint)
        drawTimeReadout(canvas, playheadX)
    }

    private fun drawRuler(canvas: Canvas, playheadX: Float) {
        canvas.drawRect(labelWidth, 0f, width.toFloat(), rulerHeight, rulerPaint)
        val secondsPerTick = 5f
        val first = floor(
            scrollOffset.toDouble() / pixelsPerSecond.toDouble() / secondsPerTick.toDouble()
        ).toLong().coerceAtLeast(0L)
        val last = ceil(
            (scrollOffset.toDouble() + width.toDouble() - labelWidth.toDouble()) /
                pixelsPerSecond.toDouble() / secondsPerTick.toDouble()
        ).toLong().coerceAtLeast(first)
        // A corrupted or extreme viewport must never create an unbounded draw loop.
        val tickCount = (last - first).coerceAtMost(MAX_VISIBLE_RULER_TICKS.toLong())
        for (offset in 0L..tickCount) {
            val tick = first + offset
            val seconds = tick.toDouble() * secondsPerTick.toDouble()
            val x = (
                labelWidth.toDouble() + seconds * pixelsPerSecond.toDouble() -
                    scrollOffset.toDouble()
                ).toFloat()
            if (x < labelWidth || x > width) continue
            canvas.drawLine(x, rulerHeight - 7f * density, x, rulerHeight, tickPaint)
            canvas.drawText(formatTime(seconds.toLong()), x + 3f * density, 11f * density, rulerTextPaint)
        }
        canvas.drawLine(labelWidth, rulerHeight, width.toFloat(), rulerHeight, tickPaint)
        canvas.drawText(formatTime(playheadUs / 1_000_000L), playheadX + 5f * density, 23f * density, clipTextPaint)
    }

    private fun drawTracks(canvas: Canvas) {
        canvas.save()
        canvas.clipRect(0f, rulerHeight, width.toFloat(), height.toFloat())
        tracks.forEachIndexed { index, track ->
            val top = rulerHeight + index * (rowHeight + rowGap) - verticalScrollOffset
            val bottom = min(height.toFloat(), top + rowHeight)
            if (bottom <= rulerHeight || top >= height) return@forEachIndexed

            val lane = if (index % 2 == 0) alternateLanePaint else lanePaint
            canvas.drawRect(0f, top, width.toFloat(), bottom, lane)
            val trackLabel = if (track.name.length > 4) "${track.name.take(3)}…" else track.name
            canvas.drawText(trackLabel, 7f * density, top + rowHeight * 0.58f, labelPaint)
            val visibilityPaint = if (onTrackVisibilityChanged == null || !track.visible) tickPaint else labelPaint
            val mutePaint = if (onTrackMuteChanged == null || track.muted) tickPaint else labelPaint
            canvas.drawText(if (track.visible) "◉" else "○", 54f * density, top + rowHeight * 0.58f, visibilityPaint)
            canvas.drawText(if (track.muted) "×" else "♪", 98f * density, top + rowHeight * 0.58f, mutePaint)

            if (!track.visible) return@forEachIndexed
            track.clips.forEach clipLoop@{ clip ->
                if (!clip.enabled || clip.startUs < 0L || clip.durationUs <= 0L || clip.startUs >= durationUs) return@clipLoop
                val visibleDurationUs = min(clip.durationUs, durationUs - clip.startUs)
                val left = (
                    labelWidth.toDouble() +
                        (clip.startUs.toDouble() / 1_000_000.0) * pixelsPerSecond.toDouble() -
                        scrollOffset.toDouble()
                    ).toFloat()
                val clipWidth = (
                    visibleDurationUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble()
                    ).toFloat()
                val right = left + max(clipWidth, 5f * density)
                if (right < labelWidth || left > width) return@clipLoop
                val rect = RectF(
                    max(left, labelWidth),
                    top + 5f * density,
                    min(right, width.toFloat()),
                    bottom - 5f * density
                )
                canvas.drawRoundRect(rect, 3f * density, 3f * density, clipPaint)
                if (selectedClip == (track.id to clip.id)) {
                    canvas.drawRoundRect(rect, 3f * density, 3f * density, selectedClipPaint)
                }
                if (rect.width() > 34f * density) {
                    canvas.save()
                    canvas.clipRect(
                        rect.left + 5f * density,
                        rect.top,
                        (rect.right - 3f * density).coerceAtLeast(rect.left),
                        rect.bottom
                    )
                    canvas.drawText(
                        clip.label,
                        rect.left + 5f * density,
                        rect.centerY() + 3f * density,
                        clipTextPaint
                    )
                    canvas.restore()
                }
            }
        }
        canvas.restore()
    }

    private fun drawEmptyTimeline(canvas: Canvas) {
        val contentTop = rulerHeight
        val contentHeight = (height.toFloat() - contentTop).coerceAtLeast(0f)
        if (contentHeight <= 0f || width.toFloat() <= labelWidth) return
        val centerX = labelWidth + (width.toFloat() - labelWidth) / 2f
        val centerY = contentTop + contentHeight / 2f
        canvas.drawText("暂无轨道内容", centerX, centerY - 4f * density, emptyStatePaint)
        canvas.drawText(
            "导入素材后，片段会显示在这里",
            centerX,
            centerY + 17f * density,
            emptyHintPaint
        )
    }

    private fun drawTimeReadout(canvas: Canvas, centerX: Float) {
        val text = formatTime(playheadUs / 1_000_000L)
        val width = readoutPaint.measureText(text) + 8f * density
        val top = 2f * density
        val left = (centerX - width / 2f).coerceIn(0f, (this.width - width).coerceAtLeast(0f))
        val rect = RectF(left, top, left + width, top + 13f * density)
        canvas.drawRoundRect(rect, 2f * density, 2f * density, playheadCapPaint)
        canvas.drawText(text, rect.left + 4f * density, rect.bottom - 3f * density, readoutPaint)
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        scaleDetector.onTouchEvent(event)
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                lastTouchX = event.x
                lastTouchY = event.y
                moved = false
                draggingTimelineVertically = false
                draggingTimelineHorizontally = false
                touchStartedInTimelineLane = event.y >= rulerHeight
                gestureHadMultiplePointers = false
                // The draggable handle is in the fixed ruler, not every point
                // along the playhead line; this keeps vertical lane scrolling usable.
                draggingPlayhead = event.y <= rulerHeight + 8f * density &&
                    kotlin.math.abs(event.x - playheadXPosition()) <= 18f * density
                parent?.requestDisallowInterceptTouchEvent(true)
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (!scaleDetector.isInProgress &&
                    !gestureHadMultiplePointers &&
                    event.pointerCount == 1
                ) {
                    val dx = event.x - lastTouchX
                    val dy = event.y - lastTouchY
                    if (draggingPlayhead) {
                        if (kotlin.math.abs(dx) > touchSlop) moved = true
                        if (moved) updatePlayheadFromTouch(event.x)
                    } else if (
                        touchStartedInTimelineLane &&
                        !draggingTimelineVertically &&
                        !draggingTimelineHorizontally &&
                        kotlin.math.abs(dy) > touchSlop &&
                        kotlin.math.abs(dy) > kotlin.math.abs(dx)
                    ) {
                        // Lock the gesture to its dominant axis on the first
                        // threshold-crossing move; do not switch axes mid-drag.
                        draggingTimelineVertically = true
                        moved = true
                        verticalScrollOffset = (verticalScrollOffset - dy)
                            .coerceIn(0f, maxVerticalScrollOffset())
                        invalidate()
                    } else if (draggingTimelineVertically) {
                        verticalScrollOffset = (verticalScrollOffset - dy)
                            .coerceIn(0f, maxVerticalScrollOffset())
                        invalidate()
                    } else if (
                        !draggingTimelineHorizontally &&
                        kotlin.math.abs(dx) > touchSlop
                    ) {
                        draggingTimelineHorizontally = true
                        moved = true
                        scrollOffset = (scrollOffset - dx).coerceIn(0f, maxScrollOffset())
                        invalidate()
                    } else if (draggingTimelineHorizontally) {
                        scrollOffset = (scrollOffset - dx).coerceIn(0f, maxScrollOffset())
                        invalidate()
                    }
                    lastTouchX = event.x
                    lastTouchY = event.y
                }
                return true
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                // Explicitly cancel a pending playhead drag as soon as a second
                // pointer joins; ScaleGestureDetector may not begin scaling yet.
                gestureHadMultiplePointers = true
                draggingPlayhead = false
                moved = true
                return true
            }
            MotionEvent.ACTION_POINTER_UP -> {
                // Keep the gesture classified as a pinch until the final UP.
                gestureHadMultiplePointers = true
                draggingPlayhead = false
                moved = true
                return true
            }
            MotionEvent.ACTION_UP -> {
                if (!moved && !gestureHadMultiplePointers && !scaleDetector.isInProgress) {
                    if (draggingPlayhead) {
                        updatePlayheadFromTouch(event.x)
                    } else {
                        val clipHandled = handleClipTap(event.x, event.y)
                        if (!clipHandled && !handleTrackControlTap(event.x, event.y)) {
                            if (isTimelineLaneTap(event.x, event.y)) {
                                selectedClip = null
                                onClipSelectionCleared?.invoke()
                                invalidate()
                            }
                            // Track labels and the fixed left gutter are not
                            // part of the time ruler; tapping them must not seek
                            // to time zero through the x-to-time clamp.
                            if (event.x >= labelWidth) {
                                updatePlayheadFromTouch(event.x)
                            }
                        }
                    }
                    performClick()
                }
                draggingPlayhead = false
                draggingTimelineVertically = false
                draggingTimelineHorizontally = false
                gestureHadMultiplePointers = false
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                draggingPlayhead = false
                draggingTimelineVertically = false
                draggingTimelineHorizontally = false
                moved = false
                gestureHadMultiplePointers = false
                parent?.requestDisallowInterceptTouchEvent(false)
                return true
            }
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun handleClipTap(x: Float, y: Float): Boolean {
        if (x < labelWidth || y < rulerHeight) return false
        val contentY = y + verticalScrollOffset
        val rowOffset = contentY - rulerHeight
        val row = (rowOffset / (rowHeight + rowGap)).toInt()
        val withinRow = rowOffset - row * (rowHeight + rowGap)
        if (row !in tracks.indices || withinRow < 5f * density ||
            withinRow >= rowHeight - 5f * density
        ) return false
        val track = tracks[row]
        if (!track.visible) return false
        // Hit-test the same pixel geometry used by drawTracks(). Very short
        // clips are rendered with a 5dp minimum width, so testing only their
        // source duration made the visible tail impossible to select.
        // Iterate in reverse draw order so the visually topmost overlapping
        // clip receives the tap.
        val clip = track.clips.asReversed().firstOrNull { candidate ->
            if (!candidate.enabled || candidate.startUs < 0L ||
                candidate.durationUs <= 0L || candidate.startUs >= durationUs
            ) {
                false
            } else {
                val visibleDurationUs = min(candidate.durationUs, durationUs - candidate.startUs)
                val left = labelWidth.toDouble() +
                    candidate.startUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble() -
                    scrollOffset.toDouble()
                val widthPx = visibleDurationUs.toDouble() / 1_000_000.0 *
                    pixelsPerSecond.toDouble()
                val right = left + max(widthPx, 5f * density).toDouble()
                x.toDouble() >= max(left, labelWidth.toDouble()) &&
                    x.toDouble() < min(right, width.toDouble())
            }
        } ?: return false
        selectedClip = track.id to clip.id
        onClipSelected?.invoke(track.engineIndex.takeIf { it >= 0 } ?: row, clip.id)
        invalidate()
        return true
    }

    private fun isTimelineLaneTap(x: Float, y: Float): Boolean {
        if (x < labelWidth || y < rulerHeight) return false
        val contentY = y + verticalScrollOffset
        val rowOffset = contentY - rulerHeight
        val row = (rowOffset / (rowHeight + rowGap)).toInt()
        val withinRow = rowOffset - row * (rowHeight + rowGap)
        return row in tracks.indices && withinRow >= 0f && withinRow < rowHeight
    }

    private fun handleTrackControlTap(x: Float, y: Float): Boolean {
        if (x >= labelWidth || y < rulerHeight) return false
        val contentY = y + verticalScrollOffset
        val rowOffset = contentY - rulerHeight
        val row = (rowOffset / (rowHeight + rowGap)).toInt()
        val withinRow = rowOffset - row * (rowHeight + rowGap)
        if (row !in tracks.indices || withinRow < 0f || withinRow >= rowHeight) return false
        val track = tracks[row]
        when {
            x in (34f * density)..(74f * density) -> {
                // Missing host wiring is an unavailable control, not a successful no-op.
                val callback = onTrackVisibilityChanged ?: return false
                // The engine snapshot is authoritative; only a refreshed snapshot changes the icon.
                callback(track.engineIndex.takeIf { it >= 0 } ?: row, !track.visible)
            }
            x in (78f * density)..(118f * density) -> {
                val callback = onTrackMuteChanged ?: return false
                callback(track.engineIndex.takeIf { it >= 0 } ?: row, !track.muted)
            }
            else -> return false
        }
        invalidate()
        return true
    }

    private fun playheadXPosition(): Float {
        val requestedX = (
            labelWidth.toDouble() +
                (playheadUs.toDouble() / 1_000_000.0) * pixelsPerSecond.toDouble() -
                scrollOffset.toDouble()
            ).toFloat()
        // A View can be drawn before its first non-zero layout pass. Keep the
        // clamp range valid even when width is temporarily below the label gutter.
        val maximumX = width.toFloat().coerceAtLeast(labelWidth)
        return requestedX.coerceIn(labelWidth, maximumX)
    }

    private fun maxScrollOffset(): Float {
        val contentWidth = durationUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble()
        val viewport = (width - labelWidth).coerceAtLeast(1f).toDouble()
        return (contentWidth - viewport).coerceIn(0.0, Float.MAX_VALUE.toDouble()).toFloat()
    }

    private fun maxVerticalScrollOffset(): Float {
        if (tracks.isEmpty()) return 0f
        // The last lane has no trailing row gap. Counting N * (height + gap)
        // overestimated the content by one gap and allowed blank space below
        // the final track at maximum scroll.
        val contentHeight = tracks.size.toDouble() * rowHeight.toDouble() +
            (tracks.size - 1).toDouble() * rowGap.toDouble()
        val viewportHeight = (height.toDouble() - rulerHeight.toDouble()).coerceAtLeast(0.0)
        val maximum = (contentHeight - viewportHeight).coerceAtLeast(0.0)
        return maximum.coerceAtMost(Float.MAX_VALUE.toDouble()).toFloat()
    }

    private fun updatePlayheadFromTouch(x: Float) {
        val seconds = (
            scrollOffset.toDouble() + x.toDouble() - labelWidth.toDouble()
            ).coerceAtLeast(0.0) / pixelsPerSecond.toDouble()
        val requestedUs = (seconds * 1_000_000.0).coerceAtMost(Long.MAX_VALUE.toDouble()).toLong()
        playheadUs = requestedUs.coerceIn(0L, durationUs)
        onPlayheadChanged?.invoke(playheadUs)
        invalidate()
    }

    private companion object {
        const val MAX_VISIBLE_RULER_TICKS = 256
    }

    private fun formatTime(seconds: Long): String {
        val safe = seconds.coerceAtLeast(0L)
        val hours = safe / 3_600L
        val minutes = (safe / 60L) % 60L
        val remaining = safe % 60L
        fun twoDigits(value: Long): String = if (value < 10L) "0$value" else value.toString()
        return if (hours > 0L) {
            "${twoDigits(hours)}:${twoDigits(minutes)}:${twoDigits(remaining)}"
        } else {
            "${twoDigits(safe / 60L)}:${twoDigits(remaining)}"
        }
    }
}
