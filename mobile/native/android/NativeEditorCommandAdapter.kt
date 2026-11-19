package com.nairoclip.app

import org.json.JSONObject

/**
 * Narrow adapter for the keyframe C ABI operations explicitly allowlisted by
 * the Android JNI DispatchEdit function.
 *
 * This class deliberately does not own an editor handle or bypass the bridge's
 * state lock. All calls go through NativeEngineBridge.call(), which serializes
 * access to the non-thread-safe NcEditor instance. Only operations with an
 * existing nc_* dispatcher branch are exposed here.
 */
class NativeEditorCommandAdapter(private val bridge: NativeEngineBridge) {

    data class ClipTarget(val trackIndex: Int, val clipId: Long) {
        init {
            require(trackIndex >= 0) { "轨道索引不能为负数" }
            require(clipId > 0L) { "片段 ID 必须大于 0" }
        }
    }

    data class Result<T>(val ok: Boolean, val value: T? = null, val error: String? = null)

    data class TransformState(
        val transform: NativeTransform,
        val enabled: Boolean,
        val anchor: Int
    )

    fun getTransform(target: ClipTarget): Result<TransformState> = try {
        val response = JSONObject(bridge.editorTransformGet(target.trackIndex, target.clipId))
        if (!response.optBoolean("ok", false)) {
            Result(false, error = response.optString("message", "读取变换失败"))
        } else {
            val raw = response.opt("result")
            val payload = when (raw) {
                is JSONObject -> raw
                is String -> JSONObject(raw)
                else -> JSONObject()
            }
            val scale = payload.getDouble("scale")
            val x = payload.getDouble("pos_x")
            val y = payload.getDouble("pos_y")
            val rotation = payload.getDouble("rotation")
            val scalePercent = scale * 100.0
            // The engine reports Double values, while Compose sliders and the
            // native transform model use Float. Finite Doubles can still
            // overflow to Float infinity; reject them before exposing UI state.
            require(scale.isFinite() && scale > 0.0 && scalePercent.isFinite())
            require(x.isFinite() && y.isFinite() && rotation.isFinite())
            require(
                scalePercent.toFloat().isFinite() &&
                    x.toFloat().isFinite() &&
                    y.toFloat().isFinite() &&
                    rotation.toFloat().isFinite()
            )
            Result(
                true,
                TransformState(
                    transform = NativeTransform(
                        x = x.toFloat(),
                        y = y.toFloat(),
                        scale = scalePercent.toFloat(),
                        rotation = rotation.toFloat()
                    ),
                    enabled = payload.optInt("enabled", 1) != 0,
                    anchor = payload.optInt("anchor", 4).coerceIn(0, 8)
                )
            )
        }
    } catch (error: Exception) {
        Result(false, error = error.message ?: "变换响应解析失败")
    }

    fun setTransform(
        target: ClipTarget,
        transform: NativeTransform,
        enabled: Boolean = true,
        anchor: Int = 4
    ): Result<Unit> = try {
        val response = JSONObject(
            bridge.editorTransformSet(target.trackIndex, target.clipId, transform, enabled, anchor)
        )
        if (response.optBoolean("ok", false)) Result(true)
        else Result(false, error = response.optString("message", "写入变换失败"))
    } catch (error: Exception) {
        Result(false, error = error.message ?: "变换写入失败")
    }

    data class ClipAnimationState(
        val transform: NativeTransform,
        val transformEnabled: Boolean,
        val anchor: Int,
        val keyframes: Map<NativeParameter, List<Keyframe>>,
        val keyedParameters: Set<NativeParameter>
    )

    /**
     * Loads a consistent UI-facing snapshot for the selected clip.
     * The caller should invoke this on selection/project refresh, not on every
     * slider frame; writes remain separate commands to preserve undo semantics.
     */
    fun readAnimationState(target: ClipTarget): Result<ClipAnimationState> =
        bridge.withEditorStateLock {
            // Keep transform + all four keyframe channels on one engine revision.
            // Without the outer lock, another editor command could mutate the clip
            // between JNI calls and produce a mixed-time UI snapshot.
            val transformResult = getTransform(target)
            if (!transformResult.ok) {
                return@withEditorStateLock Result(false, error = transformResult.error)
            }
            val transform = transformResult.value
                ?: return@withEditorStateLock Result(false, error = "片段变换数据缺失")
            val channels = LinkedHashMap<NativeParameter, List<Keyframe>>()
            for (parameter in NativeParameter.values()) {
                val result = keyframes(target, parameter)
                if (!result.ok) return@withEditorStateLock Result(false, error = result.error)
                channels[parameter] = result.value
                    ?: return@withEditorStateLock Result(false, error = "关键帧通道数据缺失")
            }
            Result(
                true,
                ClipAnimationState(
                    transform = transform.transform,
                    transformEnabled = transform.enabled,
                    anchor = transform.anchor,
                    keyframes = channels,
                    keyedParameters = channels.filterValues { it.isNotEmpty() }.keys
                )
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


    /**
     * Inserts the current native transform value at the supplied project time.
     * The engine stores scale as a unit multiplier while the native panel uses
     * percentages, so scale is normalized before crossing the JNI boundary.
     */
    fun insertTransformKeyframe(
        target: ClipTarget,
        parameter: NativeParameter,
        transform: NativeTransform,
        playheadUs: Long
    ): Result<Unit> {
        val value = when (parameter) {
            NativeParameter.SCALE -> {
                // Scale is a positive multiplier in the engine. The UI uses
                // percentages, but callers can invoke this adapter without the
                // slider, so enforce the engine invariant at this boundary.
                if (!transform.scale.isFinite() || transform.scale <= 0f) {
                    return Result(false, error = "缩放关键帧值必须是有限正数")
                }
                transform.scale / 100.0f
            }
            NativeParameter.X -> transform.x
            NativeParameter.Y -> transform.y
            NativeParameter.ROTATION -> transform.rotation
        }
        return insertKeyframe(target, parameter.engineProperty, playheadUs, value.toDouble())
    }

    data class Keyframe(
        val timeUs: Long,
        val value: Double,
        val easing: Int,
        val c1x: Double,
        val c1y: Double,
        val c2x: Double,
        val c2y: Double
    )

    fun keyframeCount(target: ClipTarget, parameter: NativeParameter): Result<Int> =
        invoke(
            "nc_keyframe_count",
            targetArgs(target).put("property", parameter.engineProperty)
        ) { payload -> payload.optInt("count", -1).also { require(it >= 0) } }

    fun keyframes(target: ClipTarget, parameter: NativeParameter): Result<List<Keyframe>> {
        val countResult = keyframeCount(target, parameter)
        if (!countResult.ok) return Result(false, error = countResult.error)
        val count = countResult.value ?: return Result(false, error = "关键帧数量缺失")
        if (count > MAX_KEYFRAMES_PER_CHANNEL) {
            return Result(false, error = "关键帧数量超出安全读取上限")
        }
        val frames = ArrayList<Keyframe>(count)
        for (index in 0 until count) {
            val result = invoke(
                "nc_keyframe_get_ex",
                targetArgs(target)
                    .put("property", parameter.engineProperty)
                    .put("index", index)
            ) { payload ->
                Keyframe(
                    timeUs = payload.getLong("time_us"),
                    value = payload.getDouble("value"),
                    easing = payload.getInt("easing"),
                    c1x = payload.optDouble("c1x", 0.0),
                    c1y = payload.optDouble("c1y", 0.0),
                    c2x = payload.optDouble("c2x", 1.0),
                    c2y = payload.optDouble("c2y", 1.0)
                ).also { frame ->
                    require(frame.timeUs >= 0L && frame.value.isFinite())
                    require(frame.easing in 0..5)
                    require(frame.c1x.isFinite() && frame.c1y.isFinite())
                    require(frame.c2x.isFinite() && frame.c2y.isFinite())
                    // The curve preview is drawn by Compose Canvas using
                    // Float coordinates; reject finite Doubles that overflow.
                    require(
                        frame.c1x.toFloat().isFinite() &&
                            frame.c1y.toFloat().isFinite() &&
                            frame.c2x.toFloat().isFinite() &&
                            frame.c2y.toFloat().isFinite()
                    )
                }
            }
            if (!result.ok) return Result(false, error = result.error)
            frames += result.value ?: return Result(false, error = "关键帧数据缺失")
        }
        return Result(true, frames)
    }

    fun removeKeyframe(
        target: ClipTarget,
        parameter: NativeParameter,
        timeUs: Long
    ): Result<Unit> {
        if (timeUs < 0L) return Result(false, error = "关键帧时间不能为负数")
        return invoke(
            "nc_remove_keyframe",
            targetArgs(target)
                .put("property", parameter.engineProperty)
                .put("time_us", timeUs)
        ) { Unit }
    }

    fun setKeyframeEasing(
        target: ClipTarget,
        parameter: NativeParameter,
        timeUs: Long,
        easing: Int,
        c1x: Double = 0.0,
        c1y: Double = 0.0,
        c2x: Double = 1.0,
        c2y: Double = 1.0
    ): Result<Unit> {
        if (timeUs < 0L || easing !in 0..5 ||
            !c1x.isFinite() || !c1y.isFinite() || !c2x.isFinite() || !c2y.isFinite() ||
            !c1x.toFloat().isFinite() || !c1y.toFloat().isFinite() ||
            !c2x.toFloat().isFinite() || !c2y.toFloat().isFinite()
        ) {
            return Result(false, error = "关键帧曲线参数非法")
        }
        if (easing == 5 && (c1x !in 0.0..1.0 || c2x !in 0.0..1.0)) {
            return Result(false, error = "贝塞尔控制点 X 必须在 0 到 1 之间")
        }
        return invoke(
            "nc_set_keyframe_easing",
            targetArgs(target)
                .put("property", parameter.engineProperty)
                .put("time_us", timeUs)
                .put("easing", easing)
                .put("c1x", c1x)
                .put("c1y", c1y)
                .put("c2x", c2x)
                .put("c2y", c2y)
        ) { Unit }
    }

    companion object {
        private const val MAX_KEYFRAMES_PER_CHANNEL = 4096
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
