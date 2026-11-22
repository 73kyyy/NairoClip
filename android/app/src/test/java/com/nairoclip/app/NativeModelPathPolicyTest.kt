package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.nio.file.Files

class NativeModelPathPolicyTest {
    @Test
    fun acceptsRegularFilesStrictlyInsidePrivateModelRoot() {
        val root = Files.createTempDirectory("nairoclip-model-root")
        val model = Files.createFile(root.resolve("encoder.onnx"))
        assertEquals(
            model.toFile().canonicalFile,
            NativeModelPathPolicy.resolveWithinRoot(root.toFile(), model.toFile())
        )
    }

    @Test
    fun rejectsRootDirectoryAndFilesOutsidePrivateModelRoot() {
        val root = Files.createTempDirectory("nairoclip-model-root")
        val outside = Files.createTempFile("nairoclip-external-model", ".onnx")
        assertNull(NativeModelPathPolicy.resolveWithinRoot(root.toFile(), root.toFile()))
        assertNull(NativeModelPathPolicy.resolveWithinRoot(root.toFile(), outside.toFile()))
    }

    @Test
    fun rejectsTraversalThatResolvesOutsidePrivateModelRoot() {
        val parent = Files.createTempDirectory("nairoclip-model-parent")
        val root = Files.createDirectory(parent.resolve("models"))
        val outside = Files.createFile(parent.resolve("outside.onnx"))
        val traversal = root.resolve("../outside.onnx")
        assertEquals(outside.toFile().canonicalFile, traversal.toFile().canonicalFile)
        assertNull(NativeModelPathPolicy.resolveWithinRoot(root.toFile(), traversal.toFile()))
    }

    @Test
    fun rejectsSymlinkInsideRootThatTargetsExternalModel() {
        val root = Files.createTempDirectory("nairoclip-model-root")
        val outside = Files.createTempFile("nairoclip-external-model", ".onnx")
        val link = root.resolve("linked-model.onnx")
        try {
            Files.createSymbolicLink(link, outside)
        } catch (_: UnsupportedOperationException) {
            return
        } catch (_: java.io.IOException) {
            return
        } catch (_: SecurityException) {
            return
        }
        assertNull(NativeModelPathPolicy.resolveWithinRoot(root.toFile(), link.toFile()))
    }
}
