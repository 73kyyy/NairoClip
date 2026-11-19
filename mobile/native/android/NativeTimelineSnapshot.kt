package com.nairoclip.app

import org.json.JSONObject

/**
 * Converts the existing NativeEngineBridge.state() envelope into the small,
 * immutable model consumed by the native timeline renderer.
 *
 * The engine remains the source of truth. Unknown or malformed records are
 * skipped rather than fabricating clips or durations.
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
                    val sourceTrack = sourceTracks.optJSONObject(trackIndex) ?: continue
                    val trackId = sourceTrack.longField("id") ?: continue
                    if (trackId <= 0L) continue
                    val hidden = sourceTrack.longField("hidden") ?: continue
                    val muted = sourceTrack.longField("muted") ?: continue

                    val clips = ArrayList<NativeTimelineClip>()
                    val sourceClips = sourceTrack.optJSONArray("clips")
                    if (sourceClips != null) {
                        for (clipIndex in 0 until sourceClips.length()) {
                            val sourceClip = sourceClips.optJSONObject(clipIndex) ?: continue
                            val clipId = sourceClip.longField("id") ?: continue
                            val assetId = sourceClip.longField("asset_id") ?: 0L
                            val startUs = sourceClip.longField("start_us") ?: continue
                            val clipDurationUs = sourceClip.longField("duration_us") ?: continue
                            val enabledValue = sourceClip.longField("enabled") ?: continue
                            if (clipId <= 0L || startUs < 0L || clipDurationUs <= 0L) continue
                            clips += NativeTimelineClip(
                                id = clipId,
                                startUs = startUs,
                                durationUs = clipDurationUs,
                                label = assetNames[assetId] ?: "片段 $clipId",
                                enabled = enabledValue != 0L
                            )
                        }
                    }

                    tracks += NativeTimelineTrack(
                        id = trackId,
                        name = sourceTrack.optString("name", "轨道 $trackId")
                            .ifBlank { "轨道 $trackId" },
                        clips = clips,
                        visible = hidden == 0L,
                        muted = muted != 0L,
                        engineIndex = trackIndex
                    )
                }
                NativeTimelineSnapshot(projectName, duration, tracks)
            } catch (_: Exception) {
                null
            }
        }

        /**
         * Compatibility convenience for display-only callers. Command paths
         * must use [tryFromEngineState] so malformed state is never mistaken
         * for a legitimate empty project.
         */
        fun fromEngineState(stateJson: String): NativeTimelineSnapshot =
            tryFromEngineState(stateJson)
                ?: NativeTimelineSnapshot("未命名工程", 1L, emptyList())
    }
}
