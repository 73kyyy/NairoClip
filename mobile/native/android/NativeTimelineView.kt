package com.nairoclip.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.RectF
import android.graphics.LinearGradient
import android.graphics.PorterDuff
import android.graphics.PorterDuffXfermode
import android.graphics.RenderEffect
import android.graphics.Shader
import android.graphics.drawable.Drawable
import android.os.Build
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
 * nodes. It draws the adaptive ruler, centered time origin, track lanes, clips,
 * floating track controls and playhead in one Canvas pass. Engine snapshots
 * remain authoritative; gestures emit commands through the callbacks below.
 */
data class NativeClipEffect(
    val id: Long,
    val type: Int,
    val value: Double,
    val enabled: Boolean
)

data class NativeTimelineClip(
    val id: Long,
    val startUs: Long,
    val durationUs: Long,
    val label: String,
    val enabled: Boolean = true,
    val speedNumerator: Long = 1L,
    val speedDenominator: Long = 1L,
    val effects: List<NativeClipEffect> = emptyList(),
    val sourceInUs: Long = 0L,
    val sourceOutUs: Long = 0L,
    val sourceRangeKnown: Boolean = false,
    val reversed: Boolean = false,
    val frozen: Boolean = false,
    val hasSpeedCurve: Boolean = false,
    val assetId: Long = 0L
) {
    /** True only when source time maps linearly and monotonically to timeline time. */
    val supportsLinearSourceMapping: Boolean
        get() = sourceRangeKnown && sourceOutUs > sourceInUs &&
            !reversed && !frozen && !hasSpeedCurve &&
            speedNumerator > 0L && speedDenominator > 0L
}

data class NativeTimelineTrack(
    val id: Long,
    val name: String,
    val clips: List<NativeTimelineClip> = emptyList(),
    val visible: Boolean = true,
    val muted: Boolean = false,
    val engineIndex: Int = -1,
    val locked: Boolean = false
)

class NativeTimelineView(context: Context) : View(context) {

    var onPlayheadChanged: ((Long) -> Unit)? = null
    var onClipSelected: ((Int, Long) -> Unit)? = null
    var onClipSelectionCleared: (() -> Unit)? = null
    var onTrackVisibilityChanged: ((Int, Boolean) -> Unit)? = null
    var onTrackMuteChanged: ((Int, Boolean) -> Unit)? = null
    var onTrackLockChanged: ((Int, Boolean) -> Unit)? = null
    var onPlayFromStart: (() -> Unit)? = null

    private val density = resources.displayMetrics.density
    private val rulerHeight = NativeEditorLayoutSpec.RULER_HEIGHT_DP.toFloat() * density
    private val trackControlsWidth = NativeEditorLayoutSpec.TRACK_CONTROL_WIDTH_DP.toFloat() * density
    private val rowHeight = 47f * density
    private val rowGap = 4f * density

    private val backgroundPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF111314.toInt() }
    private val rulerPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF151719.toInt() }
    private val lanePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF1D2022.toInt() }
    private val alternateLanePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF202326.toInt() }
    private val rulerTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF9B9B9E.toInt()
        textSize = 9f * density
        textAlign = Paint.Align.CENTER
    }
    private val tickPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF4B5053.toInt()
        strokeWidth = density
    }
    private val clipPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xFF454B4F.toInt() }
    // API 31+ applies a real Gaussian blur to a clipped copy of the track
    // content. The alpha mask below fades that blurred copy from left to right,
    // leaving the fixed eye/speaker/lock glyphs sharp when they are redrawn.
    private val trackControlBlurPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            renderEffect = RenderEffect.createBlurEffect(
                NativeTrackControlFeatherSpec.BLUR_RADIUS_DP * density,
                NativeTrackControlFeatherSpec.BLUR_RADIUS_DP * density,
                Shader.TileMode.CLAMP
            )
        }
    }
    private val trackControlFeatherMaskPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        xfermode = PorterDuffXfermode(PorterDuff.Mode.DST_IN)
        // Horizontal shader is invariant across rows; cache it instead of
        // allocating a new LinearGradient on every timeline frame.
        shader = LinearGradient(
            0f,
            0f,
            trackControlsWidth,
            0f,
            intArrayOf(
                withAlpha(android.graphics.Color.WHITE, NativeTrackControlFeatherSpec.START_ALPHA),
                withAlpha(android.graphics.Color.WHITE, NativeTrackControlFeatherSpec.END_ALPHA)
            ),
            floatArrayOf(0f, 1f),
            Shader.TileMode.CLAMP
        )
    }
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
        // Match ruler labels: the playhead readout is distinguished by its
        // background chip, not by a heavier font weight or larger glyphs.
        textSize = 9f * density
        typeface = android.graphics.Typeface.create(
            android.graphics.Typeface.DEFAULT,
            android.graphics.Typeface.NORMAL
        )
    }
    private val emptyStatePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFB8BEC2.toInt()
        textSize = 13f * density
        textAlign = Paint.Align.CENTER
    }
    private val startBadgePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF2D2118.toInt()
    }
    private val startBadgeBorderPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFB97832.toInt()
        style = Paint.Style.STROKE
        strokeWidth = 1.2f * density
    }
    private val eyeDrawable = context.getDrawable(R.drawable.ic_editor_eye)?.mutate()
    private val eyeOffDrawable = context.getDrawable(R.drawable.ic_editor_eye_off)?.mutate()
    private val speakerDrawable = context.getDrawable(R.drawable.ic_editor_speaker)?.mutate()
    private val speakerOffDrawable = context.getDrawable(R.drawable.ic_editor_speaker_off)?.mutate()
    private val lockDrawable = context.getDrawable(R.drawable.ic_editor_lock)?.mutate()
    private val unlockDrawable = context.getDrawable(R.drawable.ic_editor_unlock)?.mutate()
    private val startBadgeDrawable = context.getDrawable(R.drawable.ic_editor_prev)?.mutate()

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
    private var pendingInitialFit = false
    private var didFitInitialTimeline = false
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
                val focusOffset = detector.focusX.toDouble() -
                    NativeTimelineViewportSpec.originX(width.toFloat()).toDouble()
                val focusTimeSeconds = (
                    scrollOffset.toDouble() + focusOffset
                    ) / old.toDouble()
                pixelsPerSecond = (old * factor).coerceIn(
                    12f * density,
                    180f * density
                )
                // Preserve the timeline time under the pinch focus, subject to
                // the legal scroll range at the new zoom level.
                val requestedOffset = focusTimeSeconds * pixelsPerSecond.toDouble() - focusOffset
                scrollOffset = requestedOffset.coerceIn(0.0, maxScrollOffset().toDouble()).toFloat()
                playheadUs = (
                    scrollOffset.toDouble() / pixelsPerSecond.toDouble() * 1_000_000.0
                ).toLong().coerceIn(0L, durationUs)
                onPlayheadChanged?.invoke(playheadUs)
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
                        !track.locked &&
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
        if (!didFitInitialTimeline && tracks.isNotEmpty()) {
            pendingInitialFit = true
            if (width > 0) fitInitialTimeline()
        } else if (playheadChanged) {
            scrollOffset = (
                playheadUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble()
            ).coerceIn(0.0, maxScrollOffset().toDouble()).toFloat()
        }
        invalidate()
    }

    fun setPlayhead(timeUs: Long) {
        playheadUs = timeUs.coerceIn(0L, durationUs)
        scrollOffset = (
            playheadUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble()
        ).coerceIn(0.0, maxScrollOffset().toDouble()).toFloat()
        invalidate()
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        super.onSizeChanged(w, h, oldw, oldh)
        scrollOffset = scrollOffset.coerceIn(0f, maxScrollOffset())
        verticalScrollOffset = verticalScrollOffset.coerceIn(0f, maxVerticalScrollOffset())
        if (pendingInitialFit && width > 0) fitInitialTimeline()
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
        selectedTrackForAccessibility()?.let { track ->
            val label = track.name.ifBlank { "轨道" }
            info.addAction(AccessibilityNodeInfo.AccessibilityAction(
                ACTION_TOGGLE_TRACK_VISIBILITY,
                if (track.visible) "隐藏$label" else "显示$label"
            ))
            info.addAction(AccessibilityNodeInfo.AccessibilityAction(
                ACTION_TOGGLE_TRACK_MUTE,
                if (track.muted) "取消静音$label" else "静音$label"
            ))
            info.addAction(AccessibilityNodeInfo.AccessibilityAction(
                ACTION_TOGGLE_TRACK_LOCK,
                if (track.locked) "解锁$label" else "锁定$label"
            ))
        }
    }

    private fun selectedTrackForAccessibility(): NativeTimelineTrack? {
        val selectedTrackId = selectedClip?.first ?: return null
        return tracks.firstOrNull { it.id == selectedTrackId }
    }

    private fun performTrackControlAccessibilityAction(action: Int): Boolean {
        val track = selectedTrackForAccessibility() ?: return false
        val engineIndex = track.engineIndex.takeIf { it >= 0 } ?: tracks.indexOf(track)
        when (action) {
            ACTION_TOGGLE_TRACK_VISIBILITY ->
                (onTrackVisibilityChanged ?: return false).invoke(engineIndex, !track.visible)
            ACTION_TOGGLE_TRACK_MUTE ->
                (onTrackMuteChanged ?: return false).invoke(engineIndex, !track.muted)
            ACTION_TOGGLE_TRACK_LOCK ->
                (onTrackLockChanged ?: return false).invoke(engineIndex, !track.locked)
            else -> return false
        }
        invalidate()
        sendAccessibilityEvent(android.view.accessibility.AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED)
        return true
    }

    override fun performAccessibilityAction(action: Int, arguments: Bundle?): Boolean {
        if (performTrackControlAccessibilityAction(action)) return true
        val stepUs = 1_000_000L
        val targetUs = when (action) {
            AccessibilityNodeInfo.AccessibilityAction.ACTION_SET_PROGRESS.id -> {
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
                if (durationUs - playheadUs < stepUs) durationUs else playheadUs + stepUs
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
        drawRuler(canvas)
        drawTracks(canvas)
        if (tracks.isEmpty()) drawEmptyTimeline(canvas)

        canvas.drawLine(playheadX, 0f, playheadX, height.toFloat(), playheadPaint)
        canvas.drawCircle(playheadX, 4f * density, 3f * density, playheadCapPaint)
        drawTimeReadout(canvas, playheadX)
    }

    private fun drawRuler(canvas: Canvas) {
        canvas.drawRect(0f, 0f, width.toFloat(), rulerHeight, rulerPaint)
        val majorStep = NativeTimelineRulerSpec.majorStepSeconds(pixelsPerSecond.toDouble())
        val minorStep = majorStep / 5.0
        val originX = NativeTimelineViewportSpec.originX(width.toFloat()).toDouble()
        val visibleStart = ((scrollOffset.toDouble() - originX) /
            pixelsPerSecond.toDouble()).coerceAtLeast(0.0)
        val visibleEnd = ((scrollOffset.toDouble() + width.toDouble() - originX) /
            pixelsPerSecond.toDouble()).coerceAtLeast(visibleStart)

        val firstMinor = floor(visibleStart / minorStep).toLong().coerceAtLeast(0L)
        val lastMinor = ceil(visibleEnd / minorStep).toLong().coerceAtLeast(firstMinor)
        val minorTickCount = (lastMinor - firstMinor)
            .coerceAtMost((MAX_VISIBLE_RULER_TICKS * 5).toLong())
        for (offset in 0L..minorTickCount) {
            val seconds = (firstMinor + offset).toDouble() * minorStep
            val x = NativeTimelineViewportSpec.xForTime(
                width.toFloat(), seconds, pixelsPerSecond.toDouble(), scrollOffset.toDouble()
            ).toFloat()
            if (x < 0f || x > width.toFloat()) continue
            val majorIndex = seconds / majorStep
            val isMajor = kotlin.math.abs(majorIndex - kotlin.math.round(majorIndex)) < 1e-6
            val tickHeightDp = if (isMajor) {
                NativeEditorLayoutSpec.RULER_MAJOR_TICK_HEIGHT_DP
            } else {
                NativeEditorLayoutSpec.RULER_TICK_HEIGHT_DP
            }
            val tickHeight = tickHeightDp.toFloat() * density
            canvas.drawLine(x, rulerHeight - tickHeight, x, rulerHeight, tickPaint)
        }

        val firstMajor = ceil(visibleStart / majorStep).toLong().coerceAtLeast(0L)
        val lastMajor = floor(visibleEnd / majorStep).toLong().coerceAtLeast(firstMajor)
        val majorTickCount = (lastMajor - firstMajor)
            .coerceAtMost(MAX_VISIBLE_RULER_TICKS.toLong())
        for (offset in 0L..majorTickCount) {
            val seconds = (firstMajor + offset).toDouble() * majorStep
            val x = NativeTimelineViewportSpec.xForTime(
                width.toFloat(), seconds, pixelsPerSecond.toDouble(), scrollOffset.toDouble()
            ).toFloat()
            if (x < 12f * density || x > width.toFloat() - 12f * density) continue
            canvas.drawText(
                NativeTimelineRulerSpec.formatLabel(seconds, majorStep),
                x,
                18f * density,
                rulerTextPaint
            )
        }
        drawStartBadge(canvas)
    }

    private fun drawStartBadge(canvas: Canvas) {
        val left = 6f * density
        val top = ((rulerHeight - NativeEditorLayoutSpec.START_BADGE_HEIGHT_DP * density) / 2f)
            .coerceAtLeast(0f)
        val badgeWidth = NativeEditorLayoutSpec.START_BADGE_WIDTH_DP * density
        val badgeHeight = NativeEditorLayoutSpec.START_BADGE_HEIGHT_DP * density
        val rect = RectF(left, top, left + badgeWidth, top + badgeHeight)
        canvas.drawRoundRect(rect, 4f * density, 4f * density, startBadgePaint)
        canvas.drawRoundRect(rect, 4f * density, 4f * density, startBadgeBorderPaint)
        startBadgeDrawable?.let { drawable ->
            drawable.setTint(0xFFD58A3B.toInt())
            val iconSize = 15f * density
            val centerX = rect.centerX()
            val centerY = rect.centerY()
            drawable.setBounds(
                (centerX - iconSize / 2f).toInt(),
                (centerY - iconSize / 2f).toInt(),
                (centerX + iconSize / 2f).toInt(),
                (centerY + iconSize / 2f).toInt()
            )
            drawable.draw(canvas)
        }
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

            // Keep the timeline area outside the fixed controls completely sharp.
            canvas.save()
            canvas.clipRect(trackControlsWidth, top, width.toFloat(), bottom)
            drawTrackClips(canvas, track, top, bottom)
            canvas.restore()

            // Composite only the content passing beneath the controls. The
            // inner layer applies Gaussian blur on API 31+; the outer layer
            // then applies DST_IN AFTER blur, so the original sharp clip is
            // never left underneath the feathered copy.
            val featherRight = min(trackControlsWidth, width.toFloat())
            if (featherRight > 0f) {
                val featherBounds = RectF(0f, top, featherRight, bottom)
                canvas.saveLayer(featherBounds, null)
                canvas.saveLayer(featherBounds, trackControlBlurPaint)
                canvas.clipRect(featherBounds)
                canvas.drawRect(featherBounds, lane)
                drawTrackClips(canvas, track, top, bottom)
                canvas.restore()

                // The cached horizontal shader spans the fixed 120dp gutter.
                canvas.drawRect(featherBounds, trackControlFeatherMaskPaint)
                canvas.restore()
            }
            // Controls are always rendered last and remain perfectly sharp.
            drawTrackControls(canvas, track, top, bottom)
        }
        canvas.restore()
    }

    private fun drawTrackClips(
        canvas: Canvas,
        track: NativeTimelineTrack,
        top: Float,
        bottom: Float
    ) {
        if (!track.visible) return
        track.clips.forEach clipLoop@{ clip ->
            if (!clip.enabled || clip.startUs < 0L || clip.durationUs <= 0L ||
                clip.startUs >= durationUs
            ) return@clipLoop
            val visibleDurationUs = min(clip.durationUs, durationUs - clip.startUs)
            val left = NativeTimelineViewportSpec.xForTime(
                width.toFloat(),
                clip.startUs.toDouble() / 1_000_000.0,
                pixelsPerSecond.toDouble(),
                scrollOffset.toDouble()
            ).toFloat()
            val clipWidth = (
                visibleDurationUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble()
            ).toFloat()
            val right = left + max(clipWidth, 5f * density)
            if (right < 0f || left > width) return@clipLoop
            val rect = RectF(
                max(left, 0f),
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

    private fun withAlpha(color: Int, alpha: Float): Int {
        val safeAlpha = (alpha.coerceIn(0f, 1f) * 255f).toInt()
        return (color and 0x00FFFFFF) or (safeAlpha shl 24)
    }

    private fun drawTrackControls(
        canvas: Canvas,
        track: NativeTimelineTrack,
        top: Float,
        bottom: Float
    ) {
        val centerY = (top + bottom) / 2f
        val iconSize = 13f * density
        // State is conveyed by both glyph shape and tint, not by callback
        // availability. Neutral states stay cool-grey/white; muted and locked
        // states use restrained amber for an immediately legible distinction.
        val eyeVisibleTint = 0xFFE4E8EA.toInt()
        val eyeHiddenTint = 0xFF687177.toInt()
        val speakerOnTint = 0xFFE4E8EA.toInt()
        val speakerMutedTint = 0xFFD69A58.toInt()
        val lockOpenTint = 0xFF9AA3A8.toInt()
        val lockClosedTint = 0xFFD69A58.toInt()
        // Preserve the reference's three compact icon centers inside the fixed 40dp overlay.
        // Coordinates are already physical pixels because trackControlsWidth includes density.
        val centers = floatArrayOf(
            trackControlsWidth / 6f,
            trackControlsWidth / 2f,
            trackControlsWidth * 5f / 6f
        )
        drawTrackIcon(
            canvas,
            if (track.visible) eyeDrawable else eyeOffDrawable,
            centers[0],
            centerY,
            iconSize,
            if (track.visible) eyeVisibleTint else eyeHiddenTint
        )
        drawTrackIcon(
            canvas,
            if (track.muted) speakerOffDrawable else speakerDrawable,
            centers[1],
            centerY,
            iconSize,
            if (track.muted) speakerMutedTint else speakerOnTint
        )
        drawTrackIcon(
            canvas,
            if (track.locked) lockDrawable else unlockDrawable,
            centers[2],
            centerY,
            iconSize,
            if (track.locked) lockClosedTint else lockOpenTint
        )
    }

    private fun drawTrackIcon(
        canvas: Canvas,
        drawable: Drawable?,
        centerX: Float,
        centerY: Float,
        size: Float,
        tint: Int
    ) {
        drawable ?: return
        drawable.setTint(tint)
        drawable.setBounds(
            (centerX - size / 2f).toInt(),
            (centerY - size / 2f).toInt(),
            (centerX + size / 2f).toInt(),
            (centerY + size / 2f).toInt()
        )
        drawable.draw(canvas)
    }

    private fun drawEmptyTimeline(canvas: Canvas) {
        val contentTop = rulerHeight
        val contentHeight = (height.toFloat() - contentTop).coerceAtLeast(0f)
        if (contentHeight <= 0f || width.toFloat() <= 0f) return
        val centerX = width.toFloat() / 2f
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
                        playheadUs = (
                            scrollOffset.toDouble() / pixelsPerSecond.toDouble() * 1_000_000.0
                        ).toLong().coerceIn(0L, durationUs)
                        onPlayheadChanged?.invoke(playheadUs)
                        invalidate()
                    } else if (draggingTimelineHorizontally) {
                        scrollOffset = (scrollOffset - dx).coerceIn(0f, maxScrollOffset())
                        playheadUs = (
                            scrollOffset.toDouble() / pixelsPerSecond.toDouble() * 1_000_000.0
                        ).toLong().coerceIn(0L, durationUs)
                        onPlayheadChanged?.invoke(playheadUs)
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
                        val startHandled = handleStartBadgeTap(event.x, event.y)
                        val clipHandled = if (startHandled) true else handleClipTap(event.x, event.y)
                        if (!startHandled && !clipHandled && !handleTrackControlTap(event.x, event.y)) {
                            if (isTimelineLaneTap(event.x, event.y)) {
                                selectedClip = null
                                onClipSelectionCleared?.invoke()
                                invalidate()
                            }
                            // Track labels and the fixed left gutter are not
                            // part of the time ruler; tapping them must not seek
                            // to time zero through the x-to-time clamp.
                            if (event.x >= trackControlsWidth) {
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
        if (x < trackControlsWidth || y < rulerHeight) return false
        val contentY = y + verticalScrollOffset
        val rowOffset = contentY - rulerHeight
        val row = (rowOffset / (rowHeight + rowGap)).toInt()
        val withinRow = rowOffset - row * (rowHeight + rowGap)
        if (row !in tracks.indices || withinRow < 5f * density ||
            withinRow >= rowHeight - 5f * density
        ) return false
        val track = tracks[row]
        // A locked track remains visible, but cannot enter clip-edit selection.
        // setTimeline() also clears an existing selection when lock state changes.
        if (!track.visible || track.locked) return false
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
                val left = NativeTimelineViewportSpec.xForTime(
                    width.toFloat(), candidate.startUs.toDouble() / 1_000_000.0,
                    pixelsPerSecond.toDouble(), scrollOffset.toDouble()
                )
                val widthPx = visibleDurationUs.toDouble() / 1_000_000.0 *
                    pixelsPerSecond.toDouble()
                val right = left + max(widthPx, 5.0 * density.toDouble())
                x.toDouble() >= max(left, trackControlsWidth.toDouble()) &&
                    x.toDouble() < min(right, width.toDouble())
            }
        } ?: return false
        selectedClip = track.id to clip.id
        onClipSelected?.invoke(track.engineIndex.takeIf { it >= 0 } ?: row, clip.id)
        invalidate()
        return true
    }

    private fun isTimelineLaneTap(x: Float, y: Float): Boolean {
        if (x < trackControlsWidth || y < rulerHeight) return false
        val contentY = y + verticalScrollOffset
        val rowOffset = contentY - rulerHeight
        val row = (rowOffset / (rowHeight + rowGap)).toInt()
        val withinRow = rowOffset - row * (rowHeight + rowGap)
        return row in tracks.indices && withinRow >= 0f && withinRow < rowHeight
    }

    private fun handleStartBadgeTap(x: Float, y: Float): Boolean {
        val badgeRight = (6f + NativeEditorLayoutSpec.START_BADGE_WIDTH_DP) * density
        if (y > rulerHeight || x < 0f || x > badgeRight) return false
        val callback = onPlayFromStart
        if (callback != null) {
            callback()
        } else {
            playheadUs = 0L
            scrollOffset = 0f
            onPlayheadChanged?.invoke(0L)
            invalidate()
        }
        return true
    }

    private fun handleTrackControlTap(x: Float, y: Float): Boolean {
        if (x >= trackControlsWidth || y < rulerHeight) return false
        val contentY = y + verticalScrollOffset
        val rowOffset = contentY - rulerHeight
        val row = (rowOffset / (rowHeight + rowGap)).toInt()
        val withinRow = rowOffset - row * (rowHeight + rowGap)
        if (row !in tracks.indices || withinRow < 0f || withinRow >= rowHeight) return false
        val track = tracks[row]
        val engineIndex = track.engineIndex.takeIf { it >= 0 } ?: row
        when {
            x < trackControlsWidth / 3f -> {
                val callback = onTrackVisibilityChanged ?: return false
                callback(engineIndex, !track.visible)
            }
            x < trackControlsWidth * 2f / 3f -> {
                val callback = onTrackMuteChanged ?: return false
                callback(engineIndex, !track.muted)
            }
            else -> {
                val callback = onTrackLockChanged ?: return false
                callback(engineIndex, !track.locked)
            }
        }
        invalidate()
        return true
    }

    private fun playheadXPosition(): Float =
        NativeTimelineViewportSpec.originX(width.toFloat())

    private fun maxScrollOffset(): Float =
        NativeTimelineViewportSpec.maxScrollOffset(durationUs, pixelsPerSecond.toDouble())
            .coerceAtMost(Float.MAX_VALUE.toDouble()).toFloat()

    private fun fitInitialTimeline() {
        if (width <= 0 || durationUs <= 0L) return
        pixelsPerSecond = NativeTimelineViewportSpec.fitPixelsPerSecond(
            width.toFloat(), density, durationUs
        )
        scrollOffset = (
            playheadUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble()
        ).coerceIn(0.0, maxScrollOffset().toDouble()).toFloat()
        pendingInitialFit = false
        didFitInitialTimeline = true
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
        val seconds = NativeTimelineViewportSpec.timeAtX(
            x,
            width.toFloat(),
            pixelsPerSecond.toDouble(),
            scrollOffset.toDouble()
        ).coerceIn(0.0, durationUs.toDouble() / 1_000_000.0)
        val requestedUs = (seconds * 1_000_000.0).coerceAtMost(Long.MAX_VALUE.toDouble()).toLong()
        playheadUs = requestedUs.coerceIn(0L, durationUs)
        scrollOffset = (
            playheadUs.toDouble() / 1_000_000.0 * pixelsPerSecond.toDouble()
        ).coerceIn(0.0, maxScrollOffset().toDouble()).toFloat()
        onPlayheadChanged?.invoke(playheadUs)
        invalidate()
    }

    private companion object {
        const val MAX_VISIBLE_RULER_TICKS = 256
        const val ACTION_TOGGLE_TRACK_VISIBILITY = 0x010F0001
        const val ACTION_TOGGLE_TRACK_MUTE = 0x010F0002
        const val ACTION_TOGGLE_TRACK_LOCK = 0x010F0003
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
