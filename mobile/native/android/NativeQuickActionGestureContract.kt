package com.nairoclip.app

/**
 * Explicit gesture-to-command contract for the editor's quick-action row.
 *
 * Keeping gesture variants separate prevents a double tap or long press from
 * being silently interpreted as the corresponding single-tap command.
 */
object NativeQuickActionGestureContract {
    fun doubleTap(action: NativeQuickAction): NativeQuickAction? =
        when (action) {
            NativeQuickAction.STEP_PREVIOUS -> NativeQuickAction.STEP_PREVIOUS_TO_START
            else -> null
        }

    fun longPress(action: NativeQuickAction): NativeQuickAction? =
        when (action) {
            NativeQuickAction.ADD_MARKER -> NativeQuickAction.SHOW_MARKER_LIST
            else -> null
        }

    /**
     * Matches the legacy editor's anchor semantics: move to the nearest
     * strictly earlier clip start/end boundary, with a 1ms repeat guard.
     */
    fun previousTimelineAnchor(
        tracks: List<NativeTimelineTrack>,
        playheadUs: Long
    ): Long? {
        if (playheadUs <= 1_000L) return null
        val anchors = HashSet<Long>()
        anchors.add(0L)
        tracks.forEach { track ->
            track.clips.forEach { clip ->
                if (clip.startUs >= 0L) anchors.add(clip.startUs)
                if (clip.startUs >= 0L && clip.startUs <= Long.MAX_VALUE - clip.durationUs) {
                    anchors.add(clip.startUs + clip.durationUs)
                }
            }
        }
        return anchors.asSequence()
            .filter { it < playheadUs - 1_000L }
            .maxOrNull()
    }
}
