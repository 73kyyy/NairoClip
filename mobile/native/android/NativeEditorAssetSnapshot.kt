package com.nairoclip.app

import java.io.File
import java.net.URI

/**
 * Resolves asset IDs in the active C ABI project snapshot to Android-readable
 * media URIs. Only persisted content:// grants and existing file: or absolute
 * regular files are accepted; network and unknown schemes are never passed
 * to MediaExtractor.
 */
internal object NativeEditorAssetSnapshot {
    fun resolveUris(engineStateJson: String): Map<Long, String> = runCatching {
        val envelope = org.json.JSONObject(engineStateJson)
        if (envelope.has("ok") && !envelope.optBoolean("ok", false)) return emptyMap()
        val state = envelope.optJSONObject("result") ?: envelope
        val assets = state.optJSONArray("assets") ?: return emptyMap()
        buildMap {
            for (index in 0 until assets.length()) {
                val asset = assets.optJSONObject(index) ?: continue
                val id = asset.optLong("id", 0L)
                val path = asset.optString("path").trim()
                if (id <= 0L || path.isEmpty()) continue
                val resolved = resolvePath(path) ?: continue
                put(id, resolved)
            }
        }
    }.getOrDefault(emptyMap())

    private fun resolvePath(raw: String): String? {
        if (raw.startsWith("content://", ignoreCase = true)) {
            val uri = runCatching { URI(raw) }.getOrNull() ?: return null
            return raw.takeIf {
                uri.scheme.equals("content", ignoreCase = true) &&
                    !uri.rawAuthority.isNullOrBlank()
            }
        }
        val file = when {
            raw.startsWith("file:", ignoreCase = true) -> {
                val uri = runCatching { URI(raw) }.getOrNull() ?: return null
                if (!uri.scheme.equals("file", ignoreCase = true) ||
                    !uri.rawAuthority.isNullOrBlank()
                ) return null
                runCatching { File(uri).canonicalFile }.getOrNull() ?: return null
            }
            raw.startsWith("/") -> runCatching { File(raw).canonicalFile }.getOrNull()
            else -> null
        } ?: return null
        return file.takeIf { it.isFile }?.toURI()?.toString()
    }
}
