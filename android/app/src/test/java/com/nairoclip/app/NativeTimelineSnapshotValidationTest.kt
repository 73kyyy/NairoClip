package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeTimelineSnapshotValidationTest {
    private fun track(trackId: Long, clipId: Long) = NativeTimelineTrack(
        id = trackId,
        name = "轨道 $trackId",
        clips = listOf(
            NativeTimelineClip(
                id = clipId,
                startUs = 0L,
                durationUs = 1_000_000L,
                label = "片段 $clipId"
            )
        ),
        engineIndex = 0
    )

    @Test
    fun acceptsDistinctTrackAndClipIdsAcrossProject() {
        assertTrue(
            hasUniqueNativeTimelineIdentities(
                listOf(track(trackId = 1L, clipId = 10L), track(trackId = 2L, clipId = 20L))
            )
        )
    }

    @Test
    fun rejectsDuplicateTrackIds() {
        assertFalse(
            hasUniqueNativeTimelineIdentities(
                listOf(track(trackId = 1L, clipId = 10L), track(trackId = 1L, clipId = 20L))
            )
        )
    }

    @Test
    fun rejectsDuplicateClipIdsAcrossDifferentTracks() {
        assertFalse(
            hasUniqueNativeTimelineIdentities(
                listOf(track(trackId = 1L, clipId = 10L), track(trackId = 2L, clipId = 10L))
            )
        )
    }

    private fun stateWithClipMetadata(metadata: String): String = """
        {"ok":true,"result":{
          "project":{"name":"test","duration_us":5000000},
          "assets":[{"id":7,"name":"素材"}],
          "tracks":[{"id":1,"name":"V1","hidden":0,"muted":0,"locked":0,
            "clips":[{"id":2,"asset_id":7,"start_us":0,"duration_us":2000000,
              "speed_num":2,"speed_den":1,"enabled":1,"effects":[]$metadata}]}
          ]
        }}
    """.trimIndent()

    @Test
    fun parsesLinearSourceRangeAndSpeedMetadata() {
        val snapshot = NativeTimelineSnapshot.tryFromEngineState(
            stateWithClipMetadata(
                ",\"source_in_us\":1000000,\"source_out_us\":5000000," +
                    "\"reversed\":0,\"frozen\":0,\"has_speed_curve\":0"
            )
        )
        assertNotNull(snapshot)
        val clip = snapshot!!.tracks.single().clips.single()
        assertTrue(clip.sourceRangeKnown)
        assertEquals(1_000_000L, clip.sourceInUs)
        assertEquals(5_000_000L, clip.sourceOutUs)
        assertTrue(clip.supportsLinearSourceMapping)
        assertEquals(2L, clip.speedNumerator)
        assertEquals(7L, clip.assetId)
    }

    @Test
    fun oldSnapshotWithoutSourceMetadataIsExplicitlyUnknown() {
        val snapshot = NativeTimelineSnapshot.tryFromEngineState(stateWithClipMetadata(""))
        assertNotNull(snapshot)
        val clip = snapshot!!.tracks.single().clips.single()
        assertFalse(clip.sourceRangeKnown)
        assertFalse(clip.supportsLinearSourceMapping)
    }

    @Test
    fun rejectsPartialOrInvalidSourceMappingMetadata() {
        assertNull(
            NativeTimelineSnapshot.tryFromEngineState(
                stateWithClipMetadata(",\"source_in_us\":1000")
            )
        )
        assertNull(
            NativeTimelineSnapshot.tryFromEngineState(
                stateWithClipMetadata(
                    ",\"source_in_us\":1000,\"source_out_us\":1000," +
                        "\"reversed\":0,\"frozen\":0,\"has_speed_curve\":0"
                )
            )
        )
    }

    @Test
    fun nonLinearClipFlagsDisableLinearMapping() {
        val base = NativeTimelineClip(
            id = 1L, startUs = 0L, durationUs = 2_000_000L, label = "x",
            sourceInUs = 0L, sourceOutUs = 2_000_000L, sourceRangeKnown = true
        )
        assertTrue(base.supportsLinearSourceMapping)
        assertFalse(base.copy(reversed = true).supportsLinearSourceMapping)
        assertFalse(base.copy(frozen = true, sourceOutUs = 0L).supportsLinearSourceMapping)
        assertFalse(base.copy(hasSpeedCurve = true, sourceOutUs = 0L).supportsLinearSourceMapping)
        assertFalse(base.copy(sourceRangeKnown = false).supportsLinearSourceMapping)
    }

    @Test
    fun rejectsNonPositiveIds() {
        assertFalse(hasUniqueNativeTimelineIdentities(listOf(track(trackId = 0L, clipId = 10L))))
        assertFalse(hasUniqueNativeTimelineIdentities(listOf(track(trackId = 1L, clipId = 0L))))
    }

    private fun stateWithMarkers(markersJson: String): String =
        stateWithClipMetadata("").replace(
            "\"assets\"",
            "\"markers\":$markersJson,\"assets\""
        )

    @Test
    fun parsesNativeTimelineMarkers() {
        val snapshot = NativeTimelineSnapshot.tryFromEngineState(
            stateWithMarkers(
                """[{"id":9,"time_us":1250000,"name":"接缝","color":"#AABBCC"}]"""
            )
        )
        assertNotNull(snapshot)
        assertEquals(
            listOf(NativeTimelineMarker(9L, 1_250_000L, "接缝", "#AABBCC")),
            snapshot!!.markers
        )
    }

    @Test
    fun rejectsMalformedOrOutOfRangeMarkers() {
        assertNull(NativeTimelineSnapshot.tryFromEngineState(stateWithMarkers("""[{}]""")))
        assertNull(
            NativeTimelineSnapshot.tryFromEngineState(
                stateWithMarkers(
                    """[{"id":9,"time_us":6000000,"name":"越界","color":"#FFFFFF"}]"""
                )
            )
        )
        assertNull(
            NativeTimelineSnapshot.tryFromEngineState(
                stateWithMarkers(
                    """[{"id":9,"time_us":10,"name":"A","color":"#FFFFFF"},{"id":9,"time_us":20,"name":"B","color":"#000000"}]"""
                )
            )
        )
    }

}
