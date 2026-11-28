package com.nairoclip.app

import kotlinx.coroutines.CancellationException

/**
 * Normalizes exceptions escaping editor bridge operations into typed failures.
 * Coroutine cancellation is deliberately rethrown so structured concurrency
 * and host disposal retain their normal cancellation semantics.
 */
internal fun <T> executeNativeEditorOperationSafely(
    operation: () -> NativeEditorCommandAdapter.Result<T>
): NativeEditorCommandAdapter.Result<T> = try {
    operation()
} catch (cancelled: CancellationException) {
    throw cancelled
} catch (failure: Exception) {
    NativeEditorCommandAdapter.Result(
        ok = false,
        error = failure.message?.takeIf { it.isNotBlank() }
            ?: "原生编辑操作异常：" + failure.javaClass.simpleName
    )
}
