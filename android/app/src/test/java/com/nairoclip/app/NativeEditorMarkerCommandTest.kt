package com.nairoclip.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorMarkerCommandTest {
    @Test
    fun rejectsNegativeMarkerTimeBeforeBridgeDispatch() {
        val error = NativeMarkerInputRules.validateAdd(-1L, "标记 1", "#FFFFFF")
        assertTrue(error?.contains("不能为负数") == true)
    }

    @Test
    fun rejectsBlankOrOversizedMarkerNamesBeforeBridgeDispatch() {
        val blank = NativeMarkerInputRules.validateAdd(0L, "  ", "#FFFFFF")
        val oversized = NativeMarkerInputRules.validateAdd(0L, "标".repeat(65), "#FFFFFF")
        assertFalse(blank == null)
        assertFalse(oversized == null)
    }

    @Test
    fun rejectsInvalidMarkerColorBeforeBridgeDispatch() {
        val error = NativeMarkerInputRules.validateAdd(0L, "标记 1", "blue")
        assertTrue(error?.contains("颜色格式") == true)
    }

    @Test
    fun rejectsNonPositiveMarkerIdsBeforeBridgeDispatch() {
        val zero = NativeMarkerInputRules.validateRemove(0L)
        val negative = NativeMarkerInputRules.validateRemove(-1L)
        assertFalse(zero == null)
        assertFalse(negative == null)
        assertTrue(zero?.contains("ID") == true)
        assertTrue(negative?.contains("ID") == true)
    }

    @Test
    fun acceptsValidMarkerInput() {
        assertTrue(NativeMarkerInputRules.validateAdd(0L, " 标记 1 ", "#aabbcc") == null)
        assertTrue(NativeMarkerInputRules.validateRemove(1L) == null)
    }
}
