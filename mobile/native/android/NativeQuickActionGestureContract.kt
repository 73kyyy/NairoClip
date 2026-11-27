package com.nairoclip.app

/**
 * Explicit gesture-to-command contract for the editor's quick-action row.
 *
 * Keeping gesture variants separate prevents a double tap or long press from
 * being silently interpreted as the corresponding single-tap command.
 */
object NativeQuickActionGestureContract {
    fun doubleTap(action: NativeQuickAction): NativeQuickAction? =
        when (action) {
            NativeQuickAction.STEP_PREVIOUS -> NativeQuickAction.STEP_PREVIOUS_TO_START
            else -> null
        }

    fun longPress(action: NativeQuickAction): NativeQuickAction? =
        when (action) {
            NativeQuickAction.ADD_MARKER -> NativeQuickAction.SHOW_MARKER_LIST
            else -> null
        }
}
