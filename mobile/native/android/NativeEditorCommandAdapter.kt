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

    data class ClipEditorPanelSnapshot(
        val transform: TransformState,
        val keyframes: Map<NativeParameter, List<Keyframe>>,
        val audio: ClipAudioState,
        val denoiseStrength: Int,
        val speedCurve: List<SpeedCurvePoint>
    )

    /**
     * Reads transform and all four keyframe channels under one bridge lock so
     * the host cannot combine values from different engine revisions.
     */
    fun readClipEditorPanelSnapshot(target: ClipTarget): Result<ClipEditorPanelSnapshot> = try {
        bridge.withEditorStateLock {
            val transformResult = getTransform(target)
            val transform = transformResult.value
            if (!transformResult.ok || transform == null) {
                return@withEditorStateLock Result(
                    false,
                    error = transformResult.error ?: "读取片段变换失败"
                )
            }
            val channels = linkedMapOf<NativeParameter, List<Keyframe>>()
            NativeParameter.values().forEach { parameter ->
                val channelResult = keyframes(target, parameter)
                val frames = channelResult.value
                if (!channelResult.ok || frames == null) {
                    return@withEditorStateLock Result(
                        false,
                        error = channelResult.error ?: "读取关键帧通道失败"
                    )
                }
                if (frames.isNotEmpty()) channels[parameter] = frames
            }
            val audioResult = readClipAudio(target)
            val audio = audioResult.value
            if (!audioResult.ok || audio == null) {
                return@withEditorStateLock Result(false, error = audioResult.error ?: "读取片段音频参数失败")
            }
            val denoiseResult = readClipDenoise(target)
            val denoise = denoiseResult.value
            if (!denoiseResult.ok || denoise == null) {
                return@withEditorStateLock Result(false, error = denoiseResult.error ?: "读取片段降噪参数失败")
            }
            val speedCurveResult = readSpeedCurve(target)
            val speedCurve = speedCurveResult.value
            if (!speedCurveResult.ok || speedCurve == null) {
                return@withEditorStateLock Result(false, error = speedCurveResult.error ?: "读取片段变速曲线失败")
            }
            Result(true, ClipEditorPanelSnapshot(transform, channels, audio, denoise, speedCurve))
        }
    } catch (error: Exception) {
        Result(false, error = error.message ?: "读取片段编辑面板快照失败")
    }

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

    fun setTrackLocked(trackIndex: Int, locked: Boolean): Result<Unit> {
        if (trackIndex < 0) return Result(false, error = "轨道索引不能为负数")
        return invoke(
            "nc_set_track_locked",
            JSONObject().put("track_index", trackIndex).put("locked", locked)
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

    fun setTrackLockedAndReadSnapshot(
        trackIndex: Int,
        locked: Boolean
    ): Result<NativeTimelineSnapshot> =
        mutateAndReadTimelineSnapshot { setTrackLocked(trackIndex, locked) }

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

    private fun readTimelineSnapshotLocked(): Result<NativeTimelineSnapshot> {
        return try {
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
    }

    data class HistoryState(val canUndo: Boolean, val canRedo: Boolean)
    data class EditorStateSnapshot(
        val timeline: NativeTimelineSnapshot,
        val history: HistoryState
    )

    data class TextClipMutationResult(val clipId: Long, val editorState: EditorStateSnapshot)

    data class SubtitleCueState(val index: Int, val subtitleId: Long, val startUs: Long, val durationUs: Long, val text: String)

    data class SubtitleCueInput(val startUs: Long, val durationUs: Long, val text: String)

    /**
     * Writes ASR output in one engine transaction: one undo restores the pre-ASR subtitle track.
     * Inputs are normalized/clamped against the current project duration before JNI dispatch.
     */
    fun addSubtitlesBatchAndReadEditorState(cues: List<SubtitleCueInput>): Result<EditorStateSnapshot> {
        if (cues.isEmpty() || cues.size > 10000) return Result(false, error = "字幕数量必须在 1–10000 条")
        val normalized = ArrayList<SubtitleCueInput>(cues.size)
        for (cue in cues) {
            val text = cue.text.trim()
            if (text.isEmpty()) return Result(false, error = "字幕内容不能为空")
            if (text.toByteArray(Charsets.UTF_8).size > 16 * 1024) return Result(false, error = "单条字幕内容过长")
            if (cue.startUs < 0L || cue.durationUs <= 0L || cue.startUs > Long.MAX_VALUE - cue.durationUs) {
                return Result(false, error = "字幕时间范围无效")
            }
            normalized.add(cue.copy(text = text))
        }
        normalized.sortBy { it.startUs }
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            val projectDuration = before.value?.durationUs ?: return@withEditorStateLock Result(false, error = "工程时长缺失")
            val bounded = normalized.mapNotNull { cue ->
                if (cue.startUs >= projectDuration) null
                else cue.copy(durationUs = minOf(cue.durationUs, projectDuration - cue.startUs))
            }
            if (bounded.isEmpty()) return@withEditorStateLock Result(false, error = "识别字幕均超出工程时长")
            val starts = LongArray(bounded.size) { bounded[it].startUs }
            val durations = LongArray(bounded.size) { bounded[it].durationUs }
            val texts = Array(bounded.size) { bounded[it].text }
            val ids = try {
                bridge.upsertSubtitlesBatch(starts, durations, texts)
            } catch (error: Exception) {
                return@withEditorStateLock Result(false, error = error.message ?: "批量写入字幕失败")
            } ?: return@withEditorStateLock Result(false, error = "批量写入字幕失败，工程未保留部分结果")
            if (ids.size != bounded.size || ids.any { it <= 0L } || ids.toSet().size != ids.size) {
                // The native return payload is inconsistent, so we cannot prove
                // whether an undo record was created. Never issue a blind undo here:
                // it could otherwise undo the user's previous edit.
                return@withEditorStateLock Result(
                    false,
                    error = "引擎返回的字幕 ID 校验失败；写入状态不确定，请先刷新工程状态，不要直接重试"
                )
            }
            val refreshed = readEditorStateSnapshotLocked()
            if (refreshed.ok) return@withEditorStateLock refreshed

            // The native batch has committed as one undo record, but the readback
            // failed. Roll back while still holding the engine state lock so a
            // concurrent user edit cannot be accidentally undone.
            val rollback = try {
                JSONObject(bridge.editorUndo()).optBoolean("ok", false)
            } catch (_: Exception) {
                false
            }
            if (rollback) {
                val restored = readEditorStateSnapshotLocked()
                return@withEditorStateLock Result(
                    false,
                    error = if (restored.ok) {
                        "字幕写入后的工程快照刷新失败；本次批量写入已自动撤销，请重新检查后再试"
                    } else {
                        "字幕写入后的快照刷新失败；已请求撤销，但工程状态仍无法确认，请勿重试"
                    }
                )
            }
            Result(
                false,
                error = "字幕可能已写入，但工程快照刷新失败且自动撤销未成功；请勿重复执行，先刷新工程状态"
            )
        }
    }

    /** Reads subtitle cues from the engine-owned sequence subtitle track. */
    fun readSubtitles(): Result<List<SubtitleCueState>> = bridge.withEditorStateLock {
        invoke("subtitle_list", JSONObject()) { payload ->
            val items = payload.optJSONArray("items") ?: throw IllegalArgumentException("字幕列表缺失")
            require(items.length() <= 10000)
            buildList {
                for (index in 0 until items.length()) {
                    val item = items.optJSONObject(index) ?: throw IllegalArgumentException("字幕记录格式非法")
                    val cue = SubtitleCueState(item.getInt("index"), item.getLong("subtitle_id"), item.getLong("start_us"), item.getLong("duration_us"), item.getString("text"))
                    require(cue.index == index && cue.subtitleId > 0L && cue.startUs >= 0L && cue.durationUs > 0L && cue.text.isNotBlank())
                    require(cue.startUs <= Long.MAX_VALUE - cue.durationUs)
                    if (isNotEmpty()) require(last().startUs <= cue.startUs) { "字幕必须按开始时间排序" }
                    require(none { it.subtitleId == cue.subtitleId }) { "字幕 ID 重复" }
                    add(cue)
                }
            }
        }
    }

    /** Adds a subtitle cue and returns the coherent post-edit timeline/history snapshot. */
    fun addSubtitleAndReadEditorState(text: String, startUs: Long, durationUs: Long): Result<EditorStateSnapshot> {
        if (text.isBlank()) return Result(false, error = "字幕内容不能为空")
        if (text.toByteArray(Charsets.UTF_8).size > 16 * 1024) return Result(false, error = "字幕内容过长")
        if (startUs < 0L || durationUs <= 0L || startUs > Long.MAX_VALUE - durationUs) return Result(false, error = "字幕时间范围无效")
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            val projectDuration = before.value?.durationUs ?: return@withEditorStateLock Result(false, error = "工程时长缺失")
            if (startUs >= projectDuration) return@withEditorStateLock Result(false, error = "字幕开始时间超出工程时长")
            val boundedDuration = minOf(durationUs, projectDuration - startUs)
            val inserted = invoke("subtitle_upsert", JSONObject().put("text", text.trim()).put("start_us", startUs).put("duration_us", boundedDuration)) { it.getLong("subtitle_id") }
            if (!inserted.ok || (inserted.value ?: 0L) <= 0L) return@withEditorStateLock Result(false, error = inserted.error ?: "字幕创建失败")
            readEditorStateSnapshotLocked()
        }
    }

    fun removeSubtitleAndReadEditorState(subtitleId: Long): Result<EditorStateSnapshot> {
        if (subtitleId <= 0L) return Result(false, error = "字幕 ID 无效")
        return bridge.withEditorStateLock {
            val changed = invoke("subtitle_remove", JSONObject().put("subtitle_id", subtitleId)) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error)
            readEditorStateSnapshotLocked()
        }
    }


    data class TextStyleUpdate(
        val fontSize: Double? = null,
        val color: String? = null,
        val outlineWidth: Double? = null,
        val alignment: Int? = null
    )

    /** Updates only explicitly supplied text-style fields; null preserves the engine value. */
    fun setTextStyleAndReadEditorState(target: ClipTarget, update: TextStyleUpdate): Result<EditorStateSnapshot> {
        if (update.fontSize != null && (!update.fontSize.isFinite() || update.fontSize !in 8.0..256.0)) return Result(false, error = "字号必须在 8–256 画布点范围内")
        if (update.color != null && !update.color.matches(Regex("^#[0-9A-Fa-f]{6}([0-9A-Fa-f]{2})?$"))) return Result(false, error = "文字颜色必须为 #RRGGBB 或 #RRGGBBAA")
        if (update.outlineWidth != null && (!update.outlineWidth.isFinite() || update.outlineWidth !in 0.0..32.0)) return Result(false, error = "描边宽度必须在 0–32 画布点范围内")
        if (update.alignment != null && update.alignment !in 0..2) return Result(false, error = "文字对齐方式无效")
        if (update.fontSize == null && update.color == null && update.outlineWidth == null && update.alignment == null) return Result(false, error = "没有可提交的文字样式修改")
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            val track = before.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改文字样式")
            if (track.clips.none { it.id == target.clipId }) return@withEditorStateLock Result(false, error = "目标片段已不存在")
            val args = targetArgs(target)
                .put("font_size", update.fontSize ?: -1.0)
                .put("color", update.color ?: "")
                .put("outline_color", "")
                .put("outline_width", update.outlineWidth ?: -1.0)
                .put("alignment", update.alignment ?: -1)
                .put("anchor_x", -1.0).put("anchor_y", -1.0)
            val changed = invoke("nc_set_text_style", args) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error ?: "设置文字样式失败；请确认选中的是文字片段")
            readEditorStateSnapshotLocked()
        }
    }

    /** Creates a text clip through the existing native C ABI/JNI operation and
     * returns the authoritative timeline/history snapshot from the same lock.
     * Initial style uses the engine's documented defaults. */
    fun addTextClipAndReadEditorState(text: String, startUs: Long, durationUs: Long = 3_000_000L): Result<TextClipMutationResult> {
        if (text.isBlank()) return Result(false, error = "文字内容不能为空")
        if (text.toByteArray(Charsets.UTF_8).size > 16 * 1024) return Result(false, error = "文字内容过长")
        if (startUs < 0L || durationUs <= 0L || startUs > Long.MAX_VALUE - durationUs) return Result(false, error = "文字片段时间范围无效")
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            val projectDuration = before.value?.durationUs ?: return@withEditorStateLock Result(false, error = "工程时长缺失")
            if (startUs >= projectDuration) return@withEditorStateLock Result(false, error = "播放头已超出工程有效时长")
            val boundedDurationUs = minOf(durationUs, projectDuration - startUs)
            if (boundedDurationUs <= 0L) return@withEditorStateLock Result(false, error = "工程剩余时长不足，无法创建文字片段")
            val created = invoke("nc_add_text_clip", JSONObject()
                .put("text", text).put("start_us", startUs).put("duration_us", boundedDurationUs)
                .put("font_size", 48.0).put("color", "#FFFFFF").put("alignment", 1)
                .put("anchor_x", 0.5).put("anchor_y", 0.5)) { payload ->
                payload.getLong("value").also { require(it > 0L) }
            }
            if (!created.ok) return@withEditorStateLock Result(false, error = created.error ?: "创建文字片段失败")
            val state = readEditorStateSnapshotLocked()
            if (!state.ok) return@withEditorStateLock Result(false, error = state.error ?: "文字已创建，但读取时间轴失败")
            Result(true, TextClipMutationResult(created.value!!, state.value!!))
        }
    }
    data class ClipMutationResult(
        val editorState: EditorStateSnapshot,
        val newClipId: Long? = null
    )

    data class ClipColorGradeMutationResult(
        val grade: ClipColorGradeState,
        val editorState: EditorStateSnapshot
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

    /**
     * Adds a timeline marker through the same nc_add_marker C ABI operation
     * used by the legacy editor, then returns the authoritative post-edit
     * timeline/history snapshot.
     */
    fun addMarkerAndReadEditorState(
        timeUs: Long,
        name: String,
        color: String = "#FFFFFF"
    ): Result<EditorStateSnapshot> {
        NativeMarkerInputRules.validateAdd(timeUs, name, color)?.let {
            return Result(false, error = it)
        }
        val cleanName = name.trim()
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) {
                return@withEditorStateLock Result(false, error = before.error ?: "读取工程时间轴失败")
            }
            val durationUs = before.value?.durationUs
                ?: return@withEditorStateLock Result(false, error = "工程时长缺失")
            if (timeUs > durationUs) {
                return@withEditorStateLock Result(false, error = "标记时间超出工程时长")
            }
            val args = JSONObject()
                .put("time_us", timeUs)
                .put("name", cleanName)
                .put("color", color)
            val response = try {
                JSONObject(bridge.call("nc_add_marker", args.toString()))
            } catch (error: Exception) {
                return@withEditorStateLock Result(false, error = error.message ?: "添加标记失败")
            }
            if (!response.optBoolean("ok", false)) {
                return@withEditorStateLock Result(
                    false,
                    error = response.optString("message", "添加标记失败")
                )
            }
            readEditorStateSnapshotLocked()
        }
    }

    /** Removes one marker by its stable engine ID and reads back the committed state. */
    fun removeMarkerAndReadEditorState(markerId: Long): Result<EditorStateSnapshot> {
        NativeMarkerInputRules.validateRemove(markerId)?.let {
            return Result(false, error = it)
        }
        return bridge.withEditorStateLock {
            val before = readTimelineSnapshotLocked()
            if (!before.ok) {
                return@withEditorStateLock Result(false, error = before.error ?: "删除标记前无法读取工程状态")
            }
            if (before.value?.markers?.none { it.id == markerId } != false) {
                return@withEditorStateLock Result(false, error = "标记已不存在")
            }
            val response = try {
                JSONObject(
                    bridge.call(
                        "nc_remove_marker",
                        JSONObject().put("marker_id", markerId).toString()
                    )
                )
            } catch (error: Exception) {
                return@withEditorStateLock Result(false, error = error.message ?: "删除标记失败")
            }
            if (!response.optBoolean("ok", false)) {
                return@withEditorStateLock Result(
                    false,
                    error = response.optString("message", "删除标记失败")
                )
            }
            val refreshed = readEditorStateSnapshotLocked()
            if (!refreshed.ok) {
                return@withEditorStateLock Result(
                    false,
                    error = refreshed.error ?: "标记已删除，但刷新工程状态失败"
                )
            }
            if (refreshed.value?.timeline?.markers?.any { it.id == markerId } != false) {
                return@withEditorStateLock Result(false, error = "删除后快照仍包含该标记，请刷新工程状态")
            }
            refreshed
        }
    }


    data class PreviewCacheStats(
        val frameHits: Long,
        val frameMisses: Long,
        val readerOpens: Long,
        val cachedFrames: Int,
        val capacity: Int
    )

    /**
     * Reads the engine's real preview-cache counters. The response is parsed
     * strictly: malformed or negative counters must not be presented as
     * plausible cache telemetry in the native editor.
     */
    fun previewCacheStats(): Result<PreviewCacheStats> = bridge.withEditorStateLock {
        try {
            val response = JSONObject(bridge.call("nc_preview_cache_stats", "{}"))
            if (!response.optBoolean("ok", false)) {
                return@withEditorStateLock Result(
                    false,
                    error = response.optString("message", "读取预览缓存统计失败")
                )
            }
            val payload = response.optJSONObject("result")
                ?: throw IllegalArgumentException("预览缓存统计响应缺少 result 对象")
            fun requiredLong(name: String): Long {
                val raw = payload.opt(name) as? Number
                    ?: throw IllegalArgumentException("预览缓存字段 $name 缺失或类型错误")
                return raw.toLong().also {
                    require(it >= 0L && raw.toDouble() == it.toDouble()) {
                        "预览缓存字段 $name 必须是非负整数"
                    }
                }
            }
            val hits = requiredLong("frame_hits")
            val misses = requiredLong("frame_misses")
            val opens = requiredLong("reader_opens")
            val cached = requiredLong("cached_frames")
            val capacity = requiredLong("capacity")
            require(cached <= Int.MAX_VALUE && capacity <= Int.MAX_VALUE) {
                "预览缓存帧数超出支持范围"
            }
            require(cached <= capacity || capacity == 0L) {
                "预览缓存已缓存帧数超过容量"
            }
            Result(
                true,
                PreviewCacheStats(hits, misses, opens, cached.toInt(), capacity.toInt())
            )
        } catch (error: Exception) {
            Result(false, error = error.message ?: "预览缓存统计响应解析失败")
        }
    }

    /**
     * Warms one timeline timestamp through the same nc_preview_render path used by
     * normal preview frames. A false result means the frame could not be rendered;
     * no synthetic progress or cache-hit claim is produced.
     */
    fun prewarmPreviewFrame(timeUs: Long, width: Int = 320): Result<Unit> {
        if (timeUs < 0L) return Result(false, error = "预热时间不能为负数")
        if (width !in 16..2048) return Result(false, error = "预热画面宽度必须在 16–2048 像素范围内")
        return if (bridge.prewarmPreviewFrame(timeUs, width)) Result(true, Unit)
        else Result(false, error = "时间点 $timeUs 的预览帧渲染失败")
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

    fun insertKeyframe(
        target: ClipTarget,
        property: Int,
        clipLocalTimeUs: Long,
        value: Double
    ): Result<Unit> {
        if (property !in 0..5 || clipLocalTimeUs < 0L || !value.isFinite()) {
            return Result(false, error = "关键帧参数非法")
        }
        NativeKeyframeValueRules.validate(property, value)?.let {
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
                    NativeKeyframeValueRules.validate(parameter.engineProperty, frame.value)?.let {
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
        NativeKeyframeValueRules.validateTimes(frames.map { it.timeUs })?.let {
            return Result(false, error = it)
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

    data class ClipLutState(
        val path: String,
        val strength: Double
    )

    data class ColorCurveState(val points: List<Pair<Double, Double>>) {
        fun isValid(): Boolean = points.size in 2..32 &&
            points.first() == (0.0 to 0.0) && points.last() == (1.0 to 1.0) &&
            points.all { (x, y) -> x.isFinite() && y.isFinite() && x in 0.0..1.0 && y in 0.0..1.0 } &&
            points.zipWithNext().all { (a, b) -> b.first > a.first }

        companion object {
            fun identity() = ColorCurveState(listOf(0.0 to 0.0, 1.0 to 1.0))
        }
    }

    data class ClipColorGradeState(
        val master: ColorCurveState,
        val red: ColorCurveState,
        val green: ColorCurveState,
        val blue: ColorCurveState
    ) {
        fun isValid(): Boolean = master.isValid() && red.isValid() && green.isValid() && blue.isValid()
        companion object {
            fun identity() = ColorCurveState(listOf(0.0 to 0.0, 1.0 to 1.0))
        }
    }

    /** Reads authoritative clip-level master/RGB curves from the native core. */
    fun readClipColorGrade(target: ClipTarget): Result<ClipColorGradeState> =
        invoke("color_grade_get", targetArgs(target)) { payload ->
            fun readCurve(name: String): ColorCurveState {
                val raw = payload.optJSONArray(name) ?: throw IllegalArgumentException("缺少 $name 曲线")
                require(raw.length() in 4..64 && raw.length() % 2 == 0)
                val points = buildList {
                    for (i in 0 until raw.length() step 2) {
                        add(raw.getDouble(i) to raw.getDouble(i + 1))
                    }
                }
                return ColorCurveState(points).also {
                    require(it.isValid()) { "$name 曲线数据不合法" }
                }
            }
            ClipColorGradeState(readCurve("master"), readCurve("red"),
                readCurve("green"), readCurve("blue")).also {
                require(it.isValid()) { "引擎返回的 RGB 曲线无效" }
            }
        }

    /** Applies all four curves atomically as one undoable clip edit. */
    fun setClipColorGradeAndRead(
        target: ClipTarget,
        grade: ClipColorGradeState
    ): Result<ClipColorGradeState> {
        val result = setClipColorGradeAndReadEditorState(target, grade)
        if (!result.ok) return Result(false, error = result.error ?: "设置 RGB 曲线失败")
        return Result(true, result.value?.grade ?: return Result(false, error = "引擎未返回 RGB 曲线状态"))
    }

    /**
     * Commits a color-grade edit and refreshes both the authoritative curve and
     * undo/redo/timeline state under one bridge lock. Hosts can update the curve
     * panel and the editor's history controls from the same coherent result.
     */
    fun setClipColorGradeAndReadEditorState(
        target: ClipTarget,
        grade: ClipColorGradeState
    ): Result<ClipColorGradeMutationResult> {
        if (!grade.isValid()) return Result(false, error = "RGB 曲线必须包含 2–32 个合法控制点")
        return bridge.withEditorStateLock {
            val snapshot = readTimelineSnapshotLocked()
            if (!snapshot.ok) return@withEditorStateLock Result(false, error = snapshot.error ?: "读取工程时间轴失败")
            val track = snapshot.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            if (track.clips.none { it.id == target.clipId }) {
                return@withEditorStateLock Result(false, error = "目标片段已不存在")
            }
            fun flatten(curve: ColorCurveState) = org.json.JSONArray().also { array ->
                curve.points.forEach { (x, y) -> array.put(x).put(y) }
            }
            val changed = invoke(
                "color_grade_set",
                targetArgs(target)
                    .put("master", flatten(grade.master))
                    .put("red", flatten(grade.red))
                    .put("green", flatten(grade.green))
                    .put("blue", flatten(grade.blue))
            ) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error ?: "设置 RGB 曲线失败")
            val authoritativeGrade = readClipColorGrade(target)
            if (!authoritativeGrade.ok) {
                return@withEditorStateLock Result(false, error = authoritativeGrade.error ?: "读取 RGB 曲线回读失败")
            }
            val editorState = readEditorStateSnapshotLocked()
            if (!editorState.ok) {
                return@withEditorStateLock Result(false, error = editorState.error ?: "读取撤销状态失败")
            }
            Result(
                true,
                ClipColorGradeMutationResult(
                    grade = authoritativeGrade.value ?: return@withEditorStateLock Result(false, error = "RGB 曲线回读数据缺失"),
                    editorState = editorState.value ?: return@withEditorStateLock Result(false, error = "编辑器状态回读数据缺失")
                )
            )
        }
    }

    data class LutValidationState(
        val size: Int,
        val title: String,
        val message: String
    )

    /** Validates a .cube file using the core parser without mutating the project. */
    fun validateLutFile(path: String): Result<LutValidationState> {
        if (path.isBlank() || path.indexOf(0.toChar()) >= 0) {
            return Result(false, error = "请填写有效的 LUT 文件路径")
        }
        return invoke("lut_validate", JSONObject().put("path", path)) { payload ->
            LutValidationState(
                size = payload.getInt("size"),
                title = payload.optString("title", ""),
                message = payload.optString("message", "")
            ).also { require(it.size in 2..64) { "引擎返回的 LUT 边长无效" } }
        }
    }

    /** Reads the selected clip's persisted .cube LUT path and blend strength. */
    fun readClipLut(target: ClipTarget): Result<ClipLutState> =
        invoke("lut_get", targetArgs(target)) { payload ->
            ClipLutState(
                path = payload.optString("path", ""),
                strength = payload.getDouble("strength")
            ).also { require(it.strength.isFinite() && it.strength in 0.0..1.0) }
        }

    /** Sets or clears a clip LUT; strength follows the core's authoritative 0..1 domain. */
    fun setClipLutAndRead(
        target: ClipTarget,
        path: String,
        strength: Double
    ): Result<ClipLutState> {
        if (path.indexOf(0.toChar()) >= 0 || !strength.isFinite() || strength !in 0.0..1.0) {
            return Result(false, error = "LUT 路径或强度参数非法")
        }
        return bridge.withEditorStateLock {
            val snapshot = readTimelineSnapshotLocked()
            if (!snapshot.ok) return@withEditorStateLock Result(false, error = snapshot.error ?: "读取工程时间轴失败")
            val track = snapshot.value?.tracks?.firstOrNull { it.engineIndex == target.trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            if (track.clips.none { it.id == target.clipId }) {
                return@withEditorStateLock Result(false, error = "目标片段已不存在")
            }
            val changed = invoke(
                "lut",
                targetArgs(target).put("path", path).put("strength", strength)
            ) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error ?: "设置 LUT 失败")
            readClipLut(target)
        }
    }

    data class TransitionState(
        val id: Long,
        val type: Int,
        val durationUs: Long,
        val fromClipId: Long,
        val toClipId: Long,
        val name: String
    )

    fun readTransitions(trackIndex: Int): Result<List<TransitionState>> {
        if (trackIndex < 0) return Result(false, error = "目标轨道索引无效")
        return bridge.withEditorStateLock {
            val snapshot = readEditorStateSnapshotLocked()
            if (!snapshot.ok) return@withEditorStateLock Result(false, error = snapshot.error)
            val track = snapshot.value?.timeline?.tracks?.firstOrNull { it.engineIndex == trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            invoke("transition_get", JSONObject().put("track_index", trackIndex)) { payload ->
                val items = payload.optJSONArray("items")
                    ?: throw IllegalArgumentException("转场列表缺失")
                require(items.length() <= 10000)
                buildList {
                    for (index in 0 until items.length()) {
                        val item = items.optJSONObject(index)
                            ?: throw IllegalArgumentException("转场记录格式非法")
                        val id = item.opt("id") as? Number
                            ?: throw IllegalArgumentException("转场 ID 缺失")
                        val type = item.opt("type") as? Number
                            ?: throw IllegalArgumentException("转场类型缺失")
                        val duration = item.opt("duration_us") as? Number
                            ?: throw IllegalArgumentException("转场时长缺失")
                        val from = item.opt("from_clip") as? Number
                            ?: throw IllegalArgumentException("转场起始片段缺失")
                        val to = item.opt("to_clip") as? Number
                            ?: throw IllegalArgumentException("转场结束片段缺失")
                        val name = item.optString("name", "")
                        require(id.toLong() > 0L && type.toInt() >= 0)
                        require(duration.toLong() > 0L && from.toLong() > 0L && to.toLong() > 0L)
                        require(from.toLong() != to.toLong())
                        require(track.clips.any { it.id == from.toLong() } &&
                            track.clips.any { it.id == to.toLong() }) {
                            "转场引用了当前轨道中不存在的片段"
                        }
                        require(name.isNotBlank())
                        add(TransitionState(id.toLong(), type.toInt(), duration.toLong(),
                            from.toLong(), to.toLong(), name))
                    }
                }
            }
        }
    }

    fun readTransitionTypeCount(): Result<Int> =
        invoke("nc_transition_type_count", JSONObject()) { payload ->
            val raw = payload.opt("value") as? Number
                ?: throw IllegalArgumentException("转场类型数量缺失")
            raw.toInt().also { require(it in 1..4096) }
        }

    fun readTransitionTypeName(type: Int): Result<String> {
        if (type < 0) return Result(false, error = "转场类型无效")
        return invoke("nc_transition_type_name", JSONObject().put("type", type)) { payload ->
            payload.getString("value").also { require(it.isNotBlank() && it != "未知转场") }
        }
    }

    fun addTransition(
        trackIndex: Int,
        fromClipId: Long,
        toClipId: Long,
        type: Int,
        durationUs: Long
    ): Result<Long> {
        if (trackIndex < 0 || fromClipId <= 0L || toClipId <= 0L ||
            fromClipId == toClipId || type < 0 || durationUs <= 0L
        ) return Result(false, error = "转场参数无效")
        return bridge.withEditorStateLock {
            val snapshot = readEditorStateSnapshotLocked()
            if (!snapshot.ok) return@withEditorStateLock Result(false, error = snapshot.error)
            val track = snapshot.value?.timeline?.tracks?.firstOrNull { it.engineIndex == trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            val fromClip = track.clips.firstOrNull { it.id == fromClipId }
                ?: return@withEditorStateLock Result(false, error = "转场起始片段已不存在")
            val toClip = track.clips.firstOrNull { it.id == toClipId }
                ?: return@withEditorStateLock Result(false, error = "转场结束片段已不存在")
            if (fromClip.startUs > Long.MAX_VALUE - fromClip.durationUs ||
                fromClip.startUs + fromClip.durationUs != toClip.startUs
            ) {
                return@withEditorStateLock Result(false, error = "转场必须连接时间轴上首尾相接的相邻片段")
            }
            val count = readTransitionTypeCount()
            if (!count.ok || type !in 0 until (count.value ?: 0)) {
                return@withEditorStateLock Result(false, error = count.error ?: "转场类型超出引擎范围")
            }
            invoke("transition", JSONObject()
                .put("track_index", trackIndex)
                .put("from_clip", fromClipId)
                .put("to_clip", toClipId)
                .put("transition_type", type)
                .put("duration_us", durationUs)) { payload ->
                payload.getLong("value").also { require(it > 0L) }
            }
        }
    }

    fun removeTransition(trackIndex: Int, transitionId: Long): Result<Unit> {
        if (trackIndex < 0 || transitionId <= 0L) return Result(false, error = "转场 ID 无效")
        return bridge.withEditorStateLock {
            val snapshot = readEditorStateSnapshotLocked()
            if (!snapshot.ok) return@withEditorStateLock Result(false, error = snapshot.error)
            val track = snapshot.value?.timeline?.tracks?.firstOrNull { it.engineIndex == trackIndex }
                ?: return@withEditorStateLock Result(false, error = "目标轨道已不存在")
            if (track.locked) return@withEditorStateLock Result(false, error = "目标轨道已锁定，不能修改")
            invoke("remove_transition", JSONObject()
                .put("track_index", trackIndex)
                .put("transition_id", transitionId)) { Unit }
        }
    }

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

    /** Writes the complete mask value atomically, then reads back engine-authoritative state. */
    fun setClipMaskAndRead(target: ClipTarget, mask: ClipMaskState): Result<ClipMaskState> {
        if (mask.shape !in 0..6 ||
            !mask.centerX.isFinite() || !mask.centerY.isFinite() ||
            !mask.rotationDegrees.isFinite() || !mask.width.isFinite() ||
            !mask.height.isFinite() || !mask.feather.isFinite() ||
            !mask.cornerRadius.isFinite() ||
            mask.feather !in 0.0..0.5 || mask.cornerRadius !in 0.0..0.5
        ) return Result(false, error = "蒙版参数不是有效的有限数值或超出核心范围")
        return bridge.withEditorStateLock {
            val valid = validateEditableClipLocked(target)
            if (!valid.ok) return@withEditorStateLock Result(false, error = valid.error)
            val changed = invoke(
                "mask",
                targetArgs(target)
                    .put("shape", mask.shape)
                    .put("x", mask.centerX)
                    .put("y", mask.centerY)
                    .put("rotation", mask.rotationDegrees)
                    .put("width", mask.width)
                    .put("height", mask.height)
                    .put("feather", mask.feather)
                    .put("radius", mask.cornerRadius)
                    .put("invert", if (mask.invert) 1 else 0)
            ) { Unit }
            if (!changed.ok) return@withEditorStateLock Result(false, error = changed.error)
            readClipMask(target)
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
