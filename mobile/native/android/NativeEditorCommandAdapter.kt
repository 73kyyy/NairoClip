package com.nairoclip.app

import org.json.JSONObject

/**
 * Narrow adapter for the keyframe C ABI operations already allowlisted by DispatchEdit.
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

    data class Result<T>(val ok: Boolean, val value: T? = null, val error: String? = null)

    fun insertKeyframe(target: ClipTarget, property: Int, timeUs: Long, value: Double): Result<Unit> {
        if (property !in 0..5 || timeUs < 0L || !value.isFinite()) {
            return Result(false, error = "关键帧参数非法")
        }
        return invoke(
            "nc_insert_keyframe",
            targetArgs(target).put("property", property).put("time_us", timeUs).put("value", value)
        ) { Unit }
    }


    /**
     * Inserts the current native transform value at the supplied project time.
     * Only insertion is exposed by the current JNI dispatcher; keyframe read,
     * delete and easing operations are intentionally not fabricated here.
     */
    fun insertTransformKeyframe(
        target: ClipTarget,
        parameter: NativeParameter,
        transform: NativeTransform,
        playheadUs: Long
    ): Result<Unit> {
        val value = when (parameter) {
            NativeParameter.SCALE -> transform.scale / 100.0f
            NativeParameter.X -> transform.x
            NativeParameter.Y -> transform.y
            NativeParameter.ROTATION -> transform.rotation
        }
        return insertKeyframe(target, parameter.engineProperty, playheadUs, value.toDouble())
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
