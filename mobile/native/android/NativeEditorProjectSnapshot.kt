package com.nairoclip.app

import org.json.JSONObject

/**
 * Validates the active engine snapshot before the debug-only native editor
 * pilot is mounted. The engine project name is authoritative; the WebView
 * argument is only a fallback for older snapshots without a name.
 */
internal object NativeEditorProjectSnapshot {
    fun resolveProjectName(engineStateJson: String, fallbackName: String?): String? =
        runCatching {
            val envelope = JSONObject(engineStateJson)
            if (!envelope.optBoolean("ok", true)) return null
            val stateRoot = envelope.optJSONObject("result") ?: envelope
            val project = stateRoot.optJSONObject("project") ?: return null
            project.optString("name").takeIf { it.isNotBlank() }
                ?: fallbackName?.trim()?.takeIf { it.isNotEmpty() }
                ?: "未命名工程"
        }.getOrNull()
}
