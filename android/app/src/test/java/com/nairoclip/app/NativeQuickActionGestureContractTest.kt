package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeQuickActionGestureContractTest {
    @Test
    fun stepPreviousDoubleTapHasDistinctJumpToStartCommand() {
        assertEquals(
            NativeQuickAction.STEP_PREVIOUS_TO_START,
            NativeQuickActionGestureContract.doubleTap(NativeQuickAction.STEP_PREVIOUS)
        )
        assertNull(
            NativeQuickActionGestureContract.doubleTap(NativeQuickAction.IMPORT_MEDIA)
        )
    }

    @Test
    fun markerLongPressHasDistinctOpenMarkerListCommand() {
        assertEquals(
            NativeQuickAction.SHOW_MARKER_LIST,
            NativeQuickActionGestureContract.longPress(NativeQuickAction.ADD_MARKER)
        )
        assertNull(
            NativeQuickActionGestureContract.longPress(NativeQuickAction.AI_EDIT)
        )
    }

    @Test
    fun previousTapMovesToNearestEarlierClipBoundary() {
        val track = NativeTimelineTrack(
            id = 1L,
            name = "视频",
            clips = listOf(
                NativeTimelineClip(id = 10L, startUs = 0L, durationUs = 500_000L, label = "A"),
                NativeTimelineClip(id = 11L, startUs = 800_000L, durationUs = 700_000L, label = "B")
            ),
            engineIndex = 0
        )
        assertEquals(
            800_000L,
            NativeQuickActionGestureContract.previousTimelineAnchor(listOf(track), 1_000_000L)
        )
        assertEquals(
            500_000L,
            NativeQuickActionGestureContract.previousTimelineAnchor(listOf(track), 800_000L)
        )
        assertNull(NativeQuickActionGestureContract.previousTimelineAnchor(listOf(track), 500_000L))
    }

}
