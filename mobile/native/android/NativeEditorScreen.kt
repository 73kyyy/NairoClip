package com.nairoclip.app

import android.view.View
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView

/**
 * First native editor surface for the S5 migration.
 *
 * This composable deliberately owns presentation only. Editing commands remain
 * routed through the existing editor until their native command adapters and
 * S5 interface contract are accepted. Every not-yet-migrated tool can hand the
 * user back to the complete legacy editor through [onOpenLegacy].
 */
@Composable
fun NativeEditorScreen(
    previewView: View? = null,
    projectName: String = "未命名工程",
    timelineTracks: List<NativeTimelineTrack> = emptyList(),
    timelineDurationUs: Long = 30_000_000L,
    playheadUs: Long = 0L,
    onPlayheadChanged: (Long) -> Unit = {},
    onClipSelected: (Int, Long) -> Unit = { _, _ -> },
    onTrackVisibilityChanged: (Int, Boolean) -> Unit = { _, _ -> },
    onTrackMuteChanged: (Int, Boolean) -> Unit = { _, _ -> },
    transform: NativeTransform = NativeTransform(),
    keyedParameters: Set<NativeParameter> = emptySet(),
    keyframes: Map<NativeParameter, List<NativeEditorCommandAdapter.Keyframe>> = emptyMap(),
    onTransformChanged: (Int, Long, NativeTransform) -> Unit = { _, _, _ -> },
    onInsertKeyframe: (Int, Long, NativeParameter) -> Unit = { _, _, _ -> },
    onRemoveKeyframe: (Int, Long, NativeParameter, Long) -> Unit = { _, _, _, _ -> },
    onSetKeyframeEasing: (Int, Long, NativeParameter, Long, Int, Double, Double, Double, Double) -> Unit = { _, _, _, _, _, _, _, _, _ -> },
    canUndo: Boolean = false,
    canRedo: Boolean = false,
    onUndo: () -> Unit = {},
    onRedo: () -> Unit = {},
    onOpenLegacy: () -> Unit
) {
    var activeTool by remember { mutableStateOf("剪辑") }
    var expandedTool by remember { mutableStateOf<String?>(null) }
    var activeSubTool by remember { mutableStateOf<String?>(null) }
    var selectedClip by remember(projectName) { mutableStateOf<Pair<Int, Long>?>(null) }
    LaunchedEffect(timelineTracks, selectedClip) {
        val selection = selectedClip ?: return@LaunchedEffect
        val stillExists = timelineTracks.any { track ->
            (if (track.engineIndex >= 0) track.engineIndex else timelineTracks.indexOf(track)) == selection.first &&
                track.visible &&
                track.clips.any { it.id == selection.second && it.enabled }
        }
        if (!stillExists) selectedClip = null
    }

    MaterialTheme {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(NcColors.background)
        ) {
            EditorTopBar(projectName = projectName, onOpenLegacy = onOpenLegacy)

            PreviewStage(
                previewView = previewView,
                modifier = Modifier.weight(1f).fillMaxWidth()
            )

            QuickActionStrip(
                canUndo = canUndo,
                canRedo = canRedo,
                onUndo = onUndo,
                onRedo = onRedo,
                onOpenLegacy = onOpenLegacy
            )

            NativeTimelinePanel(
                tracks = timelineTracks,
                durationUs = timelineDurationUs,
                playheadUs = playheadUs,
                onPlayheadChanged = onPlayheadChanged,
                onClipSelected = { trackIndex, clipId ->
                    selectedClip = trackIndex to clipId
                    onClipSelected(trackIndex, clipId)
                },
                onClipSelectionCleared = { selectedClip = null },
                onTrackVisibilityChanged = onTrackVisibilityChanged,
                onTrackMuteChanged = onTrackMuteChanged,
                modifier = Modifier
                    .fillMaxWidth()
                    .height(218.dp)
            )

            selectedClip?.let { selection ->
                val selectedTrack = timelineTracks.firstOrNull { track ->
                    (if (track.engineIndex >= 0) track.engineIndex else timelineTracks.indexOf(track)) == selection.first
                }
                val selectedItem = selectedTrack?.clips?.firstOrNull { it.id == selection.second }
                if (selectedTrack != null && selectedItem != null) {
                    ClipSelectionBar(
                        trackName = selectedTrack.name,
                        clipName = selectedItem.label,
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            }

            val selectedTarget = selectedClip?.takeIf { selection ->
                timelineTracks.any { track ->
                    (if (track.engineIndex >= 0) track.engineIndex else timelineTracks.indexOf(track)) == selection.first &&
                        track.visible &&
                        track.clips.any { it.id == selection.second && it.enabled }
                }
            }
            if (activeTool == "画面" && activeSubTool in setOf("位置", "缩放", "旋转") && selectedTarget != null) {
                NativeParameterPanel(
                    targetTrackIndex = selectedTarget.first,
                    targetClipId = selectedTarget.second,
                    transform = transform,
                    keyedParameters = keyedParameters,
                    keyframes = keyframes,
                    onRemoveKeyframe = { parameter, timeUs ->
                        onRemoveKeyframe(selectedTarget.first, selectedTarget.second, parameter, timeUs)
                    },
                    onSetKeyframeEasing = { parameter, frame ->
                        onSetKeyframeEasing(
                            selectedTarget.first, selectedTarget.second, parameter,
                            frame.timeUs, frame.easing, frame.c1x, frame.c1y, frame.c2x, frame.c2y
                        )
                    },
                    onTransformChanged = { value ->
                        onTransformChanged(selectedTarget.first, selectedTarget.second, value)
                    },
                    onInsertKeyframe = { parameter ->
                        onInsertKeyframe(selectedTarget.first, selectedTarget.second, parameter)
                    },
                    modifier = Modifier.fillMaxWidth()
                )
            }

            TransportBar(onAction = onOpenLegacy)

            NativeToolBar(
                activeTool = activeTool,
                expandedTool = expandedTool,
                activeSubTool = activeSubTool,
                onToolSelected = { tool ->
                    if (expandedTool == tool) {
                        expandedTool = null
                    } else {
                        expandedTool = tool
                        activeTool = tool
                        activeSubTool = null
                    }
                },
                onSubToolSelected = { tool, subTool ->
                    // Only canvas transform subtools have a native command-backed
                    // panel today. Route every other subtool to the complete legacy
                    // editor instead of leaving a visually selectable no-op.
                    if (tool == "画面" && subTool in setOf("位置", "缩放", "旋转")) {
                        activeTool = tool
                        activeSubTool = subTool
                    } else {
                        onOpenLegacy()
                    }
                }
            )
        }
    }
}

private object NcColors {
    val background = Color(0xFF101112)
    val topBar = Color(0xFF191A1C)
    val stage = Color(0xFF242526)
    val timeline = Color(0xFF111314)
    val lane = Color(0xFF1A1D1F)
    val laneAlt = Color(0xFF202326)
    val panel = Color(0xFF252729)
    val line = Color(0xFF34373A)
    val text = Color(0xFFF1F1F1)
    val secondary = Color(0xFF9B9B9E)
    val muted = Color(0xFF666B6E)
    val danger = Color(0xFFE56A55)
}

@Composable
private fun EditorTopBar(projectName: String, onOpenLegacy: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(44.dp)
            .background(NcColors.topBar)
            .padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = "NairoClip",
            color = NcColors.text,
            fontSize = 16.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(Modifier.width(12.dp))
        Text(
            text = projectName,
            color = NcColors.secondary,
            fontSize = 11.sp,
            maxLines = 1,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = "旧版功能",
            color = NcColors.text,
            fontSize = 11.sp,
            modifier = Modifier
                .clip(RoundedCornerShape(16.dp))
                .border(1.dp, NcColors.secondary, RoundedCornerShape(16.dp))
                .clickable(onClick = onOpenLegacy)
                .padding(horizontal = 12.dp, vertical = 6.dp)
        )
        Spacer(Modifier.width(8.dp))
        Text(
            text = "导出",
            color = NcColors.text,
            fontSize = 12.sp,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier
                .clip(RoundedCornerShape(16.dp))
                .border(1.dp, NcColors.text, RoundedCornerShape(16.dp))
                .clickable(onClick = onOpenLegacy)
                .padding(horizontal = 14.dp, vertical = 6.dp)
        )
    }
}

@Composable
private fun PreviewStage(previewView: View?, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .background(NcColors.stage)
            .padding(horizontal = 10.dp, vertical = 5.dp),
        contentAlignment = Alignment.Center
    ) {
        BoxWithConstraints(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            val maxPreviewHeight = maxHeight
            val maxPreviewWidth = maxWidth
            val previewWidth = with(LocalDensity.current) {
                minOf(maxPreviewWidth, maxPreviewHeight * (9f / 16f))
            }
            Box(
                modifier = Modifier
                    .width(previewWidth)
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color.Black)
                    .border(1.dp, Color(0x33FFFFFFL), RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center
            ) {
                if (previewView != null) {
                    AndroidView(
                        factory = { previewView },
                        modifier = Modifier.fillMaxSize()
                    )
                } else {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text("N", color = Color(0xFF777777), fontSize = 34.sp, fontWeight = FontWeight.Light)
                        Text("视频预览", color = NcColors.muted, fontSize = 11.sp)
                    }
                }
            }
        }
    }
}

@Composable
private fun QuickActionStrip(
    canUndo: Boolean,
    canRedo: Boolean,
    onUndo: () -> Unit,
    onRedo: () -> Unit,
    onOpenLegacy: () -> Unit
) {
    val actions = listOf("＋", "⌂", "‹", "◇", "渲染", "◆", "＃", "▧", "AI", "↶", "↷")
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(42.dp)
            .background(NcColors.stage)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        actions.forEachIndexed { index, label ->
            Box(
                modifier = Modifier
                    .size(if (index == 4) 36.dp else 32.dp)
                    .clip(CircleShape)
                    .background(NcColors.panel)
                    .border(1.dp, NcColors.line, CircleShape)
                    .clickable(
                        enabled = when (label) {
                            "↶" -> canUndo
                            "↷" -> canRedo
                            else -> true
                        },
                        onClick = when (label) {
                            "↶" -> onUndo
                            "↷" -> onRedo
                            else -> onOpenLegacy
                        }
                    ),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = label,
                    color = when (label) {
                        "↶" -> if (canUndo) NcColors.text else NcColors.muted
                        "↷" -> if (canRedo) NcColors.text else NcColors.muted
                        else -> NcColors.text
                    },
                    fontSize = if (label == "渲染") 9.sp else 14.sp,
                    fontWeight = FontWeight.Medium
                )
            }
        }
    }
}

@Composable
private fun ClipSelectionBar(trackName: String, clipName: String, modifier: Modifier = Modifier) {
    Row(
        modifier = modifier
            .background(NcColors.panel)
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text("已选中", color = NcColors.secondary, fontSize = 10.sp)
        Spacer(Modifier.width(8.dp))
        Text(trackName, color = NcColors.text, fontSize = 10.sp, fontWeight = FontWeight.SemiBold)
        Spacer(Modifier.width(6.dp))
        Text(clipName, color = NcColors.text, fontSize = 10.sp, maxLines = 1, modifier = Modifier.weight(1f))
        Text("参数目标", color = NcColors.secondary, fontSize = 9.sp)
    }
}

@Composable
private fun NativeTimelinePanel(
    tracks: List<NativeTimelineTrack>,
    durationUs: Long,
    playheadUs: Long,
    onPlayheadChanged: (Long) -> Unit,
    onClipSelected: (Int, Long) -> Unit,
    onClipSelectionCleared: () -> Unit,
    onTrackVisibilityChanged: (Int, Boolean) -> Unit,
    onTrackMuteChanged: (Int, Boolean) -> Unit,
    modifier: Modifier = Modifier
) {
    val initialTracks = remember { tracks.toList() }
    AndroidView(
        factory = { context ->
            NativeTimelineView(context).apply {
                setTimeline(initialTracks, durationUs, playheadUs)
            }
        },
        update = { view ->
            view.onPlayheadChanged = onPlayheadChanged
            view.onClipSelected = onClipSelected
            view.onClipSelectionCleared = onClipSelectionCleared
            view.onTrackVisibilityChanged = onTrackVisibilityChanged
            view.onTrackMuteChanged = onTrackMuteChanged
            view.setTimeline(tracks, durationUs, playheadUs)
        },
        modifier = modifier
    )
}

/** Presentation inputs; the host must persist changes through the engine command API. */
data class NativeTransform(val x: Float = 0f, val y: Float = 0f, val scale: Float = 100f, val rotation: Float = 0f)
enum class NativeParameter(val engineProperty: Int) {
    SCALE(1),
    X(2),
    Y(3),
    ROTATION(4)
}

@Composable
private fun NativeParameterPanel(
    targetTrackIndex: Int,
    targetClipId: Long,
    transform: NativeTransform,
    keyedParameters: Set<NativeParameter>,
    keyframes: Map<NativeParameter, List<NativeEditorCommandAdapter.Keyframe>>,
    onTransformChanged: (NativeTransform) -> Unit,
    onInsertKeyframe: (NativeParameter) -> Unit,
    onRemoveKeyframe: (NativeParameter, Long) -> Unit,
    onSetKeyframeEasing: (NativeParameter, NativeEditorCommandAdapter.Keyframe) -> Unit,
    modifier: Modifier = Modifier
) {
    // Parameter/channel selection belongs to a clip, not to the panel's
    // compositional position. Switching clips must not carry a stale channel
    // or keyframe timestamp into the newly selected target.
    var activeParameter by remember(targetTrackIndex, targetClipId) {
        mutableStateOf(NativeParameter.SCALE)
    }
    val activeFrames = keyframes[activeParameter].orEmpty()
    // Track the selected keyframe by its project timestamp rather than list
    // index. Engine refreshes can reorder or replace a channel without changing
    // its size; an index-based selection could then delete/edit a different key.
    var selectedFrameTimeUs by remember(targetTrackIndex, targetClipId, activeParameter) {
        mutableStateOf(activeFrames.firstOrNull()?.timeUs)
    }
    // Reconcile selection when an engine refresh removes the selected keyframe.
    // Keep the state itself synchronized with the fallback shown by the panel,
    // so a later reinsert at the old timestamp cannot silently regain selection.
    LaunchedEffect(activeFrames, selectedFrameTimeUs) {
        if (selectedFrameTimeUs == null || activeFrames.none { it.timeUs == selectedFrameTimeUs }) {
            selectedFrameTimeUs = activeFrames.firstOrNull()?.timeUs
        }
    }
    val selectedFrame = activeFrames.firstOrNull { it.timeUs == selectedFrameTimeUs }
    // Keep slider motion local while the finger is down. Commit one engine
    // transform command when the gesture ends instead of dispatching a command
    // for every pointer sample (which would flood the undo history).
    var draftTransform by remember(targetTrackIndex, targetClipId) {
        mutableStateOf(transform)
    }
    var draggingParameter by remember(targetTrackIndex, targetClipId) {
        mutableStateOf<NativeParameter?>(null)
    }
    LaunchedEffect(targetTrackIndex, targetClipId, transform) {
        if (draggingParameter == null) draftTransform = transform
    }
    fun updateDraft(parameter: NativeParameter, value: Float) {
        draggingParameter = parameter
        draftTransform = when (parameter) {
            NativeParameter.X -> draftTransform.copy(x = value)
            NativeParameter.Y -> draftTransform.copy(y = value)
            NativeParameter.SCALE -> draftTransform.copy(scale = value)
            NativeParameter.ROTATION -> draftTransform.copy(rotation = value)
        }
    }
    fun commitDraft() {
        if (draggingParameter != null) {
            draggingParameter = null
            onTransformChanged(draftTransform)
        }
    }
    Column(
        modifier = modifier.background(NcColors.panel).padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(3.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("变换参数", color = NcColors.text, fontSize = 11.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.weight(1f))
            Text("关键帧 ${activeFrames.size}", color = NcColors.secondary, fontSize = 9.sp)
        }
        ParameterSlider("X", draftTransform.x, -1000f, 1000f, NativeParameter.X, keyedParameters, onInsertKeyframe, onValueChange = { updateDraft(NativeParameter.X, it) }, onValueChangeFinished = ::commitDraft)
        ParameterSlider("Y", draftTransform.y, -1000f, 1000f, NativeParameter.Y, keyedParameters, onInsertKeyframe, onValueChange = { updateDraft(NativeParameter.Y, it) }, onValueChangeFinished = ::commitDraft)
        ParameterSlider("缩放", draftTransform.scale, 1f, 400f, NativeParameter.SCALE, keyedParameters, onInsertKeyframe, "%", onValueChange = { updateDraft(NativeParameter.SCALE, it) }, onValueChangeFinished = ::commitDraft)
        ParameterSlider("旋转", draftTransform.rotation, -180f, 180f, NativeParameter.ROTATION, keyedParameters, onInsertKeyframe, "°", onValueChange = { updateDraft(NativeParameter.ROTATION, it) }, onValueChangeFinished = ::commitDraft)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalAlignment = Alignment.CenterVertically) {
            NativeParameter.values().forEach { parameter ->
                val selected = activeParameter == parameter
                Text(
                    text = when (parameter) {
                        NativeParameter.SCALE -> "缩放"
                        NativeParameter.X -> "X"
                        NativeParameter.Y -> "Y"
                        NativeParameter.ROTATION -> "旋转"
                    },
                    color = if (selected) NcColors.text else NcColors.secondary,
                    fontSize = 9.sp,
                    modifier = Modifier.clip(RoundedCornerShape(5.dp))
                        .background(if (selected) Color(0xFF3A3D40) else Color.Transparent)
                        .clickable { activeParameter = parameter }
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                )
            }
        }
        if (activeFrames.isEmpty()) {
            Text("当前通道暂无关键帧", color = NcColors.muted, fontSize = 9.sp)
        } else {
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(5.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                activeFrames.forEach { frame ->
                    val selected = frame.timeUs == selectedFrame?.timeUs
                    Text(
                        text = "%.2fs · %.2f".format(frame.timeUs / 1_000_000.0, frame.value),
                        color = if (selected) NcColors.text else NcColors.secondary,
                        fontSize = 9.sp,
                        modifier = Modifier.clip(RoundedCornerShape(5.dp))
                            .background(if (selected) Color(0xFF414549) else NcColors.lane)
                            .clickable { selectedFrameTimeUs = frame.timeUs }
                            .padding(horizontal = 7.dp, vertical = 5.dp)
                    )
                }
            }
            selectedFrame?.let { frame ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "缓动 ${easingLabel(frame.easing)}",
                        color = NcColors.secondary,
                        fontSize = 9.sp,
                        modifier = Modifier.weight(1f)
                            .clickable { onSetKeyframeEasing(activeParameter, frame.copy(easing = (frame.easing + 1) % 6)) }
                            .padding(vertical = 4.dp)
                    )
                    Text("删除关键帧", color = NcColors.danger, fontSize = 9.sp,
                        modifier = Modifier.clickable { onRemoveKeyframe(activeParameter, frame.timeUs) }.padding(4.dp))
                }
                KeyframeCurvePreview(frame = frame, modifier = Modifier.fillMaxWidth().height(44.dp))
            }
        }
    }
}
private fun easingLabel(easing: Int): String = when (easing) {
    0 -> "线性"; 1 -> "缓入"; 2 -> "缓出"; 3 -> "缓入缓出"; 4 -> "阶梯"; 5 -> "贝塞尔"; else -> "未知"
}
@Composable
private fun ParameterSlider(
    label: String, value: Float, low: Float, high: Float, parameter: NativeParameter,
    keyedParameters: Set<NativeParameter>, onInsertKeyframe: (NativeParameter) -> Unit,
    suffix: String = "",
    onValueChange: (Float) -> Unit,
    onValueChangeFinished: () -> Unit
) {
    // Engine snapshots are validated at the adapter boundary, but keep the
    // Compose control resilient to malformed/stale host values as well.
    val safeValue = value.takeIf { it.isFinite() }?.coerceIn(low, high) ?: low
    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.height(30.dp)) {
        Text(label, color = NcColors.secondary, fontSize = 10.sp, modifier = Modifier.width(34.dp))
        Slider(
            value = safeValue,
            onValueChange = onValueChange,
            onValueChangeFinished = onValueChangeFinished,
            valueRange = low..high,
            modifier = Modifier.weight(1f)
        )
        Text("%.1f".format(safeValue) + suffix, color = NcColors.text, fontSize = 9.sp, textAlign = TextAlign.End, modifier = Modifier.width(48.dp))
        Text(if (parameter in keyedParameters) "◆" else "◇",
            color = if (parameter in keyedParameters) Color.White else NcColors.secondary,
            fontSize = 15.sp, modifier = Modifier.width(26.dp).clickable { onInsertKeyframe(parameter) }, textAlign = TextAlign.Center)
    }
}
@Composable
private fun KeyframeCurvePreview(frame: NativeEditorCommandAdapter.Keyframe, modifier: Modifier = Modifier) {
    Canvas(modifier = modifier) {
        // A newly composed or temporarily constrained panel can report a
        // zero/tiny canvas before its first measured frame.
        if (size.width <= 4f || size.height <= 6f) return@Canvas
        val left = 2f; val right = size.width - 2f; val top = 3f; val bottom = size.height - 3f
        val width = right - left; val height = bottom - top
        val path = androidx.compose.ui.graphics.Path().apply {
            moveTo(left, bottom)
            when (frame.easing) {
                4 -> { lineTo(left + width * 0.5f, bottom); lineTo(left + width * 0.5f, top); lineTo(right, top) }
                0 -> lineTo(right, top)
                1 -> cubicTo(left, bottom, right, top, right, top)
                2 -> cubicTo(left, bottom, right, bottom, right, top)
                3 -> cubicTo(left + width * 0.42f, bottom, left + width * 0.58f, top, right, top)
                5 -> {
                    // Engine Bezier Y controls may overshoot [0, 1]. Keep a
                    // useful overshoot preview while bounding Canvas geometry.
                    // Keep malformed host-supplied values from poisoning
                    // the Canvas path. The command adapter validates engine
                    // data too; these bounds protect this presentation boundary.
                    val c1x = frame.c1x.takeIf { it.isFinite() }
                        ?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 0f
                    val c2x = frame.c2x.takeIf { it.isFinite() }
                        ?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(0f, 1f) ?: 1f
                    val c1y = frame.c1y.takeIf { it.isFinite() }
                        ?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(-4f, 4f) ?: 0f
                    val c2y = frame.c2y.takeIf { it.isFinite() }
                        ?.toFloat()?.takeIf { it.isFinite() }?.coerceIn(-4f, 4f) ?: 0f
                    cubicTo(
                        left + width * c1x,
                        bottom - height * c1y,
                        left + width * c2x,
                        bottom - height * c2y,
                        right,
                        top
                    )
                }
                else -> lineTo(right, top)
            }
        }
        drawLine(Color(0xFF55595C), androidx.compose.ui.geometry.Offset(left, bottom), androidx.compose.ui.geometry.Offset(right, bottom), 0.8f)
        drawLine(Color(0xFF55595C), androidx.compose.ui.geometry.Offset(left, top), androidx.compose.ui.geometry.Offset(left, bottom), 0.8f)
        drawPath(path, Color.LightGray, style = androidx.compose.ui.graphics.drawscope.Stroke(width = 1.6f))
    }
}
@Composable
private fun TransportBar(onAction: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(46.dp)
            .background(NcColors.timeline),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically
    ) {
        TransportButton("✂", onAction)
        Spacer(Modifier.width(38.dp))
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(Color(0xFF292C2E))
                .clickable(onClick = onAction),
            contentAlignment = Alignment.Center
        ) {
            Text("▶", color = Color.White, fontSize = 14.sp)
        }
        Spacer(Modifier.width(38.dp))
        TransportButton("⌫", onAction, tint = NcColors.danger)
    }
}

@Composable
private fun TransportButton(label: String, onAction: () -> Unit, tint: Color = NcColors.text) {
    Box(
        modifier = Modifier
            .size(34.dp)
            .clip(CircleShape)
            .clickable(onClick = onAction),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = tint, fontSize = 17.sp)
    }
}

@Composable
private fun NativeToolBar(
    activeTool: String,
    expandedTool: String?,
    activeSubTool: String?,
    onToolSelected: (String) -> Unit,
    onSubToolSelected: (String, String) -> Unit
) {
    val tools = listOf(
        "剪辑" to listOf("分割", "删除", "复制", "变速", "动画"),
        "音频" to listOf("音量", "淡入淡出", "降噪", "人声分离"),
        "文本" to listOf("添加文本", "自动字幕", "样式", "朗读"),
        "贴纸" to listOf("贴纸库", "表情", "自定义"),
        "特效" to listOf("视频特效", "转场", "滤镜", "蒙版"),
        "画面" to listOf("位置", "缩放", "旋转", "锚点"),
        "调节" to listOf("基础", "曲线", "色轮", "LUT"),
        "更多" to listOf("画中画", "关键帧", "跟踪", "AI工具")
    )
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(82.dp)
            .background(NcColors.topBar)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 8.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(7.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        tools.forEach { (tool, subTools) ->
            val selected = activeTool == tool
            Column(
                modifier = Modifier
                    .width(54.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (selected) Color(0xFF303235) else Color.Transparent)
                    .clickable { onToolSelected(tool) }
                    .padding(vertical = 7.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(25.dp)
                        .clip(CircleShape)
                        .border(1.dp, if (selected) Color.White else NcColors.line, CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    Text(tool.take(1), color = NcColors.text, fontSize = 11.sp)
                }
                Text(
                    tool,
                    color = if (selected) NcColors.text else NcColors.secondary,
                    fontSize = 9.sp,
                    textAlign = TextAlign.Center
                )
            }
            if (expandedTool == tool) {
                subTools.forEach { subTool ->
                    val subSelected = activeTool == tool && activeSubTool == subTool
                    Column(
                        modifier = Modifier
                            .width(58.dp)
                            .clip(RoundedCornerShape(10.dp))
                            .background(if (subSelected) Color(0xFF3A3D40) else NcColors.panel)
                            .clickable { onSubToolSelected(tool, subTool) }
                            .padding(vertical = 8.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(5.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(22.dp)
                                .clip(CircleShape)
                                .border(1.dp, if (subSelected) Color.White else NcColors.line, CircleShape),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(subTool.take(1), color = NcColors.text, fontSize = 10.sp)
                        }
                        Text(
                            subTool,
                            color = if (subSelected) NcColors.text else NcColors.secondary,
                            fontSize = 8.sp,
                            textAlign = TextAlign.Center,
                            maxLines = 1
                        )
                    }
                }
            }
        }
    }
}
