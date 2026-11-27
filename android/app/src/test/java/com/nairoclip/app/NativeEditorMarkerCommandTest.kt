package com.nairoclip.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorMarkerCommandTest {
    private val adapter = NativeEditorCommandAdapter(NativeEngineBridge())

    @Test
    fun rejectsNegativeMarkerTimeBeforeBridgeDispatch() {
        val result = adapter.addMarkerAndReadEditorState(-1L, "标记 1")
        assertFalse(result.ok)
        assertTrue(result.error?.contains("不能为负数") == true)
    }

    @Test
    fun rejectsBlankOrOversizedMarkerNamesBeforeBridgeDispatch() {
        val blank = adapter.addMarkerAndReadEditorState(0L, "  ")
        val oversized = adapter.addMarkerAndReadEditorState(0L, "标".repeat(65))
        assertFalse(blank.ok)
        assertFalse(oversized.ok)
    }

    @Test
    fun rejectsInvalidMarkerColorBeforeBridgeDispatch() {
        val result = adapter.addMarkerAndReadEditorState(0L, "标记 1", "blue")
        assertFalse(result.ok)
        assertTrue(result.error?.contains("颜色格式") == true)
    }

    @Test
    fun rejectsNonPositiveMarkerIdsBeforeBridgeDispatch() {
        val zero = adapter.removeMarkerAndReadEditorState(0L)
        val negative = adapter.removeMarkerAndReadEditorState(-1L)
        assertFalse(zero.ok)
        assertFalse(negative.ok)
        assertTrue(zero.error?.contains("ID") == true)
        assertTrue(negative.error?.contains("ID") == true)
    }
}
