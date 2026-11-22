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
                val assetIds = HashSet<Long>()
                val assets = state.optJSONArray("assets")
                if (assets != null) {
                    for (index in 0 until assets.length()) {
                        val asset = assets.optJSONObject(index) ?: return null
                        val id = asset.longField("id")?.takeIf { it > 0L } ?: return null
                        if (!assetIds.add(id)) return null
                        assetNames[id] = asset.optString("name", "").ifBlank { "素材 $id" }
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
                    for (clipIndex in 0 until sourceClips.length()) {
                        val sourceClip = sourceClips.optJSONObject(clipIndex) ?: return null
                        val clipId = sourceClip.longField("id")?.takeIf { it > 0L } ?: return null
                        val assetId = sourceClip.longField("asset_id")?.takeIf { it >= 0L } ?: return null
                        if (assetId > 0L && assetId !in assetIds) return null
                        val startUs = sourceClip.longField("start_us")?.takeIf { it >= 0L } ?: return null
                        val clipDurationUs = sourceClip.longField("duration_us")?.takeIf { it > 0L } ?: return null
                        val enabledValue = sourceClip.longField("enabled")?.takeIf { it in 0L..1L } ?: return null
                        val sourceInRaw = sourceClip.longField("source_in_us")
                        val sourceOutRaw = sourceClip.longField("source_out_us")
                        if ((sourceInRaw == null) != (sourceOutRaw == null)) return null
                        val sourceRangeKnown = sourceInRaw != null && sourceOutRaw != null
                        val sourceInUs = sourceInRaw ?: 0L
                        val sourceOutUs = sourceOutRaw ?: 0L
                        if (sourceRangeKnown && (sourceInUs < 0L || sourceOutUs < sourceInUs)) return null
                        val reversedValue = sourceClip.longField("reversed")?.takeIf { it in 0L..1L }
                        val frozenValue = sourceClip.longField("frozen")?.takeIf { it in 0L..1L }
                        val speedCurveValue = sourceClip.longField("has_speed_curve")?.takeIf { it in 0L..1L }
                        if (sourceRangeKnown && (reversedValue == null || frozenValue == null || speedCurveValue == null)) return null
                        val speedNumerator = sourceClip.longField("speed_num")?.takeIf { it > 0L } ?: return null
                        val speedDenominator = sourceClip.longField("speed_den")?.takeIf { it > 0L } ?: return null
                        val speed = speedNumerator.toDouble() / speedDenominator.toDouble()
                        if (!speed.isFinite() || speed !in 0.1..100.0) return null
                        if (startUs > Long.MAX_VALUE - clipDurationUs) return null
                        val sourceEffects = sourceClip.optJSONArray("effects") ?: return null
                        val effects = ArrayList<NativeClipEffect>(sourceEffects.length())
                        val effectIds = HashSet<Long>()
                        for (effectIndex in 0 until sourceEffects.length()) {
                            val sourceEffect = sourceEffects.optJSONObject(effectIndex) ?: return null
                            val effectId = sourceEffect.longField("id")?.takeIf { it > 0L } ?: return null
                            val effectType = sourceEffect.longField("type")?.takeIf { it in 0L..4096L } ?: return null
                            val effectEnabled = sourceEffect.longField("enabled")?.takeIf { it in 0L..1L } ?: return null
                            val rawValue = sourceEffect.opt("value") as? Number ?: return null
                            val effectValue = rawValue.toDouble()
                            if (!effectValue.isFinite() || !effectValue.toFloat().isFinite() || !effectIds.add(effectId)) return null
                            effects += NativeClipEffect(
                                id = effectId,
                                type = effectType.toInt(),
                                value = effectValue,
                                enabled = effectEnabled == 1L
                            )
                        }

                        clips += NativeTimelineClip(
                            id = clipId,
                            startUs = startUs,
                            durationUs = clipDurationUs,
                            label = assetNames[assetId] ?: "片段 $clipId",
                            enabled = enabledValue == 1L,
                            effects = effects,
                            speedNumerator = speedNumerator,
                            speedDenominator = speedDenominator,
                            sourceInUs = sourceInUs,
                            sourceOutUs = sourceOutUs,
                            sourceRangeKnown = sourceRangeKnown,
                            reversed = reversedValue == 1L,
                            frozen = frozenValue == 1L,
                            hasSpeedCurve = speedCurveValue == 1L
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
                if (!hasUniqueNativeTimelineIdentities(tracks)) return null
                NativeTimelineSnapshot(projectName, duration, tracks)
            } catch (_: Exception) {
                null
            }
        }

    }
}


/**
 * The C++ project model assigns globally unique track and clip IDs. A duplicate
 * clip ID on another track is still corrupt state: native commands and subtitle
 * writeback use stable IDs, so publishing an ambiguous snapshot is unsafe.
 */
internal fun hasUniqueNativeTimelineIdentities(tracks: List<NativeTimelineTrack>): Boolean {
    val trackIds = HashSet<Long>()
    val clipIds = HashSet<Long>()
    return tracks.all { track ->
        track.id > 0L && trackIds.add(track.id) && track.clips.all { clip ->
            clip.id > 0L && clipIds.add(clip.id)
        }
    }
}
