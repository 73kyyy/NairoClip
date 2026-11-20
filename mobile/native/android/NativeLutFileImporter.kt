package com.nairoclip.app

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.FileOutputStream
import java.util.UUID

/**
 * Android-only SAF import boundary. Projects reference the copied private file,
 * never the provider URI, so a temporary document grant cannot break later renders.
 */
internal object NativeLutFileImporter {
    suspend fun import(context: Context, uri: Uri): String = withContext(Dispatchers.IO) {
        val resolver = context.contentResolver
        val displayName = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getString(0) else null
            }.orEmpty()
        require(displayName.substringAfterLast('.', "").equals("cube", ignoreCase = true)) {
            "请选择 .cube 格式的 3D LUT 文件"
        }

        val directory = File(context.filesDir, "nairoclip/luts")
        require(directory.exists() || directory.mkdirs()) { "无法创建应用私有 LUT 目录" }
        val destination = File(directory, "${UUID.randomUUID()}.cube")
        val temporary = File(directory, "${destination.name}.part")
        try {
            val input = resolver.openInputStream(uri) ?: error("无法读取所选 LUT 文件")
            input.use { source ->
                FileOutputStream(temporary).use { output -> source.copyTo(output, DEFAULT_BUFFER_SIZE) }
            }
            require(temporary.isFile && temporary.length() > 0L) { "所选 LUT 文件为空" }
            require(temporary.renameTo(destination)) { "无法保存 LUT 文件副本" }
            destination.absolutePath
        } catch (error: Exception) {
            temporary.delete()
            destination.delete()
            throw error
        }
    }
}
