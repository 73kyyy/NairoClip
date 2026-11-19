package com.nairoclip.app

import org.json.JSONObject

/**
 * Narrow Android adapter over the existing C ABI exposed by NativeEngineBridge.
 *
 * This class deliberately does not own an editor handle or bypass the bridge's
 * state lock. All calls go through NativeEngineBridge.call(), which serializes
 * access to the non-thread-safe NcEditor instance.
 */
class NativeEditorCommandAdapter(private val bridge: NativeEngineBridge) {

    data class ClipTarget(val trackIndex: Int, val clipId: Long) {
        init {
            require(trackIndex >= 0) { "轨道索引不能为负数" }
            require(clipId > 0L) { "片段 ID 必须大于 0" }
        }
    }

    data class Transform(
        val enabled: Boolean,
        val scale: Double,
        val x: Double,
        val y: Double,
        val rotation: Double,
        val anchor: Int
    )

    data class Keyframe(
        val timeUs: Long,
        val value: Double,
        val easing: Int,
        val c1x: Double,
        val c1y: Double,
        val c2x: Double,
        val c2y: Double
    )

    data class Result<T>(val ok: Boolean, val value: T? = null, val error: String? = null)

    fun setTransform(target: ClipTarget, value: Transform): Result<Unit> {
        if (!value.scale.isFinite() || value.scale <= 0.0 ||
            !value.x.isFinite() || !value.y.isFinite() ||
            !value.rotation.isFinite() || value.anchor !in 0..8) {
            return Result(false, error = "变换参数超出有效范围")
        }
        return invoke(
            "nc_set_clip_transform",
            JSONObject()
                .put("track_index", target.trackIndex)
                .put("clip_id", target.clipId)
                .put("enabled", if (value.enabled) 1 else 0)
                .put("scale", value.scale)
                .put("pos_x", value.x)
                .put("pos_y", value.y)
                .put("rotation", value.rotation)
                .put("anchor", value.anchor)
        ) { Unit }
    }

    fun getTransform(target: ClipTarget): Result<Transform> =
        invoke("nc_get_clip_transform", targetArgs(target)) { payload ->
            Transform(
                enabled = payload.optInt("enabled", 0) != 0,
                scale = payload.optDouble("scale", 1.0),
                x = payload.optDouble("pos_x", 0.0),
                y = payload.optDouble("pos_y", 0.0),
                rotation = payload.optDouble("rotation", 0.0),
                anchor = payload.optInt("anchor", 4)
            )
        }

    fun insertKeyframe(target: ClipTarget, property: Int, timeUs: Long, value: Double): Result<Unit> {
        if (property !in 0..5 || timeUs < 0L || !value.isFinite()) {
            return Result(false, error = "关键帧参数非法")
        }
        return invoke(
            "nc_insert_keyframe",
            targetArgs(target).put("property", property).put("time_us", timeUs).put("value", value)
        ) { Unit }
    }

    fun keyframeCount(target: ClipTarget, property: Int): Result<Int> {
        if (property !in 0..5) return Result(false, error = "关键帧属性编号非法")
        return invoke(
            "nc_keyframe_count",
            targetArgs(target).put("property", property)
        ) { it.optInt("count", 0) }
    }

    fun keyframeAt(target: ClipTarget, property: Int, index: Int): Result<Keyframe> {
        if (property !in 0..5 || index < 0) return Result(false, error = "关键帧索引非法")
        return invoke(
            "nc_keyframe_get_ex",
            targetArgs(target).put("property", property).put("index", index)
        ) { payload ->
            Keyframe(
                timeUs = payload.optLong("time_us", 0L),
                value = payload.optDouble("value", 0.0),
                easing = payload.optInt("easing", 0),
                c1x = payload.optDouble("c1x", 0.0),
                c1y = payload.optDouble("c1y", 0.0),
                c2x = payload.optDouble("c2x", 1.0),
                c2y = payload.optDouble("c2y", 1.0)
            )
        }
    }

    private fun targetArgs(target: ClipTarget) =
        JSONObject().put("track_index", target.trackIndex).put("clip_id", target.clipId)

    private fun <T> invoke(
        function: String,
        args: JSONObject,
        decode: (JSONObject) -> T
    ): Result<T> {
        return try {
            val response = JSONObject(bridge.call(function, args.toString()))
            if (!response.optBoolean("ok", false)) {
                Result(false, error = response.optString("message", "引擎调用失败"))
            } else {
                val raw = response.opt("result")
                val payload = when (raw) {
                    is JSONObject -> raw
                    is String -> runCatching { JSONObject(raw) }.getOrDefault(JSONObject())
                    else -> JSONObject()
                }
                Result(true, decode(payload))
            }
        } catch (error: Exception) {
            Result(false, error = error.message ?: "引擎响应解析失败")
        }
    }
}
