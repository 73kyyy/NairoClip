package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.util.concurrent.atomic.AtomicBoolean

class NativeSpeechCancellationGuardTest {

    @Test
    fun cancellationBeforeNativeDispatchPreventsInferenceCall() {
        val cancelled = AtomicBoolean(true)
        var invoked = false

        try {
            runSpeechInferenceWithCancellation(
                isCancelled = { cancelled.get() },
                inference = {
                    invoked = true
                    "unexpected"
                }
            )
            throw AssertionError("Expected cancellation")
        } catch (expected: IllegalStateException) {
            assertEquals("语音识别已取消", expected.message)
        }

        assertFalse(invoked)
    }

    @Test
    fun cancellationDuringNativeDispatchDiscardsReturnedResult() {
        val cancelled = AtomicBoolean(false)
        var nativeCompleted = false

        try {
            runSpeechInferenceWithCancellation(
                isCancelled = { cancelled.get() },
                inference = {
                    cancelled.set(true)
                    nativeCompleted = true
                    "recognized subtitle"
                }
            )
            throw AssertionError("Expected cancellation")
        } catch (expected: IllegalStateException) {
            assertEquals("语音识别已取消", expected.message)
        }

        assertTrue(nativeCompleted)
    }

    @Test
    fun successfulInferenceReturnsResultWhenNotCancelled() {
        val cancelled = AtomicBoolean(false)
        val result = runSpeechInferenceWithCancellation(
            isCancelled = { cancelled.get() },
            inference = { "recognized subtitle" }
        )

        assertEquals("recognized subtitle", result)
    }
}
