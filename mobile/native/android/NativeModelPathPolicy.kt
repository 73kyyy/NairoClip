package com.nairoclip.app

import java.io.File

/**
 * Resolves a candidate model file only when its canonical path is a strict
 * descendant of the app-private model root. Canonicalization follows symlinks,
 * so a link inside the root that targets an external file is rejected too.
 */
internal object NativeModelPathPolicy {
    fun resolveWithinRoot(root: File, candidate: File): File? {
        val canonicalRoot = root.canonicalFile.toPath().toAbsolutePath().normalize()
        val canonicalCandidate = candidate.canonicalFile.toPath().toAbsolutePath().normalize()
        if (canonicalCandidate == canonicalRoot || !canonicalCandidate.startsWith(canonicalRoot)) {
            return null
        }
        return canonicalCandidate.toFile()
    }
}
