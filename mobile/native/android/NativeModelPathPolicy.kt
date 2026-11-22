package com.nairoclip.app

import java.io.File

/**
 * Resolves a candidate model file only when its canonical path is a strict
 * descendant of the app-private model root. Canonicalization follows symlinks,
 * so a link inside the root that targets an external file is rejected too.
 *
 * Use File.canonicalFile rather than java.nio.file.Path: the app supports API 24,
 * while java.nio.file was only added to Android platform APIs in API 26.
 */
internal object NativeModelPathPolicy {
    fun resolveWithinRoot(root: File, candidate: File): File? {
        val canonicalRoot = root.canonicalFile
        val canonicalCandidate = candidate.canonicalFile
        val rootPath = canonicalRoot.path.trimEnd(File.separatorChar) + File.separator
        if (canonicalCandidate.path == canonicalRoot.path ||
            !canonicalCandidate.path.startsWith(rootPath)
        ) {
            return null
        }
        return canonicalCandidate
    }
}
