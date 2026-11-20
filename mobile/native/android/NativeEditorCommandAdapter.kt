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
            // JNI serializes these fields as JSON numbers. Do not coerce strings,
            // default missing flags, or clamp an invalid anchor into a plausible
            // value: that would make a broken engine response look authoritative.
            fun requiredFiniteNumber(name: String): Double {
                val raw = payload.opt(name) as? Number
                    ?: throw IllegalArgumentException("变换字段 $name 缺失或类型错误")
                return raw.toDouble().also {
                    require(it.isFinite()) { "变换字段 $name 不是有限数值" }
                }
            }
            val scale = requiredFiniteNumber("scale")
            val x = requiredFiniteNumber("pos_x")
            val y = requiredFiniteNumber("pos_y")
            val rotation = requiredFiniteNumber("rotation")
            val enabledValue = requiredFiniteNumber("enabled")
            val anchorValue = requiredFiniteNumber("anchor")
            val scalePercent = scale * 100.0
            // The engine reports Double values, while Compose sliders and the
            // native transform model use Float. Finite Doubles can still
            // overflow to Float infinity; reject them before exposing UI state.
            require(
                scalePercent.isFinite() &&
                    scalePercent in NativeTransformRange.SCALE_MIN_PERCENT.toDouble()..NativeTransformRange.SCALE_MAX_PERCENT.toDouble()
            ) { "引擎缩放值超出 5%–1000% 范围" }
            require(
                x in NativeTransformRange.POSITION_MIN.toDouble()..NativeTransformRange.POSITION_MAX.toDouble() &&
                    y in NativeTransformRange.POSITION_MIN.toDouble()..NativeTransformRange.POSITION_MAX.toDouble()
            ) { "引擎位置值超出 X/Y -10..10 范围" }
            require(
                rotation in NativeTransformRange.ROTATION_MIN_DEGREES.toDouble()..NativeTransformRange.ROTATION_MAX_DEGREES.toDouble()
            ) { "引擎旋转值超出 -3600°..3600° 范围" }
            require(enabledValue == 0.0 || enabledValue == 1.0)
            require(
                anchorValue % 1.0 == 0.0 &&
                    anchorValue in NativeTransformRange.ANCHOR_MIN.toDouble()..NativeTransformRange.ANCHOR_MAX.toDouble()
            )
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
                    enabled = enabledValue == 1.0,
                    anchor = anchorValue.toInt()
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
        anchor: Int = NativeTransformRange.ANCHOR_CENTER
    ): Result<Unit> = try {
        val response = JSONObject(
            bridge.editorTransformSet(target.trackIndex, target.clipId, transform, enabled, anchor)
        )
        if (response.optBoolean("ok", false)) Result(true)
        else Result(false, error = response.optString("message", "写入变换失败"))
    } catch (error: Exception) {
        Result(false, error = error.message ?: "变换写入失败")
    }

    /**
     * Track visibility and mute are native editor commands already dispatched
     * by nc_jni.cpp. The UI passes the desired state, never a toggle opcode,
     * so retries and delayed snapshot refreshes remain idempotent.
     */
    fun setTrackVisibility(trackIndex: Int, visible: Boolean): Result<Unit> {
        if (trackIndex < 0) return Result(false, error = "轨道索引不能为负数")
        return invoke(
            "nc_set_track_hidden",
            JSONObject().put("track_index", trackIndex).put("hidden", !visible)
        ) { Unit }
    }

    fun setTrackMuted(trackIndex: Int, muted: Boolean): Result<Unit> {
        if (trackIndex < 0) return Result(false, error = "轨道索引不能为负数")
        return invoke(
            "nc_set_track_muted",
            JSONObject().put("track_index", trackIndex).put("muted", muted)
        ) { Unit }
    }

    /**
     * Reads the authoritative timeline snapshot while holding the same bridge
     * lock used by editor mutations. This prevents a concurrent command from
     * interleaving with the state JSON read.
     */
    fun readTimelineSnapshot(): Result<NativeTimelineSnapshot> =
        bridge.withEditorStateLock { readTimelineSnapshotLocked() }

    /**
     * Executes a track flag mutation and obtains the resulting timeline
     * snapshot as one serialized operation. The caller can publish the returned
     * snapshot directly instead of guessing the engine's accepted state.
     */
    fun setTrackVisibilityAndReadSnapshot(
        trackIndex: Int,
        visible: Boolean
    ): Result<NativeTimelineSnapshot> =
        mutateAndReadTimelineSnapshot { setTrackVisibility(trackIndex, visible) }

    fun setTrackMutedAndReadSnapshot(
        trackIndex: Int,
        muted: Boolean
    ): Result<NativeTimelineSnapshot> =
        mutateAndReadTimelineSnapshot { setTrackMuted(trackIndex, muted) }

    private fun mutateAndReadTimelineSnapshot(
        mutation: () -> Result<Unit>
    ): Result<NativeTimelineSnapshot> = bridge.withEditorStateLock {
        val changed = try {
            mutation()
        } catch (error: Exception) {
            Result(false, error = error.message ?: "轨道命令执行失败")
        }
        if (!changed.ok) {
            return@withEditorStateLock Result(
                false,
                error = changed.error ?: "轨道命令执行失败"
            )
        }
        readTimelineSnapshotLocked()
    }

    private fun readTimelineSnapshotLocked(): Result<NativeTimelineSnapshot> = try {
        val stateJson = bridge.state()
        val envelope = JSONObject(stateJson)
        if (!envelope.optBoolean("ok", false)) {
            Result(false, error = envelope.optString("message", "读取工程快照失败"))
        } else {
            val snapshot = NativeTimelineSnapshot.tryFromEngineState(stateJson)
                ?: return Result(false, error = "工程快照结构无效，保留当前时间轴状态")
            Result(true, snapshot)
        }
    } catch (error: Exception) {
        Result(false, error = error.message ?: "工程快照解析失败")
    }

    data class HistoryState(val canUndo: Boolean, val canRedo: Boolean)
    data class EditorStateSnapshot(
        val timeline: NativeTimelineSnapshot,
        val history: HistoryState
    )

    /** Reads timeline and history capability flags from one engine revision. */
    fun editorStateSnapshot(): Result<EditorStateSnapshot> =
        bridge.withEditorStateLock { readEditorStateSnapshotLocked() }

    fun historyState(): Result<HistoryState> = bridge.withEditorStateLock {
        val snapshot = readEditorStateSnapshotLocked()
        if (!snapshot.ok) Result(false, error = snapshot.error)
        else Result(true, snapshot.value?.history)
    }

    /**
     * Executes undo/redo and reads timeline + history from the resulting
     * engine revision under one bridge monitor. The host can publish both
     * models together instead of briefly showing stale clips/history controls.
     */
    fun undoAndReadEditorState(): Result<EditorStateSnapshot> =
        mutateAndReadEditorState { undo() }

    fun redoAndReadEditorState(): Result<EditorStateSnapshot> =
        mutateAndReadEditorState { redo() }

    private fun mutateAndReadEditorState(
        mutation: () -> Result<Unit>
    ): Result<EditorStateSnapshot> = bridge.withEditorStateLock {
        val changed = try {
            mutation()
        } catch (error: Exception) {
            Result(false, error = error.message ?: "历史命令执行失败")
        }
        if (!changed.ok) {
            return@withEditorStateLock Result(false, error = changed.error ?: "历史命令执行失败")
        }
        readEditorStateSnapshotLocked()
    }

    private fun readEditorStateSnapshotLocked(): Result<EditorStateSnapshot> {
        return try {
            val stateJson = bridge.state()
            val envelope = JSONObject(stateJson)
            if (!envelope.optBoolean("ok", false)) {
                Result(false, error = envelope.optString("message", "读取工程状态失败"))
            } else {
                val timeline = NativeTimelineSnapshot.tryFromEngineState(stateJson)
                val state = envelope.optJSONObject("result")
                val undoRaw = state?.opt("can_undo") as? Number
                val redoRaw = state?.opt("can_redo") as? Number
                val undoValue = undoRaw?.toDouble()
                val redoValue = redoRaw?.toDouble()
                if (timeline == null || undoValue == null || redoValue == null ||
                    !undoValue.isFinite() || !redoValue.isFinite() ||
                    undoValue % 1.0 != 0.0 || redoValue % 1.0 != 0.0 ||
                    undoValue !in 0.0..1.0 || redoValue !in 0.0..1.0
                ) {
                    Result(false, error = "工程快照或历史状态字段缺失/无效")
                } else {
                    Result(
                        true,
                        EditorStateSnapshot(
                            timeline = timeline,
                            history = HistoryState(undoValue == 1.0, redoValue == 1.0)
                        )
                    )
                }
            }
        } catch (error: Exception) {
            Result(false, error = error.message ?: "读取工程状态失败")
        }
    }

    /** Native history operations use the JNI DispatchEdit short-name routes. */
    fun undo(): Result<Unit> = decodeUnitResponse(bridge.editorUndo(), "撤销失败")

    fun redo(): Result<Unit> = decodeUnitResponse(bridge.editorRedo(), "重做失败")

    private fun decodeUnitResponse(json: String, fallback: String): Result<Unit> = try {
        val response = JSONObject(json)
        if (response.optBoolean("ok", false)) Result(true)
        else Result(false, error = response.optString("message", fallback))
    } catch (error: Exception) {
        Result(false, error = error.message ?: fallback)
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

    /**
     * Applies one editor mutation and reads the resulting animation snapshot
     * while retaining the bridge's reentrant state lock. Native UI hosts can use
     * this to publish the post-command state without a competing edit slipping
     * between the write and refresh. The mutation must target the supplied clip.
     */
    fun mutateAndReadAnimationState(
        target: ClipTarget,
        mutation: () -> Result<Unit>
    ): Result<ClipAnimationState> = bridge.withEditorStateLock {
        val changed = try {
            mutation()
        } catch (error: Exception) {
            Result(false, error = error.message ?: "编辑命令执行失败")
        }
        if (!changed.ok) {
            return@withEditorStateLock Result(false, error = changed.error ?: "编辑命令失败")
        }
        readAnimationState(target)
    }

    /**
     * Inserts at a clip-local timestamp, not the project timeline playhead.
     * Callers must convert project time using the selected clip's start and
     * playback speed before invoking this API.
     */
    fun insertKeyframe(
        target: ClipTarget,
        property: Int,
        clipLocalTimeUs: Long,
        value: Double
    ): Result<Unit> {
        if (property !in 0..5 || clipLocalTimeUs < 0L || !value.isFinite()) {
            return Result(false, error = "关键帧参数非法")
        }
        return invoke(
            "nc_insert_keyframe",
            targetArgs(target).put("property", property).put("time_us", clipLocalTimeUs).put("value", value)
        ) { Unit }
    }


    /**
     * Inserts the current native transform value at a clip-local timestamp.
     * Do not pass the project playhead directly: subtract the clip's timeline
     * start and account for its playback speed before calling this method.
     * The engine stores scale as a unit multiplier while the native panel uses
     * percentages, so scale is normalized before crossing the JNI boundary.
     */
    fun insertTransformKeyframe(
        target: ClipTarget,
        parameter: NativeParameter,
        transform: NativeTransform,
        clipLocalTimeUs: Long
    ): Result<Unit> {
        val value = when (parameter) {
            NativeParameter.SCALE -> {
                // Scale is stored as a positive multiplier in the engine while
                // the editor exposes percent. Keep keyframes inside the same
                // hard domain as static transform writes.
                if (!transform.scale.isFinite() ||
                    transform.scale !in NativeTransformRange.SCALE_MIN_PERCENT..NativeTransformRange.SCALE_MAX_PERCENT
                ) {
                    return Result(false, error = "缩放关键帧必须在 5%–1000% 范围内")
                }
                transform.scale / 100.0f
            }
            NativeParameter.X -> {
                if (!transform.x.isFinite() ||
                    transform.x !in NativeTransformRange.POSITION_MIN..NativeTransformRange.POSITION_MAX
                ) return Result(false, error = "X 轴关键帧必须在 -10..10 范围内")
                transform.x
            }
            NativeParameter.Y -> {
                if (!transform.y.isFinite() ||
                    transform.y !in NativeTransformRange.POSITION_MIN..NativeTransformRange.POSITION_MAX
                ) return Result(false, error = "Y 轴关键帧必须在 -10..10 范围内")
                transform.y
            }
            NativeParameter.ROTATION -> {
                if (!transform.rotation.isFinite() ||
                    transform.rotation !in NativeTransformRange.ROTATION_MIN_DEGREES..NativeTransformRange.ROTATION_MAX_DEGREES
                ) return Result(false, error = "旋转关键帧必须在 -3600°..3600° 范围内")
                transform.rotation
            }
        }
        return insertKeyframe(target, parameter.engineProperty, clipLocalTimeUs, value.toDouble())
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

    /** Removes a keyframe by its clip-local timestamp. */
    fun removeKeyframe(
        target: ClipTarget,
        parameter: NativeParameter,
        clipLocalTimeUs: Long
    ): Result<Unit> {
        if (clipLocalTimeUs < 0L) return Result(false, error = "关键帧时间不能为负数")
        return invoke(
            "nc_remove_keyframe",
            targetArgs(target)
                .put("property", parameter.engineProperty)
                .put("time_us", clipLocalTimeUs)
        ) { Unit }
    }

    fun setKeyframeEasing(
        target: ClipTarget,
        parameter: NativeParameter,
        clipLocalTimeUs: Long,
        easing: Int,
        c1x: Double = 0.0,
        c1y: Double = 0.0,
        c2x: Double = 1.0,
        c2y: Double = 1.0
    ): Result<Unit> {
        if (clipLocalTimeUs < 0L || easing !in 0..5 ||
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
                .put("time_us", clipLocalTimeUs)
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
