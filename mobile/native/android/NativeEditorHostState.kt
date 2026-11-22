package com.nairoclip.app

/**
 * Immutable state contract for the future native-editor host (Activity/ViewModel).
 *
 * The engine remains authoritative: a mutation is published only with the
 * timeline/history snapshot read back by NativeEditorCommandAdapter. Failed
 * refreshes retain the last known-good timeline instead of replacing it with
 * an empty project. This file intentionally has no Activity, View, JNI, or
 * lifecycle ownership and can be consumed by a later host without changing
 * the editor composables.
 */
data class NativeEditorHostState(
    val timeline: NativeTimelineSnapshot? = null,
    val history: NativeEditorCommandAdapter.HistoryState =
        NativeEditorCommandAdapter.HistoryState(canUndo = false, canRedo = false),
    val selectedClip: NativeEditorCommandAdapter.ClipTarget? = null,
    val playheadUs: Long = 0L,
    val isRefreshing: Boolean = false,
    val errorMessage: String? = null
) {
    val canUndo: Boolean get() = history.canUndo
    val canRedo: Boolean get() = history.canRedo

    fun containsVisibleEnabledSelection(target: NativeEditorCommandAdapter.ClipTarget): Boolean {
        val snapshot = timeline ?: return false
        val track = snapshot.tracks.firstOrNull { it.engineIndex == target.trackIndex } ?: return false
        return track.visible && track.clips.any { it.id == target.clipId && it.enabled }
    }

    fun normalized(): NativeEditorHostState {
        val snapshot = timeline ?: return copy(selectedClip = null, playheadUs = 0L)
        val selection = selectedClip?.takeIf(::containsVisibleEnabledSelection)
        return copy(
            selectedClip = selection,
            playheadUs = playheadUs.coerceIn(0L, snapshot.durationUs)
        )
    }
}

/** Events are values; the host performs I/O and dispatches the resulting event. */
sealed interface NativeEditorHostEvent {
    data object RefreshStarted : NativeEditorHostEvent
    data class SnapshotLoaded(
        val snapshot: NativeEditorCommandAdapter.EditorStateSnapshot
    ) : NativeEditorHostEvent
    data class SnapshotLoadFailed(val message: String) : NativeEditorHostEvent
    data class SelectClip(val target: NativeEditorCommandAdapter.ClipTarget) : NativeEditorHostEvent
    data object ClearSelection : NativeEditorHostEvent
    data class SetPlayhead(val timeUs: Long) : NativeEditorHostEvent
    data class MutationCommitted(
        val snapshot: NativeEditorCommandAdapter.EditorStateSnapshot,
        /** Used by duplicate/split commands when the engine returns a new clip ID. */
        val preferredSelection: NativeEditorCommandAdapter.ClipTarget? = null
    ) : NativeEditorHostEvent
    data class MutationFailed(val message: String) : NativeEditorHostEvent
    data object ClearError : NativeEditorHostEvent
}

/**
 * Pure reducer: no side effects, bridge calls, or Compose state. Hosts should
 * dispatch a committed event only after the adapter has returned a successful
 * engine readback, keeping timeline, selection and Undo/Redo in one revision.
 */
object NativeEditorHostReducer {
    fun reduce(
        current: NativeEditorHostState,
        event: NativeEditorHostEvent
    ): NativeEditorHostState = when (event) {
        NativeEditorHostEvent.RefreshStarted ->
            current.copy(isRefreshing = true, errorMessage = null)

        is NativeEditorHostEvent.SnapshotLoaded ->
            current.copy(
                timeline = event.snapshot.timeline,
                history = event.snapshot.history,
                isRefreshing = false,
                errorMessage = null
            ).normalized()

        is NativeEditorHostEvent.SnapshotLoadFailed ->
            current.copy(isRefreshing = false, errorMessage = event.message)

        is NativeEditorHostEvent.SelectClip -> {
            val candidate = current.copy(selectedClip = event.target).normalized()
            if (candidate.selectedClip == event.target) candidate.copy(errorMessage = null)
            else current.copy(errorMessage = "所选片段已不存在、已禁用或所属轨道已隐藏")
        }

        NativeEditorHostEvent.ClearSelection ->
            current.copy(selectedClip = null, errorMessage = null)

        is NativeEditorHostEvent.SetPlayhead ->
            current.copy(
                playheadUs = event.timeUs.coerceAtLeast(0L)
            ).normalized()

        is NativeEditorHostEvent.MutationCommitted ->
            current.copy(
                timeline = event.snapshot.timeline,
                history = event.snapshot.history,
                selectedClip = event.preferredSelection ?: current.selectedClip,
                isRefreshing = false,
                errorMessage = null
            ).normalized()

        is NativeEditorHostEvent.MutationFailed ->
            current.copy(isRefreshing = false, errorMessage = event.message)

        NativeEditorHostEvent.ClearError ->
            current.copy(errorMessage = null)
    }
}
