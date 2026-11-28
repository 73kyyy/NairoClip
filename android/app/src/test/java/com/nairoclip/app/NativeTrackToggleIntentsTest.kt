package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeTrackToggleIntentsTest {
    @Test
    fun rapidSecondTapTogglesThePendingTarget() {
        val intents = NativeTrackToggleIntents()
        val key = 2 to "VISIBILITY"

        assertEquals(true, intents.reserve(key, false))
        assertEquals(false, intents.reserve(key, false))
    }

    @Test
    fun completionOfOlderMutationDoesNotEraseNewerIntent() {
        val intents = NativeTrackToggleIntents()
        val key = 1 to "LOCK"
        val first = intents.reserve(key, false)!!
        val second = intents.reserve(key, false)!!

        intents.finish(key, first)

        // A third tap must continue from the second pending target.
        assertEquals(true, intents.reserve(key, false))
        intents.finish(key, false)

        // Finishing the current target retires the pending reservation.
        intents.finish(key, true)
        assertEquals(false, intents.reserve(key, true))
    }

    @Test
    fun finishingFailedCurrentMutationClearsPendingIntent() {
        val intents = NativeTrackToggleIntents()
        val key = 0 to "MUTE"

        val target = intents.reserve(key, false)!!
        intents.finish(key, target)

        // No pending target remains; a new tap starts from confirmed engine state.
        assertEquals(true, intents.reserve(key, false))
    }

    @Test
    fun missingConfirmedTrackStateDoesNotInventAToggleTarget() {
        val intents = NativeTrackToggleIntents()
        assertNull(intents.reserve(99 to "VISIBILITY", null))
    }
}
