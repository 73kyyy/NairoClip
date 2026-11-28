package com.nairoclip.app

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Feature-scoped native editor host. It is intentionally not installed into
 * MainActivity: the formal WebView entry and fallback remain unchanged until
 * the separate runtime-entry acceptance gate is approved.
 *
 * The host connects the Compose editor to engine-backed timeline/history
 * commands and the existing Surface preview controller. It renders native
 * scrub frames; full A/V playback, export and unsupported actions continue
 * through the explicit legacy callback until their system pipelines are wired.
 */
@Composable
fun NativeEditorHost(
    bridge: NativeEngineBridge,
    projectName: String,
    onOpenLegacy: () -> Unit,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val adapter = remember(bridge) { NativeEditorCommandAdapter(bridge) }
    val controller = remember(bridge, scope) {
        NativeEditorHostController(adapter, scope)
    }
    val state by controller.state.collectAsState()
    val previewView = remember(context) { PreviewSurfaceView(context) }
    val previewController = remember(previewView) {
        NativePreviewSurfaceController(previewView)
    }

    val latestPlayheadUs by rememberUpdatedState(state.playheadUs)
    var transform by remember { mutableStateOf(NativeTransform()) }
    var anchor by remember { mutableStateOf(NativeTransformRange.ANCHOR_CENTER) }
    var keyframes by remember {
        mutableStateOf<Map<NativeParameter, List<NativeEditorCommandAdapter.Keyframe>>>(emptyMap())
    }
    var speedCurvePoints by remember {
        mutableStateOf<List<NativeEditorCommandAdapter.SpeedCurvePoint>?>(null)
    }
    var clipAudio by remember { mutableStateOf<NativeEditorCommandAdapter.ClipAudioState?>(null) }
    var clipDenoiseStrength by remember { mutableStateOf<Int?>(null) }
    var clipFilter by remember { mutableStateOf<NativeEditorCommandAdapter.ClipFilterState?>(null) }
    var clipLut by remember { mutableStateOf<NativeEditorCommandAdapter.ClipLutState?>(null) }
    var clipColorGrade by remember { mutableStateOf<NativeEditorCommandAdapter.ClipColorGradeState?>(null) }
    var clipMask by remember { mutableStateOf<NativeEditorCommandAdapter.ClipMaskState?>(null) }
    var clipTransitions by remember { mutableStateOf<List<NativeEditorCommandAdapter.TransitionState>>(emptyList()) }
    var transitionTypes by remember { mutableStateOf<List<Pair<Int, String>>>(emptyList()) }
    var subtitleCues by remember { mutableStateOf<List<NativeEditorCommandAdapter.SubtitleCueState>>(emptyList()) }
    var keyframeRevision by remember { mutableStateOf(0) }

    fun seekTo(timeUs: Long) {
        controller.setPlayhead(timeUs)
        previewController.requestRenderFrame(timeUs.coerceAtLeast(0L))
    }

    LaunchedEffect(bridge) {
        controller.refresh()
        previewController.onEngineReady()
    }

    LaunchedEffect(state.timeline) {
        if (state.timeline != null) {
            previewController.requestRenderFrame(state.playheadUs)
        }
    }

    LaunchedEffect(adapter) {
        val countResult = withContext(Dispatchers.IO) { adapter.readTransitionTypeCount() }
        val count = countResult.value
        if (countResult.ok && count != null) {
            val types = buildList {
                for (type in 0 until count) {
                    val result = withContext(Dispatchers.IO) {
                        adapter.readTransitionTypeName(type)
                    }
                    val name = result.value
                    if (result.ok && !name.isNullOrBlank()) add(type to name)
                }
            }
            transitionTypes = types
        }
    }

    LaunchedEffect(state.timeline, state.history, keyframeRevision) {
        val result = withContext(Dispatchers.IO) { adapter.readSubtitles() }
        subtitleCues = if (result.ok) result.value.orEmpty() else emptyList()
    }

    LaunchedEffect(state.selectedClip, state.timeline, state.history, keyframeRevision) {
        val target = state.selectedClip
        if (target == null) {
            transform = NativeTransform()
            anchor = NativeTransformRange.ANCHOR_CENTER
            keyframes = emptyMap()
            speedCurvePoints = null
            clipAudio = null
            clipDenoiseStrength = null
            clipFilter = null
            clipLut = null
            clipColorGrade = null
            clipMask = null
            clipTransitions = emptyList()
        } else {
            val transitionsResult = withContext(Dispatchers.IO) {
                adapter.readTransitions(target.trackIndex)
            }
            clipTransitions = if (transitionsResult.ok) transitionsResult.value.orEmpty() else emptyList()
            val clipSnapshot = withContext(Dispatchers.IO) {
                adapter.readClipEditorPanelSnapshot(target)
            }
            val snapshot = clipSnapshot.value
            if (clipSnapshot.ok && snapshot != null) {
                snapshot.transform?.let {
                    transform = it.transform
                    anchor = it.anchor
                } ?: run {
                    transform = NativeTransform()
                    anchor = NativeTransformRange.ANCHOR_CENTER
                }
                keyframes = snapshot.keyframes
                speedCurvePoints = snapshot.speedCurve
                clipAudio = snapshot.audio
                clipDenoiseStrength = snapshot.denoiseStrength
                clipFilter = snapshot.filter
                clipLut = snapshot.lut
                clipColorGrade = snapshot.colorGrade
                clipMask = snapshot.mask
            }
        }
    }

    DisposableEffect(previewController) {
        previewController.onSurfaceAttached = {
            previewController.requestRenderFrame(latestPlayheadUs)
        }
        onDispose {
            previewController.onSurfaceAttached = null
            previewController.dispose()
        }
    }

    NativeEditorScreen(
        modifier = modifier,
        previewView = previewView,
        projectName = projectName,
        timelineTracks = state.timeline?.tracks.orEmpty(),
        timelineMarkers = state.timeline?.markers.orEmpty(),
        timelineDurationUs = state.timeline?.durationUs ?: 30_000_000L,
        playheadUs = state.playheadUs,
        onPlayheadChanged = ::seekTo,
        onAddMarker = { timeUs -> controller.addMarker(timeUs) },
        onRemoveMarker = { markerId -> controller.removeMarker(markerId) },
        onClipSelected = controller::selectClip,
        onClipSelectionCleared = controller::clearSelection,
        onTrackVisibilityChanged = { track, visible ->
            controller.setTrackVisibility(track, visible)
        },
        onTrackMuteChanged = { track, muted ->
            controller.setTrackMuted(track, muted)
        },
        onTrackLockChanged = { track, locked ->
            controller.setTrackLocked(track, locked)
        },
        transform = transform,
        anchor = anchor,
        keyedParameters = keyframes.filterValues { it.isNotEmpty() }.keys,
        keyframes = keyframes,
        speedCurvePoints = speedCurvePoints,
        clipAudio = clipAudio,
        clipDenoiseStrength = clipDenoiseStrength,
        clipFilter = clipFilter,
        clipLut = clipLut,
        clipColorGrade = clipColorGrade,
        clipMask = clipMask,
        clipTransitions = clipTransitions,
        transitionTypes = transitionTypes,
        subtitleCues = subtitleCues,
        onTransformChanged = { track, clipId, value ->
            transform = value
            controller.setTransform(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                value,
                anchor = anchor
            )
        },
        onAnchorChanged = { track, clipId, value ->
            anchor = value
            controller.setTransform(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                transform,
                anchor = value
            )
        },
        onInsertKeyframe = { track, clipId, parameter ->
            val target = NativeEditorCommandAdapter.ClipTarget(track, clipId)
            val clip = state.timeline?.tracks?.withIndex()?.firstOrNull { (index, item) ->
                (item.engineIndex.takeIf { it >= 0 } ?: index) == track
            }?.value?.clips?.firstOrNull { it.id == clipId }
            if (clip != null) {
                val localTimeUs = (state.playheadUs - clip.startUs)
                    .coerceIn(0L, clip.durationUs)
                val job = controller.insertTransformKeyframe(
                    target, parameter, transform, localTimeUs
                )
                scope.launch {
                    job.join()
                    keyframeRevision += 1
                }
            }
        },
        onRemoveKeyframe = { track, clipId, parameter, timeUs ->
            val job = controller.removeKeyframe(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                parameter,
                timeUs
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onSetKeyframeEasing = { track, clipId, parameter, timeUs, easing, c1x, c1y, c2x, c2y ->
            val job = controller.setKeyframeEasing(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                parameter,
                timeUs,
                easing,
                c1x,
                c1y,
                c2x,
                c2y
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onClipSpeedChanged = { track, clipId, numerator, denominator ->
            val job = controller.setClipSpeed(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                numerator,
                denominator
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onSpeedCurvePreset = { track, clipId, preset ->
            val job = controller.applySpeedCurvePreset(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                preset
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onSpeedCurveChanged = { track, clipId, points ->
            val job = controller.setSpeedCurve(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                points
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onClipAudioChanged = { track, clipId, audio ->
            val job = controller.setClipAudio(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                audio
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onClipDenoiseChanged = { track, clipId, strength ->
            val job = controller.setClipDenoise(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                strength
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onClipFilterChanged = { track, clipId, style, intensity ->
            val job = controller.setClipFilter(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                style,
                intensity
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onClipLutChanged = { track, clipId, path, strength ->
            val job = controller.setClipLut(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                path,
                strength
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onClipColorGradeChanged = { track, clipId, grade ->
            val job = controller.setClipColorGrade(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                grade
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onClipMaskChanged = { track, clipId, mask ->
            val job = controller.setClipMask(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                mask
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onAddTransition = { track, fromClipId, toClipId, type, durationUs ->
            val job = controller.addTransition(track, fromClipId, toClipId, type, durationUs)
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onRemoveTransition = { track, transitionId ->
            val job = controller.removeTransition(track, transitionId)
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onCreateTextClip = { text, startUs, durationUs ->
            val job = controller.addTextClip(text, startUs, durationUs)
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onTextStyleChanged = { track, clipId, update ->
            val job = controller.setTextStyle(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                update
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onCreateSubtitle = { text, startUs, durationUs ->
            val job = controller.addSubtitle(text, startUs, durationUs)
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onRemoveSubtitle = { subtitleId ->
            val job = controller.removeSubtitle(subtitleId)
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        canUndo = state.canUndo,
        canRedo = state.canRedo,
        onUndo = { controller.undo() },
        onRedo = { controller.redo() },
        onOpenLegacy = onOpenLegacy,
        onQuickAction = { action ->
            when (action) {
                NativeQuickAction.ADD_MARKER -> controller.addMarker(state.playheadUs)
                NativeQuickAction.PREVIEW_RENDER -> previewController.requestRenderFrame(state.playheadUs)
                NativeQuickAction.STEP_PREVIOUS_TO_START -> seekTo(0L)
                NativeQuickAction.STEP_PREVIOUS -> {
                    val previous = state.timeline?.tracks
                        ?.flatMap { it.clips }
                        ?.map { it.startUs }
                        ?.filter { it < state.playheadUs }
                        ?.maxOrNull() ?: 0L
                    seekTo(previous)
                }
                else -> onOpenLegacy()
            }
        },
        onSplitClip = { track, clipId, timeUs ->
            controller.splitClip(track, clipId, timeUs)
        },
        onDeleteClip = { track, clipId -> controller.removeClip(track, clipId) },
        onDuplicateClip = { track, clipId -> controller.duplicateClip(track, clipId) },
        onRippleDeleteClip = { track, clipId -> controller.rippleDeleteClip(track, clipId) },
        onPrewarmPreviewFrame = { timeUs, _ ->
            previewController.requestRenderFrame(timeUs)
        },
        onShareProject = onOpenLegacy,
        onExportProject = onOpenLegacy,
        onExpandPreview = onOpenLegacy
    )
}
