package com.nairoclip.app

/**
 * Tracks synchronous user intent while serialized engine mutations are queued.
 * Each reservation has a unique token: an older completion cannot erase a
 * newer request even when both requests happen to target the same Boolean.
 */
internal class NativeTrackToggleIntents {
    internal data class Reservation(
        val target: Boolean,
        val token: Long
    )

    private val lock = Any()
    private val pending = mutableMapOf<Pair<Int, String>, Reservation>()
    private var nextToken = 0L

    fun reserve(key: Pair<Int, String>, confirmedValue: Boolean?): Reservation? =
        synchronized(lock) {
            val current = pending[key]?.target ?: confirmedValue
                ?: return@synchronized null
            nextToken = if (nextToken == Long.MAX_VALUE) 1L else nextToken + 1L
            Reservation(target = !current, token = nextToken).also {
                pending[key] = it
            }
        }

    fun finish(key: Pair<Int, String>, completed: Reservation) {
        synchronized(lock) {
            if (pending[key] == completed) pending.remove(key)
        }
    }
}
