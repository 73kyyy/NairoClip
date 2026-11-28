package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeTrackToggleIntentsTest {
    @Test
    fun rapidSecondTapTogglesThePendingTarget() {
        val intents = NativeTrackToggleIntents()
        val key = 2 to "VISIBILITY"

        assertEquals(true, intents.reserve(key, false)?.target)
        assertEquals(false, intents.reserve(key, false)?.target)
    }

    @Test
    fun olderCompletionCannotEraseNewerReservationWithSameTarget() {
        val intents = NativeTrackToggleIntents()
        val key = 1 to "LOCK"
        val first = intents.reserve(key, false)!!
        val second = intents.reserve(key, false)!!
        val third = intents.reserve(key, false)!!

        assertEquals(true, first.target)
        assertEquals(false, second.target)
        assertEquals(true, third.target)

        intents.finish(key, first)

        // The older first request has the same target as the third request,
        // but its unique token must not retire the newer reservation.
        val fourth = intents.reserve(key, false)!!
        assertEquals(false, fourth.target)

        intents.finish(key, second)
        intents.finish(key, third)

        // The fourth reservation remains authoritative until it completes.
        val fifth = intents.reserve(key, false)!!
        assertEquals(true, fifth.target)
        intents.finish(key, fourth)
        intents.finish(key, fifth)
    }

    @Test
    fun finishingFailedCurrentMutationClearsPendingIntent() {
        val intents = NativeTrackToggleIntents()
        val key = 0 to "MUTE"

        val reservation = intents.reserve(key, false)!!
        intents.finish(key, reservation)

        // No pending target remains; a new tap starts from confirmed engine state.
        assertEquals(true, intents.reserve(key, false)?.target)
    }

    @Test
    fun missingConfirmedTrackStateDoesNotInventAToggleTarget() {
        val intents = NativeTrackToggleIntents()
        assertNull(intents.reserve(99 to "VISIBILITY", null))
    }

    @Test
    fun differentTracksAndControlsKeepIndependentPendingTargets() {
        val intents = NativeTrackToggleIntents()
        val visibility = intents.reserve(1 to "VISIBILITY", false)
        val mute = intents.reserve(1 to "MUTE", false)
        val otherTrack = intents.reserve(2 to "VISIBILITY", false)

        assertEquals(true, visibility?.target)
        assertEquals(true, mute?.target)
        assertEquals(true, otherTrack?.target)
    }
}
