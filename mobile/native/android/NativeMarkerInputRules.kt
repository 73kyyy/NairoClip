package com.nairoclip.app

/**
 * Pure input guards for marker commands.
 *
 * Keeping these checks independent from NativeEngineBridge lets JVM unit
 * tests verify rejected input without loading the JNI library or creating an
 * editor handle.
 */
object NativeMarkerInputRules {
    fun validateAdd(timeUs: Long, name: String, color: String): String? {
        if (timeUs < 0L) return "标记时间不能为负数"
        val cleanName = name.trim()
        if (cleanName.isEmpty() || cleanName.toByteArray(Charsets.UTF_8).size > 128) {
            return "标记名称不能为空或超过 128 字节"
        }
        if (!color.matches(Regex("^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$"))) {
            return "标记颜色格式无效"
        }
        return null
    }

    fun validateRemove(markerId: Long): String? =
        if (markerId <= 0L) "标记 ID 无效" else null
}
