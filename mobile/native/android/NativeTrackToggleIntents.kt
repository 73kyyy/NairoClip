package com.nairoclip.app

/**
 * Tracks synchronous user intent while serialized engine mutations are queued.
 * A second tap toggles the pending target rather than re-reading a stale UI
 * snapshot. Completing an older operation cannot erase a newer pending intent.
 */
internal class NativeTrackToggleIntents {
    private val lock = Any()
    private val pending = mutableMapOf<Pair<Int, String>, Boolean>()

    fun reserve(key: Pair<Int, String>, confirmedValue: Boolean?): Boolean? =
        synchronized(lock) {
            val current = pending[key] ?: confirmedValue ?: return@synchronized null
            (!current).also { pending[key] = it }
        }

    fun finish(key: Pair<Int, String>, completedTarget: Boolean) {
        synchronized(lock) {
            if (pending[key] == completedTarget) pending.remove(key)
        }
    }
}
