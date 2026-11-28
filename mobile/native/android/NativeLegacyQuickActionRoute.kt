package com.nairoclip.app

/**
 * Maps native quick actions that intentionally remain implemented by the
 * retained WebView editor to its existing data-act commands.
 */
internal object NativeLegacyQuickActionRoute {
    fun dataAction(action: NativeQuickAction): String? = when (action) {
        NativeQuickAction.IMPORT_MEDIA -> "import"
        NativeQuickAction.RETURN_HOME -> "home"
        NativeQuickAction.IMAGES -> "images"
        NativeQuickAction.AI_EDIT -> "ai"
        else -> null
    }
}
