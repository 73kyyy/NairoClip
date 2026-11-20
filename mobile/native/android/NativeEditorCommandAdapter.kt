package com.nairoclip.app

import org.json.JSONObject

/**
 * Narrow adapter for native timeline, transform and keyframe C ABI operations
 * explicitly allowlisted by the Android JNI DispatchEdit function.
 *
 * This class deliberately does not own an editor handle or bypass the bridge's
 * state lock. Mutations that need a coherent post-edit model hold the bridge
 * state lock across validation, dispatch and snapshot refresh. Only operations
 * with an existing nc_* dispatcher branch are exposed here.
 */
class NativeEditorCommandAdapter(private val bridge: NativeEngineBridge) {

    data class ClipTarget(val trackIndex: Int, val clipId: Long) {
        init {
            require(trackIndex >= 0) { "轨道索引不能为负数" }
            require(clipId > 0L) { "片段 ID 必须大于 0" }
        }
    }

    data class Result<T>(val ok: Boolean, val value: T? = null, val error: String? = null)

    data class SpeedCurvePoint(
        val sourceSeconds: Double,
        val speed: Double,
        val easing: Int
    )

    data class ClipAudioState(
        val gainDb: Double,
        val fadeInUs: Long,
        val fadeOutUs: Long
    )


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

    data class ClipMutationResult(
        val editorState: EditorStateSnapshot,
        val newClipId: Long? = null
    )

    /**
     * Sets playback speed as an exact positive rational. The engine contract
     * permits 0.1x..100x; the UI may expose a narrower common range later.
     */
    fun setClipSpeedAndReadEditorState(
        target: ClipTarget,
        numerator: Long,
        denominator: Long
    ): Result<EditorStateSnapshot> {
        if (numerator <= 0L || denominator <= 0L) {
            return Result(false, error = "速度分子和分母必须为正数")
        }
        val speed = numerator.toDouble() / denominator.toDouble()
        if (!speed.isFinite() || speed !in 0.1..100.0) {
            return Result(false, error = "播放速度必须在 0.1x–100x 范围内")
        }
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) {
                return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            }
            val track = before.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) {
                return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            }
            if (track.clips.none { it.id == target.clipId }) {
                return@withEditorStateLock Result(false, error = "目标片段已不存在")
            }
            val changed = invoke(
                "nc_set_clip_speed",
                targetArgs(target).put("num", numerator).put("den", denominator)
            ) { Unit }
            if (!changed.ok) {
                return@withEditorStateLock Result(false, error = changed.error ?: "设置播放速度失败")
            }
            readEditorStateSnapshotLocked()
        }
    }

    /**
     * Applies one of the engine-defined speed-curve presets. source duration
     * remains zero so the C ABI resolves the clip's authoritative source range.
     */
    fun applySpeedCurvePresetAndReadEditorState(
        target: ClipTarget,
        preset: Int
    ): Result<EditorStateSnapshot> {
        if (preset !in 0..6) return Result(false, error = "变速曲线预设编号无效")
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) {
                return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            }
            val track = before.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) {
                return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            }
            if (track.clips.none { it.id == target.clipId }) {
                return@withEditorStateLock Result(false, error = "目标片段已不存在")
            }
            val changed = invoke(
                "nc_apply_speed_curve_preset",
                targetArgs(target).put("preset", preset).put("source_duration_us", 0L)
            ) { Unit }
            if (!changed.ok) {
                return@withEditorStateLock Result(false, error = changed.error ?: "应用变速曲线失败")
            }
            readEditorStateSnapshotLocked()
        }
    }

    /** Reads authoritative clip audio settings from the core model. */
    fun readClipAudio(target: ClipTarget): Result<ClipAudioState> =
        invoke("nc_get_clip_audio", targetArgs(target)) { payload ->
            ClipAudioState(
                gainDb = payload.getDouble("gain_db"),
                fadeInUs = payload.getLong("fade_in_us"),
                fadeOutUs = payload.getLong("fade_out_us")
            ).also {
                require(it.gainDb.isFinite() && it.gainDb in -60.0..12.0)
                require(it.fadeInUs >= 0L && it.fadeOutUs >= 0L)
            }
        }

    /** Applies clip gain and fades after validating against the selected clip duration. */
    fun setClipAudioAndReadEditorState(
        target: ClipTarget,
        audio: ClipAudioState
    ): Result<EditorStateSnapshot> {
        if (!audio.gainDb.isFinite() || audio.gainDb !in -60.0..12.0 ||
            audio.fadeInUs < 0L || audio.fadeOutUs < 0L
        ) return Result(false, error = "音频增益或淡入淡出参数非法")
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            val track = before.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            val clip = track.clips.firstOrNull { it.id == target.clipId }
                ?: return@withEditorStateLock Result(false, error = "目标片段已不存在")
            if (audio.fadeInUs > clip.durationUs || audio.fadeOutUs > clip.durationUs ||
                audio.fadeInUs > clip.durationUs - audio.fadeOutUs
            ) return@withEditorStateLock Result(false, error = "淡入与淡出总时长不能超过片段时长")
            val changed = invoke(
                "nc_set_clip_audio",
                targetArgs(target)
                    .put("gain_db", audio.gainDb)
                    .put("fade_in_us", audio.fadeInUs)
                    .put("fade_out_us", audio.fadeOutUs)
            ) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error ?: "设置音频参数失败")
            readEditorStateSnapshotLocked()
        }
    }

    /** Reads current per-clip denoise strength (0=off, 1..3=weak/medium/strong). */
    fun readClipDenoise(target: ClipTarget): Result<Int> {
        val result = invoke("nc_get_clip_denoise", targetArgs(target)) { it.getInt("strength") }
        if (!result.ok) return result
        val strength = result.value ?: return Result(false, error = "降噪强度缺失")
        return if (strength in 0..3) Result(true, strength) else Result(false, error = "引擎返回的降噪强度非法")
    }

    fun setClipDenoiseAndReadEditorState(target: ClipTarget, strength: Int): Result<EditorStateSnapshot> {
        if (strength !in 0..3) return Result(false, error = "降噪强度必须为 0–3")
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            val track = before.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            if (track.clips.none { it.id == target.clipId }) {
                return@withEditorStateLock Result(false, error = "目标片段已不存在")
            }
            val changed = invoke("nc_set_clip_denoise", targetArgs(target).put("strength", strength)) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error ?: "设置降噪失败")
            readEditorStateSnapshotLocked()
        }
    }

    /** Reads the engine-owned speed curve without inventing a default curve. */
    fun readSpeedCurve(target: ClipTarget): Result<List<SpeedCurvePoint>> {
        return bridge.withEditorStateLock {
            val countResult = invoke("nc_clip_speed_curve_count", targetArgs(target)) {
                it.getInt("count")
            }
            if (!countResult.ok) return@withEditorStateLock Result(false, error = countResult.error)
            val count = countResult.value ?: return@withEditorStateLock Result(false, error = "速度曲线点数缺失")
            if (count !in 0..256) return@withEditorStateLock Result(false, error = "速度曲线控制点数量超出安全上限")
            val points = ArrayList<SpeedCurvePoint>(count)
            for (index in 0 until count) {
                val point = invoke(
                    "nc_clip_speed_curve_get",
                    targetArgs(target).put("index", index)
                ) { payload ->
                    SpeedCurvePoint(
                        sourceSeconds = payload.getDouble("source_seconds"),
                        speed = payload.getDouble("speed"),
                        easing = payload.getInt("easing")
                    ).also {
                        require(it.sourceSeconds.isFinite() && it.sourceSeconds >= 0.0)
                        require(it.speed.isFinite() && it.speed in 0.1..10.0)
                        require(it.easing in 0..4)
                    }
                }
                if (!point.ok) return@withEditorStateLock Result(false, error = point.error)
                points += point.value ?: return@withEditorStateLock Result(false, error = "速度曲线控制点数据缺失")
            }
            if (points.zipWithNext().any { (left, right) -> right.sourceSeconds <= left.sourceSeconds }) {
                return@withEditorStateLock Result(false, error = "引擎返回的速度曲线时间顺序非法")
            }
            Result(true, points)
        }
    }

    /** Replaces the engine curve using validated source-time/speed/easing arrays. */
    fun setSpeedCurveAndReadEditorState(
        target: ClipTarget,
        points: List<SpeedCurvePoint>
    ): Result<EditorStateSnapshot> {
        if (points.size !in 2..256 ||
            points.any { !it.sourceSeconds.isFinite() || it.sourceSeconds < 0.0 ||
                !it.speed.isFinite() || it.speed !in 0.1..10.0 || it.easing !in 0..4 } ||
            points.zipWithNext().any { (left, right) -> right.sourceSeconds <= left.sourceSeconds }
        ) return Result(false, error = "速度曲线控制点参数非法")
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            val track = before.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            if (track.clips.none { it.id == target.clipId }) {
                return@withEditorStateLock Result(false, error = "目标片段已不存在")
            }
            val args = targetArgs(target)
                .put("times", org.json.JSONArray(points.map { it.sourceSeconds }))
                .put("speeds", org.json.JSONArray(points.map { it.speed }))
                .put("easings", org.json.JSONArray(points.map { it.easing }))
            val changed = invoke("nc_set_clip_speed_curve_points", args) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error ?: "设置速度曲线失败")
            readEditorStateSnapshotLocked()
        }
    }

    /** Removes the selected clip and returns the authoritative post-edit state. */
    fun removeClipAndReadEditorState(target: ClipTarget): Result<ClipMutationResult> =
        mutateClipAndReadEditorState(target, "nc_remove_clip")

    /** Duplicates the selected clip immediately after its source clip. */
    fun duplicateClipAndReadEditorState(target: ClipTarget): Result<ClipMutationResult> =
        mutateClipAndReadEditorState(target, "nc_duplicate_clip") { payload ->
            payload.getLong("clip_id").takeIf { it > 0L }
                ?: throw IllegalArgumentException("复制结果缺少有效片段 ID")
        }

    /** Ripple-deletes the selected clip on its current track. */
    fun rippleDeleteClipAndReadEditorState(target: ClipTarget): Result<ClipMutationResult> =
        mutateClipAndReadEditorState(target, "nc_ripple_delete")

    private fun mutateClipAndReadEditorState(
        target: ClipTarget,
        operation: String,
        decodeNewClipId: (JSONObject) -> Long? = { null }
    ): Result<ClipMutationResult> = bridge.withEditorStateLock {
        val before = readTimelineSnapshotLocked()
        if (!before.ok) {
            return@withEditorStateLock Result(
                false,
                error = before.error ?: "编辑前无法读取工程时间轴"
            )
        }
        val track = before.value?.tracks
            ?.firstOrNull { it.engineIndex == target.trackIndex }
            ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
        if (track.locked) {
            return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
        }
        if (track.clips.none { it.id == target.clipId }) {
            return@withEditorStateLock Result(false, error = "目标片段已不存在")
        }

        val changed = invoke(operation, targetArgs(target), decodeNewClipId)
        if (!changed.ok) {
            return@withEditorStateLock Result(
                false,
                error = changed.error ?: "片段操作失败"
            )
        }
        val state = readEditorStateSnapshotLocked()
        if (!state.ok) {
            return@withEditorStateLock Result(
                false,
                error = state.error ?: "操作已执行，但编辑后状态读取失败"
            )
        }
        Result(true, ClipMutationResult(state.value!!, changed.value))
    }

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

    /**
     * Splits the selected clip at an absolute timeline timestamp and returns
     * the authoritative post-edit timeline/history snapshot. The JNI dispatcher
     * explicitly allowlists the nc_split_clip alias; keep the timestamp in
     * project timeline microseconds to match the existing editor command path.
     */
    fun splitClipAndReadEditorState(
        target: ClipTarget,
        timelineTimeUs: Long
    ): Result<EditorStateSnapshot> {
        if (timelineTimeUs <= 0L) {
            return Result(false, error = "分割位置必须大于 0")
        }
        return bridge.withEditorStateLock {
            // Validate against the authoritative pre-edit snapshot while holding
            // the same lock as the mutation. A stale selection, wrong track, or
            // playhead outside the clip must not reach the native split call.
            val before = readTimelineSnapshotLocked()
            if (!before.ok) {
                return@withEditorStateLock Result(
                    false,
                    error = before.error ?: "分割前无法读取工程时间轴"
                )
            }
            val track = before.value?.tracks
                ?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            val clip = track.clips.firstOrNull { it.id == target.clipId }
                ?: return@withEditorStateLock Result(false, error = "目标片段已不存在")
            if (clip.startUs > Long.MAX_VALUE - clip.durationUs) {
                return@withEditorStateLock Result(false, error = "目标片段时间范围溢出")
            }
            val clipEndUs = clip.startUs + clip.durationUs
            if (timelineTimeUs <= clip.startUs || timelineTimeUs >= clipEndUs) {
                return@withEditorStateLock Result(
                    false,
                    error = "分割位置必须位于所选片段内部"
                )
            }

            val changed = invoke(
                "nc_split_clip",
                targetArgs(target).put("at_us", timelineTimeUs)
            ) { Unit }
            if (!changed.ok) {
                return@withEditorStateLock Result(
                    false,
                    error = changed.error ?: "分割失败"
                )
            }
            readEditorStateSnapshotLocked()
        }
    }

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
    /**
     * Transform keyframes store scale as a unit multiplier (not UI percent).
     * Keep generic insert/read paths aligned with static transform hard bounds,
     * so callers cannot bypass validation by avoiding insertTransformKeyframe.
     */
    private fun transformKeyframeValueError(property: Int, value: Double): String? {
        if (!value.isFinite()) return "关键帧值必须是有限数值"
        return when (property) {
            NativeParameter.SCALE.engineProperty -> {
                val min = NativeTransformRange.SCALE_MIN_PERCENT.toDouble() / 100.0
                val max = NativeTransformRange.SCALE_MAX_PERCENT.toDouble() / 100.0
                if (value !in min..max) "缩放关键帧引擎值必须在 0.05..10.0（5%–1000%）范围内" else null
            }
            NativeParameter.X.engineProperty, NativeParameter.Y.engineProperty ->
                if (value !in NativeTransformRange.POSITION_MIN.toDouble()..NativeTransformRange.POSITION_MAX.toDouble())
                    "位置关键帧必须在 -10..10 范围内" else null
            NativeParameter.ROTATION.engineProperty ->
                if (value !in NativeTransformRange.ROTATION_MIN_DEGREES.toDouble()..NativeTransformRange.ROTATION_MAX_DEGREES.toDouble())
                    "旋转关键帧必须在 -3600°..3600° 范围内" else null
            else -> null
        }
    }

    fun insertKeyframe(
        target: ClipTarget,
        property: Int,
        clipLocalTimeUs: Long,
        value: Double
    ): Result<Unit> {
        if (property !in 0..5 || clipLocalTimeUs < 0L || !value.isFinite()) {
            return Result(false, error = "关键帧参数非法")
        }
        transformKeyframeValueError(property, value)?.let {
            return Result(false, error = it)
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
                    transformKeyframeValueError(parameter.engineProperty, frame.value)?.let {
                        throw IllegalArgumentException(it)
                    }
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

    data class EffectSpec(
        val name: String,
        val unit: String,
        val hint: String,
        val min: Double,
        val max: Double,
        val defaultValue: Double
    )

    data class ClipFilterState(
        val style: Int,
        val intensity: Double,
        val enabled: Boolean
    )

    data class ClipMaskState(
        val shape: Int,
        val centerX: Double,
        val centerY: Double,
        val rotationDegrees: Double,
        val width: Double,
        val height: Double,
        val feather: Double,
        val cornerRadius: Double,
        val invert: Boolean,
        val enabled: Boolean
    )

    /** Reads the authoritative mask values; disabled masks remain distinguishable from defaults. */
    fun readClipMask(target: ClipTarget): Result<ClipMaskState> =
        bridge.withEditorStateLock {
            val valid = validateEditableClipLocked(target)
            if (!valid.ok) return@withEditorStateLock Result(false, error = valid.error)
            invoke("mask_get", targetArgs(target)) { payload ->
                fun finite(name: String): Double {
                    val raw = payload.opt(name) as? Number
                        ?: throw IllegalArgumentException("蒙版字段 $name 缺失")
                    return raw.toDouble().also {
                        require(it.isFinite()) { "蒙版字段 $name 不是有限数值" }
                    }
                }
                val shape = finite("shape")
                val centerX = finite("center_x")
                val centerY = finite("center_y")
                val rotation = finite("rotation_degrees")
                val width = finite("width")
                val height = finite("height")
                val feather = finite("feather")
                val cornerRadius = finite("corner_radius")
                val invert = finite("invert")
                val enabled = finite("enabled")
                require(shape % 1.0 == 0.0 && shape in 0.0..6.0)
                // The core Mask contract only bounds feather/cornerRadius to 0..0.5.
                // Width/height are normalized shape-specific dimensions and are not capped at 1.
                require(feather in 0.0..0.5 && cornerRadius in 0.0..0.5)
                require(invert == 0.0 || invert == 1.0)
                require(enabled == 0.0 || enabled == 1.0)
                ClipMaskState(
                    shape.toInt(), centerX, centerY, rotation, width, height,
                    feather, cornerRadius, invert == 1.0, enabled == 1.0
                )
            }
        }

    fun readClipFilter(target: ClipTarget): Result<ClipFilterState> =
        bridge.withEditorStateLock {
            val valid = validateEditableClipLocked(target)
            if (!valid.ok) return@withEditorStateLock Result(false, error = valid.error)
            invoke("filter_get", targetArgs(target)) { payload ->
                val style = payload.opt("style") as? Number
                    ?: throw IllegalArgumentException("滤镜风格缺失")
                val intensity = payload.opt("intensity") as? Number
                    ?: throw IllegalArgumentException("滤镜强度缺失")
                val enabled = payload.opt("enabled") as? Number
                    ?: throw IllegalArgumentException("滤镜启用状态缺失")
                val styleValue = style.toDouble()
                val intensityValue = intensity.toDouble()
                val enabledValue = enabled.toDouble()
                require(styleValue % 1.0 == 0.0 && styleValue in 0.0..13.0)
                require(intensityValue.isFinite() && intensityValue in 0.0..100.0)
                require(enabledValue == 0.0 || enabledValue == 1.0)
                ClipFilterState(styleValue.toInt(), intensityValue, enabledValue == 1.0)
            }
        }

    fun setClipFilterAndRead(
        target: ClipTarget,
        style: Int,
        intensity: Double
    ): Result<ClipFilterState> {
        if (style !in 0..13 || !intensity.isFinite() || intensity !in 0.0..100.0) {
            return Result(false, error = "滤镜参数超出允许范围")
        }
        return bridge.withEditorStateLock {
            val valid = validateEditableClipLocked(target)
            if (!valid.ok) return@withEditorStateLock Result(false, error = valid.error)
            val changed = invoke(
                "filter",
                targetArgs(target).put("style", style).put("intensity", intensity)
            ) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error)
            readClipFilter(target)
        }
    }

    fun readEffectSpec(type: Int): Result<EffectSpec> {
        if (type !in 0..4096) return Result(false, error = "特效类型无效")
        return invoke("nc_effect_spec", JSONObject().put("type", type)) { payload ->
            EffectSpec(
                name = payload.getString("name"),
                unit = payload.optString("unit", ""),
                hint = payload.optString("hint", ""),
                min = payload.getDouble("min"),
                max = payload.getDouble("max"),
                defaultValue = payload.getDouble("def")
            ).also {
                require(it.name.isNotBlank())
                require(it.min.isFinite() && it.max.isFinite() && it.min <= it.max)
                require(it.min.toFloat().isFinite() && it.max.toFloat().isFinite())
                require(it.defaultValue.isFinite() && it.defaultValue in it.min..it.max &&
                    it.defaultValue.toFloat().isFinite())
            }
        }
    }

    /** Adds an engine-defined clip effect; type/value bounds come from nc_effect_spec. */
    fun addEffect(target: ClipTarget, type: Int, value: Double): Result<Long> {
        if (!value.isFinite()) return Result(false, error = "特效参数必须是有限数值")
        val spec = readEffectSpec(type)
        if (!spec.ok) return Result(false, error = spec.error ?: "读取特效参数范围失败")
        val range = spec.value ?: return Result(false, error = "特效参数范围缺失")
        if (value !in range.min..range.max) return Result(false, error = "特效参数超出引擎允许范围")
        return bridge.withEditorStateLock {
            val valid = validateEditableClipLocked(target)
            if (!valid.ok) return@withEditorStateLock Result(false, error = valid.error)
            invoke("nc_add_effect", targetArgs(target).put("type", type).put("value", value)) {
                it.getLong("value").also { id -> require(id > 0L) }
            }
        }
    }

    fun updateEffect(target: ClipTarget, effect: NativeClipEffect, value: Double): Result<Unit> {
        if (effect.id <= 0L || !value.isFinite()) return Result(false, error = "特效参数非法")
        val spec = readEffectSpec(effect.type)
        if (!spec.ok) return Result(false, error = spec.error ?: "读取特效参数范围失败")
        val range = spec.value ?: return Result(false, error = "特效参数范围缺失")
        if (value !in range.min..range.max) return Result(false, error = "特效参数超出引擎允许范围")
        return mutateEffect(target, "effect_update", JSONObject()
            .put("effect_id", effect.id).put("type", effect.type).put("value", value))
    }

    fun setEffectEnabled(target: ClipTarget, effectId: Long, enabled: Boolean): Result<Unit> {
        if (effectId <= 0L) return Result(false, error = "特效 ID 无效")
        return mutateEffect(target, "effect_enabled", JSONObject()
            .put("effect_id", effectId).put("enabled", if (enabled) 1 else 0))
    }

    fun moveEffect(target: ClipTarget, effectId: Long, direction: Int): Result<Unit> {
        if (effectId <= 0L) return Result(false, error = "特效 ID 无效")
        if (direction !in -1..1 || direction == 0) return Result(false, error = "特效移动方向无效")
        return mutateEffect(target, "effect_move", JSONObject()
            .put("effect_id", effectId).put("direction", direction))
    }

    fun removeEffect(target: ClipTarget, effectId: Long): Result<Unit> {
        if (effectId <= 0L) return Result(false, error = "特效 ID 无效")
        return mutateEffect(target, "effect_remove", JSONObject().put("effect_id", effectId))
    }

    private fun mutateEffect(target: ClipTarget, operation: String, extra: JSONObject): Result<Unit> =
        bridge.withEditorStateLock {
            val valid = validateEditableClipLocked(target)
            if (!valid.ok) return@withEditorStateLock Result(false, error = valid.error)
            val args = targetArgs(target)
            val keys = extra.keys()
            while (keys.hasNext()) {
                val key = keys.next()
                args.put(key, extra.get(key))
            }
            invoke(operation, args) { Unit }
        }

    private fun validateEditableClipLocked(target: ClipTarget): Result<Unit> {
        val snapshot = readTimelineSnapshotLocked()
        if (!snapshot.ok) return Result(false, error = snapshot.error ?: "读取工程时间轴失败")
        val track = snapshot.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
            ?: return Result(false, error = "目标轨道已不存在")
        if (track.locked) return Result(false, error = "目标轨道已锁定，不能修改")
        if (track.clips.none { it.id == target.clipId }) return Result(false, error = "目标片段已不存在")
        return Result(true)
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
