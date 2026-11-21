package com.nairoclip.app

import org.junit.Assert.assertFalse
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

    @Test
    fun rejectsNonPositiveIds() {
        assertFalse(hasUniqueNativeTimelineIdentities(listOf(track(trackId = 0L, clipId = 10L))))
        assertFalse(hasUniqueNativeTimelineIdentities(listOf(track(trackId = 1L, clipId = 0L))))
    }
}
