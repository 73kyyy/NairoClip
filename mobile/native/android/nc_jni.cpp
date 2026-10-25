// NairoClip · JNI 直调 C ABI
// ---------------------------------------------------------------------------
// 依据 docs/UI_INTEGRATION.md §7：Android 平台层用 Kotlin + JNI 直调 nc_*，
// 不经过 HTTP。本文件是 NativeEngineBridge.kt 中 external 函数的实现。
//
// 所有函数名与签名逐一对照 core/include/nairoclip/core/capi.h，
// 不臆造任何未声明的接口。
//
// 契约：
//   · JavaBridge 线程串行执行（Kotlin 侧 @Synchronized）
//   · char*            → nc_free_string
//   · uint8_t*/double* → nc_free_buffer
// ---------------------------------------------------------------------------

#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nairoclip/core/capi.h"

namespace {

std::string JStr(JNIEnv* env, jstring s) {
    if (s == nullptr) return {};
    const char* utf = env->GetStringUTFChars(s, nullptr);
    if (utf == nullptr) return {};
    std::string out(utf);
    env->ReleaseStringUTFChars(s, utf);
    return out;
}

jstring ToJStr(JNIEnv* env, const std::string& s) {
    return env->NewStringUTF(s.c_str());
}

// --- 扁平 JSON 参数读取（{"k": 1, "k2": "s"}）------------------------------

bool FindKey(const std::string& json, const std::string& key, size_t* outPos) {
    const std::string pat = "\"" + key + "\"";
    size_t p = json.find(pat);
    if (p == std::string::npos) return false;
    p = json.find(':', p + pat.size());
    if (p == std::string::npos) return false;
    *outPos = p + 1;
    return true;
}

double JNum(const std::string& json, const std::string& key, double def) {
    size_t p = 0;
    if (!FindKey(json, key, &p)) return def;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    char* end = nullptr;
    const double v = std::strtod(json.c_str() + p, &end);
    return (end == json.c_str() + p) ? def : v;
}

int64_t JInt(const std::string& json, const std::string& key, int64_t def) {
    return static_cast<int64_t>(JNum(json, key, static_cast<double>(def)));
}

std::string JStrField(const std::string& json, const std::string& key) {
    size_t p = 0;
    if (!FindKey(json, key, &p)) return {};
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '}' || json[p] == ',') return {};
        ++p;
    }
    if (p >= json.size()) return {};
    ++p;
    std::string out;
    while (p < json.size() && json[p] != '"') {
        if (json[p] == '\\' && p + 1 < json.size()) {
            const char c = json[p + 1];
            out.push_back(c == 'n' ? '\n' : c == 't' ? '\t' : c);
            p += 2;
            continue;
        }
        out.push_back(json[p]);
        ++p;
    }
    return out;
}

std::string JEsc(const std::string& in) {
    std::string o;
    o.reserve(in.size() + 8);
    for (char c : in) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o.push_back(c);
                }
        }
    }
    return o;
}

std::string Ok(const std::string& inner) {
    return "{\"ok\":true,\"code\":0,\"message\":\"\",\"result\":"
         + (inner.empty() ? std::string("{}") : inner) + "}";
}
std::string Err(const std::string& msg) {
    return "{\"ok\":false,\"code\":-1,\"message\":\"" + JEsc(msg) + "\",\"result\":{}}";
}
std::string Num(double v) {
    char b[40];
    if (v == static_cast<int64_t>(v)) std::snprintf(b, sizeof b, "%lld",
                                                    static_cast<long long>(v));
    else std::snprintf(b, sizeof b, "%.6f", v);
    return b;
}

// --- 工程快照 ---------------------------------------------------------------
// 只使用 capi.h 已声明的接口：
//   nc_project_name / nc_project_canvas / nc_project_duration_us
//   nc_track_count / nc_track_get / nc_clip_count / nc_clip_info
//   nc_asset_count / nc_asset_get
std::string BuildState(NcEditor* ed) {
    std::string j = "{\"project\":{";

    int32_t w = 0, h = 0;
    nc_project_canvas(ed, &w, &h);
    char* pname = nc_project_name(ed);
    j += "\"name\":\"" + JEsc(pname ? pname : "未命名项目") + "\"";
    if (pname) nc_free_string(pname);
    j += ",\"width\":" + std::to_string(w);
    j += ",\"height\":" + std::to_string(h);
    j += ",\"duration_us\":" + std::to_string(nc_project_duration_us(ed));
    j += "},";

    j += "\"tracks\":[";
    const int32_t tn = nc_track_count(ed);
    for (int32_t ti = 0; ti < tn; ++ti) {
        if (ti) j += ",";
        uint64_t tid = 0;
        char* tname = nullptr;
        int32_t ttype = 0, locked = 0, muted = 0, hidden = 0, solo = 0;
        double gain = 0.0;
        const int32_t rc = nc_track_get(ed, ti, &tid, &tname, &ttype,
                                        &locked, &muted, &hidden, &solo, &gain);
        if (rc != NC_OK) { j += "{}"; continue; }

        j += "{\"id\":" + std::to_string(tid);
        j += ",\"index\":" + std::to_string(ti);
        j += ",\"name\":\"" + JEsc(tname ? tname : "") + "\"";
        if (tname) nc_free_string(tname);
        j += ",\"type\":" + std::to_string(ttype);
        j += ",\"locked\":" + std::to_string(locked);
        j += ",\"muted\":" + std::to_string(muted);
        j += ",\"hidden\":" + std::to_string(hidden);
        j += ",\"solo\":" + std::to_string(solo);
        j += ",\"gain_db\":" + Num(gain);

        j += ",\"clips\":[";
        const int32_t cn = nc_clip_count(ed, ti);
        for (int32_t ci = 0; ci < cn; ++ci) {
            if (ci) j += ",";
            uint64_t cid = 0, aid = 0;
            int64_t start = 0, dur = 0, sn = 1, sd = 1;
            int32_t enabled = 1;
            if (nc_clip_info(ed, ti, static_cast<uint32_t>(ci), &cid, &start, &dur,
                             &aid, &sn, &sd, &enabled) != NC_OK) {
                j += "{}";
                continue;
            }
            j += "{\"id\":" + std::to_string(cid);
            j += ",\"asset_id\":" + std::to_string(aid);
            j += ",\"start_us\":" + std::to_string(start);
            j += ",\"duration_us\":" + std::to_string(dur);
            j += ",\"speed_num\":" + std::to_string(sn);
            j += ",\"speed_den\":" + std::to_string(sd);
            j += ",\"enabled\":" + std::to_string(enabled) + "}";
        }
        j += "]}";
    }
    j += "],";

    j += "\"assets\":[";
    const int32_t an = nc_asset_count(ed);
    for (int32_t ai = 0; ai < an; ++ai) {
        if (ai) j += ",";
        uint64_t id = 0;
        char* aname = nullptr;
        char* apath = nullptr;
        int32_t type = 0;
        int64_t adur = 0;
        if (nc_asset_get(ed, ai, &id, &aname, &apath, &type, &adur) != NC_OK) {
            j += "{}";
            continue;
        }
        j += "{\"id\":" + std::to_string(id);
        j += ",\"name\":\"" + JEsc(aname ? aname : "") + "\"";
        j += ",\"path\":\"" + JEsc(apath ? apath : "") + "\"";
        j += ",\"type\":" + std::to_string(type);
        j += ",\"duration_us\":" + std::to_string(adur) + "}";
        if (aname) nc_free_string(aname);
        if (apath) nc_free_string(apath);
    }
    j += "],";

    j += "\"can_undo\":" + std::to_string(nc_can_undo(ed));
    j += ",\"can_redo\":" + std::to_string(nc_can_redo(ed));
    j += "}";
    return Ok(j);
}

// --- 写入类 op 分发 ---------------------------------------------------------
// 只开放前端 UI 实际需要的编辑动作，映射到 capi.h 已声明函数。
bool DispatchEdit(NcEditor* ed, const std::string& op, const std::string& a,
                  std::string* out) {
    const int32_t ti = static_cast<int32_t>(JInt(a, "track", JInt(a, "track_index", 0)));
    const uint64_t cid = static_cast<uint64_t>(JInt(a, "clip_id", JInt(a, "clip", 0)));

    if (op == "split") {
        // 声明：int32_t nc_split_clip(editor, track_index, clip_id, at_us)
        // 无 out_new_id 出参；分割后新片段由 nc_clip_info 重新读取。
        const int64_t at = JInt(a, "time_us", 0);
        const int32_t rc = nc_split_clip(ed, ti, cid, at);
        *out = rc == NC_OK ? Ok("") : Err("分割失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "delete") {
        const int32_t rc = nc_remove_clip(ed, ti, cid);
        *out = rc == NC_OK ? Ok("") : Err("删除失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "trim_start" || op == "trim_end") {
        const int64_t to = JInt(a, "time_us", 0);
        const int32_t rc = (op == "trim_start")
            ? nc_trim_clip_start(ed, ti, cid, to)
            : nc_trim_clip_end(ed, ti, cid, to);
        *out = rc == NC_OK ? Ok("") : Err("裁剪失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "move") {
        const int64_t to = JInt(a, "start_us", 0);
        const int32_t rc = nc_move_clip(ed, ti, cid, to);
        *out = rc == NC_OK ? Ok("") : Err("移动失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "enabled") {
        const int32_t on = static_cast<int32_t>(JInt(a, "enabled", 1));
        const int32_t rc = nc_set_clip_enabled(ed, ti, cid, on);
        *out = rc == NC_OK ? Ok("") : Err("切换失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "opacity") {
        const int32_t rc = nc_set_clip_opacity(ed, ti, cid, JNum(a, "value", 1.0));
        *out = rc == NC_OK ? Ok("") : Err("设置不透明度失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "filter") {
        const int32_t style = static_cast<int32_t>(JInt(a, "style", 0));
        const int32_t rc = nc_set_clip_filter(ed, ti, cid, style, JNum(a, "intensity", 60.0));
        *out = rc == NC_OK ? Ok("") : Err("设置滤镜失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "rename_project") {
        const int32_t rc = nc_set_project_name(ed, JStrField(a, "name").c_str());
        *out = rc == NC_OK ? Ok("") : Err("重命名失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "rename_track") {
        const int32_t rc = nc_rename_track(ed, ti, JStrField(a, "name").c_str());
        *out = rc == NC_OK ? Ok("") : Err("重命名轨道失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "add_track") {
        const uint64_t id = nc_add_track(ed, JStrField(a, "name").c_str(),
                                         static_cast<int32_t>(JInt(a, "type", 0)));
        *out = id ? Ok("{\"track_id\":" + std::to_string(id) + "}")
                  : Err("添加轨道失败");
        return true;
    }
    if (op == "undo") {
        const int32_t rc = nc_undo(ed);
        *out = rc == NC_OK ? Ok("") : Err("无可撤销操作，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "redo") {
        const int32_t rc = nc_redo(ed);
        *out = rc == NC_OK ? Ok("") : Err("无可重做操作，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "frame_fit") {
        *out = Ok("");
        return true;
    }
    // ---- AI 任务框架（core 管状态机，平台层执行推理）----
    if (op == "nc_ai_task_begin") {
        // int32_t nc_ai_task_begin(editor, feature, target_id, char** out_error)
        char* err = nullptr;
        const int32_t rc = nc_ai_task_begin(
            ed, static_cast<int32_t>(JInt(a, "feature", 0)),
            static_cast<uint64_t>(JInt(a, "target_id", 0)), &err);
        std::string msg;
        if (err) {
            msg = err;
            nc_free_string(err);
        }
        *out = (rc == NC_OK) ? Ok("") : Err(msg.empty() ? "任务开始失败" : msg);
        return true;
    }
    if (op == "nc_ai_task_progress") {
        // int32_t nc_ai_task_progress(editor, progress, stage_note)
        const int32_t rc = nc_ai_task_progress(
            ed, JNum(a, "progress", 0.0), JStrField(a, "stage_note").c_str());
        *out = rc == NC_OK ? Ok("") : Err("进度更新失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_ai_task_succeed") {
        const int32_t rc = nc_ai_task_succeed(ed, JStrField(a, "result_payload").c_str());
        *out = rc == NC_OK ? Ok("") : Err("标记成功失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_ai_task_fail") {
        // reason 必须非空（空原因会回退成通用文案）
        std::string reason = JStrField(a, "reason");
        if (reason.empty()) reason = "平台层未提供原因";
        const int32_t rc = nc_ai_task_fail(ed, reason.c_str(),
                                           static_cast<int32_t>(JInt(a, "retryable", 0)));
        *out = rc == NC_OK ? Ok("") : Err("标记失败失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_ai_task_cancel") {
        const int32_t rc = nc_ai_task_cancel(ed);
        *out = rc == NC_OK ? Ok("") : Err("取消失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_ai_task_reset") {
        const int32_t rc = nc_ai_task_reset(ed);
        *out = rc == NC_OK ? Ok("") : Err("复位失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_ai_task_status") {
        // int32_t nc_ai_task_status(editor, out_feature, out_state, out_progress,
        //                           out_stage_note, out_failure_reason,
        //                           out_retryable, out_result_payload)
        int32_t feat = 0, st = 0, retryable = 0;
        double progress = 0.0;
        char* stage = nullptr;
        char* reason = nullptr;
        char* payload = nullptr;
        const int32_t rc = nc_ai_task_status(ed, &feat, &st, &progress, &stage,
                                             &reason, &retryable, &payload);
        if (rc != NC_OK) {
            *out = Err("查询任务状态失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string j = "{\"feature\":" + std::to_string(feat);
        j += ",\"state\":" + std::to_string(st);
        j += ",\"progress\":" + Num(progress);
        j += ",\"stage_note\":\"" + JEsc(stage ? stage : "") + "\"";
        j += ",\"failure_reason\":\"" + JEsc(reason ? reason : "") + "\"";
        j += ",\"retryable\":" + std::to_string(retryable);
        j += ",\"result_payload\":\"" + JEsc(payload ? payload : "") + "\"}";
        if (stage) nc_free_string(stage);
        if (reason) nc_free_string(reason);
        if (payload) nc_free_string(payload);
        *out = Ok(j);
        return true;
    }
    if (op == "nc_model_remove") {
        const int32_t rc = nc_model_remove(ed, JStrField(a, "model_id").c_str());
        *out = rc == NC_OK ? Ok("") : Err("删除失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_ai_feature_access") {
        // int32_t nc_ai_feature_access(editor, feature, logged_in,
        //                               out_requires_network, out_requires_pro, out_reason)
        // 返回 1=可用 0=不可用 -1=参数错误
        int32_t needNet = 0, needPro = 0;
        char* reason = nullptr;
        const int32_t rc = nc_ai_feature_access(
            ed, static_cast<int32_t>(JInt(a, "feature", 0)),
            static_cast<int32_t>(JInt(a, "logged_in", 0)), &needNet, &needPro, &reason);
        std::string j = "{\"available\":" + std::to_string(rc);
        j += ",\"requires_network\":" + std::to_string(needNet);
        j += ",\"requires_pro\":" + std::to_string(needPro);
        j += ",\"reason\":\"" + JEsc(reason ? reason : "") + "\"}";
        if (reason) nc_free_string(reason);
        *out = Ok(j);
        return true;
    }
    return false;  // 未知 op：由调用方报错，不静默成功
}

jbyteArray PngToJByteArray(JNIEnv* env, uint8_t* data, int32_t len) {
    jbyteArray arr = env->NewByteArray(len);
    if (arr) env->SetByteArrayRegion(arr, 0, len, reinterpret_cast<const jbyte*>(data));
    nc_free_buffer(data);
    return arr;
}

}  // namespace

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeCreate(JNIEnv*, jobject) {
    return reinterpret_cast<jlong>(nc_editor_create());
}

JNIEXPORT void JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeDestroy(JNIEnv*, jobject, jlong h) {
    if (h) nc_editor_destroy(reinterpret_cast<NcEditor*>(h));
}

JNIEXPORT jstring JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeCall(
    JNIEnv* env, jobject, jlong h, jstring fn, jstring argsJson) {
    auto* ed = reinterpret_cast<NcEditor*>(h);
    if (!ed) return ToJStr(env, Err("引擎未初始化"));

    const std::string name = JStr(env, fn);
    const std::string args = JStr(env, argsJson);

    std::string out;
    if (DispatchEdit(ed, name, args, &out)) return ToJStr(env, out);
    return ToJStr(env, Err("未开放的操作：" + name));
}

JNIEXPORT jstring JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeState(JNIEnv* env, jobject, jlong h) {
    auto* ed = reinterpret_cast<NcEditor*>(h);
    if (!ed) return ToJStr(env, Err("引擎未初始化"));
    return ToJStr(env, BuildState(ed));
}

JNIEXPORT jbyteArray JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeFrame(
    JNIEnv* env, jobject, jlong h, jlong timeUs, jint width) {
    auto* ed = reinterpret_cast<NcEditor*>(h);
    if (!ed) return nullptr;

    // nc_preview_render 输出的是 RGBA8 像素，不是 PNG。
    // 这里原样取出像素并交给 Kotlin 侧编码，避免在 NDK 里塞编码器。
    uint8_t* rgba = nullptr;
    int32_t w = 0, hgt = 0;
    uint64_t clipId = 0;
    double op = 1, sc = 1, px = 0, py = 0, rot = 0;
    int32_t ttype = -1;
    double tprog = 0;
    const int32_t rc = nc_preview_render(ed, static_cast<int64_t>(timeUs), &w, &hgt,
                                         &rgba, &clipId, &op, &sc, &px, &py, &rot,
                                         &ttype, &tprog);
    if (rc != NC_OK || rgba == nullptr || w <= 0 || hgt <= 0) return nullptr;

    // 打包成 [w:int32][h:int32][RGBA...] 交给 Kotlin 用 Bitmap 编码
    const int32_t header = 8;
    const int32_t pixLen = w * hgt * 4;
    jbyteArray arr = env->NewByteArray(header + pixLen);
    if (arr) {
        env->SetByteArrayRegion(arr, 0, 4,
            reinterpret_cast<const jbyte*>(&w));
        env->SetByteArrayRegion(arr, 4, 4,
            reinterpret_cast<const jbyte*>(&hgt));
        env->SetByteArrayRegion(arr, 8, pixLen,
            reinterpret_cast<const jbyte*>(rgba));
    }
    nc_free_buffer(rgba);
    return arr;
}

JNIEXPORT jbyteArray JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeThumb(
    JNIEnv* env, jobject, jlong h, jlong assetId, jint index, jint width) {
    auto* ed = reinterpret_cast<NcEditor*>(h);
    if (!ed) return nullptr;

    // 声明：int32_t nc_thumbnail_generate(editor, asset_id, count, max_width,
    //                                     max_height, uint8_t** out_images,
    //                                     int64_t* out_times_us, int32_t* out_count)
    // out_images 由库按 count 分配，每个元素都要 nc_free_buffer。
    uint8_t** images = nullptr;
    int64_t* times = nullptr;
    int32_t count = 0;
    const int32_t rc = nc_thumbnail_generate(ed, static_cast<uint64_t>(assetId), 1,
                                             width, width * 2,
                                             images, times, &count);
    if (rc != NC_OK || images == nullptr || count <= 0) {
        return nullptr;
    }

    int32_t tw = 0, th = 0;
    nc_thumbnail_size(ed, &tw, &th);

    jbyteArray arr = nullptr;
    const int32_t idx = (index >= 0 && index < count) ? index : 0;
    if (tw > 0 && th > 0 && images[idx]) {
        const int32_t pixLen = tw * th * 4;
        arr = env->NewByteArray(8 + pixLen);
        if (arr) {
            env->SetByteArrayRegion(arr, 0, 4, reinterpret_cast<const jbyte*>(&tw));
            env->SetByteArrayRegion(arr, 4, 4, reinterpret_cast<const jbyte*>(&th));
            env->SetByteArrayRegion(arr, 8, pixLen,
                                    reinterpret_cast<const jbyte*>(images[idx]));
        }
    }
    // 每个元素都是 nc_free_buffer 分配
    for (int32_t i = 0; i < count; ++i) {
        if (images[i]) nc_free_buffer(images[i]);
    }
    return arr;
}

// 预设缩略帧：临时给片段套一个预设→渲染一帧→还原。
// 完全用 capi.h 已声明的接口组合，等价于设计桥的 /api/preset_frame：
//   filter → nc_set_clip_filter
//   effect → nc_add_effect / nc_remove_effect
//   mask   → nc_set_clip_mask
JNIEXPORT jbyteArray JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativePresetFrame(
    JNIEnv* env, jobject, jlong h, jstring kind, jint style,
    jlong timeUs, jint width) {
    auto* ed = reinterpret_cast<NcEditor*>(h);
    if (!ed) return nullptr;

    // 找第一条有片段的主轨，取其首片段作为套预设的对象
    int32_t ti = -1;
    uint64_t cid = 0;
    const int32_t tn = nc_track_count(ed);
    for (int32_t i = 0; i < tn; ++i) {
        if (nc_clip_count(ed, i) <= 0) continue;
        uint64_t id = 0, aid = 0;
        int64_t st = 0, du = 0, sn = 1, sd = 1;
        int32_t en = 1;
        if (nc_clip_info(ed, i, 0, &id, &st, &du, &aid, &sn, &sd, &en) == NC_OK) {
            ti = i; cid = id; break;
        }
    }
    if (ti < 0) return nullptr;

    const std::string k = JStr(env, kind);

    // 保存原滤镜并套用预设
    int32_t oldStyle = 0;
    double oldInt = 0.0;
    const int32_t wasOn = nc_get_clip_filter(ed, ti, cid, &oldStyle, &oldInt);

    uint64_t tempEffect = 0;
    bool touchedFilter = false;
    bool touchedMask = false;

    if (k == "filter") {
        nc_set_clip_filter(ed, ti, cid, static_cast<int32_t>(style), 100.0);
        touchedFilter = true;
    } else if (k == "effect") {
        tempEffect = nc_add_effect(ed, ti, cid, static_cast<int32_t>(style), 0.85);
    } else if (k == "mask") {
        nc_set_clip_mask(ed, ti, cid, static_cast<int32_t>(style), 0.5, 0.5, 0,
                         1.0, 1.0, 0.06, 0.0, 0);
        touchedMask = true;
    }

    uint8_t* rgba = nullptr;
    int32_t w = 0, hgt = 0;
    uint64_t outClip = 0;
    double op = 1, sc = 1, px = 0, py = 0, rot = 0;
    int32_t tt = -1;
    double tp = 0;
    const int32_t rc = nc_preview_render(ed, static_cast<int64_t>(timeUs), &w, &hgt, &rgba,
                                         &outClip, &op, &sc, &px, &py, &rot, &tt, &tp);

    // 还原：滤镜、临时特效、蒙版
    if (touchedFilter) {
        nc_set_clip_filter(ed, ti, cid, wasOn > 0 ? oldStyle : 0,
                           wasOn > 0 ? oldInt : 0.0);
    }
    if (tempEffect) {
        nc_remove_effect(ed, ti, cid, tempEffect);
    }
    if (touchedMask) {
        nc_set_clip_mask(ed, ti, cid, 0, 0.5, 0.5, 0, 1.0, 1.0, 0.0, 0.0, 0);
    }
    nc_preview_invalidate(ed);

    if (rc != NC_OK || rgba == nullptr || w <= 0 || hgt <= 0) return nullptr;

    const int32_t pixLen = w * hgt * 4;
    jbyteArray arr = env->NewByteArray(8 + pixLen);
    if (arr) {
        env->SetByteArrayRegion(arr, 0, 4, reinterpret_cast<const jbyte*>(&w));
        env->SetByteArrayRegion(arr, 4, 4, reinterpret_cast<const jbyte*>(&hgt));
        env->SetByteArrayRegion(arr, 8, pixLen, reinterpret_cast<const jbyte*>(rgba));
    }
    nc_free_buffer(rgba);
    return arr;
}

// 安装模型：Kotlin 侧读文件字节后经此入口交给 core 校验并记录。
// 声明：int32_t nc_model_install(editor, model_id, ciphertext, size, path,
//                                expected_sha256_hex, now_unix,
//                                out_installed, out_message)
JNIEXPORT jstring JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeInstallModel(
    JNIEnv* env, jobject, jlong h, jstring modelId, jbyteArray data,
    jstring path, jstring sha256, jlong nowUnix) {
    auto* ed = reinterpret_cast<NcEditor*>(h);
    if (!ed) return ToJStr(env, Err("引擎未初始化"));
    if (data == nullptr) return ToJStr(env, Err("模型字节为空"));

    const std::string id = JStr(env, modelId);
    const std::string p = JStr(env, path);
    const std::string sha = JStr(env, sha256);
    const jsize len = env->GetArrayLength(data);
    if (id.empty() || len <= 0) return ToJStr(env, Err("model_id 与字节数必填"));

    std::vector<uint8_t> buf(static_cast<size_t>(len));
    env->GetByteArrayRegion(data, 0, len, reinterpret_cast<jbyte*>(buf.data()));

    int32_t installed = 0;
    char* msg = nullptr;
    const int32_t rc = nc_model_install(
        ed, id.c_str(), buf.data(), static_cast<int64_t>(len),
        p.c_str(), sha.c_str(), static_cast<int64_t>(nowUnix), &installed, &msg);

    std::string j = "{\"installed\":" + std::to_string(installed);
    j += ",\"message\":\"" + JEsc(msg ? msg : "") + "\"}";
    if (msg) nc_free_string(msg);

    return ToJStr(env, rc == NC_OK ? Ok(j)
                                  : Err("安装失败，错误码 " + std::to_string(rc)));
}

JNIEXPORT jstring JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeModels(JNIEnv* env, jobject, jlong h) {
    auto* ed = reinterpret_cast<NcEditor*>(h);

    // 声明：int32_t nc_model_catalog(char*** out_ids, int32_t capacity, int32_t* out_count)
    // 先用 capacity=0 查总数，再按实际数量分配。
    int32_t count = 0;
    if (nc_model_catalog(nullptr, 0, &count) != NC_OK || count <= 0) {
        return ToJStr(env, "{\"models\":[]}");
    }
    char** ids = new char*[static_cast<size_t>(count)]();
    int32_t got = 0;
    if (nc_model_catalog(&ids, count, &got) != NC_OK) {
        delete[] ids;
        return ToJStr(env, "{\"models\":[]}");
    }

    std::string j = "{\"models\":[";
    for (int32_t i = 0; i < got && i < count; ++i) {
        if (!ids[i]) continue;
        if (i) j += ",";
        char* name = nullptr;
        char* desc = nullptr;
        char* ver = nullptr;
        int32_t size = 0, pro = 0, cap = 0;
        nc_model_info(ids[i], &name, &desc, &size, &ver, &pro, &cap);
        // status 0=未下载 1=下载中 2=已安装 3=损坏 4=可更新 5=校验中
        const int32_t st = ed ? nc_model_status(ed, ids[i]) : 0;
        const int32_t allowed = ed ? nc_model_access_allowed(ed, ids[i]) : 0;

        j += "{\"id\":\"" + JEsc(ids[i]) + "\"";
        j += ",\"name\":\"" + JEsc(name ? name : "") + "\"";
        j += ",\"version\":\"" + JEsc(ver ? ver : "") + "\"";
        j += ",\"size_bytes\":" + std::to_string(size);
        j += ",\"requires_pro\":" + std::to_string(pro);
        j += ",\"capability\":" + std::to_string(cap);
        j += ",\"status\":" + std::to_string(st);
        j += ",\"allowed\":" + std::to_string(allowed) + "}";
        if (name) nc_free_string(name);
        if (desc) nc_free_string(desc);
        if (ver) nc_free_string(ver);
    }
    j += "]}";

    // 数组元素逐个释放，再释放数组本身
    for (int32_t i = 0; i < count; ++i) {
        if (ids[i]) nc_free_string(ids[i]);
    }
    delete[] ids;
    return ToJStr(env, j);
}

JNIEXPORT jstring JNICALL
Java_com_nairoclip_app_NativeEngineBridge_nativeWaveform(
    JNIEnv* env, jobject, jlong h, jlong assetId) {
    auto* ed = reinterpret_cast<NcEditor*>(h);
    if (!ed) return ToJStr(env, "{\"buckets\":[]}");

    double peak = 0.0;
    int32_t rate = 0, ch = 0;
    // 波形是逐桶读取：先生成，再 nc_waveform_bucket_get
    if (nc_waveform_generate(ed, static_cast<uint64_t>(assetId), 480,
                             &peak, &rate, &ch) != NC_OK) {
        return ToJStr(env, "{\"buckets\":[]}");
    }
    const int32_t n = nc_waveform_bucket_count(ed);
    std::string j = "{\"buckets\":[";
    for (int32_t i = 0; i < n; ++i) {
        double mn = 0, mx = 0, rms = 0;
        if (nc_waveform_bucket_get(ed, i, &mn, &mx, &rms) != NC_OK) continue;
        if (i) j += ",";
        j += Num(rms);
    }
    j += "]}";
    return ToJStr(env, j);
}

}  // extern "C"
