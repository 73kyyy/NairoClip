package com.nairoclip.app

import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
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
import java.io.File
import java.util.UUID

/**
 * Feature-scoped native editor host. MainActivity can mount it only through the
 * debug-only, intent-gated S5 pilot route. The formal Activity/launcher entry
 * and the retained WebView fallback remain unchanged.
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
    onOpenLegacyAction: ((NativeQuickAction) -> Unit)? = null,
    suppliedPreviewView: PreviewSurfaceView? = null,
    resolveAssetUri: (Long) -> Uri? = { null },
    speechWorker: NativeSpeechSessionWorker? = null,
    speechSessionReady: Boolean = false,
    autoSubtitleState: NativeAutoSubtitleUiState? = null,
    onSelectedSpeechSourceChanged: ((Uri?, NativeSpeechSourceRange?) -> Unit)? = null,
    onStartAutoSubtitle: (() -> Unit)? = null,
    onCancelAutoSubtitle: (() -> Unit)? = null,
    modifier: Modifier = Modifier
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val adapter = remember(bridge) { NativeEditorCommandAdapter(bridge) }
    val controller = remember(bridge, scope) {
        NativeEditorHostController(adapter, scope)
    }
    val state by controller.state.collectAsState()
    var isImportingImages by remember(projectName) { mutableStateOf(false) }
    var imagePanelMessage by remember(projectName) { mutableStateOf<String?>(null) }
    val imageDirectory = remember(context) {
        File(context.filesDir, "nairoclip/assets/images").apply { mkdirs() }
    }
    val imagePickerLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.OpenMultipleDocuments()
    ) { selectedUris ->
        if (selectedUris.isNotEmpty()) {
            scope.launch {
                isImportingImages = true
                imagePanelMessage = null
                var imported = 0
                val failures = mutableListOf<String>()
                selectedUris.forEach { uri ->
                    val failure = withContext(Dispatchers.IO) {
                        runCatching {
                            val displayName = context.contentResolver.query(
                                uri,
                                arrayOf(OpenableColumns.DISPLAY_NAME),
                                null,
                                null,
                                null
                            )?.use { cursor ->
                                if (cursor.moveToFirst()) cursor.getString(0) else null
                            }?.takeIf { it.isNotBlank() }
                                ?: uri.lastPathSegment?.substringAfterLast('/')?.takeIf { it.isNotBlank() }
                                ?: "图片"
                            val safeName = displayName
                                .substringAfterLast('/')
                                .replace(Regex("[^\\p{L}\\p{N}._-]"), "_")
                                .take(120)
                                .ifBlank { "image" }
                            if (!imageDirectory.exists() && !imageDirectory.mkdirs()) {
                                throw IllegalStateException("无法创建应用图片素材目录")
                            }
                            val destination = File(imageDirectory, "${UUID.randomUUID()}_${safeName}")
                            val input = context.contentResolver.openInputStream(uri)
                                ?: throw IllegalStateException("无法读取所选图片")
                            input.use { source ->
                                destination.outputStream().use { output -> source.copyTo(output) }
                            }
                            val result = adapter.importImageAsset(displayName, destination.absolutePath)
                            if (!result.ok) {
                                destination.delete()
                                throw IllegalStateException(result.error ?: "引擎拒绝导入图片")
                            }
                        }.exceptionOrNull()?.message
                    }
                    if (failure == null) imported++ else failures += failure
                }
                if (imported > 0) controller.refresh()
                isImportingImages = false
                imagePanelMessage = when {
                    failures.isEmpty() && imported > 0 -> "已导入 $imported 张图片"
                    failures.isEmpty() -> "没有导入图片"
                    imported > 0 -> "已导入 $imported 张；失败 ${failures.size} 张：${failures.first()}"
                    else -> "导入失败：${failures.first()}"
                }
            }
        }
    }
    var localAutoSubtitleState by remember { mutableStateOf(NativeAutoSubtitleUiState()) }
    val autoSubtitleCoordinator = remember(context, speechWorker, adapter) {
        speechWorker?.let { worker ->
            NativeAutoSubtitleCoordinator(
                context = context,
                worker = worker,
                adapter = adapter,
                onStateChanged = { updated ->
                    localAutoSubtitleState = updated
                }
            )
        }
    }
    val resolvedAutoSubtitleState = if (autoSubtitleCoordinator != null) {
        localAutoSubtitleState
    } else {
        autoSubtitleState ?: localAutoSubtitleState
    }
    val previewView = remember(context, suppliedPreviewView) {
        suppliedPreviewView ?: PreviewSurfaceView(context)
    }
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
    var clipEffectSpecs by remember { mutableStateOf<Map<Int, NativeEditorCommandAdapter.EffectSpec>>(emptyMap()) }
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

    LaunchedEffect(autoSubtitleCoordinator, speechSessionReady) {
        autoSubtitleCoordinator?.setSessionReady(speechSessionReady)
    }

    var previousAutoSubtitleRunning by remember { mutableStateOf(false) }
    LaunchedEffect(resolvedAutoSubtitleState.running) {
        val runningNow = resolvedAutoSubtitleState.running
        if (previousAutoSubtitleRunning && !runningNow) {
            controller.refresh()
            keyframeRevision += 1
        }
        previousAutoSubtitleRunning = runningNow
    }

    LaunchedEffect(state.timeline) {
        if (state.timeline != null) {
            previewController.requestRenderFrame(state.playheadUs)
        }
    }

    LaunchedEffect(
        state.selectedClip,
        state.timeline,
        resolveAssetUri,
        onSelectedSpeechSourceChanged,
        resolvedAutoSubtitleState.running,
        autoSubtitleCoordinator
    ) {
        if (resolvedAutoSubtitleState.running) return@LaunchedEffect
        val target = state.selectedClip
        val clip = if (target == null) {
            null
        } else {
            state.timeline?.tracks?.withIndex()?.firstOrNull { (index, item) ->
                (item.engineIndex.takeIf { it >= 0 } ?: index) == target.trackIndex
            }?.value?.clips?.firstOrNull { it.id == target.clipId }
        }
        if (clip == null) {
            autoSubtitleCoordinator?.setSource(null, null)
            onSelectedSpeechSourceChanged?.invoke(null, null)
        } else {
            // URI-grant verification may invoke a document provider. Keep
            // that I/O off the Compose main thread; an unavailable grant
            // resolves to null and the subtitle coordinator reports it honestly.
            val uri = withContext(Dispatchers.IO) {
                runCatching {
                    clip.assetId.takeIf { it > 0L }?.let(resolveAssetUri)
                }.getOrNull()
            }
            val range = NativeSpeechSourceRange.fromTimelineClip(clip.startUs, clip)
            autoSubtitleCoordinator?.setSource(uri, range)
            onSelectedSpeechSourceChanged?.invoke(uri, range)
        }
    }

    LaunchedEffect(adapter) {
        val countResult = withContext(Dispatchers.IO) { adapter.readTransitionTypeCount() }
        val count = countResult.value
        if (countResult.ok && count != null) {
            val types = mutableListOf<Pair<Int, String>>()
            for (type in 0 until count) {
                val result = withContext(Dispatchers.IO) {
                    adapter.readTransitionTypeName(type)
                }
                val name = result.value
                if (result.ok && !name.isNullOrBlank()) types += type to name
            }
            transitionTypes = types
        }
        val baseSpecs = linkedMapOf<Int, NativeEditorCommandAdapter.EffectSpec>()
        for (effectType in 0..13) {
            val result = withContext(Dispatchers.IO) { adapter.readEffectSpec(effectType) }
            val spec = result.value
            if (result.ok && spec != null) baseSpecs[effectType] = spec
        }
        clipEffectSpecs = clipEffectSpecs + baseSpecs
    }

    LaunchedEffect(state.timeline, state.history, keyframeRevision) {
        val result = withContext(Dispatchers.IO) { adapter.readSubtitles() }
        subtitleCues = if (result.ok) result.value.orEmpty() else emptyList()
    }

    LaunchedEffect(state.selectedClip, state.timeline, state.history, keyframeRevision, clipEffectSpecs) {
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
            val selectedClipForEffects = state.timeline?.tracks?.withIndex()?.firstOrNull { (index, item) ->
                (item.engineIndex.takeIf { it >= 0 } ?: index) == target.trackIndex
            }?.value?.clips?.firstOrNull { it.id == target.clipId }
            val missingEffectTypes = selectedClipForEffects?.effects
                ?.map { it.type }
                ?.distinct()
                ?.filterNot { it in clipEffectSpecs }
                .orEmpty()
            if (missingEffectTypes.isNotEmpty()) {
                val loadedEffectSpecs = clipEffectSpecs.toMutableMap()
                missingEffectTypes.forEach { effectType ->
                    val specResult = withContext(Dispatchers.IO) { adapter.readEffectSpec(effectType) }
                    val spec = specResult.value
                    if (specResult.ok && spec != null) loadedEffectSpecs[effectType] = spec
                }
                clipEffectSpecs = clipEffectSpecs + loadedEffectSpecs
            }
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

    DisposableEffect(previewController, autoSubtitleCoordinator) {
        previewController.onSurfaceAttached = {
            previewController.requestRenderFrame(latestPlayheadUs)
        }
        onDispose {
            previewController.onSurfaceAttached = null
            previewController.dispose()
            autoSubtitleCoordinator?.close()
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
        onTrackVisibilityChanged = controller::toggleTrackVisibility,
        onTrackMuteChanged = controller::toggleTrackMuted,
        onTrackLockChanged = controller::toggleTrackLocked,
        imageAssets = state.timeline?.assets.orEmpty(),
        backgroundRgba = state.timeline?.backgroundRgba,
        isImportingImages = isImportingImages,
        imagePanelMessage = imagePanelMessage,
        onPickImages = { imagePickerLauncher.launch(arrayOf("image/*")) },
        onRemoveImage = { assetId ->
            val assetPath = state.timeline?.assets?.firstOrNull { it.id == assetId }?.path
            scope.launch {
                val result = withContext(Dispatchers.IO) { adapter.removeImageAsset(assetId) }
                if (result.ok) {
                    if (!assetPath.isNullOrBlank()) {
                        runCatching {
                            val root = imageDirectory.canonicalFile
                            val target = File(assetPath).canonicalFile
                            if (target.path.startsWith(root.path + File.separator)) target.delete()
                        }
                    }
                    controller.refresh()
                    imagePanelMessage = null
                } else {
                    imagePanelMessage = result.error ?: "删除图片失败"
                }
            }
        },
        onSetBackgroundColor = { rgba ->
            scope.launch {
                val result = withContext(Dispatchers.IO) { adapter.setCanvasBackground(rgba) }
                if (result.ok) {
                    controller.refresh()
                    imagePanelMessage = null
                } else {
                    imagePanelMessage = result.error ?: "设置背景色失败"
                }
            }
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
        clipEffectSpecs = clipEffectSpecs,
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
        onValidateClipLut = { path -> adapter.validateLutFile(path) },
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
        onAddClipEffect = { track, clipId, effectType ->
            val value = clipEffectSpecs[effectType]?.defaultValue ?: 0.0
            val job = controller.addEffect(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                effectType,
                value
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onUpdateClipEffect = { track, clipId, effect, value ->
            val job = controller.updateEffect(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                effect,
                value
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onSetClipEffectEnabled = { track, clipId, effectId, enabled ->
            val job = controller.setEffectEnabled(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                effectId,
                enabled
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onMoveClipEffect = { track, clipId, effectId, direction ->
            val job = controller.moveEffect(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                effectId,
                direction
            )
            scope.launch {
                job.join()
                keyframeRevision += 1
            }
        },
        onRemoveClipEffect = { track, clipId, effectId ->
            val job = controller.removeEffect(
                NativeEditorCommandAdapter.ClipTarget(track, clipId),
                effectId
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
                else -> onOpenLegacyAction?.invoke(action) ?: onOpenLegacy()
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
        autoSubtitleState = resolvedAutoSubtitleState,
        onStartAutoSubtitle = onStartAutoSubtitle ?: {
            autoSubtitleCoordinator?.start()
            Unit
        },
        onCancelAutoSubtitle = onCancelAutoSubtitle ?: {
            autoSubtitleCoordinator?.cancel()
            Unit
        },
        onShareProject = onOpenLegacy,
        onExportProject = onOpenLegacy,
        onExpandPreview = onOpenLegacy
    )
}
