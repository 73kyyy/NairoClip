package com.nairoclip.app

import org.json.JSONObject

/**
 * Converts the existing NativeEngineBridge.state() envelope into the small,
 * immutable model consumed by the native timeline renderer.
 *
 * The engine remains the source of truth. An incomplete track/clip record
 * rejects the whole snapshot rather than silently hiding timeline content.
 */
data class NativeTimelineSnapshot(
    val projectName: String,
    val durationUs: Long,
    val tracks: List<NativeTimelineTrack>
) {
    companion object {
        /**
         * BuildState emits integral JSON numbers for IDs, timestamps and flags.
         * JSONObject.optLong/optInt also coerce numeric strings, which can hide
         * a broken bridge contract. Accept only actual integral JSON numbers.
         */
        private fun JSONObject.longField(name: String): Long? {
            val raw = opt(name) as? Number ?: return null
            val value = raw.toDouble()
            if (!value.isFinite() || value % 1.0 != 0.0) return null
            if (raw is Long || raw is Int || raw is Short || raw is Byte) {
                return raw.toLong()
            }
            if (value < Long.MIN_VALUE.toDouble() || value >= 9.223372036854776E18) {
                return null
            }
            return value.toLong()
        }

        /**
         * Strict parser used by command adapters. A malformed top-level
         * envelope must be reported as a failed refresh, not as a successful
         * empty project (which would erase the visible timeline).
         */
        fun tryFromEngineState(stateJson: String): NativeTimelineSnapshot? {
            return try {
                val envelope = JSONObject(stateJson)
                if (!envelope.optBoolean("ok", false)) return null
                val state = envelope.optJSONObject("result") ?: return null
                val project = state.optJSONObject("project") ?: return null
                val duration = project.longField("duration_us")?.takeIf { it > 0L } ?: return null
                val projectName = project.optString("name", "未命名工程")
                    .ifBlank { "未命名工程" }
                val sourceTracks = state.optJSONArray("tracks") ?: return null

                val assetNames = HashMap<Long, String>()
                val assets = state.optJSONArray("assets")
                if (assets != null) {
                    for (index in 0 until assets.length()) {
                        val asset = assets.optJSONObject(index) ?: continue
                        val id = asset.longField("id") ?: continue
                        if (id > 0L) {
                            assetNames[id] = asset.optString("name", "").ifBlank { "素材 $id" }
                        }
                    }
                }

                val tracks = ArrayList<NativeTimelineTrack>()
                for (trackIndex in 0 until sourceTracks.length()) {
                    // BuildState preserves track/clip array indices by emitting {} when
                    // an engine record cannot be read. Skipping that placeholder would
                    // publish a plausible but incomplete timeline and could hide media.
                    val sourceTrack = sourceTracks.optJSONObject(trackIndex) ?: return null
                    val trackId = sourceTrack.longField("id")?.takeIf { it > 0L } ?: return null
                    val hidden = sourceTrack.longField("hidden")?.takeIf { it in 0L..1L } ?: return null
                    val muted = sourceTrack.longField("muted")?.takeIf { it in 0L..1L } ?: return null
                    val locked = sourceTrack.longField("locked")?.takeIf { it in 0L..1L } ?: return null
                    val sourceClips = sourceTrack.optJSONArray("clips") ?: return null

                    val clips = ArrayList<NativeTimelineClip>(sourceClips.length())
                    val clipIds = HashSet<Long>()
                    for (clipIndex in 0 until sourceClips.length()) {
                        val sourceClip = sourceClips.optJSONObject(clipIndex) ?: return null
                        val clipId = sourceClip.longField("id")?.takeIf { it > 0L } ?: return null
                        val assetId = sourceClip.longField("asset_id")?.takeIf { it >= 0L } ?: return null
                        val startUs = sourceClip.longField("start_us")?.takeIf { it >= 0L } ?: return null
                        val clipDurationUs = sourceClip.longField("duration_us")?.takeIf { it > 0L } ?: return null
                        val enabledValue = sourceClip.longField("enabled")?.takeIf { it in 0L..1L } ?: return null
                        if (startUs > Long.MAX_VALUE - clipDurationUs || !clipIds.add(clipId)) {
                            return null
                        }
                        clips += NativeTimelineClip(
                            id = clipId,
                            startUs = startUs,
                            durationUs = clipDurationUs,
                            label = assetNames[assetId] ?: "片段 $clipId",
                            enabled = enabledValue == 1L
                        )
                    }

                    tracks += NativeTimelineTrack(
                        id = trackId,
                        name = sourceTrack.optString("name", "轨道 $trackId")
                            .ifBlank { "轨道 $trackId" },
                        clips = clips,
                        visible = hidden == 0L,
                        muted = muted != 0L,
                        engineIndex = trackIndex,
                        locked = locked != 0L
                    )
                }
                NativeTimelineSnapshot(projectName, duration, tracks)
            } catch (_: Exception) {
                null
            }
        }

    }
}
