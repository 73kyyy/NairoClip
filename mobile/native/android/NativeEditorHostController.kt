package com.nairoclip.app

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/**
 * First native editor host boundary.
 *
 * It owns no Activity and does not change the formal application entry point.
 * All mutations are serialized, executed off the UI thread, and published only
 * after NativeEditorCommandAdapter has read back the authoritative engine state.
 */
class NativeEditorHostController(
    private val adapter: NativeEditorCommandAdapter,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO
) {
    private val operationMutex = Mutex()
    private val trackToggleLock = Any()
    private enum class TrackToggle { VISIBILITY, MUTE, LOCK }
    private val pendingTrackToggleValues = mutableMapOf<Pair<Int, TrackToggle>, Boolean>()
    private val mutableState = MutableStateFlow(NativeEditorHostState())
    val state: StateFlow<NativeEditorHostState> = mutableState.asStateFlow()

    fun refresh(): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val result = withContext(ioDispatcher) { executeSafely { adapter.editorStateSnapshot() } }
            val snapshot = result.value
            if (result.ok && snapshot != null) {
                dispatch(NativeEditorHostEvent.SnapshotLoaded(snapshot))
            } else {
                dispatch(
                    NativeEditorHostEvent.SnapshotLoadFailed(
                        result.error ?: "读取原生工程状态失败"
                    )
                )
            }
        }
    }

    fun setPlayhead(timeUs: Long) {
        dispatch(NativeEditorHostEvent.SetPlayhead(timeUs))
    }

    fun selectClip(trackIndex: Int, clipId: Long) {
        dispatch(
            NativeEditorHostEvent.SelectClip(
                NativeEditorCommandAdapter.ClipTarget(trackIndex, clipId)
            )
        )
    }

    fun clearSelection() {
        dispatch(NativeEditorHostEvent.ClearSelection)
    }

    fun splitClip(trackIndex: Int, clipId: Long, timelineTimeUs: Long): Job {
        val target = NativeEditorCommandAdapter.ClipTarget(trackIndex, clipId)
        return runEditorMutation(
            operation = { adapter.splitClipAndReadEditorState(target, timelineTimeUs) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
        )
    }

    fun removeClip(trackIndex: Int, clipId: Long): Job {
        val target = NativeEditorCommandAdapter.ClipTarget(trackIndex, clipId)
        return runEditorMutation(
            operation = { adapter.removeClipAndReadEditorState(target) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it.editorState) }
        )
    }

    fun duplicateClip(trackIndex: Int, clipId: Long): Job {
        val target = NativeEditorCommandAdapter.ClipTarget(trackIndex, clipId)
        return runEditorMutation(
            operation = { adapter.duplicateClipAndReadEditorState(target) },
            onSuccess = { result ->
                NativeEditorHostEvent.MutationCommitted(
                    result.editorState,
                    result.newClipId?.let { target.copy(clipId = it) }
                )
            }
        )
    }

    fun rippleDeleteClip(trackIndex: Int, clipId: Long): Job {
        val target = NativeEditorCommandAdapter.ClipTarget(trackIndex, clipId)
        return runEditorMutation(
            operation = { adapter.rippleDeleteClipAndReadEditorState(target) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it.editorState) }
        )
    }

    fun setTrackVisibility(trackIndex: Int, visible: Boolean): Job =
        runTimelineMutation { adapter.setTrackVisibilityAndReadSnapshot(trackIndex, visible) }

    fun setTrackMuted(trackIndex: Int, muted: Boolean): Job =
        runTimelineMutation { adapter.setTrackMutedAndReadSnapshot(trackIndex, muted) }

    fun setTrackLocked(trackIndex: Int, locked: Boolean): Job =
        runTimelineMutation { adapter.setTrackLockedAndReadSnapshot(trackIndex, locked) }

    /**
     * Reserve the next toggle synchronously before launching its coroutine.
     * Rapid taps therefore build on the latest requested value, not a stale
     * Compose/AndroidView snapshot. The pending value is retired only after
     * the matching serialized engine mutation succeeds or fails.
     */
    fun toggleTrackVisibility(trackIndex: Int): Job =
        toggleTrackControl(trackIndex, TrackToggle.VISIBILITY) { value ->
            adapter.setTrackVisibilityAndReadSnapshot(trackIndex, value)
        }

    fun toggleTrackMuted(trackIndex: Int): Job =
        toggleTrackControl(trackIndex, TrackToggle.MUTE) { value ->
            adapter.setTrackMutedAndReadSnapshot(trackIndex, value)
        }

    fun toggleTrackLocked(trackIndex: Int): Job =
        toggleTrackControl(trackIndex, TrackToggle.LOCK) { value ->
            adapter.setTrackLockedAndReadSnapshot(trackIndex, value)
        }

    private fun toggleTrackControl(
        trackIndex: Int,
        control: TrackToggle,
        operation: (Boolean) -> NativeEditorCommandAdapter.Result<NativeTimelineSnapshot>
    ): Job {
        val key = trackIndex to control
        val target = synchronized(trackToggleLock) {
            val track = mutableState.value.timeline?.tracks?.withIndex()?.firstOrNull { (index, item) ->
                (item.engineIndex.takeIf { it >= 0 } ?: index) == trackIndex
            }?.value
            val confirmed = when (control) {
                TrackToggle.VISIBILITY -> track?.visible
                TrackToggle.MUTE -> track?.muted
                TrackToggle.LOCK -> track?.locked
            }
            val current = pendingTrackToggleValues[key] ?: confirmed
            if (current == null) null else !current.also { pendingTrackToggleValues[key] = it }
        } ?: return scope.launch {
            dispatch(NativeEditorHostEvent.MutationFailed("找不到轨道，无法切换轨道控制"))
        }

        return runTimelineMutation(
            operation = { operation(target) },
            pendingToggle = key to target
        )
    }

    fun addMarker(timeUs: Long, name: String = "标记", color: String = "#FFFFFF"): Job =
        runEditorMutation(
            operation = { adapter.addMarkerAndReadEditorState(timeUs, name, color) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
        )

    fun removeMarker(markerId: Long): Job =
        runEditorMutation(
            operation = { adapter.removeMarkerAndReadEditorState(markerId) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
        )

    fun undo(): Job =
        runEditorMutation(
            operation = { adapter.undoAndReadEditorState() },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
        )

    fun redo(): Job =
        runEditorMutation(
            operation = { adapter.redoAndReadEditorState() },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
        )

    fun setTransform(
        target: NativeEditorCommandAdapter.ClipTarget,
        transform: NativeTransform,
        enabled: Boolean = true,
        anchor: Int = NativeTransformRange.ANCHOR_CENTER
    ): Job = runUnitMutation {
        adapter.setTransform(target, transform, enabled, anchor)
    }

    fun setClipSpeed(
        target: NativeEditorCommandAdapter.ClipTarget,
        numerator: Long,
        denominator: Long
    ): Job = runEditorMutation(
        operation = { adapter.setClipSpeedAndReadEditorState(target, numerator, denominator) },
        onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
    )

    fun applySpeedCurvePreset(
        target: NativeEditorCommandAdapter.ClipTarget,
        preset: Int
    ): Job = runEditorMutation(
        operation = { adapter.applySpeedCurvePresetAndReadEditorState(target, preset) },
        onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
    )

    fun setSpeedCurve(
        target: NativeEditorCommandAdapter.ClipTarget,
        points: List<NativeEditorCommandAdapter.SpeedCurvePoint>
    ): Job = runEditorMutation(
        operation = { adapter.setSpeedCurveAndReadEditorState(target, points) },
        onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
    )

    fun setClipAudio(
        target: NativeEditorCommandAdapter.ClipTarget,
        audio: NativeEditorCommandAdapter.ClipAudioState
    ): Job = runEditorMutation(
        operation = { adapter.setClipAudioAndReadEditorState(target, audio) },
        onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
    )

    fun setClipDenoise(
        target: NativeEditorCommandAdapter.ClipTarget,
        strength: Int
    ): Job = runEditorMutation(
        operation = { adapter.setClipDenoiseAndReadEditorState(target, strength) },
        onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
    )

    fun setClipFilter(
        target: NativeEditorCommandAdapter.ClipTarget,
        style: Int,
        intensity: Double
    ): Job = runRefreshMutation {
        adapter.setClipFilterAndRead(target, style, intensity)
    }

    fun setClipLut(
        target: NativeEditorCommandAdapter.ClipTarget,
        path: String,
        strength: Double
    ): Job = runRefreshMutation {
        adapter.setClipLutAndRead(target, path, strength)
    }

    fun setClipColorGrade(
        target: NativeEditorCommandAdapter.ClipTarget,
        grade: NativeEditorCommandAdapter.ClipColorGradeState
    ): Job = runEditorMutation(
        operation = { adapter.setClipColorGradeAndReadEditorState(target, grade) },
        onSuccess = { NativeEditorHostEvent.MutationCommitted(it.editorState) }
    )

    fun setClipMask(
        target: NativeEditorCommandAdapter.ClipTarget,
        mask: NativeEditorCommandAdapter.ClipMaskState
    ): Job = runRefreshMutation {
        adapter.setClipMaskAndRead(target, mask)
    }

    fun addEffect(
        target: NativeEditorCommandAdapter.ClipTarget,
        type: Int,
        value: Double
    ): Job = runRefreshMutation {
        adapter.addEffect(target, type, value)
    }

    fun updateEffect(
        target: NativeEditorCommandAdapter.ClipTarget,
        effect: NativeClipEffect,
        value: Double
    ): Job = runUnitMutation {
        adapter.updateEffect(target, effect, value)
    }

    fun setEffectEnabled(
        target: NativeEditorCommandAdapter.ClipTarget,
        effectId: Long,
        enabled: Boolean
    ): Job = runUnitMutation {
        adapter.setEffectEnabled(target, effectId, enabled)
    }

    fun moveEffect(
        target: NativeEditorCommandAdapter.ClipTarget,
        effectId: Long,
        direction: Int
    ): Job = runUnitMutation {
        adapter.moveEffect(target, effectId, direction)
    }

    fun removeEffect(
        target: NativeEditorCommandAdapter.ClipTarget,
        effectId: Long
    ): Job = runUnitMutation {
        adapter.removeEffect(target, effectId)
    }

    fun addTransition(
        trackIndex: Int,
        fromClipId: Long,
        toClipId: Long,
        type: Int,
        durationUs: Long
    ): Job = runRefreshMutation {
        adapter.addTransition(trackIndex, fromClipId, toClipId, type, durationUs)
    }

    fun removeTransition(trackIndex: Int, transitionId: Long): Job = runUnitMutation {
        adapter.removeTransition(trackIndex, transitionId)
    }

    fun addTextClip(text: String, startUs: Long, durationUs: Long): Job =
        runEditorMutation(
            operation = { adapter.addTextClipAndReadEditorState(text, startUs, durationUs) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it.editorState) }
        )

    fun setTextStyle(
        target: NativeEditorCommandAdapter.ClipTarget,
        update: NativeEditorCommandAdapter.TextStyleUpdate
    ): Job = runEditorMutation(
        operation = { adapter.setTextStyleAndReadEditorState(target, update) },
        onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
    )

    fun addSubtitle(text: String, startUs: Long, durationUs: Long): Job =
        runEditorMutation(
            operation = { adapter.addSubtitleAndReadEditorState(text, startUs, durationUs) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
        )

    fun removeSubtitle(subtitleId: Long): Job =
        runEditorMutation(
            operation = { adapter.removeSubtitleAndReadEditorState(subtitleId) },
            onSuccess = { NativeEditorHostEvent.MutationCommitted(it) }
        )

    fun insertTransformKeyframe(
        target: NativeEditorCommandAdapter.ClipTarget,
        parameter: NativeParameter,
        transform: NativeTransform,
        clipLocalTimeUs: Long
    ): Job = runUnitMutation {
        adapter.insertTransformKeyframe(target, parameter, transform, clipLocalTimeUs)
    }

    fun removeKeyframe(
        target: NativeEditorCommandAdapter.ClipTarget,
        parameter: NativeParameter,
        clipLocalTimeUs: Long
    ): Job = runUnitMutation {
        adapter.removeKeyframe(target, parameter, clipLocalTimeUs)
    }

    fun setKeyframeEasing(
        target: NativeEditorCommandAdapter.ClipTarget,
        parameter: NativeParameter,
        clipLocalTimeUs: Long,
        easing: Int,
        c1x: Double,
        c1y: Double,
        c2x: Double,
        c2y: Double
    ): Job = runUnitMutation {
        adapter.setKeyframeEasing(target, parameter, clipLocalTimeUs, easing, c1x, c1y, c2x, c2y)
    }

    fun clearError() {
        dispatch(NativeEditorHostEvent.ClearError)
    }

    private fun runTimelineMutation(
        operation: () -> NativeEditorCommandAdapter.Result<NativeTimelineSnapshot>,
        pendingToggle: Pair<Pair<Int, TrackToggle>, Boolean>? = null
    ): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val result = withContext(ioDispatcher) { executeSafely(operation) }
            val snapshot = result.value
            if (result.ok && snapshot != null) {
                mutableState.value = mutableState.value.copy(
                    timeline = snapshot,
                    isRefreshing = false,
                    errorMessage = null
                ).normalized()
            } else {
                dispatch(
                    NativeEditorHostEvent.MutationFailed(
                        result.error ?: "轨道命令执行失败"
                    )
                )
            }
            pendingToggle?.let { (key, target) ->
                synchronized(trackToggleLock) {
                    if (pendingTrackToggleValues[key] == target) {
                        pendingTrackToggleValues.remove(key)
                    }
                }
            }
        }
    }

    private fun <T : Any> runEditorMutation(
        operation: () -> NativeEditorCommandAdapter.Result<T>,
        onSuccess: (T) -> NativeEditorHostEvent
    ): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val result = withContext(ioDispatcher) { executeSafely(operation) }
            val value = result.value
            if (result.ok && value != null) {
                dispatch(onSuccess(value))
            } else {
                dispatch(
                    NativeEditorHostEvent.MutationFailed(
                        result.error ?: "原生编辑命令执行失败"
                    )
                )
            }
        }
    }

    private fun <T : Any> runRefreshMutation(
        operation: () -> NativeEditorCommandAdapter.Result<T>
    ): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val changed = withContext(ioDispatcher) { executeSafely(operation) }
            if (!changed.ok || changed.value == null) {
                dispatch(
                    NativeEditorHostEvent.MutationFailed(
                        changed.error ?: "原生编辑命令执行失败"
                    )
                )
                return@withLock
            }
            val refreshed = withContext(ioDispatcher) { executeSafely { adapter.editorStateSnapshot() } }
            val snapshot = refreshed.value
            if (refreshed.ok && snapshot != null) {
                dispatch(NativeEditorHostEvent.MutationCommitted(snapshot))
            } else {
                dispatch(
                    NativeEditorHostEvent.SnapshotLoadFailed(
                        refreshed.error ?: "编辑已提交，但刷新工程状态失败"
                    )
                )
            }
        }
    }

    private fun runUnitMutation(
        operation: () -> NativeEditorCommandAdapter.Result<Unit>
    ): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val changed = withContext(ioDispatcher) { executeSafely(operation) }
            if (!changed.ok) {
                dispatch(
                    NativeEditorHostEvent.MutationFailed(
                        changed.error ?: "原生编辑命令执行失败"
                    )
                )
                return@withLock
            }
            val refreshed = withContext(ioDispatcher) { executeSafely { adapter.editorStateSnapshot() } }
            val snapshot = refreshed.value
            if (refreshed.ok && snapshot != null) {
                dispatch(NativeEditorHostEvent.MutationCommitted(snapshot))
            } else {
                dispatch(
                    NativeEditorHostEvent.SnapshotLoadFailed(
                        refreshed.error ?: "编辑已提交，但刷新工程状态失败"
                    )
                )
            }
        }
    }

    /**
     * JNI/bridge adapters normally return a typed failure, but platform and
     * native-boundary exceptions can still escape. Convert ordinary failures
     * into the same state error path so the host never remains stuck in
     * isRefreshing=true. Coroutine cancellation must remain cooperative.
     */
    private fun <T> executeSafely(
        operation: () -> NativeEditorCommandAdapter.Result<T>
    ): NativeEditorCommandAdapter.Result<T> = try {
        operation()
    } catch (cancelled: CancellationException) {
        throw cancelled
    } catch (failure: Exception) {
        NativeEditorCommandAdapter.Result(
            ok = false,
            error = failure.message?.takeIf { it.isNotBlank() }
                ?: "原生编辑操作异常：" + failure.javaClass.simpleName
        )
    }

    private fun dispatch(event: NativeEditorHostEvent) {
        mutableState.value = NativeEditorHostReducer.reduce(mutableState.value, event)
    }
}
