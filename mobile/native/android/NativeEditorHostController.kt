package com.nairoclip.app

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
    private val mutableState = MutableStateFlow(NativeEditorHostState())
    val state: StateFlow<NativeEditorHostState> = mutableState.asStateFlow()

    fun refresh(): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val result = withContext(ioDispatcher) { adapter.editorStateSnapshot() }
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
        operation: () -> NativeEditorCommandAdapter.Result<NativeTimelineSnapshot>
    ): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val result = withContext(ioDispatcher) { operation() }
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
        }
    }

    private fun <T : Any> runEditorMutation(
        operation: () -> NativeEditorCommandAdapter.Result<T>,
        onSuccess: (T) -> NativeEditorHostEvent
    ): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val result = withContext(ioDispatcher) { operation() }
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

    private fun runUnitMutation(
        operation: () -> NativeEditorCommandAdapter.Result<Unit>
    ): Job = scope.launch {
        operationMutex.withLock {
            dispatch(NativeEditorHostEvent.RefreshStarted)
            val changed = withContext(ioDispatcher) { operation() }
            if (!changed.ok) {
                dispatch(
                    NativeEditorHostEvent.MutationFailed(
                        changed.error ?: "原生编辑命令执行失败"
                    )
                )
                return@withLock
            }
            val refreshed = withContext(ioDispatcher) { adapter.editorStateSnapshot() }
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

    private fun dispatch(event: NativeEditorHostEvent) {
        mutableState.value = NativeEditorHostReducer.reduce(mutableState.value, event)
    }
}
