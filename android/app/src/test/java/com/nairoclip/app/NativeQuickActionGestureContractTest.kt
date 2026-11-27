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
}
