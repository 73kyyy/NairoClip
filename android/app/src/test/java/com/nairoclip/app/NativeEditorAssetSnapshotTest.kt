package com.nairoclip.app

import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorAssetSnapshotTest {
    @Test
    fun resolvesExistingAbsoluteAndFileUrisFromWrappedSnapshot() {
        val media = File.createTempFile("nairoclip-asset-", ".mp4")
        try {
            val absolute = media.absolutePath
            val fileUri = media.toURI().toString()
            val json = """{"ok":true,"result":{"assets":[
                {"id":11,"path":${quote(absolute)}},
                {"id":12,"path":${quote(fileUri)}}
            ]}}"""
            val resolved = NativeEditorAssetSnapshot.resolveUris(json)
            assertEquals(media.toURI().toString(), resolved[11L])
            assertEquals(media.toURI().toString(), resolved[12L])
        } finally {
            media.delete()
        }
    }

    @Test
    fun preservesContentUriAndRejectsUnsupportedSchemesAndMissingFiles() {
        val json = """{"ok":true,"result":{"assets":[
            {"id":21,"path":"content://media/external/video/media/21"},
            {"id":22,"path":"https://example.invalid/video.mp4"},
            {"id":23,"path":"/path/that/does/not/exist.mp4"},
            {"id":24,"path":"relative/video.mp4"}
        ]}}"""
        val resolved = NativeEditorAssetSnapshot.resolveUris(json)
        assertEquals("content://media/external/video/media/21", resolved[21L])
        assertFalse(resolved.containsKey(22L))
        assertFalse(resolved.containsKey(23L))
        assertFalse(resolved.containsKey(24L))
    }

    @Test
    fun rejectsErrorEnvelopeMalformedJsonAndInvalidAssetIds() {
        assertTrue(NativeEditorAssetSnapshot.resolveUris(
            """{"ok":false,"message":"engine error"}"""
        ).isEmpty())
        assertTrue(NativeEditorAssetSnapshot.resolveUris("{broken").isEmpty())
        assertTrue(NativeEditorAssetSnapshot.resolveUris(
            """{"ok":true,"result":{"assets":[
                {"id":0,"path":"content://media/item/1"},
                {"id":-1,"path":"content://media/item/2"},
                {"id":31,"path":""}
            ]}}"""
        ).isEmpty())
    }

    private fun quote(value: String): String =
        org.json.JSONObject.quote(value)
}
