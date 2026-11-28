package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class NativeEditorProjectSnapshotTest {
    @Test
    fun readsProjectNameFromCurrentCAbiResultEnvelope() {
        assertEquals(
            "Current project",
            NativeEditorProjectSnapshot.resolveProjectName(
                """{"ok":true,"result":{"project":{"id":17,"name":"Current project"},"tracks":[]}}""",
                "stale WebView title"
            )
        )
    }

    @Test
    fun engineProjectNameTakesPrecedenceOverWebViewFallback() {
        assertEquals(
            "Engine title",
            NativeEditorProjectSnapshot.resolveProjectName(
                """{"ok":true,"result":{"project":{"name":"Engine title"}}}""",
                "Different project"
            )
        )
    }

    @Test
    fun usesFallbackOnlyWhenSnapshotProjectHasNoName() {
        assertEquals(
            "WebView title",
            NativeEditorProjectSnapshot.resolveProjectName(
                """{"ok":true,"result":{"project":{"id":17}}}""",
                " WebView title "
            )
        )
    }

    @Test
    fun acceptsLegacyUnwrappedProjectSnapshot() {
        assertEquals(
            "Legacy project",
            NativeEditorProjectSnapshot.resolveProjectName(
                """{"project":{"name":"Legacy project"}}""",
                null
            )
        )
    }

    @Test
    fun rejectsEngineErrorEnvelopeAndMissingProject() {
        assertNull(
            NativeEditorProjectSnapshot.resolveProjectName(
                """{"ok":false,"message":"engine unavailable"}""",
                "Do not mount"
            )
        )
        assertNull(
            NativeEditorProjectSnapshot.resolveProjectName(
                """{"ok":true,"result":{"tracks":[]}}""",
                "Do not mount"
            )
        )
    }

    @Test
    fun rejectsMalformedJsonAndBlankFallback() {
        assertNull(NativeEditorProjectSnapshot.resolveProjectName("{broken", "fallback"))
        assertNull(
            NativeEditorProjectSnapshot.resolveProjectName(
                """{"ok":true,"result":{"project":{"name":""}}}""",
                " "
            )
        )
    }
}
