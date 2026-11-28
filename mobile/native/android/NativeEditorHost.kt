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

    LaunchedEffect(state.selectedClip, state.timeline, keyframeRevision) {
        val target = state.selectedClip
        if (target == null) {
            transform = NativeTransform()
            anchor = NativeTransformRange.ANCHOR_CENTER
            keyframes = emptyMap()
        } else {
            val transformResult = withContext(Dispatchers.IO) { adapter.getTransform(target) }
            if (transformResult.ok && transformResult.value != null) {
                transform = transformResult.value.transform
                anchor = transformResult.value.anchor
            }
            val loadedKeyframes = linkedMapOf<NativeParameter, List<NativeEditorCommandAdapter.Keyframe>>()
            NativeParameter.values().forEach { parameter ->
                val result = withContext(Dispatchers.IO) { adapter.keyframes(target, parameter) }
                val frames = result.value
                if (result.ok && !frames.isNullOrEmpty()) {
                    loadedKeyframes[parameter] = frames
                }
            }
            keyframes = loadedKeyframes
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
