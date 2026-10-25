// ===========================================================================
// 手写补充区：缓冲 / 函数指针 / 二维出参等无法自动派发的形态。
// 与 api_dispatch.inc（自动生成）分开维护，生成器不会覆盖本文件。
// ===========================================================================

/// 二进制缓冲 → base64（前端可直接拼 data URL）
static std::string base64Of(const std::uint8_t* data, std::size_t n) {
    static const char* kTable =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    std::size_t i = 0;
    for (; i + 2 < n; i += 3) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out.push_back(kTable[(v >> 6) & 63]);
        out.push_back(kTable[v & 63]);
    }
    if (i + 1 == n) {
        const std::uint32_t v = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out += "==";
    } else if (i + 2 == n) {
        const std::uint32_t v = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kTable[(v >> 18) & 63]);
        out.push_back(kTable[(v >> 12) & 63]);
        out.push_back(kTable[(v >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

/// 缓冲 / 回调 / 二维出参等无法自动派发的接口。
static bool dispatchApiSpecial(const std::string& fn, const JsonValue& args,
                               ApiCallResult& out) {
    out.result = JsonValue::object();

    if (fn == "nc_preview_render") {
        int32_t w = 0, h = 0;
        std::uint8_t* rgba = nullptr;
        std::uint64_t clipId = 0;
        double opacity = 0, scale = 0, posX = 0, posY = 0, rot = 0, transProgress = 0;
        int32_t transType = 0;
        out.code = nc_preview_render(gEditor, jint(args, "time_us"), &w, &h, &rgba,
                                     &clipId, &opacity, &scale, &posX, &posY, &rot,
                                     &transType, &transProgress);
        out.result.set("width", JsonValue::integer(w));
        out.result.set("height", JsonValue::integer(h));
        out.result.set("clip_id", JsonValue::integer(static_cast<std::int64_t>(clipId)));
        out.result.set("opacity", JsonValue::number(opacity));
        out.result.set("scale", JsonValue::number(scale));
        out.result.set("pos_x", JsonValue::number(posX));
        out.result.set("pos_y", JsonValue::number(posY));
        out.result.set("rotation", JsonValue::number(rot));
        out.result.set("transition_type", JsonValue::integer(transType));
        out.result.set("transition_progress", JsonValue::number(transProgress));
        if (rgba != nullptr && w > 0 && h > 0) {
            const std::string png = encodePng(rgba, w, h);
            out.result.set("png_base64", JsonValue::string(base64Of(
                reinterpret_cast<const std::uint8_t*>(png.data()), png.size())));
            nc_free_buffer(rgba);
        }
        return true;
    }

    if (fn == "nc_thumbnail_generate") {
        std::uint8_t* images = nullptr;   // out_images：count 个 RGBA 块，每块需 nc_free_buffer
        int64_t* times = nullptr;         // out_times_us
        int32_t count = 0;                // out_count
        out.code = nc_thumbnail_generate(gEditor,
            static_cast<std::uint64_t>(jint(args, "asset_id")),
            static_cast<int32_t>(jint(args, "count", 1)),
            static_cast<int32_t>(jint(args, "max_width", 160)),
            static_cast<int32_t>(jint(args, "max_height", 90)),
            &images, times, &count);
        out.result.set("count", JsonValue::integer(count));
        JsonValue arr = JsonValue::array();
        for (int32_t i = 0; i < count; ++i) {
            arr.push(JsonValue::integer(times != nullptr ? times[i] : 0));
        }
        out.result.set("times_us", arr);
        return true;
    }


    if (fn == "nc_chapter_detect") {
        /* 实测：在沙箱里对整条序列做无约束解码会拖垮进程（不是返回错误码，是进程退出）。
           按「诚实 AI」原则：不假装成功、也不让 UI 卡死 —— 明确返回不可用 + 原因。
           平台层若已有可用解码环境，可改为直调 nc_chapter_detect。 */
        out.code = NC_ERROR_UNSUPPORTED;
        out.result.set("note", JsonValue::string(
            "章节自动生成需要整条序列采样解码，当前沙箱环境执行会退出进程；"
            "请在具备解码环境的平台层调用 nc_chapter_detect"));
        out.result.set("summary", JsonValue::string(""));
        return true;
    }

    if (fn == "nc_scene_detect") {
        int32_t n = 0;
        double* bounds = nullptr;
        out.code = nc_scene_detect(gEditor, jnum(args, "step_seconds", 0.5),
                                   static_cast<int32_t>(jint(args, "max_samples", 64)),
                                   &n, &bounds);
        out.result.set("count", JsonValue::integer(n));
        JsonValue arr = JsonValue::array();
        for (int32_t i = 0; i < n && bounds != nullptr; ++i) {
            arr.push(JsonValue::number(bounds[i]));
        }
        out.result.set("boundaries", arr);
        return true;
    }

    if (fn == "nc_beat_detect") {
        double bpm = 0, conf = 0;
        int32_t n = 0;
        double* beats = nullptr;
        out.code = nc_beat_detect(gEditor, static_cast<int32_t>(jint(args, "track_index")),
                                  &bpm, &conf, &n, &beats);
        out.result.set("bpm", JsonValue::number(bpm));
        out.result.set("confidence", JsonValue::number(conf));
        out.result.set("count", JsonValue::integer(n));
        JsonValue arr = JsonValue::array();
        for (int32_t i = 0; i < n && beats != nullptr; ++i) {
            arr.push(JsonValue::number(beats[i]));
        }
        out.result.set("beats", arr);
        return true;
    }

    if (fn == "nc_model_catalog") {
        char** ids = nullptr;
        int32_t count = 0;
        out.code = nc_model_catalog(&ids, 64, &count);
        out.result.set("count", JsonValue::integer(count));
        out.result.set("ids", jstrArrayN(ids, count));
        return true;
    }

    if (fn == "nc_depth_apply_effect" || fn == "nc_person_effect_apply") {
        out.code = NC_ERROR_UNSUPPORTED;
        out.result.set("note", JsonValue::string(
            "需要 RGBA/alpha 像素缓冲入参：请走 /api/preset_frame 或图像上传端点"));
        return true;
    }

    if (fn == "nc_export_run") {
        /* 同步导出会长时间占用进程，且沙箱内 FFmpeg 编码路径不稳定（实测进程退出）。
           导出请走 /api/export（异步 + 进度 + 取消），它内部用同样的 nc_export_run
           但跑在独立线程并有完整的失败回传。 */
        out.code = NC_ERROR_UNSUPPORTED;
        out.result.set("note", JsonValue::string(
            "同步导出请改用 POST /api/export（异步、带进度与取消），"
            "它内部调用 nc_export_run 并在失败时回传错误码"));
        return true;
    }

    if (fn == "nc_model_resume_plan") {
        out.code = NC_ERROR_UNSUPPORTED;
        out.result.set("note", JsonValue::string("需要断点续传状态入参，沙箱内无下载任务"));
        return true;
    }

    if (fn == "nc_free_buffer") {
        out.result.set("note", JsonValue::string("释放接口由桥内部管理，前端无需调用"));
        return true;
    }

    return false;
}
