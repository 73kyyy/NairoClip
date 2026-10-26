/* NairoClip Mobile · C ABI 全量直通（window.NC_API）
   ---------------------------------------------------------------------------
   本文件由 tools/gen_ncapi.py 自动生成，请勿手工编辑。
   来源：core/include/nairoclip/core/capi.h（233 个声明，开放 228 个）

   用法：
     await NC_API.nc_track_count()
     await NC_API.nc_set_clip_opacity({ track_index:0, clip_id:10, opacity:0.5 })
   返回： { ok, code, message, result }
     · ok   —— HTTP 层是否成功（函数已登记且桥正常）
     · code —— C ABI 原始返回值。注意两种语义：
                 状态码类：0=NC_OK，1..8=错误码
                 数值类  ：直接是结果（如 nc_track_count 返回 3）
               具体属于哪类见 docs/API 清单的注释；状态码类请判断 code===0。
   ------------------------------------------------------------------------- */
'use strict';

(function () {
  /**
   * 只走平台注入的桥 window.NCEngine（Android JNI / iOS C interop / Qt 直调）。
   * 依据 docs/UI_INTEGRATION.md §7：平台层不要用 HTTP。
   * 桥是同步的，因此这里没有 fetch、没有 URL、没有端口探测。
   */
  function bridge() {
    const b = window.NCEngine;
    return (b && typeof b.call === "function") ? b : null;
  }

  function unavailable(fn, why) {
    return { ok: false, code: -1, message: `${fn}：${why}`, result: {} };
  }

  function call(fn, args) {
    const b = bridge();
    if (!b) {
      return unavailable(fn,
        "当前环境没有引擎桥（Web/WASM 不支持 C ABI；原生端需注入 NCEngine）");
    }
    let raw;
    try {
      raw = b.call(fn, JSON.stringify(args || {}));
    } catch (err) {
      return unavailable(fn, `桥调用失败：${err.message}`);
    }
    if (raw === null || raw === undefined) return unavailable(fn, "桥无返回");
    if (typeof raw === "object") return raw;
    try { return JSON.parse(String(raw)); }
    catch (err) { return unavailable(fn, `桥返回内容无法解析：${String(raw).slice(0, 60)}`); }
  }

  const NC_API = { call };

  /** int32_t nc_abi_version() */
  NC_API.nc_abi_version = () => call("nc_abi_version", {});
  /** char* nc_project_save() */
  NC_API.nc_project_save = () => call("nc_project_save", {});
  /** int32_t nc_project_load(nclip_text, detail) */
  NC_API.nc_project_load = (args = {}) => call("nc_project_load", args);
  /** int64_t nc_project_duration_us() */
  NC_API.nc_project_duration_us = () => call("nc_project_duration_us", {});
  /** int32_t nc_track_count() */
  NC_API.nc_track_count = () => call("nc_track_count", {});
  /** int32_t nc_clip_count(track_index) */
  NC_API.nc_clip_count = (args = {}) => call("nc_clip_count", args);
  /** char* nc_project_name() */
  NC_API.nc_project_name = () => call("nc_project_name", {});
  /** int32_t nc_set_project_name(name) */
  NC_API.nc_set_project_name = (args = {}) => call("nc_set_project_name", args);
  /** int32_t nc_project_canvas(out_width, out_height) */
  NC_API.nc_project_canvas = (args = {}) => call("nc_project_canvas", args);
  /** int32_t nc_set_project_canvas(width, height) */
  NC_API.nc_set_project_canvas = (args = {}) => call("nc_set_project_canvas", args);
  /** int32_t nc_project_frame_rate(out_num, out_den) */
  NC_API.nc_project_frame_rate = (args = {}) => call("nc_project_frame_rate", args);
  /** int32_t nc_set_project_frame_rate(num, den) */
  NC_API.nc_set_project_frame_rate = (args = {}) => call("nc_set_project_frame_rate", args);
  /** int32_t nc_can_undo() */
  NC_API.nc_can_undo = () => call("nc_can_undo", {});
  /** int32_t nc_can_redo() */
  NC_API.nc_can_redo = () => call("nc_can_redo", {});
  /** int32_t nc_undo_count() */
  NC_API.nc_undo_count = () => call("nc_undo_count", {});
  /** int32_t nc_redo_count() */
  NC_API.nc_redo_count = () => call("nc_redo_count", {});
  /** char* nc_undo_history_name(index) */
  NC_API.nc_undo_history_name = (args = {}) => call("nc_undo_history_name", args);
  /** int32_t nc_clear_undo_history() */
  NC_API.nc_clear_undo_history = () => call("nc_clear_undo_history", {});
  /** int32_t nc_set_undo_depth_limit(max_steps) */
  NC_API.nc_set_undo_depth_limit = (args = {}) => call("nc_set_undo_depth_limit", args);
  /** int32_t nc_asset_count() */
  NC_API.nc_asset_count = () => call("nc_asset_count", {});
  /** int32_t nc_asset_get(index, out_asset_id, out_name, out_path, out_type, out_duration_us) */
  NC_API.nc_asset_get = (args = {}) => call("nc_asset_get", args);
  /** int32_t nc_remove_asset(asset_id) */
  NC_API.nc_remove_asset = (args = {}) => call("nc_remove_asset", args);
  /** int32_t nc_asset_is_referenced(asset_id) */
  NC_API.nc_asset_is_referenced = (args = {}) => call("nc_asset_is_referenced", args);
  /** uint64_t nc_import_asset(name, path, type, duration_us) */
  NC_API.nc_import_asset = (args = {}) => call("nc_import_asset", args);
  /** uint64_t nc_add_track(name, track_type) */
  NC_API.nc_add_track = (args = {}) => call("nc_add_track", args);
  /** uint64_t nc_insert_clip(track_index, asset_id, start_us, duration_us) */
  NC_API.nc_insert_clip = (args = {}) => call("nc_insert_clip", args);
  /** int32_t nc_undo() */
  NC_API.nc_undo = () => call("nc_undo", {});
  /** int32_t nc_redo() */
  NC_API.nc_redo = () => call("nc_redo", {});
  /** int32_t nc_track_get(track_index, out_track_id, out_name, out_type, out_locked, out_muted, out_hidden, out_solo, out_gain_db) */
  NC_API.nc_track_get = (args = {}) => call("nc_track_get", args);
  /** int32_t nc_remove_track(track_index) */
  NC_API.nc_remove_track = (args = {}) => call("nc_remove_track", args);
  /** int32_t nc_rename_track(track_index, new_name) */
  NC_API.nc_rename_track = (args = {}) => call("nc_rename_track", args);
  /** int32_t nc_set_track_locked(track_index, locked) */
  NC_API.nc_set_track_locked = (args = {}) => call("nc_set_track_locked", args);
  /** int32_t nc_set_track_muted(track_index, muted) */
  NC_API.nc_set_track_muted = (args = {}) => call("nc_set_track_muted", args);
  /** int32_t nc_set_track_hidden(track_index, hidden) */
  NC_API.nc_set_track_hidden = (args = {}) => call("nc_set_track_hidden", args);
  /** int32_t nc_remove_clip(track_index, clip_id) */
  NC_API.nc_remove_clip = (args = {}) => call("nc_remove_clip", args);
  /** int32_t nc_split_clip(track_index, clip_id, at_us) */
  NC_API.nc_split_clip = (args = {}) => call("nc_split_clip", args);
  /** int32_t nc_move_clip(track_index, clip_id, new_start_us) */
  NC_API.nc_move_clip = (args = {}) => call("nc_move_clip", args);
  /** int32_t nc_move_clip_to_track(from_track, to_track, clip_id, new_start_us) */
  NC_API.nc_move_clip_to_track = (args = {}) => call("nc_move_clip_to_track", args);
  /** int32_t nc_trim_clip_start(track_index, clip_id, new_start_us) */
  NC_API.nc_trim_clip_start = (args = {}) => call("nc_trim_clip_start", args);
  /** int32_t nc_trim_clip_end(track_index, clip_id, new_end_us) */
  NC_API.nc_trim_clip_end = (args = {}) => call("nc_trim_clip_end", args);
  /** int32_t nc_set_clip_speed(track_index, clip_id, speed_num, speed_den) */
  NC_API.nc_set_clip_speed = (args = {}) => call("nc_set_clip_speed", args);
  /** int32_t nc_ripple_delete(track_index, clip_ids, clip_count) */
  NC_API.nc_ripple_delete = (args = {}) => call("nc_ripple_delete", args);
  /** uint64_t nc_duplicate_clip(track_index, clip_id) */
  NC_API.nc_duplicate_clip = (args = {}) => call("nc_duplicate_clip", args);
  /** uint64_t nc_freeze_frame(track_index, clip_id, at_us, duration_us) */
  NC_API.nc_freeze_frame = (args = {}) => call("nc_freeze_frame", args);
  /** int32_t nc_reverse_clip(track_index, clip_id) */
  NC_API.nc_reverse_clip = (args = {}) => call("nc_reverse_clip", args);
  /** int32_t nc_clip_info(track_index, clip_index, out_clip_id, out_start_us, out_duration_us, out_asset_id, out_speed_num, out_speed_den, out_enabled) */
  NC_API.nc_clip_info = (args = {}) => call("nc_clip_info", args);
  /** int32_t nc_set_clip_enabled(track_index, clip_id, enabled) */
  NC_API.nc_set_clip_enabled = (args = {}) => call("nc_set_clip_enabled", args);
  /** int32_t nc_get_clip_opacity(track_index, clip_id, out_opacity) */
  NC_API.nc_get_clip_opacity = (args = {}) => call("nc_get_clip_opacity", args);
  /** int32_t nc_set_clip_opacity(track_index, clip_id, opacity) */
  NC_API.nc_set_clip_opacity = (args = {}) => call("nc_set_clip_opacity", args);
  /** int32_t nc_set_clip_filter(track_index, clip_id, style, intensity) */
  NC_API.nc_set_clip_filter = (args = {}) => call("nc_set_clip_filter", args);
  /** int32_t nc_get_clip_filter(track_index, clip_id, out_style, out_intensity) */
  NC_API.nc_get_clip_filter = (args = {}) => call("nc_get_clip_filter", args);
  /** const char* nc_filter_display_name(style) */
  NC_API.nc_filter_display_name = (args = {}) => call("nc_filter_display_name", args);
  /** uint64_t nc_upsert_subtitle(start_us, duration_us, text) */
  NC_API.nc_upsert_subtitle = (args = {}) => call("nc_upsert_subtitle", args);
  /** int32_t nc_remove_subtitle(subtitle_id) */
  NC_API.nc_remove_subtitle = (args = {}) => call("nc_remove_subtitle", args);
  /** int32_t nc_subtitle_count() */
  NC_API.nc_subtitle_count = () => call("nc_subtitle_count", {});
  /** int32_t nc_subtitle_get(index, start_us, duration_us, out_text) */
  NC_API.nc_subtitle_get = (args = {}) => call("nc_subtitle_get", args);
  /** uint64_t nc_add_transition(track_index, transition_type, duration_us, from_clip_id, to_clip_id) */
  NC_API.nc_add_transition = (args = {}) => call("nc_add_transition", args);
  /** int32_t nc_remove_transition(track_index, transition_id) */
  NC_API.nc_remove_transition = (args = {}) => call("nc_remove_transition", args);
  /** int32_t nc_transition_count(track_index) */
  NC_API.nc_transition_count = (args = {}) => call("nc_transition_count", args);
  /** int32_t nc_transition_get(track_index, index, out_type, out_duration_us, out_from_clip, out_to_clip, out_name) */
  NC_API.nc_transition_get = (args = {}) => call("nc_transition_get", args);
  /** int32_t nc_transition_type_count() */
  NC_API.nc_transition_type_count = () => call("nc_transition_type_count", {});
  /** const char* nc_transition_type_name(type) */
  NC_API.nc_transition_type_name = (args = {}) => call("nc_transition_type_name", args);
  /** const char* nc_transition_category(type) */
  NC_API.nc_transition_category = (args = {}) => call("nc_transition_category", args);
  /** int32_t nc_transition_supports_direction(type) */
  NC_API.nc_transition_supports_direction = (args = {}) => call("nc_transition_supports_direction", args);
  /** int32_t nc_clip_get(track_index, clip_index, out_start_us, out_duration_us, out_asset_id) */
  NC_API.nc_clip_get = (args = {}) => call("nc_clip_get", args);
  /** int32_t nc_preview_render(time_us, out_width, out_height, out_rgba, out_clip_id, out_opacity, out_scale, out_pos_x, out_pos_y, out_rotation, out_transition_type, out_transition_progress) */
  NC_API.nc_preview_render = (args = {}) => call("nc_preview_render", args);
  /** char* nc_preview_subtitles(time_us) */
  NC_API.nc_preview_subtitles = (args = {}) => call("nc_preview_subtitles", args);
  /** void nc_preview_invalidate() */
  NC_API.nc_preview_invalidate = () => call("nc_preview_invalidate", {});
  /** int32_t nc_thumbnail_generate(asset_id, count, max_width, max_height, out_images, out_times_us, out_count) */
  NC_API.nc_thumbnail_generate = (args = {}) => call("nc_thumbnail_generate", args);
  /** int32_t nc_thumbnail_size(out_width, out_height) */
  NC_API.nc_thumbnail_size = (args = {}) => call("nc_thumbnail_size", args);
  /** int32_t nc_waveform_generate(asset_id, bucket_count, out_peak, out_sample_rate, out_channels) */
  NC_API.nc_waveform_generate = (args = {}) => call("nc_waveform_generate", args);
  /** int32_t nc_waveform_bucket_count() */
  NC_API.nc_waveform_bucket_count = () => call("nc_waveform_bucket_count", {});
  /** int32_t nc_waveform_bucket_get(index, out_min, out_max, out_rms) */
  NC_API.nc_waveform_bucket_get = (args = {}) => call("nc_waveform_bucket_get", args);
  /** int32_t nc_scene_detect(step_seconds, max_samples, out_boundary_count, out_boundaries) */
  NC_API.nc_scene_detect = (args = {}) => call("nc_scene_detect", args);
  /** int32_t nc_beat_detect(track_index, out_bpm, out_confidence, out_beat_count, out_beats) */
  NC_API.nc_beat_detect = (args = {}) => call("nc_beat_detect", args);
  /** int32_t nc_set_clip_speed_curve(track_index, clip_id, source_times, speeds, easings, point_count) */
  NC_API.nc_set_clip_speed_curve = (args = {}) => call("nc_set_clip_speed_curve", args);
  /** int32_t nc_clip_speed_curve_count(track_index, clip_id) */
  NC_API.nc_clip_speed_curve_count = (args = {}) => call("nc_clip_speed_curve_count", args);
  /** int32_t nc_clip_speed_curve_get(track_index, clip_id, index, out_source_seconds, out_speed, out_easing) */
  NC_API.nc_clip_speed_curve_get = (args = {}) => call("nc_clip_speed_curve_get", args);
  /** const char* nc_speed_curve_preset_name(preset) */
  NC_API.nc_speed_curve_preset_name = (args = {}) => call("nc_speed_curve_preset_name", args);
  /** const char* nc_speed_curve_preset_display_name(preset) */
  NC_API.nc_speed_curve_preset_display_name = (args = {}) => call("nc_speed_curve_preset_display_name", args);
  /** int32_t nc_apply_speed_curve_preset(track_index, clip_id, preset, source_duration_us) */
  NC_API.nc_apply_speed_curve_preset = (args = {}) => call("nc_apply_speed_curve_preset", args);
  /** int32_t nc_set_camera_tracking(subtitle_id, times_us, center_x, center_y, point_count, apply_mode) */
  NC_API.nc_set_camera_tracking = (args = {}) => call("nc_set_camera_tracking", args);
  /** int32_t nc_set_camera_tracking_scaled(subtitle_id, times_us, center_x, center_y, scales, point_count, apply_mode) */
  NC_API.nc_set_camera_tracking_scaled = (args = {}) => call("nc_set_camera_tracking_scaled", args);
  /** int32_t nc_tracking_point_count(subtitle_id) */
  NC_API.nc_tracking_point_count = (args = {}) => call("nc_tracking_point_count", args);
  /** int32_t nc_tracking_point_get(subtitle_id, index, out_time_us, out_center_x, out_center_y) */
  NC_API.nc_tracking_point_get = (args = {}) => call("nc_tracking_point_get", args);
  /** int32_t nc_tracking_point_get_scaled(subtitle_id, index, out_time_us, out_center_x, out_center_y, out_scale) */
  NC_API.nc_tracking_point_get_scaled = (args = {}) => call("nc_tracking_point_get_scaled", args);
  /** int32_t nc_clear_camera_tracking(subtitle_id) */
  NC_API.nc_clear_camera_tracking = (args = {}) => call("nc_clear_camera_tracking", args);
  /** int32_t nc_set_depth_effect(track_index, clip_id, mode, focus_distance, aperture, intensity, color_rgba) */
  NC_API.nc_set_depth_effect = (args = {}) => call("nc_set_depth_effect", args);
  /** int32_t nc_get_depth_effect(track_index, clip_id, out_mode, out_focus_distance, out_aperture, out_intensity, out_color_rgba) */
  NC_API.nc_get_depth_effect = (args = {}) => call("nc_get_depth_effect", args);
  /** int32_t nc_add_transition_keyframe(track_index, transition_id, property, offset_us, value, easing) */
  NC_API.nc_add_transition_keyframe = (args = {}) => call("nc_add_transition_keyframe", args);
  /** int32_t nc_transition_keyframe_count(track_index, transition_id, property) */
  NC_API.nc_transition_keyframe_count = (args = {}) => call("nc_transition_keyframe_count", args);
  /** int32_t nc_transition_keyframe_get(track_index, transition_id, property, index, out_offset_us, out_value, out_easing) */
  NC_API.nc_transition_keyframe_get = (args = {}) => call("nc_transition_keyframe_get", args);
  /** int32_t nc_remove_transition_keyframe(track_index, transition_id, property, offset_us) */
  NC_API.nc_remove_transition_keyframe = (args = {}) => call("nc_remove_transition_keyframe", args);
  /** int64_t nc_transition_duration_us(track_index, transition_id) */
  NC_API.nc_transition_duration_us = (args = {}) => call("nc_transition_duration_us", args);
  /** int32_t nc_keyframe_count(track_index, clip_id, property) */
  NC_API.nc_keyframe_count = (args = {}) => call("nc_keyframe_count", args);
  /** int32_t nc_keyframe_get(track_index, clip_id, property, index, out_time_us, out_value, out_easing) */
  NC_API.nc_keyframe_get = (args = {}) => call("nc_keyframe_get", args);
  /** int32_t nc_set_clip_background(track_index, clip_id, mode, rgba, intensity) */
  NC_API.nc_set_clip_background = (args = {}) => call("nc_set_clip_background", args);
  /** int32_t nc_get_clip_background(track_index, clip_id, out_mode, out_rgba, out_intensity) */
  NC_API.nc_get_clip_background = (args = {}) => call("nc_get_clip_background", args);
  /** uint64_t nc_add_marker(time_us, name, color) */
  NC_API.nc_add_marker = (args = {}) => call("nc_add_marker", args);
  /** int32_t nc_remove_marker(marker_id) */
  NC_API.nc_remove_marker = (args = {}) => call("nc_remove_marker", args);
  /** int32_t nc_marker_count() */
  NC_API.nc_marker_count = () => call("nc_marker_count", {});
  /** int32_t nc_marker_get(index, out_marker_id, out_time_us, out_name, out_color) */
  NC_API.nc_marker_get = (args = {}) => call("nc_marker_get", args);
  /** uint64_t nc_detach_audio(video_track_index, video_clip_id, audio_track_index) */
  NC_API.nc_detach_audio = (args = {}) => call("nc_detach_audio", args);
  /** int32_t nc_ai_feature_access(feature, logged_in, out_requires_network, out_requires_pro, out_reason) */
  NC_API.nc_ai_feature_access = (args = {}) => call("nc_ai_feature_access", args);
  /** int32_t nc_ai_task_status(out_feature, out_state, out_progress, out_stage_note, out_failure_reason, out_retryable, out_result_payload) */
  NC_API.nc_ai_task_status = (args = {}) => call("nc_ai_task_status", args);
  /** int32_t nc_ai_task_begin(feature, target_id, out_error) */
  NC_API.nc_ai_task_begin = (args = {}) => call("nc_ai_task_begin", args);
  /** int32_t nc_ai_task_progress(progress, stage_note) */
  NC_API.nc_ai_task_progress = (args = {}) => call("nc_ai_task_progress", args);
  /** int32_t nc_ai_task_succeed(result_payload) */
  NC_API.nc_ai_task_succeed = (args = {}) => call("nc_ai_task_succeed", args);
  /** int32_t nc_ai_task_fail(reason, retryable) */
  NC_API.nc_ai_task_fail = (args = {}) => call("nc_ai_task_fail", args);
  /** int32_t nc_ai_task_cancel() */
  NC_API.nc_ai_task_cancel = () => call("nc_ai_task_cancel", {});
  /** int32_t nc_ai_task_reset() */
  NC_API.nc_ai_task_reset = () => call("nc_ai_task_reset", {});
  /** char* nc_ai_task_failure_text() */
  NC_API.nc_ai_task_failure_text = () => call("nc_ai_task_failure_text", {});
  /** int32_t nc_ai_engine_info(out_available, out_version, out_note) */
  NC_API.nc_ai_engine_info = (args = {}) => call("nc_ai_engine_info", args);
  /** int32_t nc_depth_load(model_path, out_error) */
  NC_API.nc_depth_load = (args = {}) => call("nc_depth_load", args);
  /** int32_t nc_depth_unload() */
  NC_API.nc_depth_unload = () => call("nc_depth_unload", {});
  /** int32_t nc_depth_loaded() */
  NC_API.nc_depth_loaded = () => call("nc_depth_loaded", {});
  /** int32_t nc_depth_estimate(rgba, width, height, out_depth, out_width, out_height, out_error) */
  NC_API.nc_depth_estimate = (args = {}) => call("nc_depth_estimate", args);
  /** int32_t nc_depth_apply_effect(rgba, depth, width, height, mode, focus_distance, aperture, intensity, color_rgba, frame_time) */
  NC_API.nc_depth_apply_effect = (args = {}) => call("nc_depth_apply_effect", args);
  /** int32_t nc_superres_load(model_path, out_error) */
  NC_API.nc_superres_load = (args = {}) => call("nc_superres_load", args);
  /** int32_t nc_superres_unload() */
  NC_API.nc_superres_unload = () => call("nc_superres_unload", {});
  /** int32_t nc_superres_loaded() */
  NC_API.nc_superres_loaded = () => call("nc_superres_loaded", {});
  /** int32_t nc_superres_upscale(rgba, width, height, scale, out_rgba, out_width, out_height, out_tiles, out_fallbacks, out_error) */
  NC_API.nc_superres_upscale = (args = {}) => call("nc_superres_upscale", args);
  /** int32_t nc_translate_load(encoder_path, decoder_path, tokenizer_json_path, out_error) */
  NC_API.nc_translate_load = (args = {}) => call("nc_translate_load", args);
  /** int32_t nc_translate_unload() */
  NC_API.nc_translate_unload = () => call("nc_translate_unload", {});
  /** int32_t nc_translate_loaded() */
  NC_API.nc_translate_loaded = () => call("nc_translate_loaded", {});
  /** int32_t nc_translate_text(text, out_text, out_error) */
  NC_API.nc_translate_text = (args = {}) => call("nc_translate_text", args);
  /** int32_t nc_translate_subtitles(out_json, out_error) */
  NC_API.nc_translate_subtitles = (args = {}) => call("nc_translate_subtitles", args);
  /** int32_t nc_matting_load(model_path, out_error) */
  NC_API.nc_matting_load = (args = {}) => call("nc_matting_load", args);
  /** int32_t nc_matting_unload() */
  NC_API.nc_matting_unload = () => call("nc_matting_unload", {});
  /** int32_t nc_matting_loaded() */
  NC_API.nc_matting_loaded = () => call("nc_matting_loaded", {});
  /** int32_t nc_matting_estimate(rgba, width, height, quality, feather, out_alpha, out_error) */
  NC_API.nc_matting_estimate = (args = {}) => call("nc_matting_estimate", args);
  /** int32_t nc_person_effect_apply(rgba, alpha, width, height, effect, intensity) */
  NC_API.nc_person_effect_apply = (args = {}) => call("nc_person_effect_apply", args);
  /** int32_t nc_speech_load(encoder_path, decoder_path, tokenizer_path, out_error) */
  NC_API.nc_speech_load = (args = {}) => call("nc_speech_load", args);
  /** int32_t nc_speech_unload() */
  NC_API.nc_speech_unload = () => call("nc_speech_unload", {});
  /** int32_t nc_speech_loaded() */
  NC_API.nc_speech_loaded = () => call("nc_speech_loaded", {});
  /** int32_t nc_speech_transcribe(samples, frame_count, sample_rate, language, out_segments_json, out_error) */
  NC_API.nc_speech_transcribe = (args = {}) => call("nc_speech_transcribe", args);
  /** int32_t nc_speech_mel(samples, frame_count, sample_rate, frames, out_mel) */
  NC_API.nc_speech_mel = (args = {}) => call("nc_speech_mel", args);
  /** uint64_t nc_add_effect(track_index, clip_id, type, value) */
  NC_API.nc_add_effect = (args = {}) => call("nc_add_effect", args);
  /** int32_t nc_remove_effect(track_index, clip_id, effect_id) */
  NC_API.nc_remove_effect = (args = {}) => call("nc_remove_effect", args);
  /** int32_t nc_effect_count(track_index, clip_id) */
  NC_API.nc_effect_count = (args = {}) => call("nc_effect_count", args);
  /** int32_t nc_effect_get(track_index, clip_id, index, out_type, out_value, out_enabled) */
  NC_API.nc_effect_get = (args = {}) => call("nc_effect_get", args);
  /** int32_t nc_insert_keyframe(track_index, clip_id, property, time_us, value) */
  NC_API.nc_insert_keyframe = (args = {}) => call("nc_insert_keyframe", args);
  /** int32_t nc_remove_keyframe(track_index, clip_id, property, time_us) */
  NC_API.nc_remove_keyframe = (args = {}) => call("nc_remove_keyframe", args);
  /** int32_t nc_set_clip_audio(track_index, clip_id, gain_db, fade_in_us, fade_out_us) */
  NC_API.nc_set_clip_audio = (args = {}) => call("nc_set_clip_audio", args);
  /** int32_t nc_set_clip_denoise(track_index, clip_id, strength) */
  NC_API.nc_set_clip_denoise = (args = {}) => call("nc_set_clip_denoise", args);
  /** int32_t nc_get_clip_denoise(track_index, clip_id) */
  NC_API.nc_get_clip_denoise = (args = {}) => call("nc_get_clip_denoise", args);
  /** int32_t nc_auto_edit(track_index, profile_shots, shot_count, avg_r, avg_g, avg_b, avg_saturation, avg_contrast, asset_ids, usable_seconds, material_count, apply_color, out_error) */
  NC_API.nc_auto_edit = (args = {}) => call("nc_auto_edit", args);
  /** int64_t nc_export_run(out_path, crf, include_audio, attach_ai_label, ai_label_text, , out_error) */
  NC_API.nc_export_run = (args = {}) => call("nc_export_run", args);
  /** int32_t nc_set_frame_fit(mode) */
  NC_API.nc_set_frame_fit = (args = {}) => call("nc_set_frame_fit", args);
  /** int32_t nc_get_frame_fit() */
  NC_API.nc_get_frame_fit = () => call("nc_get_frame_fit", {});
  /** int32_t nc_set_background_color(rgba) */
  NC_API.nc_set_background_color = (args = {}) => call("nc_set_background_color", args);
  /** int32_t nc_set_track_auto_duck(track_index, enabled, sensitivity, duck_amount_db, attack_us, release_us, hold_us) */
  NC_API.nc_set_track_auto_duck = (args = {}) => call("nc_set_track_auto_duck", args);
  /** int32_t nc_get_track_auto_duck(track_index, out_enabled, out_sensitivity, out_duck_amount_db) */
  NC_API.nc_get_track_auto_duck = (args = {}) => call("nc_get_track_auto_duck", args);
  /** int32_t nc_create_compound_clip(track_index, clip_ids, count, name, out_sequence_id, out_clip_id) */
  NC_API.nc_create_compound_clip = (args = {}) => call("nc_create_compound_clip", args);
  /** int32_t nc_unwrap_compound_clip(track_index, clip_id) */
  NC_API.nc_unwrap_compound_clip = (args = {}) => call("nc_unwrap_compound_clip", args);
  /** int32_t nc_clip_is_compound(track_index, clip_id) */
  NC_API.nc_clip_is_compound = (args = {}) => call("nc_clip_is_compound", args);
  /** int32_t nc_set_track_solo(track_index, solo) */
  NC_API.nc_set_track_solo = (args = {}) => call("nc_set_track_solo", args);
  /** int32_t nc_set_track_gain(track_index, gain_db) */
  NC_API.nc_set_track_gain = (args = {}) => call("nc_set_track_gain", args);
  /** int32_t nc_get_track_mixer(track_index, out_muted, out_solo, out_gain_db) */
  NC_API.nc_get_track_mixer = (args = {}) => call("nc_get_track_mixer", args);
  /** int32_t nc_set_master_gain(gain_db) */
  NC_API.nc_set_master_gain = (args = {}) => call("nc_set_master_gain", args);
  /** int32_t nc_get_master_gain(out_gain_db) */
  NC_API.nc_get_master_gain = (args = {}) => call("nc_get_master_gain", args);
  /** int32_t nc_set_entitlement(tier, lifetime, expires_at_unix) */
  NC_API.nc_set_entitlement = (args = {}) => call("nc_set_entitlement", args);
  /** int32_t nc_get_entitlement() */
  NC_API.nc_get_entitlement = () => call("nc_get_entitlement", {});
  /** int32_t nc_can_add_track() */
  NC_API.nc_can_add_track = () => call("nc_can_add_track", {});
  /** int32_t nc_would_clamp_export(width, height) */
  NC_API.nc_would_clamp_export = (args = {}) => call("nc_would_clamp_export", args);
  /** int32_t nc_ai_check(out_report_json, out_error) */
  NC_API.nc_ai_check = (args = {}) => call("nc_ai_check", args);
  /** int32_t nc_ai_check_fix_all(out_error) */
  NC_API.nc_ai_check_fix_all = (args = {}) => call("nc_ai_check_fix_all", args);
  /** int32_t nc_proxy_check_import(asset_id, out_proxy_width, out_proxy_height, out_estimated_seconds, out_estimated_bytes, out_reason, out_error) */
  NC_API.nc_proxy_check_import = (args = {}) => call("nc_proxy_check_import", args);
  /** int32_t nc_proxy_set_record(asset_id, path, status, width, height, size_bytes, note) */
  NC_API.nc_proxy_set_record = (args = {}) => call("nc_proxy_set_record", args);
  /** int32_t nc_proxy_get_record(asset_id, out_status, out_width, out_height, out_size_bytes, out_path, out_note) */
  NC_API.nc_proxy_get_record = (args = {}) => call("nc_proxy_get_record", args);
  /** int32_t nc_proxy_usage(out_ready_count, out_generating_count, out_total_bytes) */
  NC_API.nc_proxy_usage = (args = {}) => call("nc_proxy_usage", args);
  /** int32_t nc_proxy_clear_records() */
  NC_API.nc_proxy_clear_records = () => call("nc_proxy_clear_records", {});
  /** int32_t nc_export_presets(out_ids, capacity, out_count) */
  NC_API.nc_export_presets = (args = {}) => call("nc_export_presets", args);
  /** int32_t nc_export_preset_info(preset_id, out_width, out_height, out_fps, out_requires_pro, out_label) */
  NC_API.nc_export_preset_info = (args = {}) => call("nc_export_preset_info", args);
  /** int32_t nc_export_estimate(width, height, fps, duration_us, video_bitrate_mbps, include_audio, out_file_size_bytes, out_render_seconds, out_size_text, out_time_text) */
  NC_API.nc_export_estimate = (args = {}) => call("nc_export_estimate", args);
  /** int32_t nc_export_setup_preset(preset_id, mode, cpu_cores, crf, out_width, out_height, out_fps, out_downgraded, out_preset_name, out_warning) */
  NC_API.nc_export_setup_preset = (args = {}) => call("nc_export_setup_preset", args);
  /** int32_t nc_speech_analyze(asset_id, out_speech_us, out_silence_us, out_keep_ratio, out_segment_count, out_note, out_error) */
  NC_API.nc_speech_analyze = (args = {}) => call("nc_speech_analyze", args);
  /** int32_t nc_speech_cut_apply(track_index, out_error) */
  NC_API.nc_speech_cut_apply = (args = {}) => call("nc_speech_cut_apply", args);
  /** int32_t nc_set_clip_mask(track_index, clip_id, shape, center_x, center_y, rotation_degrees, width, height, feather, corner_radius, invert) */
  NC_API.nc_set_clip_mask = (args = {}) => call("nc_set_clip_mask", args);
  /** int32_t nc_get_clip_mask(track_index, clip_id, out_shape, out_center_x, out_center_y, out_rotation_degrees, out_width, out_height, out_feather, out_corner_radius, out_invert) */
  NC_API.nc_get_clip_mask = (args = {}) => call("nc_get_clip_mask", args);
  /** int32_t nc_matting_access(quality, out_quality, out_reason) */
  NC_API.nc_matting_access = (args = {}) => call("nc_matting_access", args);
  /** int32_t nc_set_clip_chroma_key(track_index, clip_id, key_color, inner_threshold, outer_threshold, spill_suppression, quality, enabled) */
  NC_API.nc_set_clip_chroma_key = (args = {}) => call("nc_set_clip_chroma_key", args);
  /** int32_t nc_get_clip_chroma_key(track_index, clip_id, out_key_color_rgb, out_inner, out_outer, out_spill) */
  NC_API.nc_get_clip_chroma_key = (args = {}) => call("nc_get_clip_chroma_key", args);
  /** double nc_chroma_key_estimate(rgba, width, height, key_color, inner_threshold, outer_threshold, spill_suppression, quality) */
  NC_API.nc_chroma_key_estimate = (args = {}) => call("nc_chroma_key_estimate", args);
  /** int32_t nc_set_clip_blend_mode(track_index, clip_id, mode) */
  NC_API.nc_set_clip_blend_mode = (args = {}) => call("nc_set_clip_blend_mode", args);
  /** int32_t nc_get_clip_blend_mode(track_index, clip_id) */
  NC_API.nc_get_clip_blend_mode = (args = {}) => call("nc_get_clip_blend_mode", args);
  /** int32_t nc_model_catalog(out_ids, capacity, out_count) */
  NC_API.nc_model_catalog = (args = {}) => call("nc_model_catalog", args);
  /** int32_t nc_model_info(model_id, out_name, out_description, out_size_bytes, out_version, out_requires_pro, out_capability) */
  NC_API.nc_model_info = (args = {}) => call("nc_model_info", args);
  /** int32_t nc_model_status(model_id) */
  NC_API.nc_model_status = (args = {}) => call("nc_model_status", args);
  /** int32_t nc_model_access_allowed(model_id) */
  NC_API.nc_model_access_allowed = (args = {}) => call("nc_model_access_allowed", args);
  /** int32_t nc_model_download_begin(model_id, total_bytes, now_unix) */
  NC_API.nc_model_download_begin = (args = {}) => call("nc_model_download_begin", args);
  /** int32_t nc_model_download_progress(model_id, downloaded_bytes, now_unix) */
  NC_API.nc_model_download_progress = (args = {}) => call("nc_model_download_progress", args);
  /** int32_t nc_model_download_pause(model_id, now_unix) */
  NC_API.nc_model_download_pause = (args = {}) => call("nc_model_download_pause", args);
  /** int32_t nc_model_download_resume(model_id, now_unix) */
  NC_API.nc_model_download_resume = (args = {}) => call("nc_model_download_resume", args);
  /** int32_t nc_model_download_cancel(model_id, now_unix) */
  NC_API.nc_model_download_cancel = (args = {}) => call("nc_model_download_cancel", args);
  /** int32_t nc_model_download_finish(model_id, now_unix) */
  NC_API.nc_model_download_finish = (args = {}) => call("nc_model_download_finish", args);
  /** int32_t nc_model_download_fail(model_id, reason, now_unix) */
  NC_API.nc_model_download_fail = (args = {}) => call("nc_model_download_fail", args);
  /** int32_t nc_model_download_state(model_id, out_phase, out_total_bytes, out_downloaded_bytes, out_message) */
  NC_API.nc_model_download_state = (args = {}) => call("nc_model_download_state", args);
  /** int32_t nc_model_resume_plan(model_id, downloaded_bytes, out_offset_bytes, out_counter, out_intra_block_skip) */
  NC_API.nc_model_resume_plan = (args = {}) => call("nc_model_resume_plan", args);
  /** int32_t nc_model_install(model_id, ciphertext, size, path, expected_sha256_hex, now_unix, out_installed, out_message) */
  NC_API.nc_model_install = (args = {}) => call("nc_model_install", args);
  /** int32_t nc_model_remove(model_id) */
  NC_API.nc_model_remove = (args = {}) => call("nc_model_remove", args);
  /** int32_t nc_model_storage_stats(device_total_bytes, device_free_bytes, out_model_bytes, out_installed_count, out_corrupt_count, out_usage_text, out_model_text) */
  NC_API.nc_model_storage_stats = (args = {}) => call("nc_model_storage_stats", args);
  /** int32_t nc_model_decrypt(model_id, version, key, ciphertext, size, offset, out_plaintext, out_size) */
  NC_API.nc_model_decrypt = (args = {}) => call("nc_model_decrypt", args);
  /** int32_t nc_chapter_detect(min_chapter_seconds, max_chapter_seconds, silence_boundary_seconds, use_scene_detect, use_silence_detect, max_chapters, out_summary, out_note, out_error) */
  NC_API.nc_chapter_detect = (args = {}) => call("nc_chapter_detect", args);
  /** int32_t nc_chapter_apply(out_error) */
  NC_API.nc_chapter_apply = (args = {}) => call("nc_chapter_apply", args);
  /** int32_t nc_chapter_count() */
  NC_API.nc_chapter_count = () => call("nc_chapter_count", {});
  /** int32_t nc_chapter_get(index, out_start_us, out_end_us, out_title) */
  NC_API.nc_chapter_get = (args = {}) => call("nc_chapter_get", args);
  /** int32_t nc_chapter_rename(index, new_title, out_error) */
  NC_API.nc_chapter_rename = (args = {}) => call("nc_chapter_rename", args);
  /** int32_t nc_reference_style_access(out_reason) */
  NC_API.nc_reference_style_access = (args = {}) => call("nc_reference_style_access", args);
  /** int32_t nc_movie_version_access(requested, out_allowed_count, out_reason) */
  NC_API.nc_movie_version_access = (args = {}) => call("nc_movie_version_access", args);
  /** int32_t nc_smart_movie_plan(version_count, pace_factor, target_seconds, style_json, material_ids, material_seconds, material_count, out_summary) */
  NC_API.nc_smart_movie_plan = (args = {}) => call("nc_smart_movie_plan", args);
  /** int32_t nc_smart_movie_apply(track_indexes, track_count, out_applied, out_failed_json, out_error) */
  NC_API.nc_smart_movie_apply = (args = {}) => call("nc_smart_movie_apply", args);
  /** int32_t nc_regeneration_impact(scope, out_report_json) */
  NC_API.nc_regeneration_impact = (args = {}) => call("nc_regeneration_impact", args);
  /** int32_t nc_regeneration_clear(scope, out_cleared_json, out_error) */
  NC_API.nc_regeneration_clear = (args = {}) => call("nc_regeneration_clear", args);
  /** int32_t nc_chapter_clear(out_error) */
  NC_API.nc_chapter_clear = (args = {}) => call("nc_chapter_clear", args);
  /** int32_t nc_export_enqueue(name, out_path, preset_id, mode, include_audio, attach_ai_label, ai_label_text, out_job_id, out_error) */
  NC_API.nc_export_enqueue = (args = {}) => call("nc_export_enqueue", args);
  /** int32_t nc_export_poll_next(out_job_id, out_out_path, out_preset, out_mode, out_include_audio, out_attach_ai_label, out_ai_label_text) */
  NC_API.nc_export_poll_next = (args = {}) => call("nc_export_poll_next", args);
  /** int32_t nc_export_job_state(job_id, out_state, out_progress, out_frames_written, out_note) */
  NC_API.nc_export_job_state = (args = {}) => call("nc_export_job_state", args);
  /** int32_t nc_export_job_progress(job_id, progress) */
  NC_API.nc_export_job_progress = (args = {}) => call("nc_export_job_progress", args);
  /** int32_t nc_export_job_update(job_id, action, frames_written, reason, now_unix, out_error) */
  NC_API.nc_export_job_update = (args = {}) => call("nc_export_job_update", args);
  /** int32_t nc_export_queue_summary(out_queued, out_running, out_active) */
  NC_API.nc_export_queue_summary = (args = {}) => call("nc_export_queue_summary", args);
  /** void nc_export_queue_cancel_all(now_unix) */
  NC_API.nc_export_queue_cancel_all = (args = {}) => call("nc_export_queue_cancel_all", args);
  /** int32_t nc_media_scan_offline(probe_kind, out_report_json, out_error) */
  NC_API.nc_media_scan_offline = (args = {}) => call("nc_media_scan_offline", args);
  /** int32_t nc_media_apply_offline_flags() */
  NC_API.nc_media_apply_offline_flags = () => call("nc_media_apply_offline_flags", {});
  /** int32_t nc_media_relink(asset_id, new_path, verify_digest, out_warning, out_error) */
  NC_API.nc_media_relink = (args = {}) => call("nc_media_relink", args);
  /** int32_t nc_media_find_relink_candidates(search_dirs_json, out_candidates_json, out_error) */
  NC_API.nc_media_find_relink_candidates = (args = {}) => call("nc_media_find_relink_candidates", args);
  /** int32_t nc_media_relink_batch(mappings_json, out_relinked, out_report_json, out_error) */
  NC_API.nc_media_relink_batch = (args = {}) => call("nc_media_relink_batch", args);
  /** int32_t nc_media_set_digest(asset_id, sha256_hex, out_error) */
  NC_API.nc_media_set_digest = (args = {}) => call("nc_media_set_digest", args);
  /** uint32_t nc_watermark_account_hash(account_id) */
  NC_API.nc_watermark_account_hash = (args = {}) => call("nc_watermark_account_hash", args);
  /** int32_t nc_watermark_set_export(enabled, account_id, export_sequence) */
  NC_API.nc_watermark_set_export = (args = {}) => call("nc_watermark_set_export", args);
  /** int32_t nc_watermark_detect(rgba, width, height, out_account_hash, out_export_sequence, out_agreement, out_note) */
  NC_API.nc_watermark_detect = (args = {}) => call("nc_watermark_detect", args);
  /** int32_t nc_heartbeat_handle(server_time_unix, tier, lifetime, expires_at_unix, active_devices, device_limit, signature_valid, server_locked, message, out_verdict, out_message) */
  NC_API.nc_heartbeat_handle = (args = {}) => call("nc_heartbeat_handle", args);
  /** int32_t nc_heartbeat_network_error(out_message) */
  NC_API.nc_heartbeat_network_error = (args = {}) => call("nc_heartbeat_network_error", args);
  /** int32_t nc_heartbeat_status(out_locked, out_warning_count, out_last_checked_unix) */
  NC_API.nc_heartbeat_status = (args = {}) => call("nc_heartbeat_status", args);
  /** int32_t nc_heartbeat_periodic_due(now_unix, interval_seconds) */
  NC_API.nc_heartbeat_periodic_due = (args = {}) => call("nc_heartbeat_periodic_due", args);
  /** int32_t nc_heartbeat_clear_lock() */
  NC_API.nc_heartbeat_clear_lock = () => call("nc_heartbeat_clear_lock", {});

  /* 接口名清单：便于 UI 侧自检「我调的函数是否存在」 */
  NC_API.FUNCTIONS = ["nc_abi_version", "nc_project_save", "nc_project_load", "nc_project_duration_us", "nc_track_count", "nc_clip_count", "nc_project_name", "nc_set_project_name", "nc_project_canvas", "nc_set_project_canvas", "nc_project_frame_rate", "nc_set_project_frame_rate", "nc_can_undo", "nc_can_redo", "nc_undo_count", "nc_redo_count", "nc_undo_history_name", "nc_clear_undo_history", "nc_set_undo_depth_limit", "nc_asset_count", "nc_asset_get", "nc_remove_asset", "nc_asset_is_referenced", "nc_import_asset", "nc_add_track", "nc_insert_clip", "nc_undo", "nc_redo", "nc_track_get", "nc_remove_track", "nc_rename_track", "nc_set_track_locked", "nc_set_track_muted", "nc_set_track_hidden", "nc_remove_clip", "nc_split_clip", "nc_move_clip", "nc_move_clip_to_track", "nc_trim_clip_start", "nc_trim_clip_end", "nc_set_clip_speed", "nc_ripple_delete", "nc_duplicate_clip", "nc_freeze_frame", "nc_reverse_clip", "nc_clip_info", "nc_set_clip_enabled", "nc_get_clip_opacity", "nc_set_clip_opacity", "nc_set_clip_filter", "nc_get_clip_filter", "nc_filter_display_name", "nc_upsert_subtitle", "nc_remove_subtitle", "nc_subtitle_count", "nc_subtitle_get", "nc_add_transition", "nc_remove_transition", "nc_transition_count", "nc_transition_get", "nc_transition_type_count", "nc_transition_type_name", "nc_transition_category", "nc_transition_supports_direction", "nc_clip_get", "nc_preview_render", "nc_preview_subtitles", "nc_preview_invalidate", "nc_thumbnail_generate", "nc_thumbnail_size", "nc_waveform_generate", "nc_waveform_bucket_count", "nc_waveform_bucket_get", "nc_scene_detect", "nc_beat_detect", "nc_set_clip_speed_curve", "nc_clip_speed_curve_count", "nc_clip_speed_curve_get", "nc_speed_curve_preset_name", "nc_speed_curve_preset_display_name", "nc_apply_speed_curve_preset", "nc_set_camera_tracking", "nc_set_camera_tracking_scaled", "nc_tracking_point_count", "nc_tracking_point_get", "nc_tracking_point_get_scaled", "nc_clear_camera_tracking", "nc_set_depth_effect", "nc_get_depth_effect", "nc_add_transition_keyframe", "nc_transition_keyframe_count", "nc_transition_keyframe_get", "nc_remove_transition_keyframe", "nc_transition_duration_us", "nc_keyframe_count", "nc_keyframe_get", "nc_set_clip_background", "nc_get_clip_background", "nc_add_marker", "nc_remove_marker", "nc_marker_count", "nc_marker_get", "nc_detach_audio", "nc_ai_feature_access", "nc_ai_task_status", "nc_ai_task_begin", "nc_ai_task_progress", "nc_ai_task_succeed", "nc_ai_task_fail", "nc_ai_task_cancel", "nc_ai_task_reset", "nc_ai_task_failure_text", "nc_ai_engine_info", "nc_depth_load", "nc_depth_unload", "nc_depth_loaded", "nc_depth_estimate", "nc_depth_apply_effect", "nc_superres_load", "nc_superres_unload", "nc_superres_loaded", "nc_superres_upscale", "nc_translate_load", "nc_translate_unload", "nc_translate_loaded", "nc_translate_text", "nc_translate_subtitles", "nc_matting_load", "nc_matting_unload", "nc_matting_loaded", "nc_matting_estimate", "nc_person_effect_apply", "nc_speech_load", "nc_speech_unload", "nc_speech_loaded", "nc_speech_transcribe", "nc_speech_mel", "nc_add_effect", "nc_remove_effect", "nc_effect_count", "nc_effect_get", "nc_insert_keyframe", "nc_remove_keyframe", "nc_set_clip_audio", "nc_set_clip_denoise", "nc_get_clip_denoise", "nc_auto_edit", "nc_export_run", "nc_set_frame_fit", "nc_get_frame_fit", "nc_set_background_color", "nc_set_track_auto_duck", "nc_get_track_auto_duck", "nc_create_compound_clip", "nc_unwrap_compound_clip", "nc_clip_is_compound", "nc_set_track_solo", "nc_set_track_gain", "nc_get_track_mixer", "nc_set_master_gain", "nc_get_master_gain", "nc_set_entitlement", "nc_get_entitlement", "nc_can_add_track", "nc_would_clamp_export", "nc_ai_check", "nc_ai_check_fix_all", "nc_proxy_check_import", "nc_proxy_set_record", "nc_proxy_get_record", "nc_proxy_usage", "nc_proxy_clear_records", "nc_export_presets", "nc_export_preset_info", "nc_export_estimate", "nc_export_setup_preset", "nc_speech_analyze", "nc_speech_cut_apply", "nc_set_clip_mask", "nc_get_clip_mask", "nc_matting_access", "nc_set_clip_chroma_key", "nc_get_clip_chroma_key", "nc_chroma_key_estimate", "nc_set_clip_blend_mode", "nc_get_clip_blend_mode", "nc_model_catalog", "nc_model_info", "nc_model_status", "nc_model_access_allowed", "nc_model_download_begin", "nc_model_download_progress", "nc_model_download_pause", "nc_model_download_resume", "nc_model_download_cancel", "nc_model_download_finish", "nc_model_download_fail", "nc_model_download_state", "nc_model_resume_plan", "nc_model_install", "nc_model_remove", "nc_model_storage_stats", "nc_model_decrypt", "nc_chapter_detect", "nc_chapter_apply", "nc_chapter_count", "nc_chapter_get", "nc_chapter_rename", "nc_reference_style_access", "nc_movie_version_access", "nc_smart_movie_plan", "nc_smart_movie_apply", "nc_regeneration_impact", "nc_regeneration_clear", "nc_chapter_clear", "nc_export_enqueue", "nc_export_poll_next", "nc_export_job_state", "nc_export_job_progress", "nc_export_job_update", "nc_export_queue_summary", "nc_export_queue_cancel_all", "nc_media_scan_offline", "nc_media_apply_offline_flags", "nc_media_relink", "nc_media_find_relink_candidates", "nc_media_relink_batch", "nc_media_set_digest", "nc_watermark_account_hash", "nc_watermark_set_export", "nc_watermark_detect", "nc_heartbeat_handle", "nc_heartbeat_network_error", "nc_heartbeat_status", "nc_heartbeat_periodic_due", "nc_heartbeat_clear_lock"];

  window.NC_API = NC_API;
})();
