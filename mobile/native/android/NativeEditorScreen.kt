package com.nairoclip.app

import android.view.View
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Image
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
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
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.draw.clip
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ColorFilter
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import java.util.Locale
import kotlin.math.roundToInt
import kotlin.math.roundToLong
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Native editor surface for the S5 migration.
 *
 * Compose owns the editor presentation and routes migrated editing actions
 * through the native command adapter/host controller. Tools whose native
 * command contracts are not yet migrated remain available through the retained
 * legacy editor via [onOpenLegacy]; the fallback is intentional and must not be
 * removed as part of incremental page migration.
 */
data class NativePreviewCacheUiStats(
    val frameHits: Long,
    val frameMisses: Long,
    val readerOpens: Long,
    val cachedFrames: Int,
    val capacity: Int
)

enum class NativeQuickAction {
    IMPORT_MEDIA,
    RETURN_HOME,
    STEP_PREVIOUS,
    STEP_PREVIOUS_TO_START,
    ADD_MARKER,
    SHOW_MARKER_LIST,
    PREVIEW_RENDER,
    KEYFRAMES,
    TOGGLE_GUIDES,
    IMAGES,
    AI_EDIT
}

@Composable
fun NativeEditorScreen(
    previewView: View? = null,
    projectName: String = "未命名工程",
    timelineTracks: List<NativeTimelineTrack> = emptyList(),
    timelineMarkers: List<NativeTimelineMarker> = emptyList(),
    timelineDurationUs: Long = 30_000_000L,
    playheadUs: Long = 0L,
    onPlayheadChanged: (Long) -> Unit,
    onPausePlayback: (() -> Unit)? = null,
    onAddMarker: ((Long) -> Unit)? = null,
    onRemoveMarker: ((Long) -> Unit)? = null,
    onClipSelected: (Int, Long) -> Unit,
    onTrackVisibilityChanged: ((Int) -> Unit)? = null,
    onTrackMuteChanged: ((Int) -> Unit)? = null,
    transform: NativeTransform = NativeTransform(),
    anchor: Int,
    keyedParameters: Set<NativeParameter> = emptySet(),
    keyframes: Map<NativeParameter, List<NativeEditorCommandAdapter.Keyframe>> = emptyMap(),
    onTransformChanged: (Int, Long, NativeTransform) -> Unit,
    onAnchorChanged: (Int, Long, Int) -> Unit,
    onInsertKeyframe: (Int, Long, NativeParameter) -> Unit,
    onRemoveKeyframe: (Int, Long, NativeParameter, Long) -> Unit,
    onSetKeyframeEasing: (Int, Long, NativeParameter, Long, Int, Double, Double, Double, Double) -> Unit,
    canUndo: Boolean = false,
    canRedo: Boolean = false,
    onUndo: () -> Unit,
    onRedo: () -> Unit,
    onOpenLegacy: () -> Unit,
    onQuickAction: ((NativeQuickAction) -> Unit)? = null,
    onSplitClip: ((Int, Long, Long) -> Unit)? = null,
    onDeleteClip: ((Int, Long) -> Unit)? = null,
    onClipEnabledChanged: ((Int, Long, Boolean) -> Unit)? = null,
    onDuplicateClip: ((Int, Long) -> Unit)? = null,
    onReverseClip: ((Int, Long) -> Unit)? = null,
    onRippleDeleteClip: ((Int, Long) -> Unit)? = null,
    onClipSpeedChanged: ((Int, Long, Long, Long) -> Unit)? = null,
    onSpeedCurvePreset: ((Int, Long, Int) -> Unit)? = null,
    speedCurvePoints: List<NativeEditorCommandAdapter.SpeedCurvePoint>? = null,
    onSpeedCurveChanged: ((Int, Long, List<NativeEditorCommandAdapter.SpeedCurvePoint>) -> Unit)? = null,
    clipAudio: NativeEditorCommandAdapter.ClipAudioState? = null,
    onClipAudioChanged: ((Int, Long, NativeEditorCommandAdapter.ClipAudioState) -> Unit)? = null,
    clipDenoiseStrength: Int? = null,
    onClipDenoiseChanged: ((Int, Long, Int) -> Unit)? = null,
    clipOpacity: Double? = null,
    onClipOpacityChanged: ((Int, Long, Double) -> Unit)? = null,
    onFreezeClip: ((Int, Long, Long, Long) -> Unit)? = null,
    onAddClipEffect: ((Int, Long, Int) -> Unit)? = null,
    onUpdateClipEffect: ((Int, Long, NativeClipEffect, Double) -> Unit)? = null,
    onSetClipEffectEnabled: ((Int, Long, Long, Boolean) -> Unit)? = null,
    onMoveClipEffect: ((Int, Long, Long, Int) -> Unit)? = null,
    onRemoveClipEffect: ((Int, Long, Long) -> Unit)? = null,
    clipEffectSpecs: Map<Int, NativeEditorCommandAdapter.EffectSpec> = emptyMap(),
    clipFilter: NativeEditorCommandAdapter.ClipFilterState? = null,
    onClipFilterChanged: ((Int, Long, Int, Double) -> Unit)? = null,
    clipLut: NativeEditorCommandAdapter.ClipLutState? = null,
    clipColorGrade: NativeEditorCommandAdapter.ClipColorGradeState? = null,
    onClipColorGradeChanged: ((Int, Long, NativeEditorCommandAdapter.ClipColorGradeState) -> Unit)? = null,
    onClipLutChanged: ((Int, Long, String, Double) -> Unit)? = null,
    onValidateClipLut: ((String) -> NativeEditorCommandAdapter.Result<NativeEditorCommandAdapter.LutValidationState>)? = null,
    clipMask: NativeEditorCommandAdapter.ClipMaskState? = null,
    onClipMaskChanged: ((Int, Long, NativeEditorCommandAdapter.ClipMaskState) -> Unit)? = null,
    clipTransitions: List<NativeEditorCommandAdapter.TransitionState> = emptyList(),
    transitionTypes: List<Pair<Int, String>> = emptyList(),
    onAddTransition: ((Int, Long, Long, Int, Long) -> Unit)? = null,
    onRemoveTransition: ((Int, Long) -> Unit)? = null,
    onCreateTextClip: ((String, Long, Long) -> Unit)? = null,
    onTextStyleChanged: ((Int, Long, NativeEditorCommandAdapter.TextStyleUpdate) -> Unit)? = null,
    onCreateSubtitle: ((String, Long, Long) -> Unit)? = null,
    subtitleCues: List<NativeEditorCommandAdapter.SubtitleCueState> = emptyList(),
    onRemoveSubtitle: ((Long) -> Unit)? = null,
    autoSubtitleState: NativeAutoSubtitleUiState = NativeAutoSubtitleUiState(),
    onStartAutoSubtitle: (() -> Unit)? = null,
    onCancelAutoSubtitle: (() -> Unit)? = null,
    onPrewarmPreviewFrame: ((Long, Int) -> Boolean)? = null,
    onReadPreviewCacheStats: (() -> NativePreviewCacheUiStats?)? = null,
    previewFrameWidth: Int = 320,
    onShareProject: (() -> Unit)? = null,
    onExportProject: (() -> Unit)? = null,
    onExpandPreview: (() -> Unit)? = null,
    isPlaying: Boolean = false,
    onTogglePlayback: (() -> Unit)? = null,
    onTrackLockChanged: ((Int) -> Unit)? = null,
    imageAssets: List<NativeTimelineAsset> = emptyList(),
    backgroundRgba: Long? = null,
    isImportingImages: Boolean = false,
    imagePanelMessage: String? = null,
    onPickImages: (() -> Unit)? = null,
    onRemoveImage: ((Long) -> Unit)? = null,
    onSetBackgroundColor: ((Long) -> Unit)? = null,
    onPlayFromStart: (() -> Unit)? = null,
    onClipSelectionCleared: (() -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    var activeTool by remember { mutableStateOf("剪辑") }
    var expandedTool by remember { mutableStateOf<String?>(null) }
    var activeSubTool by remember { mutableStateOf<String?>(null) }
    var selectedClip by remember(projectName) { mutableStateOf<Pair<Int, Long>?>(null) }
    var showMarkerList by remember(projectName) { mutableStateOf(false) }
    var showImageBackgroundPanel by remember(projectName) { mutableStateOf(false) }
    var showGuides by remember(projectName) { mutableStateOf(false) }
    // Keyframe editing is a dedicated quick-action panel, not a fabricated L2 toolbar item.
    var showKeyframePanel by remember(projectName) { mutableStateOf(false) }
    var prewarmProgress by remember(projectName) { mutableStateOf(0f) }
    var prewarmRunning by remember(projectName) { mutableStateOf(false) }
    var prewarmStatus by remember(projectName) { mutableStateOf<String?>(null) }
    var prewarmStats by remember(projectName) { mutableStateOf<NativePreviewCacheUiStats?>(null) }
    var prewarmJob by remember { mutableStateOf<Job?>(null) }
    val prewarmScope = rememberCoroutineScope()
    LaunchedEffect(projectName) {
        // Do not carry an expanded tool or parameter context into a different project.
        prewarmJob?.cancel()
        prewarmJob = null
        prewarmRunning = false
        prewarmProgress = 0f
        prewarmStatus = null
        prewarmStats = null
        activeTool = "剪辑"
        expandedTool = null
        activeSubTool = null
        showKeyframePanel = false
        showImageBackgroundPanel = false
    }
    LaunchedEffect(timelineTracks, selectedClip) {
        val selection = selectedClip ?: return@LaunchedEffect
        val stillExists = timelineTracks.any { track ->
            (if (track.engineIndex >= 0) track.engineIndex else timelineTracks.indexOf(track)) == selection.first &&
                track.visible &&
                !track.locked &&
                track.clips.any { it.id == selection.second }
        }
        if (!stillExists) {
            selectedClip = null
            // Keep the host/controller selection in sync when a track becomes
            // hidden, locked, or the selected clip is removed.
            onClipSelectionCleared?.invoke()
        }
    }

    MaterialTheme {
        BoxWithConstraints(
            modifier = modifier
                .fillMaxSize()
                .background(NcColors.background)
        ) {
            val responsive = NativeEditorResponsiveSpec.forHeight(maxHeight.value)
            Column(modifier = Modifier.fillMaxSize()) {
            EditorTopBar(
                onShare = { onShareProject?.invoke() ?: onOpenLegacy() },
                onExport = { onExportProject?.invoke() ?: onOpenLegacy() }
            )

            PreviewStage(
                previewView = previewView,
                showGuides = showGuides,
                onExpandPreview = { onExpandPreview?.invoke() ?: onOpenLegacy() },
                modifier = Modifier
                    .weight(1f)
                    .fillMaxWidth()
                    .heightIn(min = responsive.previewMinHeightDp.dp)
            )

            QuickActionStrip(
                canUndo = canUndo,
                canRedo = canRedo,
                onUndo = onUndo,
                onRedo = onRedo,
                onQuickAction = { action ->
                    when (action) {
                        NativeQuickAction.STEP_PREVIOUS -> {
                            NativeQuickActionGestureContract.previousTimelineAnchor(
                                timelineTracks, playheadUs
                            )?.let { targetUs ->
                                onPausePlayback?.invoke()
                                onPlayheadChanged(targetUs)
                            }
                        }
                        NativeQuickAction.STEP_PREVIOUS_TO_START -> {
                            onPausePlayback?.invoke()
                            onPlayheadChanged(0L)
                        }
                        NativeQuickAction.ADD_MARKER -> {
                            if (onAddMarker != null) onAddMarker(playheadUs)
                            else onQuickAction?.invoke(action) ?: onOpenLegacy()
                        }
                        NativeQuickAction.SHOW_MARKER_LIST -> showMarkerList = true
                        NativeQuickAction.TOGGLE_GUIDES -> showGuides = !showGuides
                        NativeQuickAction.PREVIEW_RENDER -> {
                            val prewarm = onPrewarmPreviewFrame
                            if (prewarm == null) {
                                onQuickAction?.invoke(action) ?: onOpenLegacy()
                            } else if (prewarmRunning) {
                                prewarmJob?.cancel()
                                prewarmJob = null
                                prewarmRunning = false
                                prewarmProgress = 0f
                                prewarmStatus = "已取消预热"
                            } else {
                                val spanUs = timelineDurationUs.coerceAtLeast(0L)
                                if (spanUs <= 0L) {
                                    prewarmStatus = "时间轴为空，没有可预热的帧"
                                } else {
                                    val samplesUs = NativePreviewPrewarmPlan.samplesUs(spanUs, playheadUs)
                                    val total = samplesUs.size
                                    if (total == 0) {
                                        prewarmStatus = "时间轴为空，没有可预热的帧"
                                    } else {
                                        prewarmProgress = 0f
                                        prewarmStats = null
                                        prewarmStatus = "正在预热 0/$total 帧"
                                        prewarmRunning = true
                                        prewarmJob = prewarmScope.launch {
                                            var done = 0
                                            var failed = 0
                                            try {
                                                for (timeUs in samplesUs) {
                                                    val ok = withContext(Dispatchers.IO) {
                                                        prewarm(timeUs, previewFrameWidth.coerceIn(16, 2048))
                                                    }
                                                    if (!ok) failed++
                                                    done++
                                                    prewarmProgress = done.toFloat() / total
                                                    prewarmStatus = "正在预热 $done/$total 帧 · 失败 $failed"
                                                }
                                                prewarmStats = withContext(Dispatchers.IO) {
                                                    onReadPreviewCacheStats?.invoke()
                                                }
                                                prewarmStatus = "预热完成：$done/$total 帧 · 失败 $failed"
                                            } catch (_: kotlinx.coroutines.CancellationException) {
                                                prewarmStatus = "已取消预热"
                                            } catch (error: Throwable) {
                                                prewarmStatus = "预热失败：${error.message ?: "未知错误"}"
                                            } finally {
                                                prewarmRunning = false
                                                prewarmJob = null
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        NativeQuickAction.IMAGES -> {
                            if (onPickImages != null && onRemoveImage != null &&
                                onSetBackgroundColor != null
                            ) {
                                showImageBackgroundPanel = true
                            } else {
                                // Do not open a native panel whose engine/file
                                // callbacks are absent; return to the real legacy
                                // implementation instead of presenting dead controls.
                                onQuickAction?.invoke(action) ?: onOpenLegacy()
                            }
                        }
                        NativeQuickAction.KEYFRAMES -> {
                            showKeyframePanel = true
                        }
                        else -> onQuickAction?.invoke(action) ?: onOpenLegacy()
                    }
                }
            )

            if (showImageBackgroundPanel) {
                NativeImageBackgroundPanel(
                    assets = imageAssets.filter { it.type == 2 },
                    backgroundRgba = backgroundRgba,
                    isImporting = isImportingImages,
                    message = imagePanelMessage,
                    onPickImages = onPickImages ?: {},
                    onRemoveImage = onRemoveImage ?: {},
                    onSetBackgroundColor = onSetBackgroundColor ?: {},
                    onDismiss = { showImageBackgroundPanel = false }
                )
            }

            NativeTimelinePanel(
                tracks = timelineTracks,
                durationUs = timelineDurationUs,
                playheadUs = playheadUs,
                onPlayheadChanged = onPlayheadChanged,
                onClipSelected = { trackIndex, clipId ->
                    selectedClip = trackIndex to clipId
                    onClipSelected(trackIndex, clipId)
                },
                onClipSelectionCleared = {
                    selectedClip = null
                    onClipSelectionCleared?.invoke()
                },
                onTrackVisibilityChanged = onTrackVisibilityChanged,
                onTrackMuteChanged = onTrackMuteChanged,
                onTrackLockChanged = onTrackLockChanged,
                onPlayFromStart = onPlayFromStart ?: {
                    onPlayheadChanged(0L)
                    if (!isPlaying) onTogglePlayback?.invoke()
                },
                modifier = Modifier
                    .fillMaxWidth()
                    .height(responsive.timelineHeightDp.dp)
            )

            // The selected target is shared by the scrollable parameter panels,
            // transport controls and level-2 toolbar. Keep it in the enclosing
            // BoxWithConstraints scope so all three regions see the same valid
            // (visible, unlocked, enabled) selection.

            val selectedTarget = selectedClip?.takeIf { selection ->
                timelineTracks.any { track ->
                    (if (track.engineIndex >= 0) track.engineIndex else timelineTracks.indexOf(track)) == selection.first &&
                        track.visible &&
                        !track.locked &&
                        track.clips.any { it.id == selection.second }
                }
            }
            val selectedItem = selectedTarget?.let { selection ->
                timelineTracks.firstOrNull { track ->
                    (if (track.engineIndex >= 0) track.engineIndex else timelineTracks.indexOf(track)) == selection.first
                }?.clips?.firstOrNull { it.id == selection.second }
            }

            // Dynamic editor panels scroll within a bounded viewport. This keeps
            // the timeline, transport row and tool strip reachable on short screens.
            Column(
                modifier = Modifier
                    .heightIn(max = responsive.parameterPanelMaxHeightDp.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                if (prewarmRunning || prewarmStatus != null || prewarmStats != null) {
                    NativePreviewPrewarmPanel(
                        running = prewarmRunning,
                        progress = prewarmProgress,
                        status = prewarmStatus,
                        stats = prewarmStats,
                        onCancel = {
                            prewarmJob?.cancel()
                            prewarmJob = null
                            prewarmRunning = false
                            prewarmProgress = 0f
                            prewarmStatus = "已取消预热"
                        },
                        onDismiss = {
                            prewarmStatus = null
                            prewarmStats = null
                        }
                    )
                }
    
                if (showMarkerList) {
                    NativeMarkerListPanel(
                        markers = timelineMarkers,
                        onSelect = { marker ->
                            onPausePlayback?.invoke()
                            onPlayheadChanged(marker.timeUs)
                            showMarkerList = false
                        },
                        onRemove = onRemoveMarker,
                        onDismiss = { showMarkerList = false }
                    )
                }
            selectedClip?.let { selection ->
                val selectedTrack = timelineTracks.firstOrNull { track ->
                    (if (track.engineIndex >= 0) track.engineIndex else timelineTracks.indexOf(track)) == selection.first
                }
                val selectedItem = selectedTrack?.clips?.firstOrNull { it.id == selection.second }
                if (selectedTrack != null && selectedItem != null) {
                    ClipSelectionBar(
                        trackName = selectedTrack.name,
                        clipName = selectedItem.label,
                        onClearSelection = { selectedClip = null },
                        modifier = Modifier.fillMaxWidth()
                    )
                }
            }

            if (showKeyframePanel && selectedTarget == null) {
                Text(
                    "先选择一个时间轴片段，再编辑关键帧",
                    color = NcColors.secondary,
                    fontSize = 10.sp,
                    modifier = Modifier.fillMaxWidth().background(NcColors.stage).padding(horizontal = 12.dp, vertical = 8.dp)
                )
            }
            if ((((activeTool == "画中画" && activeSubTool in setOf("位置", "缩放", "旋转", "锚点")) || showKeyframePanel) && selectedTarget != null)) {
                NativeParameterPanel(
                    targetTrackIndex = selectedTarget.first,
                    targetClipId = selectedTarget.second,
                    activeSubTool = if (showKeyframePanel) "动画" else (activeSubTool ?: "位置"),
                    transform = transform,
                    anchor = anchor,
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
                    onAnchorChanged = { value ->
                        if (value in NativeTransformRange.ANCHOR_MIN..NativeTransformRange.ANCHOR_MAX) {
                            onAnchorChanged(selectedTarget.first, selectedTarget.second, value)
                        }
                    },
                    onInsertKeyframe = { parameter ->
                        onInsertKeyframe(selectedTarget.first, selectedTarget.second, parameter)
                    },
                    modifier = Modifier.fillMaxWidth()
                )
            }


            if (activeTool == "剪辑" && activeSubTool == "不透明度" && selectedTarget != null) {
                if (clipOpacity != null && onClipOpacityChanged != null) {
                    NativeClipOpacityPanel(
                        clipId = selectedTarget.second,
                        opacity = clipOpacity,
                        onChange = { value ->
                            onClipOpacityChanged.invoke(selectedTarget.first, selectedTarget.second, value)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的不透明度…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "剪辑" && activeSubTool == "变速" && selectedTarget != null) {
                val selectedSpeed = timelineTracks
                    .firstOrNull { it.engineIndex == selectedTarget.first }
                    ?.clips?.firstOrNull { it.id == selectedTarget.second }
                    ?.let { it.speedNumerator.toDouble() / it.speedDenominator.toDouble() }
                    ?.toFloat()
                    ?: 1f
                NativeSpeedPanel(
                    clipId = selectedTarget.second,
                    initialSpeed = selectedSpeed,
                    onApply = { speed ->
                        val numerator = (speed * 1000f).toLong()
                        if (numerator > 0L) {
                            onClipSpeedChanged?.invoke(selectedTarget.first, selectedTarget.second, numerator, 1000L)
                        }
                    },
                    modifier = Modifier.fillMaxWidth()
                )
            }

            if (activeTool == "剪辑" && activeSubTool == "变速曲线" && selectedTarget != null) {
                NativeSpeedCurvePresetPanel(
                    onApply = { preset ->
                        onSpeedCurvePreset?.invoke(selectedTarget.first, selectedTarget.second, preset)
                    },
                    enabled = onSpeedCurvePreset != null,
                    modifier = Modifier.fillMaxWidth()
                )
            }

            if (activeTool == "剪辑" && activeSubTool == "变速曲线" &&
                selectedTarget != null && onSpeedCurveChanged != null
            ) {
                if (speedCurvePoints != null) {
                    NativeSpeedCurvePointPanel(
                        clipId = selectedTarget.second,
                        points = speedCurvePoints,
                        onChange = { points ->
                            onSpeedCurveChanged.invoke(selectedTarget.first, selectedTarget.second, points)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的速度曲线…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "音频" && activeSubTool in setOf("音量", "淡入淡出") &&
                selectedTarget != null && onClipAudioChanged != null
            ) {
                if (clipAudio != null) {
                    NativeClipAudioPanel(
                        clipId = selectedTarget.second,
                        durationUs = selectedItemDuration(timelineTracks, selectedTarget),
                        audio = clipAudio,
                        showGain = activeSubTool == "音量",
                        showFades = activeSubTool == "淡入淡出",
                        onChange = { audio ->
                            onClipAudioChanged.invoke(selectedTarget.first, selectedTarget.second, audio)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的音频参数…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "音频" && activeSubTool == "降噪" &&
                selectedTarget != null && onClipDenoiseChanged != null
            ) {
                if (clipDenoiseStrength != null) {
                    NativeClipDenoisePanel(
                        strength = clipDenoiseStrength,
                        onChange = { strength ->
                            onClipDenoiseChanged.invoke(selectedTarget.first, selectedTarget.second, strength)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的降噪状态…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "调节" && activeSubTool == "基础" && selectedTarget != null) {
                val selectedEffects = selectedItem?.effects.orEmpty()
                if (onAddClipEffect != null && onUpdateClipEffect != null &&
                    onSetClipEffectEnabled != null && onMoveClipEffect != null &&
                    onRemoveClipEffect != null
                ) {
                    NativeClipEffectsPanel(
                        effects = selectedEffects.filter { it.type in setOf(1, 2, 3, 4, 5, 9, 10, 11) },
                        specs = clipEffectSpecs,
                        onAdd = { type -> onAddClipEffect.invoke(selectedTarget.first, selectedTarget.second, type) },
                        onUpdate = { effect, value ->
                            onUpdateClipEffect.invoke(selectedTarget.first, selectedTarget.second, effect, value)
                        },
                        onEnabled = { effectId, enabled ->
                            onSetClipEffectEnabled.invoke(selectedTarget.first, selectedTarget.second, effectId, enabled)
                        },
                        onMove = { effectId, direction ->
                            onMoveClipEffect.invoke(selectedTarget.first, selectedTarget.second, effectId, direction)
                        },
                        onRemove = { effectId ->
                            onRemoveClipEffect.invoke(selectedTarget.first, selectedTarget.second, effectId)
                        },
                        panelTitle = "基础画面调节 · 效果栈",
                        allowStackOrdering = false,
                        options = listOf(
                            NativeEffectOption(1, "亮度"), NativeEffectOption(2, "对比度"),
                            NativeEffectOption(3, "饱和度"), NativeEffectOption(4, "色温"),
                            NativeEffectOption(5, "色调"), NativeEffectOption(10, "锐化"),
                            NativeEffectOption(9, "暗角"), NativeEffectOption(11, "颗粒")
                        ),
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("基础调节面板等待宿主接入真实引擎命令与参数规格", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "特效" && activeSubTool == "视频特效" && selectedTarget != null) {
                val selectedEffects = selectedItem?.effects.orEmpty()
                if (onAddClipEffect != null && onUpdateClipEffect != null &&
                    onSetClipEffectEnabled != null && onMoveClipEffect != null &&
                    onRemoveClipEffect != null
                ) {
                    NativeClipEffectsPanel(
                        effects = selectedEffects,
                        specs = clipEffectSpecs,
                        onAdd = { type -> onAddClipEffect.invoke(selectedTarget.first, selectedTarget.second, type) },
                        onUpdate = { effect, value ->
                            onUpdateClipEffect.invoke(selectedTarget.first, selectedTarget.second, effect, value)
                        },
                        onEnabled = { effectId, enabled ->
                            onSetClipEffectEnabled.invoke(selectedTarget.first, selectedTarget.second, effectId, enabled)
                        },
                        onMove = { effectId, direction ->
                            onMoveClipEffect.invoke(selectedTarget.first, selectedTarget.second, effectId, direction)
                        },
                        onRemove = { effectId ->
                            onRemoveClipEffect.invoke(selectedTarget.first, selectedTarget.second, effectId)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("特效面板等待宿主接入引擎命令与参数规格", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "特效" && activeSubTool == "滤镜" && selectedTarget != null) {
                if (clipFilter != null && onClipFilterChanged != null) {
                    NativeClipFilterPanel(
                        state = clipFilter,
                        onChange = { style, intensity ->
                            onClipFilterChanged.invoke(selectedTarget.first, selectedTarget.second, style, intensity)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的滤镜状态…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "调节" && activeSubTool == "曲线" && selectedTarget != null) {
                if (clipColorGrade != null && onClipColorGradeChanged != null) {
                    NativeClipColorCurvePanel(
                        state = clipColorGrade,
                        onChange = { grade ->
                            onClipColorGradeChanged.invoke(selectedTarget.first, selectedTarget.second, grade)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的 RGB 曲线状态…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "调节" && activeSubTool == "LUT" && selectedTarget != null) {
                if (clipLut != null && onClipLutChanged != null) {
                    NativeClipLutPanel(
                        state = clipLut,
                        onChange = { path, strength ->
                            onClipLutChanged.invoke(selectedTarget.first, selectedTarget.second, path, strength)
                        },
                        onValidate = onValidateClipLut,
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的 LUT 状态…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "特效" && activeSubTool == "转场" && selectedTarget != null) {
                val track = timelineTracks.firstOrNull { it.engineIndex == selectedTarget.first }
                val nextClipId = track?.clips
                    ?.sortedBy { it.startUs }
                    ?.let { sorted ->
                        val current = sorted.firstOrNull { it.id == selectedTarget.second }
                        current?.let { clip ->
                            sorted.firstOrNull { it.startUs == clip.startUs + clip.durationUs }?.id
                        }
                    }
                NativeClipTransitionPanel(
                    transitions = clipTransitions,
                    types = transitionTypes,
                    trackIndex = selectedTarget.first,
                    fromClipId = selectedTarget.second,
                    toClipId = nextClipId,
                    onAdd = onAddTransition,
                    onRemove = onRemoveTransition,
                    modifier = Modifier.fillMaxWidth()
                )
            }

            if (activeTool == "特效" && activeSubTool == "蒙版" && selectedTarget != null) {
                if (clipMask != null && onClipMaskChanged != null) {
                    NativeClipMaskPanel(
                        state = clipMask,
                        onChange = { value ->
                            onClipMaskChanged.invoke(selectedTarget.first, selectedTarget.second, value)
                        },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("正在等待宿主读取当前片段的蒙版状态…", color = NcColors.secondary, fontSize = 10.sp)
                }
            }

            if (activeTool == "文字" && activeSubTool == "自动字幕") {
                NativeAutoSubtitlePanel(
                    state = autoSubtitleState,
                    onStart = onStartAutoSubtitle,
                    onCancel = onCancelAutoSubtitle,
                    modifier = Modifier.fillMaxWidth()
                )
            }
            if (activeTool == "文字" && activeSubTool == "字幕") {
                if (onCreateSubtitle != null) {
                    NativeSubtitleCreationPanel(
                        playheadUs = playheadUs,
                        cues = subtitleCues,
                        onCreate = { value -> onCreateSubtitle.invoke(value, playheadUs.coerceAtLeast(0L), 3_000_000L) },
                        onRemove = onRemoveSubtitle,
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("字幕编辑等待宿主接入原生字幕命令", color = NcColors.secondary, fontSize = 10.sp)
                }
            }
            if (activeTool == "文字" && activeSubTool == "样式" && selectedTarget != null) {
                if (onTextStyleChanged != null) {
                    NativeTextStylePanel(
                        onChange = { update -> onTextStyleChanged.invoke(selectedTarget.first, selectedTarget.second, update) },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("文字样式等待宿主接入原生引擎命令", color = NcColors.secondary, fontSize = 10.sp)
                }
            }
            if (activeTool == "文字" && activeSubTool == "添加文本") {
                if (onCreateTextClip != null) {
                    NativeTextCreationPanel(
                        playheadUs = playheadUs,
                        onCreate = { value -> onCreateTextClip.invoke(value, playheadUs.coerceAtLeast(0L), 3_000_000L) },
                        modifier = Modifier.fillMaxWidth()
                    )
                } else {
                    Text("文字创建等待宿主接入原生引擎命令", color = NcColors.secondary, fontSize = 10.sp)
                }
            }
            }
            NativeTransportBar(
                hasSelection = selectedTarget != null,
                isPlaying = isPlaying,
                onSplit = {
                    val target = selectedTarget
                    if (target != null && onSplitClip != null) {
                        onSplitClip.invoke(target.first, target.second, playheadUs)
                    } else onOpenLegacy()
                },
                onTogglePlayback = { onTogglePlayback?.invoke() ?: onOpenLegacy() },
                onDelete = {
                    val target = selectedTarget
                    if (target != null && onDeleteClip != null) {
                        onDeleteClip.invoke(target.first, target.second)
                    } else onOpenLegacy()
                }
            )

            NativeToolBar(
                activeTool = activeTool,
                expandedTool = expandedTool,
                activeSubTool = activeSubTool,
                onToolSelected = { tool ->
                    if (expandedTool == tool) {
                        expandedTool = null
                        // Collapsing the active level-2 row also closes its
                        // parameter context; do not leave a hidden tool's
                        // controls occupying the native editor layout.
                        activeSubTool = null
                        showKeyframePanel = false
                    } else {
                        expandedTool = tool
                        activeTool = tool
                        activeSubTool = null
                        showKeyframePanel = false
                    }
                },
                onSubToolSelected = { tool, subTool ->
                    // Every second-level selection replaces the prior panel context.
                    // Immediate commands and legacy-routed tools must not leave an
                    // unrelated parameter panel visible underneath the toolbar.
                    activeTool = tool
                    activeSubTool = null
                    showKeyframePanel = false
                    when {
                        tool == "文字" && subTool == "识别字幕" -> {
                            activeTool = tool
                            activeSubTool = "自动字幕"
                        }
                        tool == "文字" && subTool == "新建文本" && onCreateTextClip != null -> {
                            activeTool = tool
                            activeSubTool = "添加文本"
                        }
                        tool == "画中画" && subTool == "画面" -> {
                            activeTool = tool
                            activeSubTool = "位置"
                        }
                        tool == "调节" && subTool == "参数调节" -> {
                            activeTool = tool
                            activeSubTool = "基础"
                        }
                        tool == "特效" && subTool == "画面特效" &&
                            selectedTarget != null && onAddClipEffect != null &&
                            onUpdateClipEffect != null && onSetClipEffectEnabled != null &&
                            onMoveClipEffect != null && onRemoveClipEffect != null -> {
                            activeTool = tool
                            activeSubTool = "视频特效"
                        }
                        tool == "滤镜" && subTool == "滤镜库" &&
                            selectedTarget != null && onClipFilterChanged != null -> {
                            activeTool = "特效"
                            activeSubTool = "滤镜"
                        }
                        tool == "滤镜" && subTool == "LUT" &&
                            selectedTarget != null && onClipLutChanged != null -> {
                            activeTool = "调节"
                            activeSubTool = "LUT"
                        }
                        tool == "转场" && subTool == "预设转场" &&
                            selectedTarget != null && onAddTransition != null &&
                            onRemoveTransition != null -> {
                            activeTool = "特效"
                            activeSubTool = "转场"
                        }
                        tool == "蒙版" && subTool == "形状" &&
                            selectedTarget != null && onClipMaskChanged != null -> {
                            activeTool = "特效"
                            activeSubTool = "蒙版"
                        }
                        tool == "文字" && subTool == "自动字幕" -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "文字" && subTool == "字幕" && onCreateSubtitle != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "文字" && subTool == "样式" && selectedTarget != null && onTextStyleChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "文字" && subTool == "添加文本" && onCreateTextClip != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "剪辑" && subTool == "分割" &&
                            selectedTarget != null && onSplitClip != null -> {
                            onSplitClip(selectedTarget.first, selectedTarget.second, playheadUs)
                        }
                        tool == "剪辑" && subTool == "删除" &&
                            selectedTarget != null && onDeleteClip != null -> {
                            onDeleteClip(selectedTarget.first, selectedTarget.second)
                        }
                        tool == "剪辑" && subTool == "启用/停用" &&
                            selectedTarget != null && selectedItem != null &&
                            onClipEnabledChanged != null -> {
                            onClipEnabledChanged(
                                selectedTarget.first,
                                selectedTarget.second,
                                !selectedItem.enabled
                            )
                        }
                        tool == "剪辑" && subTool == "复制" &&
                            selectedTarget != null && onDuplicateClip != null -> {
                            onDuplicateClip(selectedTarget.first, selectedTarget.second)
                        }
                        tool == "剪辑" && subTool == "定格" &&
                            selectedTarget != null && onFreezeClip != null -> {
                            onFreezeClip(
                                selectedTarget.first,
                                selectedTarget.second,
                                playheadUs,
                                2_000_000L
                            )
                        }
                        tool == "剪辑" && subTool == "倒放" &&
                            selectedTarget != null && onReverseClip != null -> {
                            onReverseClip(selectedTarget.first, selectedTarget.second)
                        }
                        tool == "剪辑" && subTool == "波纹删除" &&
                            selectedTarget != null && onRippleDeleteClip != null -> {
                            onRippleDeleteClip(selectedTarget.first, selectedTarget.second)
                        }
                        tool == "剪辑" && subTool == "不透明度" &&
                            selectedTarget != null && clipOpacity != null &&
                            onClipOpacityChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "剪辑" && subTool == "变速" && selectedTarget != null &&
                            onClipSpeedChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "剪辑" && subTool == "变速曲线" && selectedTarget != null &&
                            (onSpeedCurvePreset != null || onSpeedCurveChanged != null) -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "音频" && subTool in setOf("音量", "淡入淡出") &&
                            selectedTarget != null && onClipAudioChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "音频" && subTool == "降噪" &&
                            selectedTarget != null && onClipDenoiseChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "画中画" && subTool in setOf("位置", "缩放", "旋转", "锚点") -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "剪辑" && subTool == "动画" && selectedTarget != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "特效" && subTool == "视频特效" &&
                            selectedTarget != null && onAddClipEffect != null &&
                            onUpdateClipEffect != null && onSetClipEffectEnabled != null &&
                            onMoveClipEffect != null && onRemoveClipEffect != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "特效" && subTool == "滤镜" &&
                            selectedTarget != null && onClipFilterChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "调节" && subTool == "基础" &&
                            selectedTarget != null && onAddClipEffect != null &&
                            onUpdateClipEffect != null && onSetClipEffectEnabled != null &&
                            onMoveClipEffect != null && onRemoveClipEffect != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "调节" && subTool == "曲线" &&
                            selectedTarget != null && onClipColorGradeChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "调节" && subTool == "LUT" &&
                            selectedTarget != null && onClipLutChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "特效" && subTool == "蒙版" &&
                            selectedTarget != null && onClipMaskChanged != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        tool == "特效" && subTool == "转场" &&
                            selectedTarget != null && onAddTransition != null &&
                            onRemoveTransition != null -> {
                            activeTool = tool
                            activeSubTool = subTool
                        }
                        else -> onOpenLegacy()
                    }
                }
            )
            }
        }
    }
}

@Composable
private fun NativeSubtitleCreationPanel(
    playheadUs: Long,
    cues: List<NativeEditorCommandAdapter.SubtitleCueState>,
    onCreate: (String) -> Unit,
    onRemove: ((Long) -> Unit)?,
    modifier: Modifier = Modifier
) {
    var content by remember { mutableStateOf("") }
    Column(modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp))
        .border(1.dp, NcColors.line, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("添加字幕", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        BasicTextField(value = content,
            onValueChange = { if (it.toByteArray(Charsets.UTF_8).size <= 16 * 1024) content = it },
            modifier = Modifier.fillMaxWidth().background(NcColors.stage, RoundedCornerShape(8.dp)).padding(10.dp),
            minLines = 2, textStyle = TextStyle(color = NcColors.text, fontSize = 12.sp),
            decorationBox = { inner -> Box { if (content.isEmpty()) Text("输入字幕内容…", color = NcColors.secondary, fontSize = 11.sp); inner() } })
        if (cues.isNotEmpty()) {
            Text("工程字幕（${cues.size}）", color = NcColors.secondary, fontSize = 10.sp)
            cues.take(30).forEach { cue ->
                Row(modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp),
                    horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(cue.text, color = NcColors.text, fontSize = 10.sp, maxLines = 2)
                        Text("${String.format(Locale.ROOT, "%.2f", cue.startUs / 1_000_000.0)}s · ${String.format(Locale.ROOT, "%.2f", cue.durationUs / 1_000_000.0)}s",
                            color = NcColors.secondary, fontSize = 9.sp)
                    }
                    if (onRemove != null) {
                        Text("删除", color = NcColors.secondary, fontSize = 10.sp,
                            modifier = Modifier.clickable { onRemove.invoke(cue.subtitleId) }.padding(8.dp))
                    }
                }
            }
            if (cues.size > 30) Text("仅显示前 30 条；字幕总数：${cues.size}", color = NcColors.secondary, fontSize = 9.sp)
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
            Text("起始：${String.format(Locale.ROOT, "%.2f", playheadUs.coerceAtLeast(0L) / 1_000_000.0)}s · 默认 3 秒", color = NcColors.secondary, fontSize = 9.sp)
            Box(modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(if (content.isNotBlank()) Color(0xFF303235) else NcColors.stage)
                .clickable(enabled = content.isNotBlank()) { onCreate(content.trim()) }.padding(horizontal = 12.dp, vertical = 8.dp)) {
                Text("添加字幕", color = if (content.isNotBlank()) NcColors.text else NcColors.secondary, fontSize = 10.sp)
            }
        }
    }
}

@Composable
private fun NativeTextStylePanel(onChange: (NativeEditorCommandAdapter.TextStyleUpdate) -> Unit, modifier: Modifier = Modifier) {
    var sizeText by remember { mutableStateOf("48") }
    var outlineText by remember { mutableStateOf("2") }
    val colors = listOf("#FFFFFF", "#000000", "#FFD54F", "#FF5252", "#40C4FF")
    Column(modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp))
        .border(1.dp, NcColors.line, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("文字样式", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("字号", color = NcColors.secondary, fontSize = 10.sp)
            BasicTextField(value = sizeText, onValueChange = { if (it.length <= 3 && it.all(Char::isDigit)) sizeText = it },
                modifier = Modifier.width(64.dp).background(NcColors.stage, RoundedCornerShape(6.dp)).padding(7.dp),
                singleLine = true, textStyle = TextStyle(color = NcColors.text, fontSize = 11.sp, textAlign = TextAlign.End),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
            Text("描边", color = NcColors.secondary, fontSize = 10.sp)
            BasicTextField(value = outlineText, onValueChange = { if (it.length <= 2 && it.all(Char::isDigit)) outlineText = it },
                modifier = Modifier.width(52.dp).background(NcColors.stage, RoundedCornerShape(6.dp)).padding(7.dp),
                singleLine = true, textStyle = TextStyle(color = NcColors.text, fontSize = 11.sp, textAlign = TextAlign.End),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
            Box(modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(Color(0xFF303235))
                .clickable { sizeText.toDoubleOrNull()?.let { onChange(NativeEditorCommandAdapter.TextStyleUpdate(fontSize = it)) } }
                .padding(horizontal = 8.dp, vertical = 7.dp)) { Text("设置字号", color = NcColors.text, fontSize = 9.sp) }
            Box(modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(Color(0xFF303235))
                .clickable { outlineText.toDoubleOrNull()?.let { onChange(NativeEditorCommandAdapter.TextStyleUpdate(outlineWidth = it)) } }
                .padding(horizontal = 8.dp, vertical = 7.dp)) { Text("设置描边", color = NcColors.text, fontSize = 9.sp) }
        }
        Text("文字颜色", color = NcColors.secondary, fontSize = 10.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            colors.forEach { hex ->
                Box(modifier = Modifier.size(27.dp).clip(CircleShape).background(runCatching { Color(android.graphics.Color.parseColor(hex)) }.getOrDefault(Color.White))
                    .border(1.dp, NcColors.line, CircleShape).clickable { onChange(NativeEditorCommandAdapter.TextStyleUpdate(color = hex)) })
            }
        }
        Text("对齐方式", color = NcColors.secondary, fontSize = 10.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(7.dp)) {
            listOf("左对齐" to 0, "居中" to 1, "右对齐" to 2).forEach { (label, value) ->
                Box(modifier = Modifier.clip(RoundedCornerShape(7.dp)).background(NcColors.stage)
                    .clickable { onChange(NativeEditorCommandAdapter.TextStyleUpdate(alignment = value)) }
                    .padding(horizontal = 12.dp, vertical = 7.dp)) { Text(label, color = NcColors.text, fontSize = 10.sp) }
            }
        }
        Text("字号 8–256 点 · 描边 0–32 点；每次只提交所选字段，其余样式保持不变", color = NcColors.secondary, fontSize = 9.sp)
    }
}

@Composable
private fun NativeTextCreationPanel(playheadUs: Long, onCreate: (String) -> Unit, modifier: Modifier = Modifier) {
    var content by remember { mutableStateOf("") }
    Column(modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp))
        .border(1.dp, NcColors.line, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("添加文本", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        BasicTextField(value = content,
            onValueChange = { if (it.toByteArray(Charsets.UTF_8).size <= 16 * 1024) content = it },
            modifier = Modifier.fillMaxWidth().background(NcColors.stage, RoundedCornerShape(8.dp))
                .padding(horizontal = 10.dp, vertical = 9.dp),
            minLines = 2, textStyle = TextStyle(color = NcColors.text, fontSize = 12.sp),
            decorationBox = { inner -> Box {
                if (content.isEmpty()) Text("输入文字内容，支持换行…", color = NcColors.secondary, fontSize = 11.sp)
                inner()
            } })
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically) {
            Text("插入位置：" + String.format(Locale.ROOT, "%.2f", playheadUs.coerceAtLeast(0L) / 1_000_000.0) + "s · 默认白色居中 · 3 秒",
                color = NcColors.secondary, fontSize = 9.sp)
            Box(modifier = Modifier.clip(RoundedCornerShape(8.dp))
                .background(if (content.isNotBlank()) Color(0xFF303235) else NcColors.stage)
                .clickable(enabled = content.isNotBlank()) { onCreate(content.trim()) }
                .padding(horizontal = 14.dp, vertical = 8.dp)) {
                Text("创建文字片段", color = if (content.isNotBlank()) NcColors.text else NcColors.secondary, fontSize = 10.sp)
            }
        }
    }
}

@Composable
private fun NativeClipTransitionPanel(
    transitions: List<NativeEditorCommandAdapter.TransitionState>,
    types: List<Pair<Int, String>>,
    trackIndex: Int,
    fromClipId: Long,
    toClipId: Long?,
    onAdd: ((Int, Long, Long, Int, Long) -> Unit)?,
    onRemove: ((Int, Long) -> Unit)?,
    modifier: Modifier = Modifier
) {
    var selectedType by remember(types) { mutableStateOf(types.firstOrNull()?.first ?: 0) }
    var durationText by remember { mutableStateOf("500000") }
    val focusManager = LocalFocusManager.current
    val current = transitions.filter { it.fromClipId == fromClipId || it.toClipId == fromClipId }
    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("转场", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        if (types.isEmpty()) {
            Text("正在等待宿主读取引擎转场类型库…", color = NcColors.secondary, fontSize = 9.sp)
        } else {
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                types.forEach { (type, name) ->
                    val selected = selectedType == type
                    Text(
                        name,
                        color = if (selected) NcColors.background else NcColors.text,
                        fontSize = 9.sp,
                        modifier = Modifier.clip(RoundedCornerShape(7.dp))
                            .background(if (selected) NcColors.text else NcColors.stage)
                            .clickable { selectedType = type }
                            .padding(horizontal = 9.dp, vertical = 7.dp)
                    )
                }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text("时长（微秒）", color = NcColors.text, fontSize = 9.sp, modifier = Modifier.weight(1f))
                BasicTextField(
                    value = durationText,
                    onValueChange = { input ->
                        if (input.length <= 12 && input.all(Char::isDigit)) durationText = input
                    },
                    modifier = Modifier.width(100.dp).background(NcColors.stage, RoundedCornerShape(6.dp))
                        .padding(horizontal = 7.dp, vertical = 6.dp),
                    singleLine = true,
                    textStyle = TextStyle(color = NcColors.text, fontSize = 10.sp, textAlign = TextAlign.End),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number, imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() })
                )
            }
            val duration = durationText.toLongOrNull()
            val canAdd = toClipId != null && duration != null && duration > 0L && onAdd != null
            Text(
                if (toClipId == null) "当前片段后没有可连接的片段" else "添加到当前片段与后续片段之间",
                color = NcColors.secondary,
                fontSize = 8.sp
            )
            Text(
                "添加转场",
                color = if (canAdd) NcColors.text else NcColors.secondary,
                fontSize = 10.sp,
                modifier = Modifier.clip(RoundedCornerShape(7.dp))
                    .background(NcColors.stage)
                    .clickable(enabled = canAdd) {
                        onAdd?.invoke(trackIndex, fromClipId, toClipId!!, selectedType, duration!!)
                    }
                    .padding(horizontal = 12.dp, vertical = 8.dp)
            )
        }
        if (current.isEmpty()) {
            Text("当前片段没有关联转场", color = NcColors.secondary, fontSize = 9.sp)
        } else {
            current.forEach { transition ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        "${transition.name} · ${transition.durationUs} μs",
                        color = NcColors.text,
                        fontSize = 9.sp,
                        modifier = Modifier.weight(1f)
                    )
                    Text(
                        "删除",
                        color = NcColors.secondary,
                        fontSize = 9.sp,
                        modifier = Modifier.clickable(enabled = onRemove != null) {
                            onRemove?.invoke(trackIndex, transition.id)
                        }.padding(horizontal = 8.dp, vertical = 5.dp)
                    )
                }
            }
        }
    }
}

private data class NativeMaskShapeOption(val shape: Int, val name: String)

@Composable
private fun NativeClipMaskPanel(
    state: NativeEditorCommandAdapter.ClipMaskState,
    onChange: (NativeEditorCommandAdapter.ClipMaskState) -> Unit,
    modifier: Modifier = Modifier
) {
    val shapes = listOf(
        NativeMaskShapeOption(0, "关闭"),
        NativeMaskShapeOption(1, "线性"),
        NativeMaskShapeOption(2, "镜面"),
        NativeMaskShapeOption(3, "圆形"),
        NativeMaskShapeOption(4, "矩形"),
        NativeMaskShapeOption(5, "星形"),
        NativeMaskShapeOption(6, "爱心")
    )
    var draft by remember(state) { mutableStateOf(state) }
    var rotationText by remember(state) { mutableStateOf(String.format(Locale.US, "%.2f", state.rotationDegrees)) }
    var widthText by remember(state) { mutableStateOf(String.format(Locale.US, "%.4f", state.width)) }
    var heightText by remember(state) { mutableStateOf(String.format(Locale.US, "%.4f", state.height)) }
    var radiusText by remember(state) { mutableStateOf(String.format(Locale.US, "%.4f", state.cornerRadius)) }
    var featherText by remember(state) { mutableStateOf(String.format(Locale.US, "%.4f", state.feather)) }

    fun commitNumeric() {
        val rotation = rotationText.toDoubleOrNull()
        val width = widthText.toDoubleOrNull()
        val height = heightText.toDoubleOrNull()
        val radius = radiusText.toDoubleOrNull()
        val feather = featherText.toDoubleOrNull()
        if (rotation == null || width == null || height == null || radius == null || feather == null ||
            !rotation.isFinite() || !width.isFinite() || !height.isFinite() ||
            !radius.isFinite() || !feather.isFinite() ||
            radius !in 0.0..0.5 || feather !in 0.0..0.5
        ) {
            rotationText = String.format(Locale.US, "%.2f", draft.rotationDegrees)
            widthText = String.format(Locale.US, "%.4f", draft.width)
            heightText = String.format(Locale.US, "%.4f", draft.height)
            radiusText = String.format(Locale.US, "%.4f", draft.cornerRadius)
            featherText = String.format(Locale.US, "%.4f", draft.feather)
            return
        }
        draft = draft.copy(
            rotationDegrees = rotation,
            width = width,
            height = height,
            cornerRadius = radius,
            feather = feather
        )
        onChange(draft)
    }

    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("蒙版", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            shapes.forEach { option ->
                val selected = draft.shape == option.shape
                Text(
                    option.name,
                    color = if (selected) NcColors.background else NcColors.text,
                    fontSize = 9.sp,
                    modifier = Modifier.clip(RoundedCornerShape(8.dp))
                        .background(if (selected) NcColors.text else NcColors.stage)
                        .clickable {
                            draft = draft.copy(shape = option.shape, enabled = option.shape != 0)
                            onChange(draft)
                        }
                        .padding(horizontal = 10.dp, vertical = 8.dp)
                )
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("反转蒙版", color = NcColors.text, fontSize = 10.sp, modifier = Modifier.weight(1f))
            Text(
                if (draft.invert) "已开启" else "已关闭",
                color = NcColors.secondary,
                fontSize = 10.sp,
                modifier = Modifier.clickable {
                    draft = draft.copy(invert = !draft.invert)
                    onChange(draft)
                }.padding(horizontal = 8.dp, vertical = 5.dp)
            )
        }
        NativeMaskSlider("中心 X", draft.centerX.toFloat(), 0f..1f) {
            draft = draft.copy(centerX = it.toDouble())
            onChange(draft)
        }
        NativeMaskSlider("中心 Y", draft.centerY.toFloat(), 0f..1f) {
            draft = draft.copy(centerY = it.toDouble())
            onChange(draft)
        }
        NativeMaskSlider("羽化", draft.feather.toFloat(), 0f..0.5f, "0–0.5") {
            draft = draft.copy(feather = it.toDouble())
            featherText = String.format(Locale.US, "%.4f", it)
            onChange(draft)
        }
        NativeMaskSlider("圆角", draft.cornerRadius.toFloat(), 0f..0.5f, "0–0.5") {
            draft = draft.copy(cornerRadius = it.toDouble())
            radiusText = String.format(Locale.US, "%.4f", it)
            onChange(draft)
        }
        Text("旋转 / 宽度 / 高度可直接输入；尺寸按当前蒙版形状解释。", color = NcColors.secondary, fontSize = 8.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            NativeMaskNumberField("旋转°", rotationText, { rotationText = it }, ::commitNumeric, Modifier.weight(1f))
            NativeMaskNumberField("宽度", widthText, { widthText = it }, ::commitNumeric, Modifier.weight(1f))
            NativeMaskNumberField("高度", heightText, { heightText = it }, ::commitNumeric, Modifier.weight(1f))
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            NativeMaskNumberField("羽化", featherText, { featherText = it }, ::commitNumeric, Modifier.weight(1f))
            NativeMaskNumberField("圆角", radiusText, { radiusText = it }, ::commitNumeric, Modifier.weight(1f))
        }
    }
}

@Composable
private fun NativeMaskSlider(
    label: String,
    value: Float,
    range: ClosedFloatingPointRange<Float>,
    rangeHint: String? = null,
    onCommit: (Float) -> Unit
) {
    var draft by remember(label, value) { mutableStateOf(value.coerceIn(range)) }
    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, color = NcColors.text, fontSize = 9.sp, modifier = Modifier.weight(1f))
            Text(String.format(Locale.US, "%.3f", draft), color = NcColors.secondary, fontSize = 9.sp)
            if (rangeHint != null) Text("  $rangeHint", color = NcColors.secondary, fontSize = 8.sp)
        }
        Slider(
            value = draft,
            onValueChange = { draft = it },
            onValueChangeFinished = { onCommit(draft) },
            valueRange = range
        )
    }
}

@Composable
private fun NativeMaskNumberField(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
    onCommit: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier, verticalArrangement = Arrangement.spacedBy(3.dp)) {
        Text(label, color = NcColors.secondary, fontSize = 8.sp)
        BasicTextField(
            value = value,
            onValueChange = { input ->
                if (input.length <= 16 && input.all { it.isDigit() || it == '.' || it == '-' }) onValueChange(input)
            },
            modifier = Modifier.fillMaxWidth().background(NcColors.stage, RoundedCornerShape(6.dp))
                .padding(horizontal = 6.dp, vertical = 7.dp)
                .onFocusChanged { if (!it.isFocused) onCommit() },
            singleLine = true,
            textStyle = TextStyle(color = NcColors.text, fontSize = 9.sp, textAlign = TextAlign.End),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { onCommit() })
        )
    }
}

private data class NativeFilterOption(val style: Int, val name: String)

@Composable
private fun NativeClipFilterPanel(
    state: NativeEditorCommandAdapter.ClipFilterState,
    onChange: (Int, Double) -> Unit,
    modifier: Modifier = Modifier
) {
    val options = listOf(
        NativeFilterOption(0, "原片"), NativeFilterOption(1, "电影"),
        NativeFilterOption(2, "复古"), NativeFilterOption(3, "清新"),
        NativeFilterOption(4, "黑白"), NativeFilterOption(5, "赛博"),
        NativeFilterOption(6, "日系"), NativeFilterOption(7, "胶片"),
        NativeFilterOption(8, "暖阳"), NativeFilterOption(9, "冷调"),
        NativeFilterOption(10, "港风"), NativeFilterOption(11, "高级灰"),
        NativeFilterOption(12, "奶油"), NativeFilterOption(13, "负片")
    )
    var intensity by remember(state.style, state.intensity) {
        mutableStateOf(state.intensity.toFloat())
    }
    var intensityText by remember(state.style, state.intensity) {
        mutableStateOf(String.format(Locale.US, "%.0f", state.intensity))
    }
    val focusManager = LocalFocusManager.current
    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("滤镜", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            options.forEach { option ->
                val selected = state.style == option.style
                Text(
                    option.name,
                    color = if (selected) NcColors.background else NcColors.text,
                    fontSize = 9.sp,
                    modifier = Modifier.clip(RoundedCornerShape(8.dp))
                        .background(if (selected) NcColors.text else NcColors.stage)
                        .clickable { onChange(option.style, intensity.toDouble()) }
                        .padding(horizontal = 10.dp, vertical = 8.dp)
                )
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("强度", color = NcColors.text, fontSize = 10.sp, modifier = Modifier.weight(1f))
            BasicTextField(
                value = intensityText,
                onValueChange = { input ->
                    if (input.length <= 6 && input.all { it.isDigit() || it == '.' }) intensityText = input
                },
                modifier = Modifier.width(48.dp).onFocusChanged { focus ->
                    if (!focus.isFocused) {
                        val parsed = intensityText.toDoubleOrNull()
                        if (parsed != null && parsed.isFinite() && parsed in 0.0..100.0) {
                            intensity = parsed.toFloat()
                            onChange(state.style, parsed)
                        } else {
                            intensityText = String.format(Locale.US, "%.0f", intensity)
                        }
                    }
                },
                singleLine = true,
                enabled = state.style != 0,
                textStyle = TextStyle(color = NcColors.text, fontSize = 10.sp, textAlign = TextAlign.End),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() })
            )
            Text("%", color = NcColors.secondary, fontSize = 10.sp)
        }
        Slider(
            value = intensity.coerceIn(0f, 100f),
            onValueChange = {
                intensity = it
                intensityText = String.format(Locale.US, "%.0f", it)
            },
            onValueChangeFinished = { onChange(state.style, intensity.toDouble()) },
            valueRange = 0f..100f,
            enabled = state.style != 0
        )
        Text("滤镜强度范围 0–100；选择原片将关闭滤镜。", color = NcColors.secondary, fontSize = 8.sp)
    }
}

@Composable
private fun NativeClipLutPanel(
    state: NativeEditorCommandAdapter.ClipLutState,
    onChange: (String, Double) -> Unit,
    onValidate: ((String) -> NativeEditorCommandAdapter.Result<NativeEditorCommandAdapter.LutValidationState>)?,
    modifier: Modifier = Modifier
) {
    var path by remember(state.path) { mutableStateOf(state.path) }
    var strength by remember(state.path, state.strength) { mutableStateOf(state.strength.toFloat()) }
    var strengthText by remember(state.path, state.strength) {
        mutableStateOf(String.format(Locale.US, "%.2f", state.strength))
    }
    val focusManager = LocalFocusManager.current
    val context = LocalContext.current
    val importScope = rememberCoroutineScope()
    var importStatus by remember { mutableStateOf<String?>(null) }
    var validationText by remember(path) { mutableStateOf<String?>(null) }
    fun applyValidatedLut() {
        if (path.isBlank()) {
            onChange("", strength.toDouble())
            validationText = "已请求清除 LUT"
            return
        }
        val result = onValidate?.invoke(path)
        val info = result?.value
        if (result?.ok == true && info != null) {
            onChange(path, strength.toDouble())
            validationText = "校验通过，已应用：${info.size}³" +
                if (info.title.isBlank()) "" else " · ${info.title}"
        } else {
            validationText = result?.error ?: "尚未接入 LUT 文件校验，已阻止应用"
        }
    }
    val lutDocumentPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument()
    ) { uri ->
        if (uri != null) {
            importScope.launch {
                importStatus = "正在导入 LUT 文件…"
                try {
                    val importedPath = NativeLutFileImporter.import(context, uri)
                    path = importedPath
                    importStatus = "文件已复制到应用私有目录"
                    applyValidatedLut()
                } catch (error: Exception) {
                    importStatus = error.message ?: "LUT 文件导入失败"
                }
            }
        }
    }
    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("LUT 调色", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Text("导入 .cube 后复制到应用私有目录；工程引用私有副本，不依赖原文件 URI。", color = NcColors.secondary, fontSize = 9.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text(
                "选择 LUT 文件",
                color = NcColors.text,
                fontSize = 9.sp,
                modifier = Modifier.clip(RoundedCornerShape(7.dp)).background(NcColors.stage)
                    .clickable { lutDocumentPicker.launch(arrayOf("*/*")) }
                    .padding(horizontal = 10.dp, vertical = 8.dp)
            )
            Text("仅接受 .cube 3D LUT", color = NcColors.secondary, fontSize = 8.sp)
        }
        importStatus?.let { Text(it, color = NcColors.secondary, fontSize = 9.sp) }
        BasicTextField(
            value = path,
            onValueChange = { path = it.take(2048) },
            modifier = Modifier.fillMaxWidth().background(NcColors.stage, RoundedCornerShape(7.dp)).padding(9.dp),
            singleLine = true,
            textStyle = TextStyle(color = NcColors.text, fontSize = 10.sp),
            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Ascii, imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() }),
            decorationBox = { inner ->
                Box {
                    if (path.isEmpty()) Text("输入 .cube 文件完整路径", color = NcColors.secondary, fontSize = 10.sp)
                    inner()
                }
            }
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("混合强度", color = NcColors.text, fontSize = 10.sp, modifier = Modifier.weight(1f))
            BasicTextField(
                value = strengthText,
                onValueChange = { input ->
                    if (input.length <= 5 && input.all { it.isDigit() || it == '.' }) strengthText = input
                },
                modifier = Modifier.width(56.dp).onFocusChanged { focus ->
                    if (!focus.isFocused) {
                        val parsed = strengthText.toDoubleOrNull()
                        if (parsed != null && parsed.isFinite() && parsed in 0.0..1.0) {
                            strength = parsed.toFloat()
                            applyValidatedLut()
                        } else {
                            strengthText = String.format(Locale.US, "%.2f", strength)
                        }
                    }
                },
                singleLine = true,
                textStyle = TextStyle(color = NcColors.text, fontSize = 10.sp, textAlign = TextAlign.End),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() })
            )
            Text(" / 1.00", color = NcColors.secondary, fontSize = 9.sp)
        }
        Slider(
            value = strength.coerceIn(0f, 1f),
            onValueChange = {
                strength = it
                strengthText = String.format(Locale.US, "%.2f", it)
            },
            onValueChangeFinished = { applyValidatedLut() },
            valueRange = 0f..1f
        )
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(
                "校验 .cube",
                color = if (onValidate != null) NcColors.text else NcColors.secondary,
                fontSize = 9.sp,
                modifier = Modifier.clip(RoundedCornerShape(7.dp)).background(NcColors.stage)
                    .clickable {
                        val result = onValidate?.invoke(path)
                        validationText = when {
                            result == null -> "等待宿主接入 LUT 文件校验"
                            !result.ok -> result.error ?: "LUT 文件校验失败"
                            else -> {
                                val info = result.value
                                if (info == null) "引擎未返回 LUT 校验结果"
                                else "校验通过：${info.size}³" + if (info.title.isBlank()) "" else " · ${info.title}"
                            }
                        }
                    }
                    .padding(horizontal = 10.dp, vertical = 8.dp)
            )
            Text(
                "应用路径与强度",
                color = NcColors.text,
                fontSize = 9.sp,
                modifier = Modifier.clip(RoundedCornerShape(7.dp)).background(NcColors.stage)
                    .clickable { focusManager.clearFocus(); applyValidatedLut() }
                    .padding(horizontal = 10.dp, vertical = 8.dp)
            )
            Text(
                "清除 LUT",
                color = NcColors.text,
                fontSize = 9.sp,
                modifier = Modifier.clip(RoundedCornerShape(7.dp)).background(NcColors.stage)
                    .clickable { path = ""; applyValidatedLut() }
                    .padding(horizontal = 10.dp, vertical = 8.dp)
            )
        }
        validationText?.let { Text(it, color = NcColors.secondary, fontSize = 9.sp) }
        Text("导入副本保存在应用私有目录并供工程持续引用；引擎仍负责校验 .cube 3D LUT（边长 2–64）。", color = NcColors.secondary, fontSize = 8.sp)
    }
}

@Composable
private fun NativeClipColorCurvePanel(
    state: NativeEditorCommandAdapter.ClipColorGradeState,
    onChange: (NativeEditorCommandAdapter.ClipColorGradeState) -> Unit,
    modifier: Modifier = Modifier
) {
    val channels = listOf("master" to "主曲线", "red" to "红", "green" to "绿", "blue" to "蓝")
    var channel by remember { mutableStateOf("master") }
    val sourceCurve = when (channel) {
        "red" -> state.red
        "green" -> state.green
        "blue" -> state.blue
        else -> state.master
    }
    var points by remember(channel, sourceCurve) { mutableStateOf(sourceCurve.points) }
    var selectedPoint by remember(channel) { mutableStateOf(1) }
    val latestPoints by rememberUpdatedState(points)
    val latestSelectedPoint by rememberUpdatedState(selectedPoint)
    val focusManager = LocalFocusManager.current
    var inputX by remember(channel, selectedPoint, points) {
        mutableStateOf(points.getOrNull(selectedPoint)?.first?.let { String.format(Locale.US, "%.3f", it) } ?: "0.500")
    }
    var outputY by remember(channel, selectedPoint, points) {
        mutableStateOf(points.getOrNull(selectedPoint)?.second?.let { String.format(Locale.US, "%.3f", it) } ?: "0.500")
    }

    fun publish(next: List<Pair<Double, Double>>) {
        points = next
        val curve = NativeEditorCommandAdapter.ColorCurveState(next)
        val grade = when (channel) {
            "red" -> state.copy(red = curve)
            "green" -> state.copy(green = curve)
            "blue" -> state.copy(blue = curve)
            else -> state.copy(master = curve)
        }
        if (grade.isValid()) onChange(grade)
    }

    fun commitNumericPoint() {
        val old = points.getOrNull(selectedPoint) ?: return
        val x = inputX.toDoubleOrNull()
        val y = outputY.toDoubleOrNull()
        if (x == null || y == null || !x.isFinite() || !y.isFinite() ||
            y !in 0.0..1.0 || selectedPoint <= 0 || selectedPoint >= points.lastIndex) {
            inputX = String.format(Locale.US, "%.3f", old.first)
            outputY = String.format(Locale.US, "%.3f", old.second)
            return
        }
        val minX = points[selectedPoint - 1].first + 0.001
        val maxX = points[selectedPoint + 1].first - 0.001
        if (minX > maxX || x !in minX..maxX) {
            inputX = String.format(Locale.US, "%.3f", old.first)
            outputY = String.format(Locale.US, "%.3f", old.second)
            return
        }
        val updated = points.toMutableList().also { it[selectedPoint] = x to y }
        publish(updated)
    }

    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("RGB 曲线调色", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Text("主曲线先处理整体亮度，再依次处理红、绿、蓝通道；曲线变化直接进入核心预览与导出。", color = NcColors.secondary, fontSize = 9.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            channels.forEach { (id, label) ->
                Text(
                    label,
                    color = if (channel == id) NcColors.text else NcColors.secondary,
                    fontSize = 9.sp,
                    modifier = Modifier.clip(RoundedCornerShape(7.dp))
                        .background(if (channel == id) NcColors.stage else NcColors.background)
                        .clickable { channel = id; selectedPoint = 1 }
                        .padding(horizontal = 12.dp, vertical = 7.dp)
                )
            }
        }
        Canvas(
            modifier = Modifier.fillMaxWidth().height(174.dp)
                .clip(RoundedCornerShape(8.dp)).background(NcColors.stage)
                .pointerInput(channel) {
                    detectDragGestures(
                        onDragStart = { position ->
                            val currentPoints = latestPoints
                            val width = size.width.coerceAtLeast(1)
                            val height = size.height.coerceAtLeast(1)
                            selectedPoint = currentPoints.indices.minByOrNull { index ->
                                val px = currentPoints[index].first * width
                                val py = (1.0 - currentPoints[index].second) * height
                                val dx = px - position.x
                                val dy = py - position.y
                                dx * dx + dy * dy
                            } ?: 0
                        },
                        onDragEnd = { publish(latestPoints) },
                        onDrag = { change, _ ->
                            val currentPoints = latestPoints
                            val pointIndex = latestSelectedPoint
                            if (pointIndex in 1 until currentPoints.lastIndex) {
                                val width = size.width.coerceAtLeast(1)
                                val height = size.height.coerceAtLeast(1)
                                val minX = currentPoints[pointIndex - 1].first + 0.001
                                val maxX = currentPoints[pointIndex + 1].first - 0.001
                                if (minX <= maxX) {
                                    val x = (change.position.x / width).toDouble().coerceIn(minX, maxX)
                                    val y = (1.0 - change.position.y / height).coerceIn(0.0, 1.0)
                                    points = currentPoints.toMutableList().also { it[pointIndex] = x to y }
                                    inputX = String.format(Locale.US, "%.3f", x)
                                    outputY = String.format(Locale.US, "%.3f", y)
                                }
                            }
                            change.consume()
                        }
                    )
                }
        ) {
            val w = size.width
            val h = size.height
            for (i in 0..4) {
                val x = w * i / 4f
                val y = h * i / 4f
                drawLine(NcColors.secondary.copy(alpha = 0.22f), Offset(x, 0f), Offset(x, h), 1f)
                drawLine(NcColors.secondary.copy(alpha = 0.22f), Offset(0f, y), Offset(w, y), 1f)
            }
            val curvePath = Path()
            points.forEachIndexed { index, point ->
                val x = (point.first * w).toFloat()
                val y = ((1.0 - point.second) * h).toFloat()
                if (index == 0) curvePath.moveTo(x, y) else curvePath.lineTo(x, y)
            }
            drawPath(curvePath, NcColors.text, style = Stroke(width = 2.5f))
            points.forEachIndexed { index, point ->
                drawCircle(
                    color = if (index == selectedPoint) NcColors.text else NcColors.secondary,
                    radius = if (index == selectedPoint) 6f else 4f,
                    center = Offset((point.first * w).toFloat(), ((1.0 - point.second) * h).toFloat())
                )
            }
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("控制点 ${selectedPoint + 1}/${points.size}", color = NcColors.text, fontSize = 9.sp, modifier = Modifier.weight(1f))
            Text(
                "重置通道",
                color = NcColors.text,
                fontSize = 9.sp,
                modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(NcColors.stage)
                    .clickable {
                        selectedPoint = 1
                        publish(NativeEditorCommandAdapter.ColorCurveState.identity().points)
                    }.padding(horizontal = 9.dp, vertical = 6.dp)
            )
            Text(
                "添加点",
                color = if (points.size < 32) NcColors.text else NcColors.secondary,
                fontSize = 9.sp,
                modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(NcColors.stage)
                    .clickable {
                        if (points.size < 32) {
                            val gap = points.zipWithNext().mapIndexed { index, pair ->
                                Triple(index, pair.first.first, pair.second.first)
                            }.maxByOrNull { it.third - it.second }
                            if (gap != null) {
                                val x = (gap.second + gap.third) / 2.0
                                val y = (points[gap.first].second + points[gap.first + 1].second) / 2.0
                                val updated = points.toMutableList().apply { add(gap.first + 1, x to y) }
                                selectedPoint = gap.first + 1
                                publish(updated)
                            }
                        }
                    }.padding(horizontal = 9.dp, vertical = 6.dp)
            )
            Text(
                "删除点",
                color = if (selectedPoint in 1 until points.lastIndex) NcColors.text else NcColors.secondary,
                fontSize = 9.sp,
                modifier = Modifier.clip(RoundedCornerShape(6.dp)).background(NcColors.stage)
                    .clickable {
                        if (selectedPoint in 1 until points.lastIndex) {
                            val updated = points.toMutableList().also { it.removeAt(selectedPoint) }
                            selectedPoint = (selectedPoint - 1).coerceIn(1, updated.lastIndex - 1)
                            publish(updated)
                        }
                    }.padding(horizontal = 9.dp, vertical = 6.dp)
            )
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Text("输入 X", color = NcColors.secondary, fontSize = 9.sp)
            BasicTextField(
                value = inputX,
                onValueChange = { inputX = it.take(8) },
                modifier = Modifier.width(62.dp).background(NcColors.stage, RoundedCornerShape(5.dp))
                    .padding(6.dp).onFocusChanged { if (!it.isFocused) commitNumericPoint() },
                singleLine = true,
                textStyle = TextStyle(color = NcColors.text, fontSize = 10.sp, textAlign = TextAlign.End),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() })
            )
            Text("输出 Y", color = NcColors.secondary, fontSize = 9.sp)
            BasicTextField(
                value = outputY,
                onValueChange = { outputY = it.take(8) },
                modifier = Modifier.width(62.dp).background(NcColors.stage, RoundedCornerShape(5.dp))
                    .padding(6.dp).onFocusChanged { if (!it.isFocused) commitNumericPoint() },
                singleLine = true,
                textStyle = TextStyle(color = NcColors.text, fontSize = 10.sp, textAlign = TextAlign.End),
                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                keyboardActions = KeyboardActions(onDone = { focusManager.clearFocus() })
            )
        }
        Text("首尾端点固定为 (0,0)/(1,1)，内部点 X 必须严格递增；每条曲线最多 32 个点。", color = NcColors.secondary, fontSize = 8.sp)
    }
}

private data class NativeEffectOption(val type: Int, val name: String)

@Composable
private fun NativeClipEffectsPanel(
    effects: List<NativeClipEffect>,
    specs: Map<Int, NativeEditorCommandAdapter.EffectSpec>,
    onAdd: (Int) -> Unit,
    onUpdate: (NativeClipEffect, Double) -> Unit,
    onEnabled: (Long, Boolean) -> Unit,
    onMove: (Long, Int) -> Unit,
    onRemove: (Long) -> Unit,
    modifier: Modifier = Modifier,
    panelTitle: String = "视频特效 · 效果栈",
    allowStackOrdering: Boolean = true,
    options: List<NativeEffectOption> = listOf(
        NativeEffectOption(13, "故障"), NativeEffectOption(14, "分屏"),
        NativeEffectOption(15, "镜像"), NativeEffectOption(16, "旋转模糊"),
        NativeEffectOption(17, "扫描线"), NativeEffectOption(18, "RGB分离"),
        NativeEffectOption(19, "雪花"), NativeEffectOption(20, "下雨"),
        NativeEffectOption(21, "光斑"), NativeEffectOption(22, "老电影"),
        NativeEffectOption(23, "星光"), NativeEffectOption(24, "缩放冲击"),
        NativeEffectOption(25, "甩镜"), NativeEffectOption(26, "变速闪烁"),
        NativeEffectOption(27, "节奏律动"), NativeEffectOption(28, "运动模糊"),
        NativeEffectOption(29, "闪光"), NativeEffectOption(30, "马赛克"),
        NativeEffectOption(31, "抖动"), NativeEffectOption(32, "放大"),
        NativeEffectOption(33, "边缘检测"), NativeEffectOption(34, "瘦身"),
        NativeEffectOption(35, "发光轮廓"), NativeEffectOption(36, "震动"),
        NativeEffectOption(37, "泛光"), NativeEffectOption(38, "镜头光晕")
    )
) {
    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp)).padding(10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text(panelTitle, color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        if (effects.isEmpty()) Text("当前片段尚未添加特效", color = NcColors.secondary, fontSize = 10.sp)
        effects.forEach { effect ->
            var draftValue by remember(effect.id, effect.value) {
                mutableStateOf(effect.value.toFloat())
            }
            Column(
                modifier = Modifier.fillMaxWidth().background(NcColors.stage, RoundedCornerShape(8.dp)).padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("效果 " + effect.type, color = NcColors.text, fontSize = 10.sp, modifier = Modifier.weight(1f))
                    Text(if (effect.enabled) "关闭" else "启用", color = NcColors.secondary, fontSize = 9.sp,
                        modifier = Modifier.clickable { onEnabled(effect.id, !effect.enabled) }.padding(horizontal = 5.dp))
                    if (allowStackOrdering) {
                        Text("↑", color = NcColors.text, modifier = Modifier.clickable { onMove(effect.id, -1) }.padding(horizontal = 5.dp))
                        Text("↓", color = NcColors.text, modifier = Modifier.clickable { onMove(effect.id, 1) }.padding(horizontal = 5.dp))
                    }
                    Text("删除", color = NcColors.secondary, modifier = Modifier.clickable { onRemove(effect.id) }.padding(start = 5.dp))
                }
                val spec = specs[effect.type]
                if (spec == null) {
                    Text("正在读取引擎参数规格…", color = NcColors.secondary, fontSize = 9.sp)
                } else {
                    Text(spec.name + ": " + String.format(Locale.US, "%.2f %s", draftValue, spec.unit),
                        color = NcColors.text, fontSize = 9.sp)
                    if (spec.min < spec.max) {
                        Slider(
                            value = draftValue.coerceIn(spec.min.toFloat(), spec.max.toFloat()),
                            onValueChange = { draftValue = it },
                            onValueChangeFinished = { onUpdate(effect, draftValue.toDouble()) },
                            valueRange = spec.min.toFloat()..spec.max.toFloat(),
                            enabled = effect.enabled
                        )
                    }
                    if (spec.hint.isNotBlank()) Text(spec.hint, color = NcColors.secondary, fontSize = 8.sp)
                }
            }
        }
        Text("添加效果", color = NcColors.text, fontSize = 10.sp, fontWeight = FontWeight.Medium)
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            options.forEach { option ->
                Text(
                    option.name,
                    color = NcColors.text,
                    fontSize = 9.sp,
                    modifier = Modifier.clip(RoundedCornerShape(8.dp)).background(NcColors.stage)
                        .clickable { onAdd(option.type) }.padding(horizontal = 9.dp, vertical = 7.dp)
                )
            }
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
private fun EditorTopBar(
    onShare: () -> Unit,
    onExport: () -> Unit
) {
    // Match the reference image: share action and export capsule are grouped at the right.
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(NativeEditorLayoutSpec.TOP_BAR_HEIGHT_DP.dp)
            .background(NcColors.topBar)
            .padding(horizontal = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Spacer(Modifier.weight(1f))
        Box(
            modifier = Modifier
                .size(40.dp)
                .clip(CircleShape)
                .clickable(onClick = onShare),
            contentAlignment = Alignment.Center
        ) {
            Image(
                painter = painterResource(R.drawable.ic_editor_share),
                contentDescription = "分享",
                modifier = Modifier.size(18.dp),
                colorFilter = ColorFilter.tint(NcColors.text)
            )
        }
        Spacer(Modifier.width(4.dp))
        Text(
            text = "导出",
            color = NcColors.text,
            fontSize = 13.sp,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier
                .clip(RoundedCornerShape(20.dp))
                .border(1.dp, NcColors.text, RoundedCornerShape(20.dp))
                .clickable(onClick = onExport)
                .padding(horizontal = 24.dp, vertical = 8.dp)
        )
    }
}

@Composable
private fun PreviewStage(
    previewView: View?,
    showGuides: Boolean,
    onExpandPreview: () -> Unit,
    modifier: Modifier = Modifier
) {
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
            val previewSize = NativeEditorLayoutSpec.previewSize(maxWidth.value, maxHeight.value)
            Box(
                modifier = Modifier
                    .width(previewSize.widthDp.dp)
                    .height(previewSize.heightDp.dp)
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
                if (showGuides) {
                    Canvas(modifier = Modifier.fillMaxSize()) {
                        val stroke = 1.dp.toPx()
                        val guide = Color.White.copy(alpha = 0.48f)
                        val thirdX1 = size.width / 3f
                        val thirdX2 = size.width * 2f / 3f
                        val thirdY1 = size.height / 3f
                        val thirdY2 = size.height * 2f / 3f
                        drawLine(guide, Offset(thirdX1, 0f), Offset(thirdX1, size.height), stroke)
                        drawLine(guide, Offset(thirdX2, 0f), Offset(thirdX2, size.height), stroke)
                        drawLine(guide, Offset(0f, thirdY1), Offset(size.width, thirdY1), stroke)
                        drawLine(guide, Offset(0f, thirdY2), Offset(size.width, thirdY2), stroke)
                        drawLine(guide.copy(alpha = 0.28f), Offset(size.width / 2f, 0f), Offset(size.width / 2f, size.height), stroke)
                        drawLine(guide.copy(alpha = 0.28f), Offset(0f, size.height / 2f), Offset(size.width, size.height / 2f), stroke)
                    }
                }
            }
        }
        Box(
            modifier = Modifier
                .align(Alignment.BottomEnd)
                .padding(end = 6.dp, bottom = 6.dp)
                .size(NativeEditorLayoutSpec.PREVIEW_EXPAND_DIAMETER_DP.dp)
                .clip(CircleShape)
                .background(Color(0xCC101112))
                .border(1.dp, NcColors.line, CircleShape)
                .clickable(onClick = onExpandPreview),
            contentAlignment = Alignment.Center
        ) {
            Image(
                painter = painterResource(R.drawable.ic_editor_fullscreen),
                contentDescription = "展开预览",
                modifier = Modifier.size(20.dp),
                colorFilter = ColorFilter.tint(NcColors.text)
            )
        }
    }
}

@Composable
private fun NativeMarkerListPanel(
    markers: List<NativeTimelineMarker>,
    onSelect: (NativeTimelineMarker) -> Unit,
    onRemove: ((Long) -> Unit)?,
    onDismiss: () -> Unit
) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(max = 168.dp)
            .background(NcColors.stage)
            .padding(horizontal = 12.dp, vertical = 8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "标记（${markers.size}）",
                color = NcColors.text,
                fontSize = 12.sp,
                fontWeight = FontWeight.Medium
            )
            Spacer(Modifier.weight(1f))
            Text(
                text = "关闭",
                color = NcColors.muted,
                fontSize = 11.sp,
                modifier = Modifier.clickable(onClick = onDismiss).padding(6.dp)
            )
        }
        if (markers.isEmpty()) {
            Text(
                text = "暂无标记",
                color = NcColors.muted,
                fontSize = 11.sp,
                modifier = Modifier.padding(vertical = 12.dp)
            )
        } else {
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f, fill = false)
                    .verticalScroll(rememberScrollState())
            ) {
                markers.sortedBy { it.timeUs }.forEach { marker ->
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .clickable { onSelect(marker) }
                            .padding(vertical = 7.dp, horizontal = 4.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text(
                            text = marker.name,
                            color = NcColors.text,
                            fontSize = 12.sp,
                            maxLines = 1
                        )
                        Spacer(Modifier.weight(1f))
                        Text(
                            text = String.format(Locale.ROOT, "%.2fs", marker.timeUs / 1_000_000.0),
                            color = NcColors.muted,
                            fontSize = 11.sp
                        )
                        if (onRemove != null) {
                            Text(
                                text = "删除",
                                color = NcColors.muted,
                                fontSize = 11.sp,
                                modifier = Modifier
                                    .clickable { onRemove(marker.id) }
                                    .padding(start = 10.dp, top = 6.dp, bottom = 6.dp)
                            )
                        }
                    }
                }
            }
        }
    }
}
 @Composable
private fun NativePreviewPrewarmPanel(
    running: Boolean,
    progress: Float,
    status: String?,
    stats: NativePreviewCacheUiStats?,
    onCancel: () -> Unit,
    onDismiss: () -> Unit
) {
    Column(
        modifier = Modifier.fillMaxWidth().background(NcColors.stage).padding(horizontal = 12.dp, vertical = 8.dp),
        verticalArrangement = Arrangement.spacedBy(5.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(status ?: "预览缓存", color = NcColors.text, fontSize = 10.sp, modifier = Modifier.weight(1f))
            Text(if (running) "取消" else "关闭", color = NcColors.secondary, fontSize = 10.sp,
                modifier = Modifier.clickable { if (running) onCancel() else onDismiss() }.padding(4.dp))
        }
        Box(Modifier.fillMaxWidth().height(3.dp).clip(RoundedCornerShape(2.dp)).background(NcColors.line)) {
            Box(Modifier.fillMaxWidth(progress.coerceIn(0f, 1f)).height(3.dp).background(NcColors.secondary))
        }
        stats?.let {
            Text("缓存 ${it.cachedFrames}/${it.capacity} · 命中 ${it.frameHits} · 未命中 ${it.frameMisses} · 解码器打开 ${it.readerOpens}",
                color = NcColors.muted, fontSize = 9.sp)
        }
    }
}

@Composable
private fun QuickActionStrip(
    canUndo: Boolean,
    canRedo: Boolean,
    onUndo: () -> Unit,
    onRedo: () -> Unit,
    onQuickAction: (NativeQuickAction) -> Unit
) {
    // Match the reference screenshot: eleven quick actions in their original order.
    // All eleven controls share the same 30dp circular visual component.
    // The typed callback lets a host implement each action without conflating semantics.
    val actions = listOf(
        Triple("i-plus", "添加素材", NativeQuickAction.IMPORT_MEDIA),
        Triple("i-home", "返回首页", NativeQuickAction.RETURN_HOME),
        Triple("i-prev", "向左逐段跳", NativeQuickAction.STEP_PREVIOUS),
        Triple("i-diamond", "标记点", NativeQuickAction.ADD_MARKER),
        Triple("渲染", "渲染预览缓存", NativeQuickAction.PREVIEW_RENDER),
        Triple("i-keyframes", "关键帧", NativeQuickAction.KEYFRAMES),
        Triple("i-hash", "构图参考线", NativeQuickAction.TOGGLE_GUIDES),
        Triple("i-image", "图片与背景", NativeQuickAction.IMAGES),
        Triple("AI", "AI 仿剪", NativeQuickAction.AI_EDIT)
    )
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(NativeEditorLayoutSpec.QUICK_ACTION_ROW_HEIGHT_DP.dp)
            .background(NcColors.stage)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 6.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(4.dp)
    ) {
        actions.forEachIndexed { index, action ->
            QuickActionButton(
                symbol = action.first.takeUnless { it.startsWith("i-") },
                iconId = action.first.takeIf { it.startsWith("i-") },
                label = action.second,
                emphasized = index == 8,
                outlined = action.third == NativeQuickAction.ADD_MARKER,
                pinkEmphasis = action.third == NativeQuickAction.IMAGES,
                accentColor = when (action.third) {
                    NativeQuickAction.ADD_MARKER -> Color(0xFFE34E55)
                    NativeQuickAction.KEYFRAMES -> Color(0xFFE5A33D)
                    NativeQuickAction.IMAGES -> Color.White
                    else -> null
                },
                renderIndicator = action.third == NativeQuickAction.PREVIEW_RENDER,
                onClick = { onQuickAction(action.third) },
                onDoubleClick = NativeQuickActionGestureContract.doubleTap(action.third)
                    ?.let { command -> { onQuickAction(command) } },
                onLongClick = NativeQuickActionGestureContract.longPress(action.third)
                    ?.let { command -> { onQuickAction(command) } }
            )
        }
        QuickActionButton(
            iconId = "i-undo",
            label = "撤销",
            enabled = canUndo,
            onClick = onUndo
        )
        QuickActionButton(
            iconId = "i-redo",
            label = "重做",
            enabled = canRedo,
            onClick = onRedo
        )
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun QuickActionButton(
    symbol: String? = null,
    iconId: String? = null,
    label: String,
    enabled: Boolean = true,
    emphasized: Boolean = false,
    outlined: Boolean = false,
    pinkEmphasis: Boolean = false,
    accentColor: Color? = null,
    renderIndicator: Boolean = false,
    onDoubleClick: (() -> Unit)? = null,
    onLongClick: (() -> Unit)? = null,
    onClick: () -> Unit
) {
    val fill = if (emphasized) Color(0xFF3976D8) else NcColors.lane
    val fillBrush = when {
        emphasized -> Brush.linearGradient(listOf(Color(0xFFB65BD8), Color(0xFF477FE5)))
        pinkEmphasis -> Brush.linearGradient(listOf(Color(0xFFE65FB8), Color(0xFFB94FD1)))
        else -> Brush.linearGradient(listOf(fill, fill))
    }
    val foreground = if (enabled) accentColor ?: NcColors.text else NcColors.muted
    val borderColor = when {
        outlined -> NcColors.text
        emphasized -> Color(0xFFB16AD9)
        pinkEmphasis -> Color(0xFFE16AC5)
        else -> NcColors.line
    }

    // Keep the screenshot-sized visual circle, but give every action a
    // consistent 48dp touch target so adjacent controls remain independently
    // accessible and usable on dense phone layouts.
    Box(
        modifier = Modifier
            .size(NativeEditorLayoutSpec.QUICK_ACTION_TOUCH_TARGET_DP.dp)
            .combinedClickable(
                enabled = enabled,
                onClickLabel = label,
                onLongClickLabel = if (onLongClick != null) "$label：长按" else null,
                onLongClick = onLongClick,
                onDoubleClick = onDoubleClick,
                onClick = onClick
            ),
        contentAlignment = Alignment.Center
    ) {
        Box(
            modifier = Modifier
                .size(NativeEditorLayoutSpec.QUICK_ACTION_DIAMETER_DP.dp)
                .clip(CircleShape)
                .background(
                    if (enabled) fillBrush else Brush.linearGradient(
                        listOf(NcColors.lane.copy(alpha = 0.45f), NcColors.lane.copy(alpha = 0.45f))
                    )
                )
                .border(
                    if (outlined) 1.4.dp else 1.dp,
                    borderColor,
                    CircleShape
                ),
            contentAlignment = Alignment.Center
        ) {
            if (iconId != null) {
                Image(
                    painter = painterResource(nativeQuickActionIcon(iconId)),
                    contentDescription = label,
                    modifier = Modifier.size(19.dp),
                    colorFilter = ColorFilter.tint(foreground)
                )
            } else if (renderIndicator) {
                Column(
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(1.dp)
                ) {
                    Text("渲染", color = foreground, fontSize = 7.sp, fontWeight = FontWeight.Medium)
                    Box(
                        Modifier
                            .width(13.dp)
                            .height(2.dp)
                            .clip(CircleShape)
                            .background(Color(0xFF438BE8))
                    )
                }
            } else {
                Text(
                    text = symbol.orEmpty(),
                    color = foreground,
                    fontSize = if (symbol == "AI") 10.sp else 15.sp,
                    fontWeight = if (emphasized || symbol == "AI") FontWeight.SemiBold else FontWeight.Normal,
                    maxLines = 1
                )
            }
        }
    }
}

@Composable
private fun HistoryAction(
    label: String,
    symbol: String,
    enabled: Boolean,
    onClick: () -> Unit
) {
    Row(
        modifier = Modifier
            .clip(RoundedCornerShape(16.dp))
            .background(NcColors.panel)
            .border(1.dp, NcColors.line, RoundedCornerShape(16.dp))
            .clickable(enabled = enabled, onClick = onClick)
            .padding(horizontal = 12.dp, vertical = 7.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(5.dp)
    ) {
        Text(symbol, color = if (enabled) NcColors.text else NcColors.muted, fontSize = 14.sp)
        Text(label, color = if (enabled) NcColors.text else NcColors.muted, fontSize = 10.sp)
    }
}

@Composable
private fun ClipSelectionBar(
    trackName: String,
    clipName: String,
    onClearSelection: () -> Unit,
    modifier: Modifier = Modifier
) {
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
        Text("编辑目标", color = NcColors.secondary, fontSize = 9.sp)
        Spacer(Modifier.width(6.dp))
        Box(
            modifier = Modifier
                .size(30.dp)
                .clip(CircleShape)
                .clickable(onClick = onClearSelection),
            contentAlignment = Alignment.Center
        ) {
            Text("×", color = NcColors.secondary, fontSize = 19.sp)
        }
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
    onTrackVisibilityChanged: ((Int) -> Unit)?,
    onTrackMuteChanged: ((Int) -> Unit)?,
    onTrackLockChanged: ((Int) -> Unit)?,
    onPlayFromStart: (() -> Unit)?,
    modifier: Modifier = Modifier
) {
    AndroidView(
        factory = { context -> NativeTimelineView(context) },
        update = { view ->
            view.onPlayheadChanged = onPlayheadChanged
            view.onClipSelected = onClipSelected
            view.onClipSelectionCleared = onClipSelectionCleared
            view.onTrackVisibilityChanged = onTrackVisibilityChanged
            view.onTrackMuteChanged = onTrackMuteChanged
            view.onTrackLockChanged = onTrackLockChanged
            view.onPlayFromStart = onPlayFromStart
            view.setTimeline(tracks, durationUs, playheadUs)
        },
        modifier = modifier
    )
}

/**
 * Shared transform value domains. Slider and numeric entry deliberately use
 * the exact same hard range: the slider provides coarse navigation and the
 * numeric field provides precise positioning inside that same domain.
 */
object NativeTransformRange {
    const val POSITION_MIN = -10f
    const val POSITION_MAX = 10f
    const val SCALE_MIN_PERCENT = 5f
    const val SCALE_MAX_PERCENT = 1000f

    const val ROTATION_MIN_DEGREES = -3600f
    const val ROTATION_MAX_DEGREES = 3600f
    const val ANCHOR_MIN = 0
    const val ANCHOR_MAX = 8
    const val ANCHOR_CENTER = 4
}

/**
 * Presentation inputs; the host must persist changes through the engine command API.
 * X/Y are normalized canvas offsets (1.0 = one full canvas width/height),
 * scale is expressed as a percentage, and rotation is in degrees.
 */
data class NativeTransform(val x: Float = 0f, val y: Float = 0f, val scale: Float = 100f, val rotation: Float = 0f)
enum class NativeParameter(val engineProperty: Int) {
    SCALE(1),
    X(2),
    Y(3),
    ROTATION(4)
}

private fun selectedItemDuration(
    tracks: List<NativeTimelineTrack>,
    target: Pair<Int, Long>
): Long = tracks.firstOrNull { it.engineIndex == target.first }
    ?.clips?.firstOrNull { it.id == target.second }?.durationUs ?: 0L

@Composable
private fun NativeClipAudioPanel(
    clipId: Long,
    durationUs: Long,
    audio: NativeEditorCommandAdapter.ClipAudioState,
    showGain: Boolean,
    showFades: Boolean,
    onChange: (NativeEditorCommandAdapter.ClipAudioState) -> Unit,
    modifier: Modifier = Modifier
) {
    var gain by remember(clipId, audio.gainDb) { mutableStateOf(audio.gainDb.toFloat().coerceIn(-60f, 12f)) }
    var fadeIn by remember(clipId, audio.fadeInUs, durationUs) {
        mutableStateOf(audio.fadeInUs.coerceIn(0L, durationUs))
    }
    var fadeOut by remember(clipId, audio.fadeOutUs, durationUs) {
        mutableStateOf(audio.fadeOutUs.coerceIn(0L, durationUs))
    }
    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp))
            .border(1.dp, NcColors.line, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        if (showGain) {
            Text("片段音量", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Slider(value = gain, onValueChange = { gain = it },
                    onValueChangeFinished = { onChange(audio.copy(gainDb = gain.toDouble())) },
                    valueRange = -60f..12f, modifier = Modifier.weight(1f))
                Text(String.format(Locale.US, "%.1f dB", gain), color = NcColors.text, fontSize = 10.sp)
            }
        }
        if (showFades) {
            Text("淡入淡出", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Text("淡入与淡出总时长不能超过片段时长。", color = NcColors.secondary, fontSize = 9.sp)
            Text("淡入  ${String.format(Locale.US, "%.2f", fadeIn / 1_000_000.0)} s",
                color = NcColors.text, fontSize = 10.sp)
            Slider(value = fadeIn.toFloat(), onValueChange = {
                fadeIn = it.toLong().coerceIn(0L, (durationUs - fadeOut).coerceAtLeast(0L))
            }, onValueChangeFinished = {
                onChange(audio.copy(fadeInUs = fadeIn, fadeOutUs = fadeOut))
            }, valueRange = 0f..durationUs.toFloat().coerceAtLeast(1f))
            Text("淡出  ${String.format(Locale.US, "%.2f", fadeOut / 1_000_000.0)} s",
                color = NcColors.text, fontSize = 10.sp)
            Slider(value = fadeOut.toFloat(), onValueChange = {
                fadeOut = it.toLong().coerceIn(0L, (durationUs - fadeIn).coerceAtLeast(0L))
            }, onValueChangeFinished = {
                onChange(audio.copy(fadeInUs = fadeIn, fadeOutUs = fadeOut))
            }, valueRange = 0f..durationUs.toFloat().coerceAtLeast(1f))
        }
    }
}

@Composable
private fun NativeClipDenoisePanel(
    strength: Int,
    onChange: (Int) -> Unit,
    modifier: Modifier = Modifier
) {
    val labels = listOf("关闭", "弱", "中", "强")
    Column(
        modifier = modifier.background(NcColors.panel, RoundedCornerShape(12.dp))
            .border(1.dp, NcColors.line, RoundedCornerShape(12.dp)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("音频降噪", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            labels.forEachIndexed { index, label ->
                Text(label, color = if (strength == index) NcColors.text else NcColors.secondary,
                    fontSize = 10.sp,
                    modifier = Modifier.clip(RoundedCornerShape(8.dp))
                        .background(if (strength == index) Color(0xFF3A3D40) else NcColors.lane)
                        .clickable { onChange(index) }.padding(horizontal = 13.dp, vertical = 9.dp))
            }
        }
        Text("0 关闭 · 1 弱 · 2 中 · 3 强；实际降噪由引擎处理。", color = NcColors.secondary, fontSize = 9.sp)
    }
}

@Composable
private fun NativeSpeedCurvePointPanel(
    clipId: Long,
    points: List<NativeEditorCommandAdapter.SpeedCurvePoint>,
    onChange: (List<NativeEditorCommandAdapter.SpeedCurvePoint>) -> Unit,
    modifier: Modifier = Modifier
) {
    var draft by remember(clipId, points) { mutableStateOf(points) }
    val currentDraft by rememberUpdatedState(draft)
    val currentOnChange by rememberUpdatedState(onChange)
    Column(
        modifier = modifier
            .background(NcColors.panel, RoundedCornerShape(12.dp))
            .border(1.dp, NcColors.line, RoundedCornerShape(12.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("曲线控制点", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.weight(1f))
            Text("${draft.size}/256", color = NcColors.secondary, fontSize = 10.sp)
            Text(
                "＋ 添加",
                color = if (draft.size < 256 && draft.size >= 2) NcColors.text else NcColors.muted,
                fontSize = 10.sp,
                modifier = Modifier.clickable(enabled = draft.size in 2 until 256) {
                    val pair = draft.zipWithNext().firstOrNull { (a, b) ->
                        b.sourceSeconds - a.sourceSeconds > 0.004
                    }
                    if (pair != null) {
                        val (left, right) = pair
                        val inserted = NativeEditorCommandAdapter.SpeedCurvePoint(
                            sourceSeconds = (left.sourceSeconds + right.sourceSeconds) / 2.0,
                            speed = (left.speed + right.speed) / 2.0,
                            easing = 0
                        )
                        draft = (draft + inserted).sortedBy { it.sourceSeconds }
                        onChange(draft)
                    }
                }
            )
        }
        if (draft.isEmpty()) {
            Text("当前片段没有自定义速度曲线。可选择上方预设，或从常规变速开始。", color = NcColors.secondary, fontSize = 10.sp)
        } else {
            if (draft.size >= 2) {
                Canvas(
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(144.dp)
                        .clip(RoundedCornerShape(8.dp))
                        .background(NcColors.lane)
                        .pointerInput(clipId) {
                            var activeIndex = -1
                            var changedDuringGesture = false
                            detectDragGestures(
                                onDragStart = { pointer ->
                                    val snapshot = currentDraft
                                    if (snapshot.size >= 2) {
                                        val inset = 12f
                                        val plotWidth = (size.width - inset * 2f).coerceAtLeast(1f)
                                        val plotHeight = (size.height - inset * 2f).coerceAtLeast(1f)
                                        val minTime = snapshot.first().sourceSeconds
                                        val maxTime = snapshot.last().sourceSeconds
                                        val span = (maxTime - minTime).takeIf { it > 0.0 } ?: 1.0
                                        activeIndex = snapshot.indices.minByOrNull { index ->
                                            val point = snapshot[index]
                                            val x = inset + ((point.sourceSeconds - minTime) / span).toFloat() * plotWidth
                                            val y = inset + (1f - ((point.speed - 0.1) / 9.9).toFloat()) * plotHeight
                                            val dx = pointer.x - x
                                            val dy = pointer.y - y
                                            dx * dx + dy * dy
                                        } ?: -1
                                        val point = snapshot.getOrNull(activeIndex)
                                        if (point != null) {
                                            val x = inset + ((point.sourceSeconds - minTime) / span).toFloat() * plotWidth
                                            val y = inset + (1f - ((point.speed - 0.1) / 9.9).toFloat()) * plotHeight
                                            val dx = pointer.x - x
                                            val dy = pointer.y - y
                                            if (dx * dx + dy * dy > 40f * 40f) activeIndex = -1
                                        }
                                    }
                                    changedDuringGesture = false
                                },
                                onDragEnd = {
                                    if (changedDuringGesture) currentOnChange(currentDraft)
                                    activeIndex = -1
                                },
                                onDragCancel = {
                                    activeIndex = -1
                                    changedDuringGesture = false
                                },
                                onDrag = { change, _ ->
                                    val snapshot = currentDraft
                                    val index = activeIndex
                                    if (index in snapshot.indices && snapshot.size >= 2) {
                                        val inset = 12f
                                        val plotWidth = (size.width - inset * 2f).coerceAtLeast(1f)
                                        val plotHeight = (size.height - inset * 2f).coerceAtLeast(1f)
                                        val minTime = snapshot.first().sourceSeconds
                                        val maxTime = snapshot.last().sourceSeconds
                                        val span = (maxTime - minTime).takeIf { it > 0.0 } ?: 1.0
                                        val pointerX = change.position.x.coerceIn(inset, inset + plotWidth)
                                        val pointerY = change.position.y.coerceIn(inset, inset + plotHeight)
                                        val old = snapshot[index]
                                        val speed = (0.1 + (1f - (pointerY - inset) / plotHeight) * 9.9f)
                                            .toDouble().coerceIn(0.1, 10.0)
                                        val minGap = 0.002
                                        val time = if (index == 0 || index == snapshot.lastIndex) {
                                            old.sourceSeconds
                                        } else {
                                            val lower = snapshot[index - 1].sourceSeconds + minGap
                                            val upper = snapshot[index + 1].sourceSeconds - minGap
                                            if (lower <= upper) {
                                                (minTime + ((pointerX - inset) / plotWidth) * span).coerceIn(lower, upper)
                                            } else old.sourceSeconds
                                        }
                                        if (speed != old.speed || time != old.sourceSeconds) {
                                            draft = snapshot.toMutableList().also {
                                                it[index] = old.copy(sourceSeconds = time, speed = speed)
                                            }
                                            changedDuringGesture = true
                                        }
                                        change.consume()
                                    }
                                }
                            )
                        }
                ) {
                    val inset = 12f
                    val plotWidth = (size.width - inset * 2f).coerceAtLeast(1f)
                    val plotHeight = (size.height - inset * 2f).coerceAtLeast(1f)
                    val minTime = draft.first().sourceSeconds
                    val maxTime = draft.last().sourceSeconds
                    val timeSpan = (maxTime - minTime).takeIf { it > 0.0 } ?: 1.0
                    fun pointOffset(point: NativeEditorCommandAdapter.SpeedCurvePoint): androidx.compose.ui.geometry.Offset {
                        val x = inset + (((point.sourceSeconds - minTime) / timeSpan).toFloat() * plotWidth)
                        val normalizedSpeed = ((point.speed.coerceIn(0.1, 10.0) - 0.1) / 9.9).toFloat()
                        val y = inset + (1f - normalizedSpeed) * plotHeight
                        return androidx.compose.ui.geometry.Offset(x, y)
                    }
                    for (step in 0..4) {
                        val y = inset + plotHeight * step / 4f
                        drawLine(
                            color = NcColors.line.copy(alpha = 0.55f),
                            start = androidx.compose.ui.geometry.Offset(inset, y),
                            end = androidx.compose.ui.geometry.Offset(inset + plotWidth, y),
                            strokeWidth = 1f
                        )
                    }
                    for (step in 0..4) {
                        val x = inset + plotWidth * step / 4f
                        drawLine(
                            color = NcColors.line.copy(alpha = 0.35f),
                            start = androidx.compose.ui.geometry.Offset(x, inset),
                            end = androidx.compose.ui.geometry.Offset(x, inset + plotHeight),
                            strokeWidth = 1f
                        )
                    }
                    val curvePath = androidx.compose.ui.graphics.Path()
                    draft.forEachIndexed { index, point ->
                        val position = pointOffset(point)
                        if (index == 0) curvePath.moveTo(position.x, position.y)
                        else curvePath.lineTo(position.x, position.y)
                    }
                    drawPath(
                        path = curvePath,
                        color = NcColors.text,
                        style = androidx.compose.ui.graphics.drawscope.Stroke(width = 2.5f)
                    )
                    draft.forEach { point ->
                        drawCircle(
                            color = NcColors.text,
                            radius = 6f,
                            center = pointOffset(point)
                        )
                    }
                }
                Row {
                    Text("10×", color = NcColors.muted, fontSize = 8.sp)
                    Spacer(Modifier.weight(1f))
                    Text("拖动曲线节点直接调节 · 首尾时间点固定", color = NcColors.secondary, fontSize = 8.sp)
                    Spacer(Modifier.weight(1f))
                    Text("0.1×", color = NcColors.muted, fontSize = 8.sp)
                }
            }
            draft.forEachIndexed { index, point ->
                Column(
                    modifier = Modifier.fillMaxWidth().background(NcColors.lane, RoundedCornerShape(8.dp)).padding(8.dp),
                    verticalArrangement = Arrangement.spacedBy(5.dp)
                ) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("P${index + 1}  ·  ${String.format(Locale.US, "%.3f", point.sourceSeconds)} s",
                            color = NcColors.text, fontSize = 10.sp)
                        Spacer(Modifier.weight(1f))
                        Text(
                            "缓动 ${listOf("线性", "缓入", "缓出", "缓入缓出", "阶梯").getOrElse(point.easing) { "未知" }}",
                            color = NcColors.secondary,
                            fontSize = 9.sp,
                            modifier = Modifier.clickable {
                                draft = draft.toMutableList().also {
                                    it[index] = point.copy(easing = (point.easing + 1) % 5)
                                }
                                onChange(draft)
                            }
                        )
                        if (draft.size > 2) {
                            Text("删除", color = NcColors.secondary, fontSize = 9.sp,
                                modifier = Modifier.padding(start = 10.dp).clickable {
                                    draft = draft.toMutableList().also { it.removeAt(index) }
                                    onChange(draft)
                                })
                        }
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text("速度", color = NcColors.secondary, fontSize = 9.sp)
                        Slider(
                            value = point.speed.toFloat().coerceIn(0.1f, 10f),
                            onValueChange = { value ->
                                draft = draft.toMutableList().also { it[index] = point.copy(speed = value.toDouble()) }
                            },
                            onValueChangeFinished = { onChange(draft) },
                            valueRange = 0.1f..10f,
                            modifier = Modifier.weight(1f)
                        )
                        Text(String.format(Locale.US, "%.2f×", point.speed), color = NcColors.text, fontSize = 9.sp)
                    }
                    if (index in 1 until draft.lastIndex) {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Text("时间", color = NcColors.secondary, fontSize = 9.sp)
                            var timeText by remember(clipId, point.sourceSeconds) {
                                mutableStateOf(String.format(Locale.US, "%.3f", point.sourceSeconds))
                            }
                            BasicTextField(
                                value = timeText,
                                onValueChange = { candidate ->
                                    if (candidate.length <= 12 && candidate.matches(Regex("[0-9]*[.]?[0-9]*"))) {
                                        timeText = candidate
                                    }
                                },
                                modifier = Modifier.width(76.dp).background(NcColors.panel, RoundedCornerShape(5.dp))
                                    .padding(horizontal = 6.dp, vertical = 4.dp),
                                singleLine = true,
                                keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal, imeAction = ImeAction.Done),
                                keyboardActions = KeyboardActions(onDone = {
                                    val parsed = timeText.toDoubleOrNull()
                                    val lower = draft[index - 1].sourceSeconds + 0.002
                                    val upper = draft[index + 1].sourceSeconds - 0.002
                                    if (parsed != null && parsed.isFinite() && lower <= upper && parsed in lower..upper) {
                                        draft = draft.toMutableList().also {
                                            it[index] = it[index].copy(sourceSeconds = parsed)
                                        }
                                        onChange(draft)
                                    } else {
                                        timeText = String.format(Locale.US, "%.3f", draft[index].sourceSeconds)
                                    }
                                })
                            )
                            Text("s（相邻点间隔至少 2ms）", color = NcColors.muted, fontSize = 8.sp)
                        }
                    }
                }
            }
        }
        Text("时间点按源素材秒数严格递增；速度范围为 0.1×–10×。曲线拖动在松手时统一提交，首尾时间点固定。", color = NcColors.secondary, fontSize = 9.sp)
    }
}

@Composable
private fun NativeSpeedCurvePresetPanel(
    onApply: (Int) -> Unit,
    enabled: Boolean,
    modifier: Modifier = Modifier
) {
    val presets = listOf(
        "自定义/恒速" to 0,
        "蒙太奇" to 1,
        "英雄时刻" to 2,
        "子弹时间" to 3,
        "跳跃" to 4,
        "闪进" to 5,
        "闪出" to 6
    )
    Column(
        modifier = modifier
            .background(NcColors.panel)
            .padding(horizontal = 12.dp, vertical = 10.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Text("变速曲线预设", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
        Text("预设由剪辑引擎生成；选择后会替换当前片段的速度曲线。", color = NcColors.secondary, fontSize = 10.sp)
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(7.dp)
        ) {
            presets.forEach { (name, id) ->
                Text(
                    name,
                    color = if (enabled) NcColors.text else NcColors.muted,
                    fontSize = 10.sp,
                    modifier = Modifier
                        .clip(RoundedCornerShape(14.dp))
                        .border(1.dp, NcColors.line, RoundedCornerShape(14.dp))
                        .clickable(enabled = enabled) { onApply(id) }
                        .padding(horizontal = 11.dp, vertical = 8.dp)
                )
            }
        }
    }
}


@Composable
private fun NativeClipOpacityPanel(
    clipId: Long,
    opacity: Double,
    onChange: (Double) -> Unit,
    modifier: Modifier = Modifier
) {
    val safeOpacity = opacity.takeIf { it.isFinite() }?.coerceIn(0.0, 1.0) ?: 1.0
    var value by remember(clipId) { mutableStateOf(safeOpacity.toFloat()) }
    LaunchedEffect(clipId, safeOpacity) { value = safeOpacity.toFloat() }
    Column(
        modifier = modifier
            .background(NcColors.panel, RoundedCornerShape(10.dp))
            .border(1.dp, NcColors.line, RoundedCornerShape(10.dp))
            .padding(horizontal = 12.dp, vertical = 9.dp),
        verticalArrangement = Arrangement.spacedBy(5.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("片段不透明度", color = NcColors.text, fontSize = 11.sp)
            Text("${(value * 100f).roundToInt()}%", color = NcColors.text, fontSize = 11.sp)
        }
        Slider(
            value = value,
            onValueChange = { value = it.coerceIn(0f, 1f) },
            onValueChangeFinished = { onChange(value.toDouble()) },
            valueRange = 0f..1f,
            steps = 19
        )
        Text("0% 完全透明 · 100% 完全不透明", color = NcColors.secondary, fontSize = 9.sp)
    }
}

@Composable
private fun NativeSpeedPanel(
    clipId: Long,
    initialSpeed: Float,
    onApply: (Float) -> Unit,
    modifier: Modifier = Modifier
) {
    var speed by remember(clipId, initialSpeed) {
        mutableStateOf(initialSpeed.coerceIn(0.1f, 100f))
    }
    var numericText by remember(clipId, initialSpeed) {
        mutableStateOf(String.format(Locale.US, "%.3f", initialSpeed.coerceIn(0.1f, 100f)))
    }
    Column(
        modifier = modifier
            .background(NcColors.panel, RoundedCornerShape(12.dp))
            .border(1.dp, NcColors.line, RoundedCornerShape(12.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text("常规变速", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.weight(1f))
            BasicTextField(
                value = numericText,
                onValueChange = { candidate ->
                    if (candidate.length <= 8 && candidate.matches(Regex("[0-9]*[.]?[0-9]*"))) {
                        numericText = candidate
                    }
                },
                modifier = Modifier
                    .width(76.dp)
                    .background(NcColors.lane, RoundedCornerShape(5.dp))
                    .padding(horizontal = 7.dp, vertical = 5.dp),
                singleLine = true,
                keyboardOptions = KeyboardOptions(
                    keyboardType = KeyboardType.Decimal,
                    imeAction = ImeAction.Done
                ),
                keyboardActions = KeyboardActions(onDone = {
                    val parsed = numericText.toFloatOrNull()
                    if (parsed != null && parsed.isFinite() && parsed in 0.1f..100f) {
                        speed = parsed
                        numericText = String.format(Locale.US, "%.3f", parsed)
                        onApply(parsed)
                    } else {
                        numericText = String.format(Locale.US, "%.3f", speed)
                    }
                }),
                textStyle = TextStyle(color = NcColors.text, fontSize = 12.sp, textAlign = TextAlign.End)
            )
            Text(" ×", color = NcColors.secondary, fontSize = 10.sp)
        }
        Slider(
            value = speed,
            onValueChange = { value ->
                speed = value
                numericText = String.format(Locale.US, "%.3f", value)
            },
            onValueChangeFinished = { onApply(speed) },
            valueRange = 0.1f..100f
        )
        Row {
            Text("0.1×", color = NcColors.muted, fontSize = 9.sp)
            Spacer(Modifier.weight(1f))
            Text("1× 原速", color = NcColors.secondary, fontSize = 9.sp)
            Spacer(Modifier.weight(1f))
            Text("100×", color = NcColors.muted, fontSize = 9.sp)
        }
        Text("滑块与数值输入范围一致；数值支持三位小数。", color = NcColors.muted, fontSize = 9.sp)
    }
}

@Composable
private fun NativeParameterPanel(
    targetTrackIndex: Int,
    targetClipId: Long,
    activeSubTool: String,
    transform: NativeTransform,
    anchor: Int,
    keyedParameters: Set<NativeParameter>,
    keyframes: Map<NativeParameter, List<NativeEditorCommandAdapter.Keyframe>>,
    onTransformChanged: (NativeTransform) -> Unit,
    onAnchorChanged: (Int) -> Unit,
    onInsertKeyframe: (NativeParameter) -> Unit,
    onRemoveKeyframe: (NativeParameter, Long) -> Unit,
    onSetKeyframeEasing: (NativeParameter, NativeEditorCommandAdapter.Keyframe) -> Unit,
    modifier: Modifier = Modifier
) {
    // Parameter/channel selection belongs to a clip, not to the panel's
    // compositional position. Switching clips must not carry a stale channel
    // or keyframe timestamp into the newly selected target.
    var activeParameter by remember(targetTrackIndex, targetClipId, activeSubTool) {
        mutableStateOf(
            when (activeSubTool) {
                "缩放" -> NativeParameter.SCALE
                "旋转" -> NativeParameter.ROTATION
                else -> NativeParameter.X
            }
        )
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
        modifier = modifier
            .background(NcColors.panel)
            .padding(horizontal = 12.dp, vertical = 8.dp)
            .heightIn(max = 220.dp)
            .verticalScroll(rememberScrollState()),
        verticalArrangement = Arrangement.spacedBy(3.dp)
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                when (activeSubTool) {
                    "动画" -> "动画关键帧"
                    "缩放" -> "缩放参数"
                    "旋转" -> "旋转参数"
                    "锚点" -> "锚点参数"
                    else -> "位置参数"
                },
                color = NcColors.text,
                fontSize = 11.sp,
                fontWeight = FontWeight.SemiBold
            )
            Spacer(Modifier.weight(1f))
            Text(if (activeSubTool == "锚点") "3×3 九宫格预设" else "关键帧 ${activeFrames.size}", color = NcColors.secondary, fontSize = 9.sp)
        }
        if (activeSubTool == "动画") {
            Text(
                "选择变换通道，管理关键帧与缓动。新增关键帧使用当前播放头位置。",
                color = NcColors.muted,
                fontSize = 9.sp
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                verticalAlignment = Alignment.CenterVertically
            ) {
                Text(
                    "选择 X / Y / 缩放 / 旋转通道后添加关键帧",
                    color = NcColors.secondary,
                    fontSize = 9.sp,
                    modifier = Modifier.weight(1f)
                )
                Text(
                    "＋ 添加关键帧",
                    color = NcColors.text,
                    fontSize = 10.sp,
                    modifier = Modifier
                        .clip(RoundedCornerShape(6.dp))
                        .background(NcColors.lane)
                        .clickable { onInsertKeyframe(activeParameter) }
                        .padding(horizontal = 10.dp, vertical = 8.dp)
                )
            }
        }
        if (activeSubTool == "位置") {
            Text(
                "位置以画布尺寸为单位：1.0 = 一个画布宽/高；滑块与数值输入均为 -10～10",
                color = NcColors.muted,
                fontSize = 9.sp
            )
            ParameterSlider(
                "X", draftTransform.x,
                NativeTransformRange.POSITION_MIN, NativeTransformRange.POSITION_MAX,
                NativeParameter.X, keyedParameters, onInsertKeyframe,
                onValueChange = { updateDraft(NativeParameter.X, it) },
                onValueChangeFinished = ::commitDraft
            )
            ParameterSlider(
                "Y", draftTransform.y,
                NativeTransformRange.POSITION_MIN, NativeTransformRange.POSITION_MAX,
                NativeParameter.Y, keyedParameters, onInsertKeyframe,
                onValueChange = { updateDraft(NativeParameter.Y, it) },
                onValueChangeFinished = ::commitDraft
            )
        }
        if (activeSubTool == "缩放") {
            Text("100% 为原始尺寸；滑块与数值输入均为 5%–1000%", color = NcColors.muted, fontSize = 9.sp)
            ParameterSlider(
                "缩放", draftTransform.scale,
                NativeTransformRange.SCALE_MIN_PERCENT, NativeTransformRange.SCALE_MAX_PERCENT,
                NativeParameter.SCALE, keyedParameters, onInsertKeyframe, "%",
                onValueChange = { updateDraft(NativeParameter.SCALE, it) },
                onValueChangeFinished = ::commitDraft
            )
        }
        if (activeSubTool == "旋转") {
            Text(
                "滑块与数值输入均支持正负 10 圈（±3600°）；角度不自动折返",
                color = NcColors.muted,
                fontSize = 9.sp
            )
            ParameterSlider(
                "旋转", draftTransform.rotation,
                NativeTransformRange.ROTATION_MIN_DEGREES,
                NativeTransformRange.ROTATION_MAX_DEGREES,
                NativeParameter.ROTATION, keyedParameters, onInsertKeyframe, "°",
                onValueChange = { updateDraft(NativeParameter.ROTATION, it) },
                onValueChangeFinished = ::commitDraft
            )
        }
        if (activeSubTool == "锚点") {
            Text("锚点（缩放 / 旋转中心）", color = NcColors.secondary, fontSize = 9.sp)
            val anchorLabels = listOf("左上", "上中", "右上", "左中", "中心", "右中", "左下", "下中", "右下")
            Column(
                modifier = Modifier
                    .fillMaxWidth()
                    .selectableGroup(),
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                for (anchorRow in 0..2) {
                    Row(
                        horizontalArrangement = Arrangement.spacedBy(5.dp),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        for (anchorColumn in 0..2) {
                            val anchorIndex = anchorRow * 3 + anchorColumn
                            val selected = anchor == anchorIndex
                            Box(
                                modifier = Modifier
                                    .weight(1f)
                                    .height(48.dp)
                                    .clip(RoundedCornerShape(5.dp))
                                    .background(if (selected) Color(0xFF3A3D40) else NcColors.lane)
                                    .border(
                                        1.dp,
                                        if (selected) Color.White else NcColors.line,
                                        RoundedCornerShape(5.dp)
                                    )
                                    .selectable(
                                        selected = selected,
                                        role = Role.RadioButton,
                                        onClick = { onAnchorChanged(anchorIndex) }
                                    ),
                                contentAlignment = Alignment.Center
                            ) {
                                Text(
                                    anchorLabels[anchorIndex],
                                    color = if (selected) NcColors.text else NcColors.secondary,
                                    fontSize = 9.sp
                                )
                            }
                        }
                    }
                }
            }
            if (anchor !in NativeTransformRange.ANCHOR_MIN..NativeTransformRange.ANCHOR_MAX) {
                Text("锚点数据无效，请重新读取片段变换", color = NcColors.muted, fontSize = 9.sp)
            }
        }
        if (activeSubTool != "锚点") {
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
                        .padding(horizontal = 8.dp, vertical = 16.dp)
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
                    val formattedValue = when (activeParameter) {
                        NativeParameter.SCALE -> "%.1f%%".format(Locale.US, frame.value * 100.0)
                        NativeParameter.X, NativeParameter.Y -> "%.3f".format(Locale.US, frame.value)
                        NativeParameter.ROTATION -> "%.1f°".format(Locale.US, frame.value)
                    }
                    Text(
                        text = "%.2fs · %s".format(Locale.US, frame.timeUs / 1_000_000.0, formattedValue),
                        color = if (selected) NcColors.text else NcColors.secondary,
                        fontSize = 9.sp,
                        modifier = Modifier
                            .clip(RoundedCornerShape(5.dp))
                            .background(if (selected) Color(0xFF414549) else NcColors.lane)
                            .clickable { selectedFrameTimeUs = frame.timeUs }
                            .padding(horizontal = 7.dp, vertical = 16.dp)
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
                            .padding(vertical = 16.dp)
                    )
                    Text(
                        "删除关键帧",
                        color = NcColors.danger,
                        fontSize = 9.sp,
                        modifier = Modifier
                            .clickable { onRemoveKeyframe(activeParameter, frame.timeUs) }
                            .padding(horizontal = 8.dp, vertical = 16.dp)
                    )
                }
                KeyframeCurvePreview(frame = frame, modifier = Modifier.fillMaxWidth().height(44.dp))
            }
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
    // Slider and numeric entry intentionally share one fixed domain.
    // Dragging gives coarse control; numeric entry allows precise placement.
    val safeValue = value.takeIf { it.isFinite() }?.coerceIn(low, high) ?: low
    val valueFormat = when (parameter) {
        NativeParameter.X, NativeParameter.Y -> "%.3f"
        NativeParameter.SCALE, NativeParameter.ROTATION -> "%.1f"
    }
    fun formatValue(value: Float): String = valueFormat.format(Locale.US, value)
    val focusManager = LocalFocusManager.current
    val valueFocusRequester = remember { FocusRequester() }
    var editingValue by remember(parameter, safeValue) { mutableStateOf(false) }
    var numericFieldFocused by remember(parameter, safeValue) { mutableStateOf(false) }
    var numericText by remember(parameter, safeValue) {
        mutableStateOf(formatValue(safeValue))
    }
    LaunchedEffect(editingValue) {
        if (editingValue) valueFocusRequester.requestFocus()
    }

    fun finishNumericEdit() {
        val parsed = numericText.trim().replace(',', '.').toFloatOrNull()
        if (parsed != null && parsed.isFinite()) {
            onValueChange(parsed.coerceIn(low, high))
            onValueChangeFinished()
        } else {
            numericText = formatValue(safeValue)
        }
        editingValue = false
    }

    fun commitNumericValue() {
        finishNumericEdit()
        focusManager.clearFocus()
    }

    Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.height(48.dp)) {
        Text(label, color = NcColors.secondary, fontSize = 10.sp, modifier = Modifier.width(34.dp))
        Slider(
            value = safeValue,
            onValueChange = onValueChange,
            onValueChangeFinished = onValueChangeFinished,
            valueRange = low..high,
            modifier = Modifier.weight(1f)
        )
        Box(
            modifier = Modifier.width(58.dp).height(44.dp).clip(RoundedCornerShape(5.dp))
                .background(if (editingValue) NcColors.lane else Color.Transparent)
                .border(1.dp, NcColors.line, RoundedCornerShape(5.dp))
                .clickable {
                    numericFieldFocused = false
                    editingValue = true
                }
                .padding(horizontal = 3.dp, vertical = 5.dp),
            contentAlignment = Alignment.CenterEnd
        ) {
            if (editingValue) {
                BasicTextField(
                    value = numericText,
                    onValueChange = { candidate ->
                        // Keep the input bounded; the value is committed only
                        // when the user confirms it, not on every keystroke.
                        if (candidate.length <= 14 &&
                            candidate.all { it.isDigit() || it == '-' || it == '.' || it == ',' }
                        ) numericText = candidate
                    },
                    singleLine = true,
                    modifier = Modifier
                        .focusRequester(valueFocusRequester)
                        .onFocusChanged { state ->
                            if (state.isFocused) {
                                numericFieldFocused = true
                            } else if (numericFieldFocused && editingValue) {
                                // Tapping elsewhere commits the edit just like
                                // the keyboard Done action.
                                numericFieldFocused = false
                                finishNumericEdit()
                            }
                        },
                    textStyle = TextStyle(
                        color = NcColors.text,
                        fontSize = 10.sp,
                        textAlign = TextAlign.End
                    ),
                    keyboardOptions = KeyboardOptions(
                        keyboardType = KeyboardType.Decimal,
                        imeAction = ImeAction.Done
                    ),
                    keyboardActions = KeyboardActions(onDone = { commitNumericValue() })
                )
            } else {
                Text(
                    formatValue(safeValue) + suffix,
                    color = NcColors.text,
                    fontSize = 9.sp,
                    textAlign = TextAlign.End
                )
            }
        }
        Text(
            if (parameter in keyedParameters) "◆" else "◇",
            color = if (parameter in keyedParameters) Color.White else NcColors.secondary,
            fontSize = 15.sp,
            modifier = Modifier.width(26.dp).clickable { onInsertKeyframe(parameter) },
            textAlign = TextAlign.Center
        )
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
private fun NativeTransportBar(
    hasSelection: Boolean,
    isPlaying: Boolean,
    onSplit: () -> Unit,
    onTogglePlayback: () -> Unit,
    onDelete: () -> Unit
) {
    // Three centered transport actions preserve the reference row's fixed height.
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(NativeEditorLayoutSpec.TRANSPORT_ROW_HEIGHT_DP.dp)
            .background(NcColors.timeline),
        horizontalArrangement = Arrangement.spacedBy(40.dp, Alignment.CenterHorizontally),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Box(
            modifier = Modifier
                .size(44.dp)
                .clip(CircleShape)
                .clickable(onClick = onSplit),
            contentAlignment = Alignment.Center
        ) {
            Image(
                painter = painterResource(R.drawable.ic_editor_scissors),
                contentDescription = "分割片段",
                modifier = Modifier.size(23.dp).graphicsLayer { rotationZ = 90f },
                colorFilter = ColorFilter.tint(if (hasSelection) NcColors.text else NcColors.secondary)
            )
        }
        Box(
            modifier = Modifier
                .size(44.dp)
                .clip(CircleShape)
                .clickable(onClick = onTogglePlayback),
            contentAlignment = Alignment.Center
        ) {
            Image(
                painter = painterResource(if (isPlaying) R.drawable.ic_editor_pause else R.drawable.ic_editor_play),
                contentDescription = if (isPlaying) "暂停" else "播放",
                modifier = Modifier.size(22.dp),
                colorFilter = ColorFilter.tint(NcColors.text)
            )
        }
        Box(
            modifier = Modifier
                .size(44.dp)
                .clip(CircleShape)
                .clickable(onClick = onDelete),
            contentAlignment = Alignment.Center
        ) {
            Image(
                painter = painterResource(R.drawable.ic_editor_trash),
                contentDescription = "删除片段",
                modifier = Modifier.size(21.dp),
                colorFilter = ColorFilter.tint(NcColors.danger)
            )
        }
    }
}

private fun nativeQuickActionIcon(iconId: String): Int = when (iconId) {
    "i-plus" -> R.drawable.ic_editor_plus
    "i-home" -> R.drawable.ic_editor_home
    "i-prev" -> R.drawable.ic_editor_prev
    "i-diamond" -> R.drawable.ic_editor_diamond
    "i-keyframes" -> R.drawable.ic_editor_keyframes
    "i-hash" -> R.drawable.ic_editor_hash
    "i-image" -> R.drawable.ic_editor_image
    "i-undo" -> R.drawable.ic_editor_undo
    "i-redo" -> R.drawable.ic_editor_redo
    else -> R.drawable.ic_editor_plus
}

private fun nativeEditorToolbarIcon(iconId: String): Int = when (iconId) {
    "i-clip" -> R.drawable.ic_editor_clip
    "i-auto" -> R.drawable.ic_editor_auto
    "i-audio" -> R.drawable.ic_editor_audio
    "i-text" -> R.drawable.ic_editor_text
    "i-pip" -> R.drawable.ic_editor_pip
    "i-fx" -> R.drawable.ic_editor_fx
    "i-trans" -> R.drawable.ic_editor_trans
    "i-filter" -> R.drawable.ic_editor_filter
    "i-adjust" -> R.drawable.ic_editor_adjust
    "i-ratio" -> R.drawable.ic_editor_ratio
    "i-bg" -> R.drawable.ic_editor_bg
    "i-mask" -> R.drawable.ic_editor_mask
    "i-ai" -> R.drawable.ic_editor_ai
    else -> R.drawable.ic_editor_clip
}

@Composable
private fun NativeToolBar(
    activeTool: String,
    expandedTool: String?,
    activeSubTool: String?,
    onToolSelected: (String) -> Unit,
    onSubToolSelected: (String, String) -> Unit
) {
    // Mirror the complete first-level inventory from mobile/assets/editor.js (L1).
    // Unsupported native commands remain reachable through the retained WebView fallback.
    val tools = NativeEditorToolbarSpec.categories
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(NativeEditorLayoutSpec.BOTTOM_TOOLBAR_HEIGHT_DP.dp)
            .background(NcColors.topBar)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 8.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(7.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        tools.forEach { category ->
            val tool = category.name
            val subTools = category.subTools
            val selected = activeTool == tool
            Column(
                modifier = Modifier
                    .width(54.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (selected) Color(0xFF303235) else Color.Transparent)
                    .selectable(
                        selected = selected,
                        role = Role.Tab,
                        onClick = { onToolSelected(tool) }
                    )
                    .padding(vertical = 7.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Image(
                    painter = painterResource(nativeEditorToolbarIcon(category.iconId)),
                    contentDescription = tool,
                    modifier = Modifier.size(22.dp),
                    colorFilter = ColorFilter.tint(if (selected) NcColors.text else NcColors.secondary)
                )
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
                            .width(54.dp)
                            .clip(RoundedCornerShape(10.dp))
                            .background(if (subSelected) Color(0xFF303235) else Color.Transparent)
                            .selectable(
                                selected = subSelected,
                                role = Role.Tab,
                                onClick = { onSubToolSelected(tool, subTool) }
                            )
                            .padding(vertical = 7.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.spacedBy(5.dp)
                    ) {
                        Box(
                            modifier = Modifier
                                .size(25.dp)
                                .clip(CircleShape)
                                // Selection is indicated by a restrained fill, not a bright white ring.
                                .border(1.dp, NcColors.line, CircleShape),
                            contentAlignment = Alignment.Center
                        ) {
                            Text(subTool.take(1), color = NcColors.text, fontSize = 11.sp)
                        }
                        Text(
                            subTool,
                            color = if (subSelected) NcColors.text else NcColors.secondary,
                            fontSize = 9.sp,
                            textAlign = TextAlign.Center,
                            maxLines = 1
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun NativeImageBackgroundPanel(
    assets: List<NativeTimelineAsset>,
    backgroundRgba: Long?,
    isImporting: Boolean,
    message: String?,
    onPickImages: () -> Unit,
    onRemoveImage: (Long) -> Unit,
    onSetBackgroundColor: (Long) -> Unit,
    onDismiss: () -> Unit
) {
    var pendingDelete by remember { mutableStateOf<NativeTimelineAsset?>(null) }
    val presets = listOf(
        "纯黑" to 0x000000FFL,
        "纯白" to 0xFFFFFFFFL,
        "深灰" to 0x1C1C1EFFL,
        "米白" to 0xF5F1EAFFL,
        "雾蓝" to 0x2E3A4CFFL,
        "墨绿" to 0x1F3A32FFL,
        "酒红" to 0x3A1F28FFL,
        "暖沙" to 0xE8DCC8FFL
    )
    Column(
        modifier = Modifier.fillMaxWidth()
            .background(NcColors.panel, RoundedCornerShape(12.dp))
            .border(1.dp, NcColors.line, RoundedCornerShape(12.dp))
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(9.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text("图片与背景", color = NcColors.text, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
            Text("关闭", color = NcColors.secondary, fontSize = 10.sp, modifier = Modifier.clickable(onClick = onDismiss).padding(6.dp))
        }
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text("项目图片", color = NcColors.text, fontSize = 11.sp)
                Text("图片素材默认时长 5 秒；删除前会检查时间轴引用", color = NcColors.secondary, fontSize = 9.sp)
            }
            Text(
                if (isImporting) "正在导入…" else "添加图片",
                color = if (isImporting) NcColors.muted else NcColors.text,
                fontSize = 10.sp,
                modifier = Modifier.clip(RoundedCornerShape(8.dp))
                    .background(NcColors.stage)
                    .clickable(enabled = !isImporting, onClick = onPickImages)
                    .padding(horizontal = 12.dp, vertical = 8.dp)
            )
        }
        if (assets.isEmpty()) {
            Text("项目里还没有图片", color = NcColors.secondary, fontSize = 10.sp)
        } else {
            Column(
                modifier = Modifier.fillMaxWidth().heightIn(max = 112.dp)
                    .verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                assets.forEach { asset ->
                    Row(
                        modifier = Modifier.fillMaxWidth()
                            .background(NcColors.stage, RoundedCornerShape(7.dp))
                            .padding(start = 9.dp, end = 5.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.SpaceBetween
                    ) {
                        Column(modifier = Modifier.weight(1f).padding(vertical = 7.dp)) {
                            Text(asset.name, color = NcColors.text, fontSize = 10.sp, maxLines = 1)
                            Text("图片素材 · ${(asset.durationUs / 1_000_000L).coerceAtLeast(0L)} 秒", color = NcColors.secondary, fontSize = 9.sp)
                        }
                        Text("删除", color = NcColors.secondary, fontSize = 10.sp,
                            modifier = Modifier.clickable { pendingDelete = asset }.padding(9.dp))
                    }
                }
            }
        }
        Text("画布背景色", color = NcColors.text, fontSize = 11.sp, fontWeight = FontWeight.Medium)
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(10.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            presets.forEach { (label, rgba) ->
                val selected = backgroundRgba == rgba
                val color = Color(android.graphics.Color.rgb(
                    ((rgba shr 24) and 0xFF).toInt(),
                    ((rgba shr 16) and 0xFF).toInt(),
                    ((rgba shr 8) and 0xFF).toInt()
                ))
                Column(horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.spacedBy(3.dp)) {
                    Box(
                        modifier = Modifier.size(29.dp).clip(CircleShape).background(color)
                            .border(if (selected) 2.dp else 1.dp,
                                if (selected) NcColors.text else NcColors.line, CircleShape)
                            .clickable { onSetBackgroundColor(rgba) }
                    )
                    Text(label, color = if (selected) NcColors.text else NcColors.secondary, fontSize = 8.sp)
                }
            }
        }
        if (!message.isNullOrBlank()) {
            Text(message, color = Color(0xFFD69A58), fontSize = 9.sp)
        }
    }
    pendingDelete?.let { asset ->
        AlertDialog(
            onDismissRequest = { pendingDelete = null },
            title = { Text("删除图片素材？") },
            text = { Text("确认删除「${asset.name}」？已被时间轴引用的图片不能删除。") },
            confirmButton = {
                TextButton(onClick = {
                    pendingDelete = null
                    onRemoveImage(asset.id)
                }) { Text("删除") }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete = null }) { Text("取消") }
            }
        )
    }
}

