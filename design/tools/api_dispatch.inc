// ===========================================================================
// 自动生成的调度表（生成器已移除，此文件为权威产物，如需改动请手工编辑）
// 来源：core/include/nairoclip/core/capi.h
// 用途：/api/call 通用派发，前端 {"fn":"nc_xxx","args":{...}} 调用任意 C ABI
// ===========================================================================

struct ApiCallResult {
    int32_t code = NC_OK;
    JsonValue result = JsonValue::object();
};

/// 通用 C ABI 派发；fn 不在表内返回 false。
static bool dispatchApi(const std::string& fn, const JsonValue& args,
                        ApiCallResult& out) {
    out.result = JsonValue::object();
    if (fn == "nc_abi_version") {
        const auto ret = nc_abi_version();
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_project_save") {
        out.result.set("value", JsonValue::string(takeString(nc_project_save(gEditor))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_project_load") {
        char* detail = nullptr;
        const auto ret = nc_project_load(gEditor, jstr(args, "nclip_text").c_str(), &detail);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("detail", JsonValue::string(takeString(detail)));
        return true;
    }
    if (fn == "nc_project_duration_us") {
        const auto ret = nc_project_duration_us(gEditor);
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_track_count") {
        const auto ret = nc_track_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_clip_count") {
        const auto ret = nc_clip_count(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_project_name") {
        out.result.set("value", JsonValue::string(takeString(nc_project_name(gEditor))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_set_project_name") {
        const auto ret = nc_set_project_name(gEditor, jstr(args, "name").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_project_canvas") {
        int32_t out_width = 0;
        int32_t out_height = 0;
        const auto ret = nc_project_canvas(gEditor, &out_width, &out_height);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_width", JsonValue::integer(out_width));
        out.result.set("out_height", JsonValue::integer(out_height));
        return true;
    }
    if (fn == "nc_set_project_canvas") {
        const auto ret = nc_set_project_canvas(gEditor, static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_project_frame_rate") {
        int64_t out_num = 0;
        int64_t out_den = 0;
        const auto ret = nc_project_frame_rate(gEditor, &out_num, &out_den);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_num", JsonValue::integer(out_num));
        out.result.set("out_den", JsonValue::integer(out_den));
        return true;
    }
    if (fn == "nc_set_project_frame_rate") {
        const auto ret = nc_set_project_frame_rate(gEditor, static_cast<std::int64_t>(jint(args, "num", 0)), static_cast<std::int64_t>(jint(args, "den", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_can_undo") {
        const auto ret = nc_can_undo(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_can_redo") {
        const auto ret = nc_can_redo(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_undo_count") {
        const auto ret = nc_undo_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_redo_count") {
        const auto ret = nc_redo_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_undo_history_name") {
        out.result.set("value", JsonValue::string(takeString(nc_undo_history_name(gEditor, static_cast<int32_t>(jint(args, "index", 0))))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_clear_undo_history") {
        const auto ret = nc_clear_undo_history(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_undo_depth_limit") {
        const auto ret = nc_set_undo_depth_limit(gEditor, static_cast<int32_t>(jint(args, "max_steps", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_asset_count") {
        const auto ret = nc_asset_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_asset_get") {
        std::uint64_t out_asset_id = 0;
        char* out_name = nullptr;
        char* out_path = nullptr;
        int32_t out_type = 0;
        int64_t out_duration_us = 0;
        const auto ret = nc_asset_get(gEditor, static_cast<int32_t>(jint(args, "index", 0)), &out_asset_id, &out_name, &out_path, &out_type, &out_duration_us);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_asset_id", JsonValue::integer(static_cast<std::int64_t>(out_asset_id)));
        out.result.set("out_name", JsonValue::string(takeString(out_name)));
        out.result.set("out_path", JsonValue::string(takeString(out_path)));
        out.result.set("out_type", JsonValue::integer(out_type));
        out.result.set("out_duration_us", JsonValue::integer(out_duration_us));
        return true;
    }
    if (fn == "nc_remove_asset") {
        const auto ret = nc_remove_asset(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_asset_is_referenced") {
        const auto ret = nc_asset_is_referenced(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_import_asset") {
        const auto ret = nc_import_asset(gEditor, jstr(args, "name").c_str(), jstr(args, "path").c_str(), static_cast<int32_t>(jint(args, "type", 0)), static_cast<std::int64_t>(jint(args, "duration_us", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_add_track") {
        const auto ret = nc_add_track(gEditor, jstr(args, "name").c_str(), static_cast<int32_t>(jint(args, "track_type", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_insert_clip") {
        const auto ret = nc_insert_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "asset_id", 0)), static_cast<std::int64_t>(jint(args, "start_us", 0)), static_cast<std::int64_t>(jint(args, "duration_us", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_undo") {
        const auto ret = nc_undo(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_redo") {
        const auto ret = nc_redo(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_track_get") {
        std::uint64_t out_track_id = 0;
        char* out_name = nullptr;
        int32_t out_type = 0;
        int32_t out_locked = 0;
        int32_t out_muted = 0;
        int32_t out_hidden = 0;
        int32_t out_solo = 0;
        double out_gain_db = 0.0;
        const auto ret = nc_track_get(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), &out_track_id, &out_name, &out_type, &out_locked, &out_muted, &out_hidden, &out_solo, &out_gain_db);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_track_id", JsonValue::integer(static_cast<std::int64_t>(out_track_id)));
        out.result.set("out_name", JsonValue::string(takeString(out_name)));
        out.result.set("out_type", JsonValue::integer(out_type));
        out.result.set("out_locked", JsonValue::integer(out_locked));
        out.result.set("out_muted", JsonValue::integer(out_muted));
        out.result.set("out_hidden", JsonValue::integer(out_hidden));
        out.result.set("out_solo", JsonValue::integer(out_solo));
        out.result.set("out_gain_db", JsonValue::number(out_gain_db));
        return true;
    }
    if (fn == "nc_remove_track") {
        const auto ret = nc_remove_track(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_rename_track") {
        const auto ret = nc_rename_track(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), jstr(args, "new_name").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_track_locked") {
        const auto ret = nc_set_track_locked(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "locked", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_track_muted") {
        const auto ret = nc_set_track_muted(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "muted", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_track_hidden") {
        const auto ret = nc_set_track_hidden(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "hidden", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_remove_clip") {
        const auto ret = nc_remove_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_split_clip") {
        const auto ret = nc_split_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::int64_t>(jint(args, "at_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_move_clip") {
        const auto ret = nc_move_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::int64_t>(jint(args, "new_start_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_move_clip_to_track") {
        const auto ret = nc_move_clip_to_track(gEditor, static_cast<int32_t>(jint(args, "from_track", 0)), static_cast<int32_t>(jint(args, "to_track", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::int64_t>(jint(args, "new_start_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_trim_clip_start") {
        const auto ret = nc_trim_clip_start(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::int64_t>(jint(args, "new_start_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_trim_clip_end") {
        const auto ret = nc_trim_clip_end(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::int64_t>(jint(args, "new_end_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_clip_speed") {
        const auto ret = nc_set_clip_speed(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::int64_t>(jint(args, "speed_num", 0)), static_cast<std::int64_t>(jint(args, "speed_den", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ripple_delete") {
        const auto ret = nc_ripple_delete(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), jvecU64(args, "clip_ids").data(), static_cast<int32_t>(jint(args, "clip_count", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_duplicate_clip") {
        const auto ret = nc_duplicate_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_freeze_frame") {
        const auto ret = nc_freeze_frame(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::int64_t>(jint(args, "at_us", 0)), static_cast<std::int64_t>(jint(args, "duration_us", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_reverse_clip") {
        const auto ret = nc_reverse_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_clip_info") {
        std::uint64_t out_clip_id = 0;
        int64_t out_start_us = 0;
        int64_t out_duration_us = 0;
        std::uint64_t out_asset_id = 0;
        int64_t out_speed_num = 0;
        int64_t out_speed_den = 0;
        int32_t out_enabled = 0;
        const auto ret = nc_clip_info(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint32_t>(jint(args, "clip_index", 0)), &out_clip_id, &out_start_us, &out_duration_us, &out_asset_id, &out_speed_num, &out_speed_den, &out_enabled);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_clip_id", JsonValue::integer(static_cast<std::int64_t>(out_clip_id)));
        out.result.set("out_start_us", JsonValue::integer(out_start_us));
        out.result.set("out_duration_us", JsonValue::integer(out_duration_us));
        out.result.set("out_asset_id", JsonValue::integer(static_cast<std::int64_t>(out_asset_id)));
        out.result.set("out_speed_num", JsonValue::integer(out_speed_num));
        out.result.set("out_speed_den", JsonValue::integer(out_speed_den));
        out.result.set("out_enabled", JsonValue::integer(out_enabled));
        return true;
    }
    if (fn == "nc_set_clip_enabled") {
        const auto ret = nc_set_clip_enabled(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "enabled", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_clip_opacity") {
        double out_opacity = 0.0;
        const auto ret = nc_get_clip_opacity(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), &out_opacity);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_opacity", JsonValue::number(out_opacity));
        return true;
    }
    if (fn == "nc_set_clip_opacity") {
        const auto ret = nc_set_clip_opacity(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), jnum(args, "opacity", 0.0));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_clip_filter") {
        const auto ret = nc_set_clip_filter(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "style", 0)), jnum(args, "intensity", 0.0));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_clip_filter") {
        int32_t out_style = 0;
        double out_intensity = 0.0;
        const auto ret = nc_get_clip_filter(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), &out_style, &out_intensity);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_style", JsonValue::integer(out_style));
        out.result.set("out_intensity", JsonValue::number(out_intensity));
        return true;
    }
    if (fn == "nc_filter_display_name") {
        out.result.set("value", JsonValue::string(std::string(nc_filter_display_name(static_cast<int32_t>(jint(args, "style", 0))))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_upsert_subtitle") {
        const auto ret = nc_upsert_subtitle(gEditor, static_cast<std::int64_t>(jint(args, "start_us", 0)), static_cast<std::int64_t>(jint(args, "duration_us", 0)), jstr(args, "text").c_str());
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_remove_subtitle") {
        const auto ret = nc_remove_subtitle(gEditor, static_cast<std::uint64_t>(jint(args, "subtitle_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_subtitle_count") {
        const auto ret = nc_subtitle_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_subtitle_get") {
        int64_t start_us = 0;
        int64_t duration_us = 0;
        char* out_text = nullptr;
        const auto ret = nc_subtitle_get(gEditor, static_cast<int32_t>(jint(args, "index", 0)), &start_us, &duration_us, &out_text);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("start_us", JsonValue::integer(start_us));
        out.result.set("duration_us", JsonValue::integer(duration_us));
        out.result.set("out_text", JsonValue::string(takeString(out_text)));
        return true;
    }
    if (fn == "nc_add_transition") {
        const auto ret = nc_add_transition(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "transition_type", 0)), static_cast<std::int64_t>(jint(args, "duration_us", 0)), static_cast<std::uint64_t>(jint(args, "from_clip_id", 0)), static_cast<std::uint64_t>(jint(args, "to_clip_id", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_remove_transition") {
        const auto ret = nc_remove_transition(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "transition_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_transition_count") {
        const auto ret = nc_transition_count(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_transition_get") {
        int32_t out_type = 0;
        int64_t out_duration_us = 0;
        std::uint64_t out_from_clip = 0;
        std::uint64_t out_to_clip = 0;
        char* out_name = nullptr;
        const auto ret = nc_transition_get(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "index", 0)), &out_type, &out_duration_us, &out_from_clip, &out_to_clip, &out_name);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_type", JsonValue::integer(out_type));
        out.result.set("out_duration_us", JsonValue::integer(out_duration_us));
        out.result.set("out_from_clip", JsonValue::integer(static_cast<std::int64_t>(out_from_clip)));
        out.result.set("out_to_clip", JsonValue::integer(static_cast<std::int64_t>(out_to_clip)));
        out.result.set("out_name", JsonValue::string(takeString(out_name)));
        return true;
    }
    if (fn == "nc_transition_type_count") {
        const auto ret = nc_transition_type_count();
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_transition_type_name") {
        out.result.set("value", JsonValue::string(std::string(nc_transition_type_name(static_cast<int32_t>(jint(args, "type", 0))))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_transition_category") {
        out.result.set("value", JsonValue::string(std::string(nc_transition_category(static_cast<int32_t>(jint(args, "type", 0))))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_transition_supports_direction") {
        const auto ret = nc_transition_supports_direction(static_cast<int32_t>(jint(args, "type", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_clip_get") {
        int64_t out_start_us = 0;
        int64_t out_duration_us = 0;
        std::uint64_t out_asset_id = 0;
        const auto ret = nc_clip_get(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "clip_index", 0)), &out_start_us, &out_duration_us, &out_asset_id);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_start_us", JsonValue::integer(out_start_us));
        out.result.set("out_duration_us", JsonValue::integer(out_duration_us));
        out.result.set("out_asset_id", JsonValue::integer(static_cast<std::int64_t>(out_asset_id)));
        return true;
    }
    if (fn == "nc_preview_render") {
        int32_t out_width = 0;
        int32_t out_height = 0;
        uint8_t* out_rgba = nullptr;
        std::uint64_t out_clip_id = 0;
        double out_opacity = 0.0;
        double out_scale = 0.0;
        double out_pos_x = 0.0;
        double out_pos_y = 0.0;
        double out_rotation = 0.0;
        int32_t out_transition_type = 0;
        double out_transition_progress = 0.0;
        const auto ret = nc_preview_render(gEditor, static_cast<std::int64_t>(jint(args, "time_us", 0)), &out_width, &out_height, &out_rgba, &out_clip_id, &out_opacity, &out_scale, &out_pos_x, &out_pos_y, &out_rotation, &out_transition_type, &out_transition_progress);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_width", JsonValue::integer(out_width));
        out.result.set("out_height", JsonValue::integer(out_height));
        out.result.set("out_clip_id", JsonValue::integer(static_cast<std::int64_t>(out_clip_id)));
        out.result.set("out_opacity", JsonValue::number(out_opacity));
        out.result.set("out_scale", JsonValue::number(out_scale));
        out.result.set("out_pos_x", JsonValue::number(out_pos_x));
        out.result.set("out_pos_y", JsonValue::number(out_pos_y));
        out.result.set("out_rotation", JsonValue::number(out_rotation));
        out.result.set("out_transition_type", JsonValue::integer(out_transition_type));
        out.result.set("out_transition_progress", JsonValue::number(out_transition_progress));
        return true;
    }
    if (fn == "nc_preview_subtitles") {
        out.result.set("value", JsonValue::string(takeString(nc_preview_subtitles(gEditor, static_cast<std::int64_t>(jint(args, "time_us", 0))))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_preview_invalidate") {
        nc_preview_invalidate(gEditor);
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_preview_invalidate") {
        nc_preview_invalidate(gEditor);
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_thumbnail_generate") {
        uint8_t* out_images = nullptr;
        int64_t out_times_us = 0;
        int32_t out_count = 0;
        const auto ret = nc_thumbnail_generate(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), static_cast<int32_t>(jint(args, "count", 0)), static_cast<int32_t>(jint(args, "max_width", 0)), static_cast<int32_t>(jint(args, "max_height", 0)), &out_images, &out_times_us, &out_count);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_times_us", JsonValue::integer(out_times_us));
        out.result.set("out_count", JsonValue::integer(out_count));
        return true;
    }
    if (fn == "nc_thumbnail_size") {
        int32_t out_width = 0;
        int32_t out_height = 0;
        const auto ret = nc_thumbnail_size(gEditor, &out_width, &out_height);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_width", JsonValue::integer(out_width));
        out.result.set("out_height", JsonValue::integer(out_height));
        return true;
    }
    if (fn == "nc_waveform_generate") {
        double out_peak = 0.0;
        int32_t out_sample_rate = 0;
        int32_t out_channels = 0;
        const auto ret = nc_waveform_generate(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), static_cast<int32_t>(jint(args, "bucket_count", 0)), &out_peak, &out_sample_rate, &out_channels);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_peak", JsonValue::number(out_peak));
        out.result.set("out_sample_rate", JsonValue::integer(out_sample_rate));
        out.result.set("out_channels", JsonValue::integer(out_channels));
        return true;
    }
    if (fn == "nc_waveform_bucket_count") {
        const auto ret = nc_waveform_bucket_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_waveform_bucket_get") {
        double out_min = 0.0;
        double out_max = 0.0;
        double out_rms = 0.0;
        const auto ret = nc_waveform_bucket_get(gEditor, static_cast<int32_t>(jint(args, "index", 0)), &out_min, &out_max, &out_rms);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_min", JsonValue::number(out_min));
        out.result.set("out_max", JsonValue::number(out_max));
        out.result.set("out_rms", JsonValue::number(out_rms));
        return true;
    }
    if (fn == "nc_scene_detect") {
        int32_t out_boundary_count = 0;
        double* out_boundaries = nullptr;
        const auto ret = nc_scene_detect(gEditor, jnum(args, "step_seconds", 0.0), static_cast<int32_t>(jint(args, "max_samples", 0)), &out_boundary_count, &out_boundaries);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_boundary_count", JsonValue::integer(out_boundary_count));
        // 长度必须用引擎回填的 out_boundary_count：调用方事先并不知道有几条边界，
        // 依赖入参 count 会把结果裁成空数组（此前即为此问题）。
        // 该数组每 2 个 double 一组 [start_s, end_s]，故元素数为 count*2。
        out.result.set("out_boundaries",
                       jnumArray(out_boundaries, out_boundary_count * 2, 0));
        return true;
    }
    if (fn == "nc_beat_detect") {
        double out_bpm = 0.0;
        double out_confidence = 0.0;
        int32_t out_beat_count = 0;
        double* out_beats = nullptr;
        const auto ret = nc_beat_detect(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), &out_bpm, &out_confidence, &out_beat_count, &out_beats);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_bpm", JsonValue::number(out_bpm));
        out.result.set("out_confidence", JsonValue::number(out_confidence));
        out.result.set("out_beat_count", JsonValue::integer(out_beat_count));
        // 同上：长度用引擎回填值，不依赖调用方入参。
        out.result.set("out_beats", jnumArray(out_beats, out_beat_count, 0));
        return true;
    }
    if (fn == "nc_set_clip_speed_curve") {
        const auto ret = nc_set_clip_speed_curve(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), jvecNum(args, "source_times").data(), jvecNum(args, "speeds").data(), jvecI32(args, "easings").data(), static_cast<int32_t>(jint(args, "point_count", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_clip_speed_curve_count") {
        const auto ret = nc_clip_speed_curve_count(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_clip_speed_curve_get") {
        double out_source_seconds = 0.0;
        double out_speed = 0.0;
        int32_t out_easing = 0;
        const auto ret = nc_clip_speed_curve_get(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "index", 0)), &out_source_seconds, &out_speed, &out_easing);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_source_seconds", JsonValue::number(out_source_seconds));
        out.result.set("out_speed", JsonValue::number(out_speed));
        out.result.set("out_easing", JsonValue::integer(out_easing));
        return true;
    }
    if (fn == "nc_speed_curve_preset_name") {
        out.result.set("value", JsonValue::string(std::string(nc_speed_curve_preset_name(static_cast<int32_t>(jint(args, "preset", 0))))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_speed_curve_preset_display_name") {
        out.result.set("value", JsonValue::string(std::string(nc_speed_curve_preset_display_name(static_cast<int32_t>(jint(args, "preset", 0))))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_apply_speed_curve_preset") {
        const auto ret = nc_apply_speed_curve_preset(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "preset", 0)), static_cast<std::int64_t>(jint(args, "source_duration_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_camera_tracking") {
        const auto ret = nc_set_camera_tracking(gEditor, static_cast<std::uint64_t>(jint(args, "subtitle_id", 0)), jvecNum(args, "times_us").data(), jvecNum(args, "center_x").data(), jvecNum(args, "center_y").data(), static_cast<int32_t>(jint(args, "point_count", 0)), static_cast<int32_t>(jint(args, "apply_mode", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_camera_tracking_scaled") {
        const auto ret = nc_set_camera_tracking_scaled(gEditor, static_cast<std::uint64_t>(jint(args, "subtitle_id", 0)), jvecNum(args, "times_us").data(), jvecNum(args, "center_x").data(), jvecNum(args, "center_y").data(), jvecNum(args, "scales").data(), static_cast<int32_t>(jint(args, "point_count", 0)), static_cast<int32_t>(jint(args, "apply_mode", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_tracking_point_count") {
        const auto ret = nc_tracking_point_count(gEditor, static_cast<std::uint64_t>(jint(args, "subtitle_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_tracking_point_get") {
        double out_time_us = 0.0;
        double out_center_x = 0.0;
        double out_center_y = 0.0;
        const auto ret = nc_tracking_point_get(gEditor, static_cast<std::uint64_t>(jint(args, "subtitle_id", 0)), static_cast<int32_t>(jint(args, "index", 0)), &out_time_us, &out_center_x, &out_center_y);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_time_us", JsonValue::number(out_time_us));
        out.result.set("out_center_x", JsonValue::number(out_center_x));
        out.result.set("out_center_y", JsonValue::number(out_center_y));
        return true;
    }
    if (fn == "nc_tracking_point_get_scaled") {
        double out_time_us = 0.0;
        double out_center_x = 0.0;
        double out_center_y = 0.0;
        double out_scale = 0.0;
        const auto ret = nc_tracking_point_get_scaled(gEditor, static_cast<std::uint64_t>(jint(args, "subtitle_id", 0)), static_cast<int32_t>(jint(args, "index", 0)), &out_time_us, &out_center_x, &out_center_y, &out_scale);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_time_us", JsonValue::number(out_time_us));
        out.result.set("out_center_x", JsonValue::number(out_center_x));
        out.result.set("out_center_y", JsonValue::number(out_center_y));
        out.result.set("out_scale", JsonValue::number(out_scale));
        return true;
    }
    if (fn == "nc_clear_camera_tracking") {
        const auto ret = nc_clear_camera_tracking(gEditor, static_cast<std::uint64_t>(jint(args, "subtitle_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_depth_effect") {
        const auto ret = nc_set_depth_effect(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "mode", 0)), jnum(args, "focus_distance", 0.0), jnum(args, "aperture", 0.0), jnum(args, "intensity", 0.0), static_cast<std::uint32_t>(jint(args, "color_rgba", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_depth_effect") {
        int32_t out_mode = 0;
        double out_focus_distance = 0.0;
        double out_aperture = 0.0;
        double out_intensity = 0.0;
        std::uint32_t out_color_rgba = 0;
        const auto ret = nc_get_depth_effect(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), &out_mode, &out_focus_distance, &out_aperture, &out_intensity, &out_color_rgba);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_mode", JsonValue::integer(out_mode));
        out.result.set("out_focus_distance", JsonValue::number(out_focus_distance));
        out.result.set("out_aperture", JsonValue::number(out_aperture));
        out.result.set("out_intensity", JsonValue::number(out_intensity));
        out.result.set("out_color_rgba", JsonValue::integer(static_cast<std::int64_t>(out_color_rgba)));
        return true;
    }
    if (fn == "nc_add_transition_keyframe") {
        const auto ret = nc_add_transition_keyframe(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "transition_id", 0)), static_cast<int32_t>(jint(args, "property", 0)), static_cast<std::int64_t>(jint(args, "offset_us", 0)), jnum(args, "value", 0.0), static_cast<int32_t>(jint(args, "easing", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_transition_keyframe_count") {
        const auto ret = nc_transition_keyframe_count(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "transition_id", 0)), static_cast<int32_t>(jint(args, "property", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_transition_keyframe_get") {
        double out_offset_us = 0.0;
        double out_value = 0.0;
        int32_t out_easing = 0;
        const auto ret = nc_transition_keyframe_get(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "transition_id", 0)), static_cast<int32_t>(jint(args, "property", 0)), static_cast<int32_t>(jint(args, "index", 0)), &out_offset_us, &out_value, &out_easing);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_offset_us", JsonValue::number(out_offset_us));
        out.result.set("out_value", JsonValue::number(out_value));
        out.result.set("out_easing", JsonValue::integer(out_easing));
        return true;
    }
    if (fn == "nc_remove_transition_keyframe") {
        const auto ret = nc_remove_transition_keyframe(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "transition_id", 0)), static_cast<int32_t>(jint(args, "property", 0)), static_cast<std::int64_t>(jint(args, "offset_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_transition_duration_us") {
        const auto ret = nc_transition_duration_us(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "transition_id", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_keyframe_count") {
        const auto ret = nc_keyframe_count(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "property", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_keyframe_get") {
        int64_t out_time_us = 0;
        double out_value = 0.0;
        int32_t out_easing = 0;
        const auto ret = nc_keyframe_get(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "property", 0)), static_cast<int32_t>(jint(args, "index", 0)), &out_time_us, &out_value, &out_easing);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_time_us", JsonValue::integer(out_time_us));
        out.result.set("out_value", JsonValue::number(out_value));
        out.result.set("out_easing", JsonValue::integer(out_easing));
        return true;
    }
    if (fn == "nc_set_clip_background") {
        const auto ret = nc_set_clip_background(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "mode", 0)), static_cast<std::uint32_t>(jint(args, "rgba", 0)), jnum(args, "intensity", 0.0));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_clip_background") {
        int32_t out_mode = 0;
        std::uint32_t out_rgba = 0;
        double out_intensity = 0.0;
        const auto ret = nc_get_clip_background(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), &out_mode, &out_rgba, &out_intensity);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_mode", JsonValue::integer(out_mode));
        out.result.set("out_rgba", JsonValue::integer(static_cast<std::int64_t>(out_rgba)));
        out.result.set("out_intensity", JsonValue::number(out_intensity));
        return true;
    }
    if (fn == "nc_add_marker") {
        const auto ret = nc_add_marker(gEditor, static_cast<std::int64_t>(jint(args, "time_us", 0)), jstr(args, "name").c_str(), jstr(args, "color").c_str());
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_remove_marker") {
        const auto ret = nc_remove_marker(gEditor, static_cast<std::uint64_t>(jint(args, "marker_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_marker_count") {
        const auto ret = nc_marker_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_marker_get") {
        std::uint64_t out_marker_id = 0;
        int64_t out_time_us = 0;
        char* out_name = nullptr;
        char* out_color = nullptr;
        const auto ret = nc_marker_get(gEditor, static_cast<int32_t>(jint(args, "index", 0)), &out_marker_id, &out_time_us, &out_name, &out_color);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_marker_id", JsonValue::integer(static_cast<std::int64_t>(out_marker_id)));
        out.result.set("out_time_us", JsonValue::integer(out_time_us));
        out.result.set("out_name", JsonValue::string(takeString(out_name)));
        out.result.set("out_color", JsonValue::string(takeString(out_color)));
        return true;
    }
    if (fn == "nc_detach_audio") {
        const auto ret = nc_detach_audio(gEditor, static_cast<int32_t>(jint(args, "video_track_index", 0)), static_cast<std::uint64_t>(jint(args, "video_clip_id", 0)), static_cast<int32_t>(jint(args, "audio_track_index", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ai_feature_access") {
        int32_t out_requires_network = 0;
        int32_t out_requires_pro = 0;
        char* out_reason = nullptr;
        const auto ret = nc_ai_feature_access(gEditor, static_cast<int32_t>(jint(args, "feature", 0)), static_cast<int32_t>(jint(args, "logged_in", 0)), &out_requires_network, &out_requires_pro, &out_reason);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_requires_network", JsonValue::integer(out_requires_network));
        out.result.set("out_requires_pro", JsonValue::integer(out_requires_pro));
        out.result.set("out_reason", JsonValue::string(takeString(out_reason)));
        return true;
    }
    if (fn == "nc_ai_task_status") {
        int32_t out_feature = 0;
        int32_t out_state = 0;
        double out_progress = 0.0;
        char* out_stage_note = nullptr;
        char* out_failure_reason = nullptr;
        int32_t out_retryable = 0;
        char* out_result_payload = nullptr;
        const auto ret = nc_ai_task_status(gEditor, &out_feature, &out_state, &out_progress, &out_stage_note, &out_failure_reason, &out_retryable, &out_result_payload);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_feature", JsonValue::integer(out_feature));
        out.result.set("out_state", JsonValue::integer(out_state));
        out.result.set("out_progress", JsonValue::number(out_progress));
        out.result.set("out_stage_note", JsonValue::string(takeString(out_stage_note)));
        out.result.set("out_failure_reason", JsonValue::string(takeString(out_failure_reason)));
        out.result.set("out_retryable", JsonValue::integer(out_retryable));
        out.result.set("out_result_payload", JsonValue::string(takeString(out_result_payload)));
        return true;
    }
    if (fn == "nc_ai_task_begin") {
        char* out_error = nullptr;
        const auto ret = nc_ai_task_begin(gEditor, static_cast<int32_t>(jint(args, "feature", 0)), static_cast<std::uint64_t>(jint(args, "target_id", 0)), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_ai_task_progress") {
        const auto ret = nc_ai_task_progress(gEditor, jnum(args, "progress", 0.0), jstr(args, "stage_note").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ai_task_succeed") {
        const auto ret = nc_ai_task_succeed(gEditor, jstr(args, "result_payload").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ai_task_fail") {
        const auto ret = nc_ai_task_fail(gEditor, jstr(args, "reason").c_str(), static_cast<int32_t>(jint(args, "retryable", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ai_task_cancel") {
        const auto ret = nc_ai_task_cancel(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ai_task_reset") {
        const auto ret = nc_ai_task_reset(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ai_task_failure_text") {
        out.result.set("value", JsonValue::string(takeString(nc_ai_task_failure_text(gEditor))));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_ai_engine_info") {
        int32_t out_available = 0;
        char* out_version = nullptr;
        char* out_note = nullptr;
        const auto ret = nc_ai_engine_info(gEditor, &out_available, &out_version, &out_note);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_available", JsonValue::integer(out_available));
        out.result.set("out_version", JsonValue::string(takeString(out_version)));
        out.result.set("out_note", JsonValue::string(takeString(out_note)));
        return true;
    }
    if (fn == "nc_depth_load") {
        char* out_error = nullptr;
        const auto ret = nc_depth_load(gEditor, jstr(args, "model_path").c_str(), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_depth_unload") {
        const auto ret = nc_depth_unload(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_depth_loaded") {
        const auto ret = nc_depth_loaded(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_depth_estimate") {
        uint8_t* out_depth = nullptr;
        int32_t out_width = 0;
        int32_t out_height = 0;
        char* out_error = nullptr;
        const auto ret = nc_depth_estimate(gEditor, jvecByte(args, "rgba").data(), static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)), &out_depth, &out_width, &out_height, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_width", JsonValue::integer(out_width));
        out.result.set("out_height", JsonValue::integer(out_height));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_matting_load") {
        char* out_error = nullptr;
        const auto ret = nc_matting_load(gEditor, jstr(args, "model_path").c_str(), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_matting_unload") {
        const auto ret = nc_matting_unload(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_matting_loaded") {
        const auto ret = nc_matting_loaded(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_matting_estimate") {
        uint8_t* out_alpha = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_matting_estimate(gEditor, jvecByte(args, "rgba").data(), static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)), static_cast<int32_t>(jint(args, "quality", 0)), jnum(args, "feather", 0.0), &out_alpha, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_speech_load") {
        char* out_error = nullptr;
        const auto ret = nc_speech_load(gEditor, jstr(args, "encoder_path").c_str(), jstr(args, "decoder_path").c_str(), jstr(args, "tokenizer_path").c_str(), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_speech_unload") {
        const auto ret = nc_speech_unload(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_speech_loaded") {
        const auto ret = nc_speech_loaded(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_speech_transcribe") {
        char* out_segments_json = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_speech_transcribe(gEditor, jvecF32(args, "samples").data(), static_cast<std::int64_t>(jint(args, "frame_count", 0)), static_cast<int32_t>(jint(args, "sample_rate", 0)), static_cast<int32_t>(jint(args, "language", 0)), &out_segments_json, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_segments_json", JsonValue::string(takeString(out_segments_json)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_speech_mel") {
        float* out_mel = nullptr;
        const auto ret = nc_speech_mel(jvecF32(args, "samples").data(), static_cast<std::int64_t>(jint(args, "frame_count", 0)), static_cast<int32_t>(jint(args, "sample_rate", 0)), static_cast<int32_t>(jint(args, "frames", 0)), &out_mel);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_mel", jnumArrayF(out_mel, static_cast<int32_t>(jint(args, "count", 0)), jint(args, "mel_count", 0)));
        return true;
    }
    if (fn == "nc_add_effect") {
        const auto ret = nc_add_effect(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "type", 0)), jnum(args, "value", 0.0));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_remove_effect") {
        const auto ret = nc_remove_effect(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<std::uint64_t>(jint(args, "effect_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_effect_count") {
        const auto ret = nc_effect_count(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_effect_get") {
        int32_t out_type = 0;
        double out_value = 0.0;
        int32_t out_enabled = 0;
        const auto ret = nc_effect_get(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "index", 0)), &out_type, &out_value, &out_enabled);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_type", JsonValue::integer(out_type));
        out.result.set("out_value", JsonValue::number(out_value));
        out.result.set("out_enabled", JsonValue::integer(out_enabled));
        return true;
    }
    if (fn == "nc_insert_keyframe") {
        const auto ret = nc_insert_keyframe(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "property", 0)), static_cast<std::int64_t>(jint(args, "time_us", 0)), jnum(args, "value", 0.0));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_remove_keyframe") {
        const auto ret = nc_remove_keyframe(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "property", 0)), static_cast<std::int64_t>(jint(args, "time_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_clip_audio") {
        const auto ret = nc_set_clip_audio(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), jnum(args, "gain_db", 0.0), static_cast<std::int64_t>(jint(args, "fade_in_us", 0)), static_cast<std::int64_t>(jint(args, "fade_out_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_clip_denoise") {
        const auto ret = nc_set_clip_denoise(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "strength", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_clip_denoise") {
        const auto ret = nc_get_clip_denoise(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_auto_edit") {
        char* out_error = nullptr;
        const auto ret = nc_auto_edit(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), jvecNum(args, "profile_shots").data(), static_cast<int32_t>(jint(args, "shot_count", 0)), jnum(args, "avg_r", 0.0), jnum(args, "avg_g", 0.0), jnum(args, "avg_b", 0.0), jnum(args, "avg_saturation", 0.0), jnum(args, "avg_contrast", 0.0), jvecU64(args, "asset_ids").data(), jvecNum(args, "usable_seconds").data(), static_cast<int32_t>(jint(args, "material_count", 0)), static_cast<int32_t>(jint(args, "apply_color", 0)), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_set_frame_fit") {
        const auto ret = nc_set_frame_fit(gEditor, static_cast<int32_t>(jint(args, "mode", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_frame_fit") {
        const auto ret = nc_get_frame_fit(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_background_color") {
        const auto ret = nc_set_background_color(gEditor, static_cast<std::uint32_t>(jint(args, "rgba", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_track_auto_duck") {
        const auto ret = nc_set_track_auto_duck(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "enabled", 0)), jnum(args, "sensitivity", 0.0), jnum(args, "duck_amount_db", 0.0), static_cast<std::int64_t>(jint(args, "attack_us", 0)), static_cast<std::int64_t>(jint(args, "release_us", 0)), static_cast<std::int64_t>(jint(args, "hold_us", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_track_auto_duck") {
        int32_t out_enabled = 0;
        double out_sensitivity = 0.0;
        double out_duck_amount_db = 0.0;
        const auto ret = nc_get_track_auto_duck(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), &out_enabled, &out_sensitivity, &out_duck_amount_db);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_enabled", JsonValue::integer(out_enabled));
        out.result.set("out_sensitivity", JsonValue::number(out_sensitivity));
        out.result.set("out_duck_amount_db", JsonValue::number(out_duck_amount_db));
        return true;
    }
    if (fn == "nc_create_compound_clip") {
        std::uint64_t out_sequence_id = 0;
        std::uint64_t out_clip_id = 0;
        const auto ret = nc_create_compound_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), jvecU64(args, "clip_ids").data(), static_cast<int32_t>(jint(args, "count", 0)), jstr(args, "name").c_str(), &out_sequence_id, &out_clip_id);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_sequence_id", JsonValue::integer(static_cast<std::int64_t>(out_sequence_id)));
        out.result.set("out_clip_id", JsonValue::integer(static_cast<std::int64_t>(out_clip_id)));
        return true;
    }
    if (fn == "nc_unwrap_compound_clip") {
        const auto ret = nc_unwrap_compound_clip(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_clip_is_compound") {
        const auto ret = nc_clip_is_compound(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_track_solo") {
        const auto ret = nc_set_track_solo(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<int32_t>(jint(args, "solo", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_track_gain") {
        const auto ret = nc_set_track_gain(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), jnum(args, "gain_db", 0.0));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_track_mixer") {
        int32_t out_muted = 0;
        int32_t out_solo = 0;
        double out_gain_db = 0.0;
        const auto ret = nc_get_track_mixer(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), &out_muted, &out_solo, &out_gain_db);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_muted", JsonValue::integer(out_muted));
        out.result.set("out_solo", JsonValue::integer(out_solo));
        out.result.set("out_gain_db", JsonValue::number(out_gain_db));
        return true;
    }
    if (fn == "nc_set_master_gain") {
        const auto ret = nc_set_master_gain(gEditor, jnum(args, "gain_db", 0.0));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_master_gain") {
        double out_gain_db = 0.0;
        const auto ret = nc_get_master_gain(gEditor, &out_gain_db);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_gain_db", JsonValue::number(out_gain_db));
        return true;
    }
    if (fn == "nc_set_entitlement") {
        const auto ret = nc_set_entitlement(gEditor, static_cast<int32_t>(jint(args, "tier", 0)), static_cast<int32_t>(jint(args, "lifetime", 0)), static_cast<std::int64_t>(jint(args, "expires_at_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_entitlement") {
        const auto ret = nc_get_entitlement(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_can_add_track") {
        const auto ret = nc_can_add_track(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_would_clamp_export") {
        const auto ret = nc_would_clamp_export(gEditor, static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_ai_check") {
        char* out_report_json = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_ai_check(gEditor, &out_report_json, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_report_json", JsonValue::string(takeString(out_report_json)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_ai_check_fix_all") {
        char* out_error = nullptr;
        const auto ret = nc_ai_check_fix_all(gEditor, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_proxy_check_import") {
        int32_t out_proxy_width = 0;
        int32_t out_proxy_height = 0;
        double out_estimated_seconds = 0.0;
        int64_t out_estimated_bytes = 0;
        char* out_reason = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_proxy_check_import(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), &out_proxy_width, &out_proxy_height, &out_estimated_seconds, &out_estimated_bytes, &out_reason, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_proxy_width", JsonValue::integer(out_proxy_width));
        out.result.set("out_proxy_height", JsonValue::integer(out_proxy_height));
        out.result.set("out_estimated_seconds", JsonValue::number(out_estimated_seconds));
        out.result.set("out_estimated_bytes", JsonValue::integer(out_estimated_bytes));
        out.result.set("out_reason", JsonValue::string(takeString(out_reason)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_proxy_set_record") {
        const auto ret = nc_proxy_set_record(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), jstr(args, "path").c_str(), static_cast<int32_t>(jint(args, "status", 0)), static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)), static_cast<std::int64_t>(jint(args, "size_bytes", 0)), jstr(args, "note").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_proxy_get_record") {
        int32_t out_status = 0;
        int32_t out_width = 0;
        int32_t out_height = 0;
        int64_t out_size_bytes = 0;
        char* out_path = nullptr;
        char* out_note = nullptr;
        const auto ret = nc_proxy_get_record(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), &out_status, &out_width, &out_height, &out_size_bytes, &out_path, &out_note);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_status", JsonValue::integer(out_status));
        out.result.set("out_width", JsonValue::integer(out_width));
        out.result.set("out_height", JsonValue::integer(out_height));
        out.result.set("out_size_bytes", JsonValue::integer(out_size_bytes));
        out.result.set("out_path", JsonValue::string(takeString(out_path)));
        out.result.set("out_note", JsonValue::string(takeString(out_note)));
        return true;
    }
    if (fn == "nc_proxy_usage") {
        int32_t out_ready_count = 0;
        int32_t out_generating_count = 0;
        int64_t out_total_bytes = 0;
        const auto ret = nc_proxy_usage(gEditor, &out_ready_count, &out_generating_count, &out_total_bytes);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_ready_count", JsonValue::integer(out_ready_count));
        out.result.set("out_generating_count", JsonValue::integer(out_generating_count));
        out.result.set("out_total_bytes", JsonValue::integer(out_total_bytes));
        return true;
    }
    if (fn == "nc_proxy_clear_records") {
        const auto ret = nc_proxy_clear_records(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_export_presets") {
        int32_t out_ids = 0;
        int32_t out_count = 0;
        const auto ret = nc_export_presets(&out_ids, static_cast<int32_t>(jint(args, "capacity", 0)), &out_count);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_ids", JsonValue::integer(out_ids));
        out.result.set("out_count", JsonValue::integer(out_count));
        return true;
    }
    if (fn == "nc_export_preset_info") {
        int32_t out_width = 0;
        int32_t out_height = 0;
        int32_t out_fps = 0;
        int32_t out_requires_pro = 0;
        char* out_label = nullptr;
        const auto ret = nc_export_preset_info(static_cast<int32_t>(jint(args, "preset_id", 0)), &out_width, &out_height, &out_fps, &out_requires_pro, &out_label);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_width", JsonValue::integer(out_width));
        out.result.set("out_height", JsonValue::integer(out_height));
        out.result.set("out_fps", JsonValue::integer(out_fps));
        out.result.set("out_requires_pro", JsonValue::integer(out_requires_pro));
        out.result.set("out_label", JsonValue::string(takeString(out_label)));
        return true;
    }
    if (fn == "nc_export_estimate") {
        int64_t out_file_size_bytes = 0;
        double out_render_seconds = 0.0;
        char* out_size_text = nullptr;
        char* out_time_text = nullptr;
        const auto ret = nc_export_estimate(static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)), static_cast<int32_t>(jint(args, "fps", 0)), static_cast<std::int64_t>(jint(args, "duration_us", 0)), jnum(args, "video_bitrate_mbps", 0.0), static_cast<int32_t>(jint(args, "include_audio", 0)), &out_file_size_bytes, &out_render_seconds, &out_size_text, &out_time_text);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_file_size_bytes", JsonValue::integer(out_file_size_bytes));
        out.result.set("out_render_seconds", JsonValue::number(out_render_seconds));
        out.result.set("out_size_text", JsonValue::string(takeString(out_size_text)));
        out.result.set("out_time_text", JsonValue::string(takeString(out_time_text)));
        return true;
    }
    if (fn == "nc_export_setup_preset") {
        int32_t out_width = 0;
        int32_t out_height = 0;
        int32_t out_fps = 0;
        int32_t out_downgraded = 0;
        char* out_preset_name = nullptr;
        char* out_warning = nullptr;
        const auto ret = nc_export_setup_preset(gEditor, static_cast<int32_t>(jint(args, "preset_id", 0)), static_cast<int32_t>(jint(args, "mode", 0)), static_cast<int32_t>(jint(args, "cpu_cores", 0)), static_cast<int32_t>(jint(args, "crf", 0)), &out_width, &out_height, &out_fps, &out_downgraded, &out_preset_name, &out_warning);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_width", JsonValue::integer(out_width));
        out.result.set("out_height", JsonValue::integer(out_height));
        out.result.set("out_fps", JsonValue::integer(out_fps));
        out.result.set("out_downgraded", JsonValue::integer(out_downgraded));
        out.result.set("out_preset_name", JsonValue::string(takeString(out_preset_name)));
        out.result.set("out_warning", JsonValue::string(takeString(out_warning)));
        return true;
    }
    if (fn == "nc_speech_analyze") {
        int64_t out_speech_us = 0;
        int64_t out_silence_us = 0;
        double out_keep_ratio = 0.0;
        int32_t out_segment_count = 0;
        char* out_note = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_speech_analyze(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), &out_speech_us, &out_silence_us, &out_keep_ratio, &out_segment_count, &out_note, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_speech_us", JsonValue::integer(out_speech_us));
        out.result.set("out_silence_us", JsonValue::integer(out_silence_us));
        out.result.set("out_keep_ratio", JsonValue::number(out_keep_ratio));
        out.result.set("out_segment_count", JsonValue::integer(out_segment_count));
        out.result.set("out_note", JsonValue::string(takeString(out_note)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_speech_cut_apply") {
        char* out_error = nullptr;
        const auto ret = nc_speech_cut_apply(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_set_clip_mask") {
        const auto ret = nc_set_clip_mask(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "shape", 0)), jnum(args, "center_x", 0.0), jnum(args, "center_y", 0.0), jnum(args, "rotation_degrees", 0.0), jnum(args, "width", 0.0), jnum(args, "height", 0.0), jnum(args, "feather", 0.0), jnum(args, "corner_radius", 0.0), static_cast<int32_t>(jint(args, "invert", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_clip_mask") {
        int32_t out_shape = 0;
        double out_center_x = 0.0;
        double out_center_y = 0.0;
        double out_rotation_degrees = 0.0;
        double out_width = 0.0;
        double out_height = 0.0;
        double out_feather = 0.0;
        double out_corner_radius = 0.0;
        int32_t out_invert = 0;
        const auto ret = nc_get_clip_mask(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), &out_shape, &out_center_x, &out_center_y, &out_rotation_degrees, &out_width, &out_height, &out_feather, &out_corner_radius, &out_invert);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_shape", JsonValue::integer(out_shape));
        out.result.set("out_center_x", JsonValue::number(out_center_x));
        out.result.set("out_center_y", JsonValue::number(out_center_y));
        out.result.set("out_rotation_degrees", JsonValue::number(out_rotation_degrees));
        out.result.set("out_width", JsonValue::number(out_width));
        out.result.set("out_height", JsonValue::number(out_height));
        out.result.set("out_feather", JsonValue::number(out_feather));
        out.result.set("out_corner_radius", JsonValue::number(out_corner_radius));
        out.result.set("out_invert", JsonValue::integer(out_invert));
        return true;
    }
    if (fn == "nc_matting_access") {
        int32_t out_quality = 0;
        char* out_reason = nullptr;
        const auto ret = nc_matting_access(gEditor, static_cast<int32_t>(jint(args, "quality", 0)), &out_quality, &out_reason);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_quality", JsonValue::integer(out_quality));
        out.result.set("out_reason", JsonValue::string(takeString(out_reason)));
        return true;
    }
    if (fn == "nc_set_clip_chroma_key") {
        const auto ret = nc_set_clip_chroma_key(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), jstr(args, "key_color").c_str(), jnum(args, "inner_threshold", 0.0), jnum(args, "outer_threshold", 0.0), jnum(args, "spill_suppression", 0.0), static_cast<int32_t>(jint(args, "quality", 0)), static_cast<int32_t>(jint(args, "enabled", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_clip_chroma_key") {
        std::uint32_t out_key_color_rgb = 0;
        double out_inner = 0.0;
        double out_outer = 0.0;
        double out_spill = 0.0;
        const auto ret = nc_get_clip_chroma_key(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), &out_key_color_rgb, &out_inner, &out_outer, &out_spill);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_key_color_rgb", JsonValue::integer(static_cast<std::int64_t>(out_key_color_rgb)));
        out.result.set("out_inner", JsonValue::number(out_inner));
        out.result.set("out_outer", JsonValue::number(out_outer));
        out.result.set("out_spill", JsonValue::number(out_spill));
        return true;
    }
    if (fn == "nc_chroma_key_estimate") {
        const auto ret = nc_chroma_key_estimate(jvecByte(args, "rgba").data(), static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)), jstr(args, "key_color").c_str(), jnum(args, "inner_threshold", 0.0), jnum(args, "outer_threshold", 0.0), jnum(args, "spill_suppression", 0.0), static_cast<int32_t>(jint(args, "quality", 0)));
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_set_clip_blend_mode") {
        const auto ret = nc_set_clip_blend_mode(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)), static_cast<int32_t>(jint(args, "mode", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_get_clip_blend_mode") {
        const auto ret = nc_get_clip_blend_mode(gEditor, static_cast<int32_t>(jint(args, "track_index", 0)), static_cast<std::uint64_t>(jint(args, "clip_id", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_catalog") {
        char** out_ids = nullptr;
        int32_t out_count = 0;
        const auto ret = nc_model_catalog(&out_ids, static_cast<int32_t>(jint(args, "capacity", 0)), &out_count);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_ids", jstrArrayN(out_ids, static_cast<int32_t>(jint(args, "capacity", 64))));
        out.result.set("out_count", JsonValue::integer(out_count));
        return true;
    }
    if (fn == "nc_model_info") {
        char* out_name = nullptr;
        char* out_description = nullptr;
        int32_t out_size_bytes = 0;
        char* out_version = nullptr;
        int32_t out_requires_pro = 0;
        int32_t out_capability = 0;
        const auto ret = nc_model_info(jstr(args, "model_id").c_str(), &out_name, &out_description, &out_size_bytes, &out_version, &out_requires_pro, &out_capability);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_name", JsonValue::string(takeString(out_name)));
        out.result.set("out_description", JsonValue::string(takeString(out_description)));
        out.result.set("out_size_bytes", JsonValue::integer(out_size_bytes));
        out.result.set("out_version", JsonValue::string(takeString(out_version)));
        out.result.set("out_requires_pro", JsonValue::integer(out_requires_pro));
        out.result.set("out_capability", JsonValue::integer(out_capability));
        return true;
    }
    if (fn == "nc_model_status") {
        const auto ret = nc_model_status(gEditor, jstr(args, "model_id").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_access_allowed") {
        const auto ret = nc_model_access_allowed(gEditor, jstr(args, "model_id").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_begin") {
        const auto ret = nc_model_download_begin(gEditor, jstr(args, "model_id").c_str(), static_cast<std::int64_t>(jint(args, "total_bytes", 0)), static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_progress") {
        const auto ret = nc_model_download_progress(gEditor, jstr(args, "model_id").c_str(), static_cast<std::int64_t>(jint(args, "downloaded_bytes", 0)), static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_pause") {
        const auto ret = nc_model_download_pause(gEditor, jstr(args, "model_id").c_str(), static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_resume") {
        const auto ret = nc_model_download_resume(gEditor, jstr(args, "model_id").c_str(), static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_cancel") {
        const auto ret = nc_model_download_cancel(gEditor, jstr(args, "model_id").c_str(), static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_finish") {
        const auto ret = nc_model_download_finish(gEditor, jstr(args, "model_id").c_str(), static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_fail") {
        const auto ret = nc_model_download_fail(gEditor, jstr(args, "model_id").c_str(), jstr(args, "reason").c_str(), static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_download_state") {
        int32_t out_phase = 0;
        int64_t out_total_bytes = 0;
        int64_t out_downloaded_bytes = 0;
        char* out_message = nullptr;
        const auto ret = nc_model_download_state(gEditor, jstr(args, "model_id").c_str(), &out_phase, &out_total_bytes, &out_downloaded_bytes, &out_message);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_phase", JsonValue::integer(out_phase));
        out.result.set("out_total_bytes", JsonValue::integer(out_total_bytes));
        out.result.set("out_downloaded_bytes", JsonValue::integer(out_downloaded_bytes));
        out.result.set("out_message", JsonValue::string(takeString(out_message)));
        return true;
    }
    if (fn == "nc_model_install") {
        int32_t out_installed = 0;
        char* out_message = nullptr;
        const auto ret = nc_model_install(gEditor, jstr(args, "model_id").c_str(), jvecByte(args, "ciphertext").data(), static_cast<std::int64_t>(jint(args, "size", 0)), jstr(args, "path").c_str(), jstr(args, "expected_sha256_hex").c_str(), static_cast<std::int64_t>(jint(args, "now_unix", 0)), &out_installed, &out_message);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_installed", JsonValue::integer(out_installed));
        out.result.set("out_message", JsonValue::string(takeString(out_message)));
        return true;
    }
    if (fn == "nc_model_remove") {
        const auto ret = nc_model_remove(gEditor, jstr(args, "model_id").c_str());
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_model_storage_stats") {
        int64_t out_model_bytes = 0;
        int32_t out_installed_count = 0;
        int32_t out_corrupt_count = 0;
        char* out_usage_text = nullptr;
        char* out_model_text = nullptr;
        const auto ret = nc_model_storage_stats(gEditor, static_cast<std::int64_t>(jint(args, "device_total_bytes", 0)), static_cast<std::int64_t>(jint(args, "device_free_bytes", 0)), &out_model_bytes, &out_installed_count, &out_corrupt_count, &out_usage_text, &out_model_text);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_model_bytes", JsonValue::integer(out_model_bytes));
        out.result.set("out_installed_count", JsonValue::integer(out_installed_count));
        out.result.set("out_corrupt_count", JsonValue::integer(out_corrupt_count));
        out.result.set("out_usage_text", JsonValue::string(takeString(out_usage_text)));
        out.result.set("out_model_text", JsonValue::string(takeString(out_model_text)));
        return true;
    }
    if (fn == "nc_model_decrypt") {
        uint8_t* out_plaintext = nullptr;
        int64_t out_size = 0;
        const auto ret = nc_model_decrypt(jstr(args, "model_id").c_str(), jstr(args, "version").c_str(), jvecByte(args, "key").data(), jvecByte(args, "ciphertext").data(), static_cast<std::int64_t>(jint(args, "size", 0)), static_cast<std::int64_t>(jint(args, "offset", 0)), &out_plaintext, &out_size);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_size", JsonValue::integer(out_size));
        return true;
    }
    if (fn == "nc_chapter_detect") {
        char* out_summary = nullptr;
        char* out_note = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_chapter_detect(gEditor, jnum(args, "min_chapter_seconds", 0.0), jnum(args, "max_chapter_seconds", 0.0), jnum(args, "silence_boundary_seconds", 0.0), static_cast<int32_t>(jint(args, "use_scene_detect", 0)), static_cast<int32_t>(jint(args, "use_silence_detect", 0)), static_cast<int32_t>(jint(args, "max_chapters", 0)), &out_summary, &out_note, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_summary", JsonValue::string(takeString(out_summary)));
        out.result.set("out_note", JsonValue::string(takeString(out_note)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_chapter_apply") {
        char* out_error = nullptr;
        const auto ret = nc_chapter_apply(gEditor, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_chapter_count") {
        const auto ret = nc_chapter_count(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_chapter_get") {
        int64_t out_start_us = 0;
        int64_t out_end_us = 0;
        char* out_title = nullptr;
        const auto ret = nc_chapter_get(gEditor, static_cast<int32_t>(jint(args, "index", 0)), &out_start_us, &out_end_us, &out_title);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_start_us", JsonValue::integer(out_start_us));
        out.result.set("out_end_us", JsonValue::integer(out_end_us));
        out.result.set("out_title", JsonValue::string(takeString(out_title)));
        return true;
    }
    if (fn == "nc_chapter_rename") {
        char* out_error = nullptr;
        const auto ret = nc_chapter_rename(gEditor, static_cast<int32_t>(jint(args, "index", 0)), jstr(args, "new_title").c_str(), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_reference_style_access") {
        char* out_reason = nullptr;
        const auto ret = nc_reference_style_access(gEditor, &out_reason);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_reason", JsonValue::string(takeString(out_reason)));
        return true;
    }
    if (fn == "nc_movie_version_access") {
        int32_t out_allowed_count = 0;
        char* out_reason = nullptr;
        const auto ret = nc_movie_version_access(gEditor, static_cast<int32_t>(jint(args, "requested", 0)), &out_allowed_count, &out_reason);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_allowed_count", JsonValue::integer(out_allowed_count));
        out.result.set("out_reason", JsonValue::string(takeString(out_reason)));
        return true;
    }
    if (fn == "nc_smart_movie_plan") {
        char* out_summary = nullptr;
        const auto ret = nc_smart_movie_plan(gEditor, static_cast<int32_t>(jint(args, "version_count", 0)), jnum(args, "pace_factor", 0.0), jnum(args, "target_seconds", 0.0), jstr(args, "style_json").c_str(), jvecU64(args, "material_ids").data(), jvecNum(args, "material_seconds").data(), static_cast<int32_t>(jint(args, "material_count", 0)), &out_summary);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_summary", JsonValue::string(takeString(out_summary)));
        return true;
    }
    if (fn == "nc_smart_movie_apply") {
        int32_t out_applied = 0;
        char* out_failed_json = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_smart_movie_apply(gEditor, jvecI32(args, "track_indexes").data(), static_cast<int32_t>(jint(args, "track_count", 0)), &out_applied, &out_failed_json, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_applied", JsonValue::integer(out_applied));
        out.result.set("out_failed_json", JsonValue::string(takeString(out_failed_json)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_regeneration_impact") {
        char* out_report_json = nullptr;
        const auto ret = nc_regeneration_impact(gEditor, static_cast<int32_t>(jint(args, "scope", 0)), &out_report_json);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_report_json", JsonValue::string(takeString(out_report_json)));
        return true;
    }
    if (fn == "nc_regeneration_clear") {
        char* out_cleared_json = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_regeneration_clear(gEditor, static_cast<int32_t>(jint(args, "scope", 0)), &out_cleared_json, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_cleared_json", JsonValue::string(takeString(out_cleared_json)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_chapter_clear") {
        char* out_error = nullptr;
        const auto ret = nc_chapter_clear(gEditor, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_export_enqueue") {
        std::uint64_t out_job_id = 0;
        char* out_error = nullptr;
        const auto ret = nc_export_enqueue(gEditor, jstr(args, "name").c_str(), jstr(args, "out_path").c_str(), static_cast<int32_t>(jint(args, "preset_id", 0)), static_cast<int32_t>(jint(args, "mode", 0)), static_cast<int32_t>(jint(args, "include_audio", 0)), static_cast<int32_t>(jint(args, "attach_ai_label", 0)), jstr(args, "ai_label_text").c_str(), &out_job_id, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_job_id", JsonValue::integer(static_cast<std::int64_t>(out_job_id)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_export_poll_next") {
        std::uint64_t out_job_id = 0;
        char* out_out_path = nullptr;
        int32_t out_preset = 0;
        int32_t out_mode = 0;
        int32_t out_include_audio = 0;
        int32_t out_attach_ai_label = 0;
        char* out_ai_label_text = nullptr;
        const auto ret = nc_export_poll_next(gEditor, &out_job_id, &out_out_path, &out_preset, &out_mode, &out_include_audio, &out_attach_ai_label, &out_ai_label_text);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_job_id", JsonValue::integer(static_cast<std::int64_t>(out_job_id)));
        out.result.set("out_out_path", JsonValue::string(takeString(out_out_path)));
        out.result.set("out_preset", JsonValue::integer(out_preset));
        out.result.set("out_mode", JsonValue::integer(out_mode));
        out.result.set("out_include_audio", JsonValue::integer(out_include_audio));
        out.result.set("out_attach_ai_label", JsonValue::integer(out_attach_ai_label));
        out.result.set("out_ai_label_text", JsonValue::string(takeString(out_ai_label_text)));
        return true;
    }
    if (fn == "nc_export_job_state") {
        int32_t out_state = 0;
        double out_progress = 0.0;
        int64_t out_frames_written = 0;
        char* out_note = nullptr;
        const auto ret = nc_export_job_state(gEditor, static_cast<std::uint64_t>(jint(args, "job_id", 0)), &out_state, &out_progress, &out_frames_written, &out_note);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_state", JsonValue::integer(out_state));
        out.result.set("out_progress", JsonValue::number(out_progress));
        out.result.set("out_frames_written", JsonValue::integer(out_frames_written));
        out.result.set("out_note", JsonValue::string(takeString(out_note)));
        return true;
    }
    if (fn == "nc_export_job_progress") {
        const auto ret = nc_export_job_progress(gEditor, static_cast<std::uint64_t>(jint(args, "job_id", 0)), jnum(args, "progress", 0.0));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_export_job_update") {
        char* out_error = nullptr;
        const auto ret = nc_export_job_update(gEditor, static_cast<std::uint64_t>(jint(args, "job_id", 0)), static_cast<int32_t>(jint(args, "action", 0)), static_cast<std::int64_t>(jint(args, "frames_written", 0)), jstr(args, "reason").c_str(), static_cast<std::int64_t>(jint(args, "now_unix", 0)), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_export_queue_summary") {
        int32_t out_queued = 0;
        int32_t out_running = 0;
        int32_t out_active = 0;
        const auto ret = nc_export_queue_summary(gEditor, &out_queued, &out_running, &out_active);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_queued", JsonValue::integer(out_queued));
        out.result.set("out_running", JsonValue::integer(out_running));
        out.result.set("out_active", JsonValue::integer(out_active));
        return true;
    }
    if (fn == "nc_export_queue_cancel_all") {
        nc_export_queue_cancel_all(gEditor, static_cast<std::int64_t>(jint(args, "now_unix", 0)));
        out.code = NC_OK;
        return true;
    }
    if (fn == "nc_media_scan_offline") {
        char* out_report_json = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_media_scan_offline(gEditor, static_cast<int32_t>(jint(args, "probe_kind", 0)), &out_report_json, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_report_json", JsonValue::string(takeString(out_report_json)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_media_apply_offline_flags") {
        const auto ret = nc_media_apply_offline_flags(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_media_relink") {
        char* out_warning = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_media_relink(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), jstr(args, "new_path").c_str(), static_cast<int32_t>(jint(args, "verify_digest", 0)), &out_warning, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_warning", JsonValue::string(takeString(out_warning)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_media_find_relink_candidates") {
        char* out_candidates_json = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_media_find_relink_candidates(gEditor, jstr(args, "search_dirs_json").c_str(), &out_candidates_json, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_candidates_json", JsonValue::string(takeString(out_candidates_json)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_media_relink_batch") {
        int32_t out_relinked = 0;
        char* out_report_json = nullptr;
        char* out_error = nullptr;
        const auto ret = nc_media_relink_batch(gEditor, jstr(args, "mappings_json").c_str(), &out_relinked, &out_report_json, &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_relinked", JsonValue::integer(out_relinked));
        out.result.set("out_report_json", JsonValue::string(takeString(out_report_json)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_media_set_digest") {
        char* out_error = nullptr;
        const auto ret = nc_media_set_digest(gEditor, static_cast<std::uint64_t>(jint(args, "asset_id", 0)), jstr(args, "sha256_hex").c_str(), &out_error);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_error", JsonValue::string(takeString(out_error)));
        return true;
    }
    if (fn == "nc_watermark_account_hash") {
        const auto ret = nc_watermark_account_hash(jstr(args, "account_id").c_str());
        out.code = NC_OK;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_watermark_set_export") {
        const auto ret = nc_watermark_set_export(gEditor, static_cast<int32_t>(jint(args, "enabled", 0)), jstr(args, "account_id").c_str(), static_cast<int32_t>(jint(args, "export_sequence", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_watermark_detect") {
        std::uint32_t out_account_hash = 0;
        int32_t out_export_sequence = 0;
        double out_agreement = 0.0;
        char* out_note = nullptr;
        const auto ret = nc_watermark_detect(jvecByte(args, "rgba").data(), static_cast<int32_t>(jint(args, "width", 0)), static_cast<int32_t>(jint(args, "height", 0)), &out_account_hash, &out_export_sequence, &out_agreement, &out_note);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_account_hash", JsonValue::integer(static_cast<std::int64_t>(out_account_hash)));
        out.result.set("out_export_sequence", JsonValue::integer(out_export_sequence));
        out.result.set("out_agreement", JsonValue::number(out_agreement));
        out.result.set("out_note", JsonValue::string(takeString(out_note)));
        return true;
    }
    if (fn == "nc_heartbeat_handle") {
        int32_t out_verdict = 0;
        char* out_message = nullptr;
        const auto ret = nc_heartbeat_handle(gEditor, static_cast<std::int64_t>(jint(args, "server_time_unix", 0)), static_cast<int32_t>(jint(args, "tier", 0)), static_cast<int32_t>(jint(args, "lifetime", 0)), static_cast<std::int64_t>(jint(args, "expires_at_unix", 0)), static_cast<int32_t>(jint(args, "active_devices", 0)), static_cast<int32_t>(jint(args, "device_limit", 0)), static_cast<int32_t>(jint(args, "signature_valid", 0)), static_cast<int32_t>(jint(args, "server_locked", 0)), jstr(args, "message").c_str(), &out_verdict, &out_message);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_verdict", JsonValue::integer(out_verdict));
        out.result.set("out_message", JsonValue::string(takeString(out_message)));
        return true;
    }
    if (fn == "nc_heartbeat_network_error") {
        char* out_message = nullptr;
        const auto ret = nc_heartbeat_network_error(gEditor, &out_message);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_message", JsonValue::string(takeString(out_message)));
        return true;
    }
    if (fn == "nc_heartbeat_status") {
        int32_t out_locked = 0;
        int32_t out_warning_count = 0;
        int64_t out_last_checked_unix = 0;
        const auto ret = nc_heartbeat_status(gEditor, &out_locked, &out_warning_count, &out_last_checked_unix);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        out.result.set("out_locked", JsonValue::integer(out_locked));
        out.result.set("out_warning_count", JsonValue::integer(out_warning_count));
        out.result.set("out_last_checked_unix", JsonValue::integer(out_last_checked_unix));
        return true;
    }
    if (fn == "nc_heartbeat_periodic_due") {
        const auto ret = nc_heartbeat_periodic_due(gEditor, static_cast<std::int64_t>(jint(args, "now_unix", 0)), static_cast<std::int64_t>(jint(args, "interval_seconds", 0)));
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    if (fn == "nc_heartbeat_clear_lock") {
        const auto ret = nc_heartbeat_clear_lock(gEditor);
        out.code = ret;
        out.result.set("value", JsonValue::integer(static_cast<std::int64_t>(ret)));
        return true;
    }
    return false;
}
