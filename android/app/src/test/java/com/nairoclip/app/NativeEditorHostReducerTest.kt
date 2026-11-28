package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorHostReducerTest {
    private fun timeline(
        durationUs: Long = 1_000_000L,
        visible: Boolean = true,
        locked: Boolean = false,
        clips: List<NativeTimelineClip> = listOf(
            NativeTimelineClip(id = 10L, startUs = 0L, durationUs = 500_000L, label = "片段")
        )
    ) = NativeTimelineSnapshot(
        projectName = "测试工程",
        durationUs = durationUs,
        tracks = listOf(
            NativeTimelineTrack(
                id = 1L,
                name = "视频 1",
                clips = clips,
                visible = visible,
                locked = locked,
                engineIndex = 0
            )
        )
    )

    private fun editorSnapshot(
        durationUs: Long = 1_000_000L,
        visible: Boolean = true,
        locked: Boolean = false,
        canUndo: Boolean = false,
        canRedo: Boolean = false,
        clips: List<NativeTimelineClip> = listOf(
            NativeTimelineClip(id = 10L, startUs = 0L, durationUs = 500_000L, label = "片段")
        )
    ) = NativeEditorCommandAdapter.EditorStateSnapshot(
        timeline = timeline(durationUs, visible, locked, clips),
        history = NativeEditorCommandAdapter.HistoryState(canUndo, canRedo)
    )

    private val target = NativeEditorCommandAdapter.ClipTarget(trackIndex = 0, clipId = 10L)

    @Test
    fun failedRefreshKeepsLastKnownGoodTimelineAndSelection() {
        val initial = NativeEditorHostReducer.reduce(
            NativeEditorHostState(),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val selected = NativeEditorHostReducer.reduce(initial, NativeEditorHostEvent.SelectClip(target))
        val refreshing = NativeEditorHostReducer.reduce(selected, NativeEditorHostEvent.RefreshStarted)
        val failed = NativeEditorHostReducer.reduce(
            refreshing,
            NativeEditorHostEvent.SnapshotLoadFailed("读取失败")
        )

        assertEquals(initial.timeline, failed.timeline)
        assertEquals(target, failed.selectedClip)
        assertFalse(failed.isRefreshing)
        assertEquals("读取失败", failed.errorMessage)
    }

    @Test
    fun refreshedHiddenTrackInvalidatesSelectionAndClampsPlayhead() {
        val loaded = NativeEditorHostReducer.reduce(
            NativeEditorHostState(playheadUs = 900_000L),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val selected = NativeEditorHostReducer.reduce(loaded, NativeEditorHostEvent.SelectClip(target))
        val updated = NativeEditorHostReducer.reduce(
            selected,
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot(durationUs = 400_000L, visible = false))
        )

        assertNull(updated.selectedClip)
        assertEquals(400_000L, updated.playheadUs)
    }

    @Test
    fun lockingTrackInvalidatesSelectionAndRejectsFutureSelection() {
        val loaded = NativeEditorHostReducer.reduce(
            NativeEditorHostState(),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val selected = NativeEditorHostReducer.reduce(
            loaded,
            NativeEditorHostEvent.SelectClip(target)
        )
        assertEquals(target, selected.selectedClip)

        val locked = NativeEditorHostReducer.reduce(
            selected,
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot(locked = true))
        )
        assertNull(locked.selectedClip)

        val rejected = NativeEditorHostReducer.reduce(
            locked,
            NativeEditorHostEvent.SelectClip(target)
        )
        assertNull(rejected.selectedClip)
        assertTrue(rejected.errorMessage?.contains("锁定") == true)
    }

    @Test
    fun mutationPublishesTimelineAndHistoryFromOneSnapshot() {
        val initial = NativeEditorHostReducer.reduce(
            NativeEditorHostState(),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val committed = NativeEditorHostReducer.reduce(
            initial,
            NativeEditorHostEvent.MutationCommitted(
                snapshot = editorSnapshot(canUndo = true, canRedo = false),
                preferredSelection = NativeEditorCommandAdapter.ClipTarget(0, 11L)
            )
        )

        assertTrue(committed.canUndo)
        assertFalse(committed.canRedo)
        assertNull(committed.selectedClip) // The preferred ID must exist in the committed engine snapshot.
        assertEquals(1_000_000L, committed.timeline?.durationUs)
    }

    @Test
    fun validPreferredSelectionSurvivesCommittedMutation() {
        val initial = NativeEditorHostReducer.reduce(
            NativeEditorHostState(),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val committed = NativeEditorHostReducer.reduce(
            initial,
            NativeEditorHostEvent.MutationCommitted(
                snapshot = editorSnapshot(canUndo = true),
                preferredSelection = target
            )
        )

        assertEquals(target, committed.selectedClip)
        assertTrue(committed.canUndo)
    }

    @Test
    fun validSelectionClearsPreviousSelectionError() {
        val loaded = NativeEditorHostReducer.reduce(
            NativeEditorHostState(),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val invalid = NativeEditorHostReducer.reduce(
            loaded,
            NativeEditorHostEvent.SelectClip(NativeEditorCommandAdapter.ClipTarget(0, 999L))
        )
        assertTrue(invalid.errorMessage?.isNotBlank() == true)

        val recovered = NativeEditorHostReducer.reduce(
            invalid,
            NativeEditorHostEvent.SelectClip(target)
        )

        assertEquals(target, recovered.selectedClip)
        assertNull(recovered.errorMessage)
    }

    @Test
    fun clearingSelectionAlsoClearsStaleSelectionError() {
        val loaded = NativeEditorHostReducer.reduce(
            NativeEditorHostState(),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val invalid = NativeEditorHostReducer.reduce(
            loaded,
            NativeEditorHostEvent.SelectClip(NativeEditorCommandAdapter.ClipTarget(0, 999L))
        )
        assertTrue(invalid.errorMessage?.isNotBlank() == true)

        val cleared = NativeEditorHostReducer.reduce(
            invalid,
            NativeEditorHostEvent.ClearSelection
        )

        assertNull(cleared.selectedClip)
        assertNull(cleared.errorMessage)
    }

    @Test
    fun invalidSelectionIsRejectedAndPlayheadIsBounded() {
        val loaded = NativeEditorHostReducer.reduce(
            NativeEditorHostState(),
            NativeEditorHostEvent.SnapshotLoaded(editorSnapshot())
        )
        val invalid = NativeEditorHostReducer.reduce(
            loaded,
            NativeEditorHostEvent.SelectClip(NativeEditorCommandAdapter.ClipTarget(0, 999L))
        )
        val moved = NativeEditorHostReducer.reduce(
            invalid,
            NativeEditorHostEvent.SetPlayhead(Long.MAX_VALUE)
        )

        assertNull(invalid.selectedClip)
        assertEquals(1_000_000L, moved.playheadUs)
        assertTrue(invalid.errorMessage?.isNotBlank() == true)
    }
}
