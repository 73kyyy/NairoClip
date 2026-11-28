package com.nairoclip.app

internal data class NativeEditorResponsiveMetrics(
    val timelineHeightDp: Int,
    val parameterPanelMaxHeightDp: Int,
    val previewMinHeightDp: Int
)

/**
 * Height-class policy for the editor's fixed interaction regions.
 *
 * The timeline, transport and tool strip remain reachable in compact windows;
 * parameter content gets a bounded scroll viewport instead of pushing them
 * below the screen. The reference-height class retains the established 282dp
 * timeline geometry.
 */
internal object NativeEditorResponsiveSpec {
    fun forHeight(maxHeightDp: Float): NativeEditorResponsiveMetrics {
        val height = if (maxHeightDp.isFinite()) maxHeightDp.coerceAtLeast(0f) else 0f
        return when {
            height < 520f -> NativeEditorResponsiveMetrics(
                timelineHeightDp = 112,
                parameterPanelMaxHeightDp = 88,
                previewMinHeightDp = 32
            )
            height < 640f -> NativeEditorResponsiveMetrics(
                timelineHeightDp = 136,
                parameterPanelMaxHeightDp = 72,
                previewMinHeightDp = 64
            )
            height < 760f -> NativeEditorResponsiveMetrics(
                timelineHeightDp = 168,
                parameterPanelMaxHeightDp = 128,
                previewMinHeightDp = 96
            )
            height < 880f -> NativeEditorResponsiveMetrics(
                timelineHeightDp = 208,
                parameterPanelMaxHeightDp = 176,
                previewMinHeightDp = 128
            )
            else -> NativeEditorResponsiveMetrics(
                timelineHeightDp = NativeEditorLayoutSpec.TIMELINE_HEIGHT_DP,
                parameterPanelMaxHeightDp = 192,
                previewMinHeightDp = 160
            )
        }
    }
}
