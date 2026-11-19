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
        fun fromEngineState(stateJson: String): NativeTimelineSnapshot {
            val empty = NativeTimelineSnapshot("未命名工程", 1L, emptyList())
            return try {
                val envelope = JSONObject(stateJson)
                if (envelope.optBoolean("ok", true).not()) return empty
                val state = envelope.optJSONObject("result") ?: envelope
                val project = state.optJSONObject("project") ?: JSONObject()
                val duration = project.optLong("duration_us", 1L).coerceAtLeast(1L)
                val projectName = project.optString("name", "未命名工程")
                    .ifBlank { "未命名工程" }

                val assetNames = HashMap<Long, String>()
                val assets = state.optJSONArray("assets")
                if (assets != null) {
                    for (index in 0 until assets.length()) {
                        val asset = assets.optJSONObject(index) ?: continue
                        val id = asset.optLong("id", 0L)
                        if (id > 0L) {
                            assetNames[id] = asset.optString("name", "").ifBlank { "素材 $id" }
                        }
                    }
                }

                val tracks = ArrayList<NativeTimelineTrack>()
                val sourceTracks = state.optJSONArray("tracks")
                if (sourceTracks != null) {
                    for (trackIndex in 0 until sourceTracks.length()) {
                        val sourceTrack = sourceTracks.optJSONObject(trackIndex) ?: continue
                        val trackId = sourceTrack.optLong("id", 0L)
                        if (trackId <= 0L) continue

                        val clips = ArrayList<NativeTimelineClip>()
                        val sourceClips = sourceTrack.optJSONArray("clips")
                        if (sourceClips != null) {
                            for (clipIndex in 0 until sourceClips.length()) {
                                val sourceClip = sourceClips.optJSONObject(clipIndex) ?: continue
                                val clipId = sourceClip.optLong("id", 0L)
                                val assetId = sourceClip.optLong("asset_id", 0L)
                                val startUs = sourceClip.optLong("start_us", -1L)
                                val clipDurationUs = sourceClip.optLong("duration_us", 0L)
                                if (clipId <= 0L || startUs < 0L || clipDurationUs <= 0L) continue
                                val enabled = sourceClip.optInt("enabled", 1) != 0
                                clips += NativeTimelineClip(
                                    id = clipId,
                                    startUs = startUs,
                                    durationUs = clipDurationUs,
                                    label = assetNames[assetId] ?: "片段 $clipId",
                                    enabled = enabled
                                )
                            }
                        }

                        tracks += NativeTimelineTrack(
                            id = trackId,
                            name = sourceTrack.optString("name", "轨道 $trackId")
                                .ifBlank { "轨道 $trackId" },
                            clips = clips,
                            visible = sourceTrack.optInt("hidden", 0) == 0,
                            muted = sourceTrack.optInt("muted", 0) != 0
                        )
                    }
                }
                NativeTimelineSnapshot(projectName, duration, tracks)
            } catch (_: Exception) {
                empty
            }
        }
    }
}
