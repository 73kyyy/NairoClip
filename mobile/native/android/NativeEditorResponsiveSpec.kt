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
                timelineHeightDp = 128,
                parameterPanelMaxHeightDp = 64,
                previewMinHeightDp = 48
            )
            height < 640f -> NativeEditorResponsiveMetrics(
                timelineHeightDp = 156,
                parameterPanelMaxHeightDp = 112,
                previewMinHeightDp = 80
            )
            height < 760f -> NativeEditorResponsiveMetrics(
                timelineHeightDp = 192,
                parameterPanelMaxHeightDp = 152,
                previewMinHeightDp = 112
            )
            height < 880f -> NativeEditorResponsiveMetrics(
                timelineHeightDp = 236,
                parameterPanelMaxHeightDp = 184,
                previewMinHeightDp = 144
            )
            else -> NativeEditorResponsiveMetrics(
                timelineHeightDp = NativeEditorLayoutSpec.TIMELINE_HEIGHT_DP,
                parameterPanelMaxHeightDp = 224,
                previewMinHeightDp = 160
            )
        }
    }
}
