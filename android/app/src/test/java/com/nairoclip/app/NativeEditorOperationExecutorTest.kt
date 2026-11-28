package com.nairoclip.app

import kotlinx.coroutines.CancellationException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorOperationExecutorTest {
    @Test
    fun ordinaryExceptionBecomesTypedFailure() {
        val result = executeNativeEditorOperationSafely<Int> {
            throw IllegalStateException("bridge unavailable")
        }

        assertFalse(result.ok)
        assertNull(result.value)
        assertEquals("bridge unavailable", result.error)
    }

    @Test
    fun blankExceptionMessageFallsBackToExceptionType() {
        val result = executeNativeEditorOperationSafely<String> {
            throw IllegalArgumentException("  ")
        }

        assertFalse(result.ok)
        assertEquals("原生编辑操作异常：IllegalArgumentException", result.error)
    }

    @Test
    fun successfulOperationPreservesItsResult() {
        val result = executeNativeEditorOperationSafely {
            NativeEditorCommandAdapter.Result(ok = true, value = 42)
        }

        assertTrue(result.ok)
        assertEquals(42, result.value)
        assertNull(result.error)
    }

    @Test
    fun coroutineCancellationIsRethrownUnchanged() {
        val cancellation = CancellationException("host disposed")
        var caught: CancellationException? = null

        try {
            executeNativeEditorOperationSafely<Unit> { throw cancellation }
        } catch (error: CancellationException) {
            caught = error
        }

        assertSame(cancellation, caught)
    }
}
