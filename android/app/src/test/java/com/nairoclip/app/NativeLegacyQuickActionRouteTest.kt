package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeLegacyQuickActionRouteTest {
    @Test
    fun mapsOnlySupportedLegacyActionsToExistingEditorSelectors() {
        assertEquals("import", NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.IMPORT_MEDIA))
        assertEquals("home", NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.RETURN_HOME))
        assertEquals("images", NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.IMAGES))
        assertEquals("ai", NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.AI_EDIT))
    }

    @Test
    fun nativeActionsDoNotAccidentallyRouteBackToWebView() {
        assertNull(NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.STEP_PREVIOUS))
        assertNull(NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.ADD_MARKER))
        assertNull(NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.PREVIEW_RENDER))
        assertNull(NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.KEYFRAMES))
        assertNull(NativeLegacyQuickActionRoute.dataAction(NativeQuickAction.TOGGLE_GUIDES))
    }
}
