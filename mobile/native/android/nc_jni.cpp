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
#include "nairoclip/core/simd.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "nairoclip/core/capi.h"
#include "nairoclip/core/xform.h"

namespace {
/* 本文件是 JNI 入口，整体在全局命名空间（capi.h 导出全局 C ABI）。
   只把用到的 core 类型显式引入，不用 using namespace 以免污染。 */
using nairoclip::core::XformKind;

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

/// 读取 double 数组（如 "usable_seconds": [3.5, 2.0]）。
/// 与 JVecU64 同形状，只是用 strtod 解析小数。
bool JVecDouble(const std::string& json, const std::string& key, std::vector<double>* out) {
    if (out == nullptr) return false;
    out->clear();
    size_t p = 0;
    if (!FindKey(json, key, &p)) return false;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size() || json[p] != '[') return false;
    ++p;
    while (p < json.size() && json[p] != ']') {
        if (json[p] == ' ' || json[p] == '\t' || json[p] == ',') { ++p; continue; }
        char* end = nullptr;
        const double v = std::strtod(json.c_str() + p, &end);
        if (end == json.c_str() + p) break;  // 非数字：停止，避免死循环
        out->push_back(v);
        p = static_cast<size_t>(end - json.c_str());
    }
    return !out->empty();
}

/// 读取 uint64 数组（如 "material_ids": [1,2,3]）。
/// 只支持扁平整数数组：本桥的入参是桥自己序列化的，不含嵌套对象。
/// 返回 false 表示键不存在或不是数组。
bool JVecU64(const std::string& json, const std::string& key,
             std::vector<uint64_t>* out) {
    if (out == nullptr) return false;
    out->clear();
    size_t p = 0;
    if (!FindKey(json, key, &p)) return false;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size() || json[p] != '[') return false;
    ++p;
    while (p < json.size() && json[p] != ']') {
        // 跳过空白与逗号
        if (json[p] == ' ' || json[p] == '\t' || json[p] == ',') { ++p; continue; }
        char* end = nullptr;
        const unsigned long long v = std::strtoull(json.c_str() + p, &end, 10);
        if (end == json.c_str() + p) break;  // 非数字：停止，避免死循环
        out->push_back(static_cast<uint64_t>(v));
        p = static_cast<size_t>(end - json.c_str());
    }
    return !out->empty();
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
        /* 所属轨道组（B2-7）。0 = 未分组。前端据此画组框与联动开关。
           放这里而不是另开一个 groups 数组：轨道组的作用就是「这些轨是一伙的」，
           挂在轨道上读起来最直接，也免去前端再做一次 id→tracks 的匹配。 */
        j += ",\"group_id\":" + std::to_string(nc_track_group_of(ed, ti));

        // 轨道视图（B2-10）：高度与折叠。由独立的 nc_track_get_view 提供，
        // nc_track_get 不返回这两个字段。
        // 读失败时给默认值（48 / 未折叠），与 core 的默认一致，
        // 不因为一个视图字段读不到就让整条轨道变成 {}。
        {
            int32_t viewH = 48, viewCollapsed = 0;
            if (nc_track_get_view(ed, ti, &viewH, &viewCollapsed) == NC_OK) {
                j += ",\"height_px\":" + std::to_string(viewH);
                j += ",\"collapsed\":" + std::to_string(viewCollapsed);
            } else {
                j += ",\"height_px\":48,\"collapsed\":0";
            }
        }

        // 调整图层（B2-6 / P-19）：效果与 LUT 挂在**轨道**上，作用于下方所有轨道。
        // 非调整图层不发这个字段，前端据此区分要不要渲染齿轮与斜纹底。
        // 读失败就不发 adjustment —— 前端按「没有这个字段」降级，
        // 不塞一堆 0 让 UI 显示成「0 个效果」，那是假信息。
        if (ttype == 3) {
            int32_t adjCount = 0, adjHasLut = 0;
            double adjStrength = 0.0;
            char* adjPath = nullptr;
            if (nc_adjustment_get(ed, ti, &adjCount, &adjHasLut, &adjStrength,
                                  &adjPath) == NC_OK) {
                j += ",\"adjustment\":{";
                j += "\"effect_count\":" + std::to_string(adjCount);
                j += ",\"has_lut\":" + std::to_string(adjHasLut);
                j += ",\"lut_strength\":" + Num(adjStrength);
                j += ",\"lut_path\":\"" + JEsc(adjPath ? adjPath : "") + "\"}";
                if (adjPath) nc_free_string(adjPath);
            }
        }

        j += ",\"clips\":[";
        const int32_t cn = nc_clip_count(ed, ti);
        for (int32_t ci = 0; ci < cn; ++ci) {
            uint64_t cid = 0, aid = 0;
            int64_t start = 0, dur = 0, sn = 1, sd = 1;
            int32_t enabled = 1;
            if (nc_clip_info(ed, ti, static_cast<uint32_t>(ci), &cid, &start, &dur,
                             &aid, &sn, &sd, &enabled) != NC_OK) {
                // 读失败仍然占一个槽位（{}），保持 index 与引擎一致；
                // 逗号在写入前统一补，避免失败分支留下 ",," 这种非法 JSON。
                if (ci) j += ",";
                j += "{}";
                continue;
            }
            if (ci) j += ",";
            j += "{\"id\":" + std::to_string(cid);
            j += ",\"asset_id\":" + std::to_string(aid);
            j += ",\"start_us\":" + std::to_string(start);
            j += ",\"duration_us\":" + std::to_string(dur);
            j += ",\"speed_num\":" + std::to_string(sn);
            j += ",\"speed_den\":" + std::to_string(sd);
            j += ",\"enabled\":" + std::to_string(enabled);
            // 特效栈：前端要靠这份数据渲染「已添加效果列表」。
            // 只给 id/type/value/enabled —— 名称由前端按 type 映射，
            // 协议里不传中文字符串（省字节，改名也不必动后端）。
            j += ",\"effects\":[";
            const int32_t enCount = nc_effect_count(ed, ti, cid);
            int32_t enWritten = 0;   // 用独立计数控制逗号：读失败时跳过，不能留下多余逗号
            for (int32_t ei = 0; ei < enCount; ++ei) {
                uint64_t eid = 0;
                int32_t etype = 0;
                int32_t een = 1;
                double eval = 0.0;
                if (nc_effect_get(ed, ti, cid, ei, &eid, &etype, &eval, &een) != NC_OK) {
                    continue;
                }
                if (enWritten > 0) {
                    j += ",";
                }
                j += "{\"id\":" + std::to_string(eid);
                j += ",\"type\":" + std::to_string(etype);
                j += ",\"value\":" + Num(eval);
                j += ",\"enabled\":" + std::to_string(een) + "}";
                ++enWritten;
            }
            j += "]}";
            // 几何变换 8 槽：前端靠 enabled 决定宫格「已添加」小圆点。
            // 只下发启用的槽（未启用的用默认值也没意义，省带宽）。
            j += ",\"xforms\":[";
            {
                int32_t xfWritten = 0;
                for (int32_t xk = 0; xk < static_cast<int32_t>(XformKind::Count); ++xk) {
                    double xargs[NC_XFORM_ARG_COUNT] = {0.0};
                    int32_t xen = 0;
                    int32_t xedge = 1;
                    if (nc_get_clip_xform(ed, ti, cid, xk, xargs, &xen, &xedge) != NC_OK) {
                        continue;
                    }
                    if (xen == 0) {
                        continue;
                    }
                    if (xfWritten > 0) {
                        j += ",";
                    }
                    j += "{\"kind\":" + std::to_string(xk);
                    j += ",\"edge\":" + std::to_string(xedge);
                    j += ",\"args\":[";
                    for (int32_t ai = 0; ai < NC_XFORM_ARG_COUNT; ++ai) {
                        if (ai > 0) j += ",";
                        j += Num(xargs[ai]);
                    }
                    j += "]}";
                    ++xfWritten;
                }
            }
            j += "]";
        }
        j += "]}";
    }
    j += "],";

    /* 轨道组清单（B2-7）：组名与成员工数。
       成员明细不在这里重复下发 —— 每条轨道已带 group_id，
       前端按 group_id 聚合即可，无需两处保持一致。 */
    j += "\"groups\":[";
    {
        const int32_t gc = nc_group_count(ed);
        for (int32_t gi = 0; gi < gc; ++gi) {
            if (gi) j += ",";
            uint64_t gid = 0;
            int32_t gcount = 0;
            char* gname = nullptr;
            if (nc_group_get(ed, gi, &gid, &gname, &gcount) != NC_OK) {
                j += "{}";
                continue;
            }
            j += "{\"id\":" + std::to_string(gid);
            j += ",\"name\":\"" + JEsc(gname ? gname : "") + "\"";
            j += ",\"track_count\":" + std::to_string(gcount) + "}";
            if (gname) nc_free_string(gname);
        }
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

    /* ---- 9 个此前缺失的 op（PIP_GESTURE_PLAN.md §G0）----
       背景：前端会发这些 op，但 JNI 没有分支 → 真机点按钮返回
       「未开放的操作」，界面毫无反应。底层 nc_* 全部就位，纯补接线。
       实现照抄 design/tools/nc_design_harness.cpp 的成熟写法。 */

    // 轨道标志位：左眼 / 喇叭 / 锁 / 独奏。前端用 track_flag 统一下发。
    if (op == "track_flag") {
        const std::string flag = JStrField(a, "flag");
        const int32_t value = JInt(a, "value", 0) ? 1 : 0;
        if (ti < 0) { *out = Err("缺少目标轨道"); return true; }
        int32_t rc = NC_OK;
        if (flag == "locked")      rc = nc_set_track_locked(ed, ti, value);
        else if (flag == "muted")   rc = nc_set_track_muted(ed, ti, value);
        else if (flag == "hidden")  rc = nc_set_track_hidden(ed, ti, value);
        else if (flag == "solo")    rc = nc_set_track_solo(ed, ti, value);
        else { *out = Err("未知轨道标志位：" + flag); return true; }
        *out = rc == NC_OK ? Ok("") : Err("设置轨道标志失败，错误码 " + std::to_string(rc));
        return true;
    }
    // 轨道增益（dB）
    if (op == "track_gain") {
        if (ti < 0) { *out = Err("缺少目标轨道"); return true; }
        const int32_t rc = nc_set_track_gain(ed, ti, JNum(a, "gain_db", 0.0));
        *out = rc == NC_OK ? Ok("") : Err("设置轨道增益失败，错误码 " + std::to_string(rc));
        return true;
    }
    // 复制片段
    if (op == "duplicate") {
        if (cid == 0) { *out = Err("缺少目标片段"); return true; }
        const uint64_t newId = nc_duplicate_clip(ed, ti, cid);
        *out = newId ? Ok("{\"clip_id\":" + std::to_string(newId) + "}")
                     : Err("复制片段失败");
        return true;
    }
    // 跨轨移动
    if (op == "move_to_track") {
        if (cid == 0) { *out = Err("缺少目标片段"); return true; }
        const int32_t toTrack = static_cast<int32_t>(JInt(a, "to_track", -1));
        if (toTrack < 0) { *out = Err("缺少目标轨道"); return true; }
        const int64_t at = JInt(a, "start_us", 0);
        const int32_t rc = nc_move_clip_to_track(ed, ti, toTrack, cid, at);
        *out = rc == NC_OK ? Ok("") : Err("跨轨移动失败，错误码 " + std::to_string(rc));
        return true;
    }
    // 波纹删除（删后自动前移）
    if (op == "ripple_delete") {
        if (cid == 0) { *out = Err("缺少目标片段"); return true; }
        const uint64_t ids[1] = {cid};
        const int32_t rc = nc_ripple_delete(ed, ti, ids, 1);
        *out = rc == NC_OK ? Ok("") : Err("波纹删除失败，错误码 " + std::to_string(rc));
        return true;
    }
    // 删除关键帧（与上面的「插入关键帧」成对）
    if (op == "remove_keyframe") {
        const int32_t prop = static_cast<int32_t>(JInt(a, "prop", JInt(a, "property", 0)));
        int64_t atUs = static_cast<int64_t>(JInt(a, "at_us", -1));
        if (atUs < 0) atUs = static_cast<int64_t>(JNum(a, "t", 0.0) * 1e6);
        const int32_t rc = nc_remove_keyframe(ed, ti, cid, prop, atUs);
        *out = rc == NC_OK ? Ok("") : Err("删除关键帧失败，错误码 " + std::to_string(rc));
        return true;
    }
    // 删除转场
    if (op == "remove_transition") {
        if (ti < 0) { *out = Err("缺少目标轨道"); return true; }
        const uint64_t tid = static_cast<uint64_t>(JInt(a, "transition", 0));
        if (tid == 0) { *out = Err("缺少转场 id"); return true; }
        const int32_t rc = nc_remove_transition(ed, ti, tid);
        *out = rc == NC_OK ? Ok("") : Err("删除转场失败，错误码 " + std::to_string(rc));
        return true;
    }
    // 加时间标记 / 改标记颜色（两者都走 nc_add_marker，颜色是参数）
    if (op == "marker" || op == "marker_color") {
        const char* defColor = (op == "marker_color") ? "#F0A03C" : "#C6F24E";
        const uint64_t mid = nc_add_marker(ed, JInt(a, "time_us", 0),
                                           JStrField(a, "name").empty()
                                               ? "标记" : JStrField(a, "name").c_str(),
                                           JStrField(a, "color").empty()
                                               ? defColor : JStrField(a, "color").c_str());
        *out = mid ? Ok("{\"marker_id\":" + std::to_string(mid) + "}")
                   : Err("添加标记失败");
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
    // ---- 片段属性（l3-panel 的 16 种 op 缺一不可）----
    // 背景：这里此前只有 split/delete/trim/move/enabled/opacity/filter，
    // 而 l3-panel.js 会发 blend/chroma/mask/effect_add/audio/denoise/
    // speed/speed_curve/keyframe/freeze/reverse/transition/canvas/lut。
    // 结果是那些面板在 Android 上点「应用」后桥返回「未开放的操作」，
    // 用户看到的是「面板能开、改了没反应」——最坏的一类坏体验。
    /* ---- 画面变换（画中画位置/大小/旋转；G2）---- */
    if (op == "transform_set") {
        if (ti < 0 || cid == 0) { *out = Err("缺少目标片段"); return true; }
        const int32_t rc = nc_set_clip_transform(
            ed, ti, cid,
            JInt(a, "enabled", 1) ? 1 : 0,
            JNum(a, "scale", 1.0),
            JNum(a, "pos_x", 0.0),
            JNum(a, "pos_y", 0.0),
            JNum(a, "rotation", 0.0),
            static_cast<int32_t>(JInt(a, "anchor", 4)));
        *out = rc == NC_OK ? Ok("") : Err("设置画面变换失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "transform_get") {
        if (ti < 0 || cid == 0) { *out = Err("缺少目标片段"); return true; }
        int32_t en = 0, anchor = 4;
        double sc = 1.0, px = 0.0, py = 0.0, rot = 0.0;
        const int32_t rc = nc_get_clip_transform(ed, ti, cid, &en, &sc, &px, &py, &rot, &anchor);
        if (rc != NC_OK) {
            *out = Err("读取画面变换失败，错误码 " + std::to_string(rc));
            return true;
        }
        *out = Ok("{\"enabled\":" + std::to_string(en) +
                  ",\"scale\":" + Num(sc) +
                  ",\"pos_x\":" + Num(px) +
                  ",\"pos_y\":" + Num(py) +
                  ",\"rotation\":" + Num(rot) +
                  ",\"anchor\":" + std::to_string(anchor) + "}");
        return true;
    }
    if (op == "transform_clear") {
        if (ti < 0 || cid == 0) { *out = Err("缺少目标片段"); return true; }
        const int32_t rc = nc_clear_clip_transform(ed, ti, cid);
        *out = rc == NC_OK ? Ok("") : Err("清除画面变换失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "blend") {
        const int32_t mode = static_cast<int32_t>(JInt(a, "mode", 0));
        const int32_t rc = nc_set_clip_blend_mode(ed, ti, cid, mode);
        *out = rc == NC_OK ? Ok("") : Err("设置混合模式失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "speed") {
        // nc_set_clip_speed 收的是**整数分数**（num/den），不是 double ——
        // 用 double 传会直接编译失败，且分数形式能保证 1/3 这类速度精确可逆。
        int64_t num = static_cast<int64_t>(JInt(a, "num", -1));
        int64_t den = static_cast<int64_t>(JInt(a, "den", -1));
        if (num <= 0 || den <= 0) {
            // 兼容只传 speed 的旧调用方：按千分位换算，最大误差 0.0005x
            const double speed = JNum(a, "speed", 1.0);
            if (!(speed > 0.0)) {
                *out = Err("速度必须大于 0");
                return true;
            }
            num = static_cast<int64_t>(speed * 1000.0 + 0.5);
            den = 1000;
        }
        const int32_t rc = nc_set_clip_speed(ed, ti, cid, num, den);
        *out = rc == NC_OK ? Ok("") : Err("设置速度失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "speed_curve") {
        // 参数是数组：points:[{t,v,e}]。JInt/JNum 只能读标量，
        // 这里用最小 JSON 扫描取出三元组；点少于 2 个视为清除曲线。
        std::vector<double> ts, vs;
        std::vector<int32_t> es;
        size_t p = a.find("\"points\"");
        if (p != std::string::npos) {
            size_t lb = a.find('[', p);
            size_t rb = a.find(']', lb == std::string::npos ? p : lb);
            if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
                const std::string arr = a.substr(lb + 1, rb - lb - 1);
                size_t pos = 0;
                while (pos < arr.size()) {
                    size_t ob = arr.find('{', pos);
                    size_t cb = arr.find('}', ob == std::string::npos ? pos : ob);
                    if (ob == std::string::npos || cb == std::string::npos) break;
                    const std::string obj = arr.substr(ob, cb - ob + 1);
                    ts.push_back(JNum(obj, "t", 0.0));
                    vs.push_back(JNum(obj, "v", 1.0));
                    es.push_back(static_cast<int32_t>(JInt(obj, "e", 0)));
                    pos = cb + 1;
                }
            }
        }
        if (ts.size() < 2) {
            *out = Err("速度曲线至少需要 2 个控制点");
            return true;
        }
        const int32_t rc = nc_set_clip_speed_curve(ed, ti, cid, ts.data(), vs.data(),
                                                   es.data(), static_cast<int32_t>(ts.size()));
        *out = rc == NC_OK ? Ok("") : Err("设置速度曲线失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "effect_add") {
        const int32_t type = static_cast<int32_t>(JInt(a, "type", 0));
        const double value = JNum(a, "value", 0.5);
        const uint64_t id = nc_add_effect(ed, ti, cid, type, value);
        *out = id ? Ok("{\"value\":" + std::to_string(id) + "}")
                  : Err("添加特效失败");
        return true;
    }
    if (op == "effect_update") {
        // 拖强度滑块必须走这里：传已有 effect_id 原地更新。
        // 若走 effect_add，每拖一次都会新增一条，一次拖动留下几十个重复特效。
        const uint64_t id = static_cast<uint64_t>(JInt(a, "effect_id", 0));
        const int32_t type = static_cast<int32_t>(JInt(a, "type", 0));
        const double value = JNum(a, "value", 0.0);
        const int32_t rc = nc_update_effect(ed, ti, cid, id, type, value);
        *out = rc == NC_OK ? Ok("") : Err("更新特效失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "effect_enabled") {
        const uint64_t id = static_cast<uint64_t>(JInt(a, "effect_id", 0));
        const int32_t on = static_cast<int32_t>(JInt(a, "enabled", 1));
        const int32_t rc = nc_set_effect_enabled(ed, ti, cid, id, on);
        *out = rc == NC_OK ? Ok("") : Err("切换特效失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "effect_move") {
        const uint64_t id = static_cast<uint64_t>(JInt(a, "effect_id", 0));
        const int32_t dir = static_cast<int32_t>(JInt(a, "direction", 0));
        const int32_t rc = nc_move_effect(ed, ti, cid, id, dir);
        *out = rc == NC_OK ? Ok("") : Err("调整特效顺序失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "effect_remove") {
        const uint64_t id = static_cast<uint64_t>(JInt(a, "effect_id", 0));
        const int32_t rc = nc_remove_effect(ed, ti, cid, id);
        *out = rc == NC_OK ? Ok("") : Err("删除特效失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "mask") {
        const int32_t shape = static_cast<int32_t>(JInt(a, "shape", 4));
        const int32_t rc = nc_set_clip_mask(
            ed, ti, cid, shape,
            JNum(a, "x", 0.5), JNum(a, "y", 0.5), JNum(a, "rotation", 0.0),
            JNum(a, "width", 1.0), JNum(a, "height", 1.0),
            JNum(a, "feather", 0.02), JNum(a, "radius", 0.0),
            static_cast<int32_t>(JInt(a, "invert", 0)));
        *out = rc == NC_OK ? Ok("") : Err("设置蒙版失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "chroma") {
        const std::string key = JStrField(a, "key");
        const int32_t rc = nc_set_clip_chroma_key(
            ed, ti, cid, key.empty() ? "#00FF00" : key.c_str(),
            JNum(a, "inner", 0.35), JNum(a, "outer", 0.55),
            JNum(a, "spill", 0.3),
            static_cast<int32_t>(JInt(a, "quality", 1)),
            static_cast<int32_t>(JInt(a, "enabled", 1)));
        *out = rc == NC_OK ? Ok("") : Err("设置抠像失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "denoise") {
        const int32_t rc = nc_set_clip_denoise(
            ed, ti, cid, static_cast<int32_t>(JInt(a, "strength", 0)));
        *out = rc == NC_OK ? Ok("") : Err("设置降噪失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "audio") {
        const int32_t rc = nc_set_clip_audio(
            ed, ti, cid, JNum(a, "gain_db", 0.0),
            static_cast<int64_t>(JInt(a, "fade_in_us", 0)),
            static_cast<int64_t>(JInt(a, "fade_out_us", 0)));
        *out = rc == NC_OK ? Ok("") : Err("设置音频失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "keyframe") {
        // l3-panel 传 prop（0..5）与 t（秒）/v；at_us 优先（更精确）
        const int32_t prop = static_cast<int32_t>(JInt(a, "prop",
                                       JInt(a, "property", 0)));
        int64_t atUs = static_cast<int64_t>(JInt(a, "at_us", -1));
        if (atUs < 0) {
            atUs = static_cast<int64_t>(JNum(a, "t", 0.0) * 1e6);
        }
        const int32_t rc = nc_insert_keyframe(ed, ti, cid, prop, atUs,
                                              JNum(a, "v", 1.0));
        *out = rc == NC_OK ? Ok("") : Err("插入关键帧失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "freeze") {
        const int64_t atUs = static_cast<int64_t>(JInt(a, "at_us", 0));
        const int64_t durUs = static_cast<int64_t>(JInt(a, "duration_us", 2000000));
        const uint64_t id = nc_freeze_frame(ed, ti, cid, atUs, durUs);
        *out = id ? Ok("{\"value\":" + std::to_string(id) + "}") : Err("定格失败");
        return true;
    }
    if (op == "reverse") {
        const int32_t rc = nc_reverse_clip(ed, ti, cid);
        *out = rc == NC_OK ? Ok("") : Err("倒放失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "transition") {
        const int32_t type = static_cast<int32_t>(JInt(a, "type", 0));
        const int64_t durUs = static_cast<int64_t>(JInt(a, "duration_us", 500000));
        const uint64_t fromId = static_cast<uint64_t>(JInt(a, "from_clip", 0));
        const uint64_t toId = static_cast<uint64_t>(JInt(a, "to_clip", 0));
        const uint64_t id = nc_add_transition(ed, ti, type, durUs, fromId, toId);
        *out = id ? Ok("{\"value\":" + std::to_string(id) + "}") : Err("添加转场失败");
        return true;
    }
    if (op == "canvas") {
        const int32_t rc = nc_set_project_canvas(
            ed, static_cast<int32_t>(JInt(a, "width", 1080)),
            static_cast<int32_t>(JInt(a, "height", 1920)));
        *out = rc == NC_OK ? Ok("") : Err("设置画布失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "lut") {
        // 片段 3D LUT（P-01）。path 为空 = 清除。
        const std::string path = JStrField(a, "path");
        const double strength = JNum(a, "strength", 1.0);
        if (!(strength >= 0.0 && strength <= 1.0)) {
            *out = Err("LUT 强度必须在 0..1");
            return true;
        }
        const int32_t rc = nc_set_clip_lut(ed, ti, cid,
                                           path.empty() ? nullptr : path.c_str(),
                                           strength);
        *out = rc == NC_OK ? Ok("") : Err("设置 LUT 失败，错误码 " + std::to_string(rc));
        return true;
    }

    // ---- AI 任务框架（core 管状态机，平台层执行推理）----
    // ---- 调整图层（B2-6：效果/LUT 挂在轨道上，作用于下方所有轨道）----
    if (op == "nc_adjustment_add_effect") {
        // uint64_t nc_adjustment_add_effect(editor, track_index, type, value)
        const uint64_t id = nc_adjustment_add_effect(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<int32_t>(JInt(a, "type", 0)), JNum(a, "value", 1.0));
        *out = id ? Ok("{\"value\":" + std::to_string(id) + "}") : Err("添加调整效果失败");
        return true;
    }
    if (op == "nc_adjustment_remove_effect") {
        // int32_t nc_adjustment_remove_effect(editor, track_index, effect_id)
        const int32_t rc = nc_adjustment_remove_effect(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<uint64_t>(JInt(a, "effect_id", 0)));
        *out = rc == NC_OK ? Ok("") : Err("删除调整效果失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_adjustment_set_lut") {
        // int32_t nc_adjustment_set_lut(editor, track_index, path, strength)
        // path 为空 = 清除 LUT。
        const std::string path = JStrField(a, "path");
        const int32_t rc = nc_adjustment_set_lut(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            path.empty() ? nullptr : path.c_str(), JNum(a, "strength", 1.0));
        *out = rc == NC_OK ? Ok("") : Err("设置调整图层 LUT 失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_adjustment_get") {
        // int32_t nc_adjustment_get(editor, track_index, out_effect_count, out_has_lut,
        //                           out_lut_strength, out_lut_path)
        int32_t count = 0, hasLut = 0;
        double strength = 0.0;
        char* path = nullptr;
        const int32_t rc = nc_adjustment_get(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)), &count, &hasLut,
            &strength, &path);
        if (rc != NC_OK) {
            *out = Err("读取调整图层失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string j = "{\"effect_count\":" + std::to_string(count);
        j += ",\"has_lut\":" + std::to_string(hasLut);
        j += ",\"lut_strength\":" + Num(strength);
        j += ",\"lut_path\":\"" + JEsc(path ? path : "") + "\"}";
        if (path) nc_free_string(path);
        *out = Ok(j);
        return true;
    }
    // ---- 素材 / 片段（编辑器导入与插入链路必需）----
    if (op == "nc_remove_asset") {
        // int32_t nc_remove_asset(editor, asset_id)
        const uint64_t id = static_cast<uint64_t>(JInt(a, "asset_id", 0));
        const int32_t rc = nc_remove_asset(ed, id);
        *out = rc == NC_OK ? Ok("") : Err("删除素材失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_asset_is_referenced") {
        // int32_t nc_asset_is_referenced(editor, asset_id)
        // 返回 1=已被片段引用 0=未引用 -1=参数错误。
        // 注意：这与 Ok/Err 的 ok 语义不同，不能把 rc 直接当状态码；
        // -1 才走 Err，0/1 都是有效结果，原样放进 result。
        const uint64_t id = static_cast<uint64_t>(JInt(a, "asset_id", 0));
        const int32_t rc = nc_asset_is_referenced(ed, id);
        if (rc < 0) {
            *out = Err("查询素材引用失败，错误码 " + std::to_string(rc));
            return true;
        }
        *out = Ok("{\"referenced\":" + std::to_string(rc) +
                  ",\"value\":" + std::to_string(rc) + "}");
        return true;
    }
    if (op == "nc_asset_reorder") {
        // int32_t nc_asset_reorder(editor, from_index, to_index)
        // to_index 是移动后的目标位置；from == to 由 core 当 no-op。
        const int32_t rc = nc_asset_reorder(
            ed, static_cast<int32_t>(JInt(a, "from_index", 0)),
            static_cast<int32_t>(JInt(a, "to_index", 0)));
        *out = rc == NC_OK ? Ok("") : Err("素材排序失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_set_track_view") {
        // int32_t nc_set_track_view(editor, track_index, height_px, collapsed)
        const int32_t rc = nc_set_track_view(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<int32_t>(JInt(a, "height_px", 0)),
            static_cast<int32_t>(JInt(a, "collapsed", 0)));
        *out = rc == NC_OK ? Ok("") : Err("调整轨道视图失败，错误码 " + std::to_string(rc));
        return true;
    }
    /* 调整图层的效果操作（P-19）。
     * 与片段效果（effect_add/effect_update/...）是两套东西：
     * 片段效果挂在 Clip 上，这两个挂在 Track::adjustmentEffects 上。
     * 因此不复用同一组 op 名，避免前端传 clip 却打到轨道上的歧义。 */
    if (op == "adjust_effect_add") {
        // uint64_t nc_adjustment_add_effect(editor, track_index, type, value)
        const uint64_t eid = nc_adjustment_add_effect(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<int32_t>(JInt(a, "type", 0)), JNum(a, "value", 1.0));
        *out = eid ? Ok("{\"effect_id\":" + std::to_string(eid) + "}") : Err("添加调整效果失败");
        return true;
    }
    if (op == "adjust_effect_remove") {
        // int32_t nc_adjustment_remove_effect(editor, track_index, effect_id)
        const int32_t rc = nc_adjustment_remove_effect(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<uint64_t>(JInt(a, "effect_id", 0)));
        *out = rc == NC_OK ? Ok("") : Err("删除调整效果失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "adjust_effect_get") {
        // int32_t nc_adjustment_effect_get(editor, track_index, index,
        //                                  out_effect_id, out_type, out_value, out_enabled)
        uint64_t eid = 0;
        int32_t etype = 0, een = 0;
        double eval = 0.0;
        const int32_t rc = nc_adjustment_effect_get(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<int32_t>(JInt(a, "index", 0)),
            &eid, &etype, &eval, &een);
        if (rc != NC_OK) {
            *out = Err("读取调整效果失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string j = "{\"effect_id\":" + std::to_string(eid);
        j += ",\"type\":" + std::to_string(etype);
        j += ",\"value\":" + Num(eval);
        j += ",\"enabled\":" + std::to_string(een) + "}";
        *out = Ok(j);
        return true;
    }
    if (op == "adjust_lut") {
        // int32_t nc_adjustment_set_lut(editor, track_index, path, strength)
        // path 为空 = 清除该轨道的 LUT。
        const std::string lp = JStrField(a, "path");
        const int32_t rc = nc_adjustment_set_lut(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            lp.empty() ? nullptr : lp.c_str(), JNum(a, "strength", 1.0));
        *out = rc == NC_OK ? Ok("") : Err("设置调整图层 LUT 失败，错误码 " + std::to_string(rc));
        return true;
    }
    /* 轨道组（B2-7）。创建时前端传轨道下标数组，这里转成 int32_t 序列交给 ABI。 */
    if (op == "group_create") {
        // int32_t nc_create_track_group(editor, track_indices, count, name, out_group_id)
        /* 用仓库既有的 JVecU64 读整数数组。
           不要用 JStrField —— 它只处理带引号的字符串值，
           遇到 [ 会直接返回空串，导致「永远未选择轨道」。 */
        std::vector<uint64_t> raw;
        if (!JVecU64(a, "track_indices", &raw)) {
            *out = Err("未选择轨道");
            return true;
        }
        std::vector<int32_t> idxs;
        idxs.reserve(raw.size());
        for (uint64_t v : raw) {
            if (v > 0x7fffffffULL) {
                *out = Err("轨道下标超出范围");
                return true;
            }
            idxs.push_back(static_cast<int32_t>(v));
        }
        uint64_t gid = 0;
        const std::string gname = JStrField(a, "name");
        const int32_t rc = nc_create_track_group(
            ed, idxs.data(), static_cast<int32_t>(idxs.size()),
            gname.empty() ? nullptr : gname.c_str(), &gid);
        *out = rc == NC_OK ? Ok("{\"group_id\":" + std::to_string(gid) + "}")
                           : Err("创建轨道组失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "group_remove") {
        const int32_t rc = nc_remove_track_group(
            ed, static_cast<uint64_t>(JInt(a, "group_id", 0)));
        *out = rc == NC_OK ? Ok("") : Err("解组失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "group_flag") {
        /* 组内联动开关。flag 决定调哪个 ABI —— 三个函数签名一致，
           只有语义不同，用 name 分支比开三个 op 名更省事。 */
        const uint64_t gid = static_cast<uint64_t>(JInt(a, "group_id", 0));
        const std::string flag = JStrField(a, "flag");
        const int32_t val = static_cast<int32_t>(JInt(a, "value", 0));
        int32_t rc = 0;
        if (flag == "hidden")      rc = nc_set_group_hidden(ed, gid, val);
        else if (flag == "muted")  rc = nc_set_group_muted(ed, gid, val);
        else if (flag == "locked") rc = nc_set_group_locked(ed, gid, val);
        else {
            *out = Err("未知的组标志：" + flag);
            return true;
        }
        *out = rc == NC_OK ? Ok("") : Err("设置组标志失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_track_accepts_media") {
        // int32_t nc_track_accepts_media(track_type, media_type) —— 无 editor 参数
        // 1 = 可放；0 = 拒绝。供拖动过程实时反馈，避免「拖完才报错」。
        const int32_t ok = nc_track_accepts_media(
            static_cast<int32_t>(JInt(a, "track_type", 0)),
            static_cast<int32_t>(JInt(a, "media_type", 0)));
        *out = Ok("{\"value\":" + std::to_string(ok) + "}");
        return true;
    }
    if (op == "nc_track_placement_reason") {
        // int32_t nc_track_placement_reason(track_type, media_type, out_reason, out_error)
        char* reason = nullptr;
        char* error = nullptr;
        const int32_t rc = nc_track_placement_reason(
            static_cast<int32_t>(JInt(a, "track_type", 0)),
            static_cast<int32_t>(JInt(a, "media_type", 0)), &reason, &error);
        std::string j = "{\"code\":" + std::to_string(rc);
        j += ",\"reason\":\"" + JEsc(reason ? reason : "") + "\"";
        j += ",\"message\":\"" + JEsc(error ? error : "") + "\"}";
        if (reason) nc_free_string(reason);
        if (error) nc_free_string(error);
        *out = Ok(j);
        return true;
    }
    if (op == "nc_move_track") {
        // int32_t nc_move_track(editor, track_index, new_index)
        // 锁定轨道拒绝；越界返回 NC_ERROR_OUT_OF_RANGE（由内核判定，这里只转达）。
        const int32_t rc = nc_move_track(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<int32_t>(JInt(a, "new_index", 0)));
        *out = rc == NC_OK ? Ok("") : Err("调整轨道顺序失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_blend_mode_count") {
        // int32_t nc_blend_mode_count(void) —— 无 editor 参数
        *out = Ok("{\"value\":" + std::to_string(nc_blend_mode_count()) + "}");
        return true;
    }
    if (op == "nc_blend_mode_name") {
        // const char* nc_blend_mode_name(int32_t mode) —— 静态字符串，不 free
        const char* nm = nc_blend_mode_name(static_cast<int32_t>(JInt(a, "mode", 0)));
        *out = Ok("{\"value\":\"" + JEsc(nm ? nm : "") + "\"}");
        return true;
    }
    if (op == "nc_transition_type_count") {
        // int32_t nc_transition_type_count(void) —— 无 editor 参数
        *out = Ok("{\"value\":" + std::to_string(nc_transition_type_count()) + "}");
        return true;
    }
    if (op == "nc_transition_type_name") {
        // const char* nc_transition_type_name(int32_t type) —— 静态字符串
        const char* nm = nc_transition_type_name(
            static_cast<int32_t>(JInt(a, "type", 0)));
        *out = Ok("{\"value\":\"" + JEsc(nm ? nm : "") + "\"}");
        return true;
    }
    if (op == "nc_transition_category") {
        // const char* nc_transition_category(int32_t type) —— 静态字符串
        const char* cat = nc_transition_category(
            static_cast<int32_t>(JInt(a, "type", 0)));
        *out = Ok("{\"value\":\"" + JEsc(cat ? cat : "") + "\"}");
        return true;
    }
    if (op == "nc_template_list") {
        // int32_t nc_template_list(out_json, out_error)
        // 无 editor 参数；返回内置模板数组的 JSON 文本，前端期望 result 是数组。
        char* json = nullptr;
        char* err = nullptr;
        const int32_t rc = nc_template_list(&json, &err);
        if (rc != NC_OK) {
            const std::string msg = err ? std::string(err) : ("错误码 " + std::to_string(rc));
            if (err) nc_free_string(err);
            if (json) nc_free_string(json);
            *out = Err("读取模板列表失败：" + msg);
            return true;
        }
        // out_json 本身就是数组文本，直接作为 result 内联（不再包一层字符串，
        // 否则前端要二次 JSON.parse，与其 Array.isArray 的用法不符）。
        *out = Ok(json ? std::string(json) : std::string("[]"));
        if (err) nc_free_string(err);
        if (json) nc_free_string(json);
        return true;
    }
    if (op == "nc_template_apply") {
        // int32_t nc_template_apply(editor, template_id, material_ids, count,
        //                           out_summary, out_error)
        const std::string templateId = JStrField(a, "template_id");
        if (templateId.empty()) {
            *out = Err("template_id 为空");
            return true;
        }
        std::vector<uint64_t> ids;
        if (!JVecU64(a, "material_ids", &ids) || ids.empty()) {
            *out = Err("material_ids 为空");
            return true;
        }
        int32_t count = static_cast<int32_t>(JInt(a, "count", static_cast<int64_t>(ids.size())));
        if (count <= 0) count = static_cast<int32_t>(ids.size());
        if (count > static_cast<int32_t>(ids.size())) count = static_cast<int32_t>(ids.size());
        char* summary = nullptr;
        char* err = nullptr;
        const int32_t rc = nc_template_apply(ed, templateId.c_str(), ids.data(), count,
                                             &summary, &err);
        if (rc != NC_OK) {
            const std::string msg = err ? std::string(err)
                                        : ("错误码 " + std::to_string(rc));
            if (err) nc_free_string(err);
            if (summary) nc_free_string(summary);
            *out = Err("套用模板失败：" + msg);
            return true;
        }
        *out = Ok("{\"summary\":\"" + JEsc(summary ? summary : "") + "\"}");
        if (err) nc_free_string(err);
        if (summary) nc_free_string(summary);
        return true;
    }
    if (op == "nc_lut_validate_file") {
        // int32_t nc_lut_validate_file(path, out_size, out_title, out_message)
        // 无 editor 参数；只读文件，不改工程状态。
        const std::string path = JStrField(a, "path");
        if (path.empty()) {
            *out = Err("LUT 路径为空");
            return true;
        }
        int32_t size = 0;
        char* title = nullptr;
        char* message = nullptr;
        const int32_t rc = nc_lut_validate_file(path.c_str(), &size, &title, &message);
        if (rc != NC_OK) {
            const std::string msg = message ? std::string(message)
                                            : ("错误码 " + std::to_string(rc));
            *out = Err("LUT 校验失败：" + msg);
        } else {
            std::string j = "{\"size\":" + std::to_string(size);
            j += ",\"title\":\"" + JEsc(title ? title : "") + "\"";
            j += ",\"message\":\"" + JEsc(message ? message : "") + "\"}";
            *out = Ok(j);
        }
        if (title) nc_free_string(title);
        if (message) nc_free_string(message);
        return true;
    }
    if (op == "nc_import_asset") {
        // uint64_t nc_import_asset(editor, name, path, type, duration_us)
        // 返回素材 ID（0 = 失败）
        const uint64_t id = nc_import_asset(
            ed, JStrField(a, "name").c_str(), JStrField(a, "path").c_str(),
            static_cast<int32_t>(JInt(a, "type", 0)),
            static_cast<int64_t>(JInt(a, "duration_us", 0)));
        *out = id ? Ok("{\"value\":" + std::to_string(id) + "}") : Err("导入素材失败");
        return true;
    }
    if (op == "nc_insert_clip") {
        // uint64_t nc_insert_clip(editor, track_index, asset_id, start_us, duration_us)
        const uint64_t id = nc_insert_clip(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            static_cast<uint64_t>(JInt(a, "asset_id", 0)),
            static_cast<int64_t>(JInt(a, "start_us", 0)),
            static_cast<int64_t>(JInt(a, "duration_us", 0)));
        *out = id ? Ok("{\"value\":" + std::to_string(id) + "}") : Err("插入片段失败");
        return true;
    }
    if (op == "nc_scene_detect") {
        // int32_t nc_scene_detect(editor, step_seconds, max_samples,
        //                         out_boundary_count, double** out_boundaries)
        int32_t count = 0;
        double* bounds = nullptr;
        const int32_t rc = nc_scene_detect(
            ed, JNum(a, "step_seconds", 0.5),
            static_cast<int32_t>(JInt(a, "max_samples", 240)), &count, &bounds);
        if (rc != NC_OK) {
            *out = Err("场景检测失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string j = "{\"boundary_count\":" + std::to_string(count) + ",\"boundaries\":[";
        for (int32_t i = 0; i < count && bounds; ++i) {
            if (i) j += ",";
            j += Num(bounds[i]);
        }
        j += "]}";
        if (bounds) nc_free_buffer(reinterpret_cast<uint8_t*>(bounds));
        *out = Ok(j);
        return true;
    }
    if (op == "nc_beat_detect") {
        // int32_t nc_beat_detect(editor, track_index, out_bpm, out_confidence,
        //                        out_beat_count, double** out_beats)
        int32_t count = 0;
        double bpm = 0.0, confidence = 0.0;
        double* beats = nullptr;
        const int32_t rc = nc_beat_detect(
            ed, static_cast<int32_t>(JInt(a, "track_index", 0)),
            &bpm, &confidence, &count, &beats);
        if (rc != NC_OK) {
            *out = Err("节拍检测失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string j = "{\"bpm\":" + Num(bpm) + ",\"confidence\":" + Num(confidence);
        j += ",\"beat_count\":" + std::to_string(count) + ",\"beats\":[";
        for (int32_t i = 0; i < count && beats; ++i) {
            if (i) j += ",";
            j += Num(beats[i]);
        }
        j += "]}";
        if (beats) nc_free_buffer(reinterpret_cast<uint8_t*>(beats));
        *out = Ok(j);
        return true;
    }
    if (op == "shake_set") {
        /* 片段摇晃（B1-5 / B2-3）。六维参数一次写入，可撤销。 */
        const int32_t rc = nc_set_clip_shake(
            ed, ti, cid,
            static_cast<int32_t>(JInt(a, "trajectory", 0)),
            JNum(a, "amplitude", 0.0),
            JNum(a, "frequency", 8.0),
            JNum(a, "phase", 0.0),
            JNum(a, "decay_seconds", 0.0),
            static_cast<uint32_t>(JInt(a, "seed", 0)));
        *out = rc == NC_OK ? Ok("") : Err("设置摇晃失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "xform_set") {
        /* 几何变换（「转换」8 项）。13 个参数走扁平数组，
           避免形参过多在调用点错位。
           注意：本文件用自己的字符串版 JSON 工具（JNum/JInt/JVecDouble），
           不是 core 的 JsonValue —— 别混用。 */
        double xargs[NC_XFORM_ARG_COUNT] = {0.0};
        std::vector<double> raw;
        if (JVecDouble(a, "args", &raw)) {
            for (std::size_t i = 0;
                 i < static_cast<std::size_t>(NC_XFORM_ARG_COUNT) && i < raw.size(); ++i) {
                xargs[i] = raw[i];
            }
        }
        const int32_t rc = nc_set_clip_xform(ed, ti, cid,
                                             static_cast<int32_t>(JInt(a, "kind", 0)),
                                             xargs);
        *out = rc == NC_OK ? Ok("") : Err("设置几何变换失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "xform_get") {
        double xargs[NC_XFORM_ARG_COUNT] = {0.0};
        int32_t enabled = 0;
        int32_t edge = 1;
        const int32_t rc = nc_get_clip_xform(ed, ti, cid,
                                             static_cast<int32_t>(JInt(a, "kind", 0)),
                                             xargs, &enabled, &edge);
        if (rc != NC_OK) {
            *out = Err("读取几何变换失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string js = "{\"enabled\":" + std::to_string(enabled) +
                         ",\"edge\":" + std::to_string(edge) + ",\"args\":[";
        for (int i = 0; i < NC_XFORM_ARG_COUNT; ++i) {
            if (i > 0) js += ",";
            js += Num(xargs[i]);
        }
        js += "]}";
        *out = Ok(js);
        return true;
    }
    if (op == "xform_clear") {
        const int32_t rc = nc_clear_clip_xform(ed, ti, cid,
                                               static_cast<int32_t>(JInt(a, "kind", 0)));
        *out = rc == NC_OK ? Ok("") : Err("清除几何变换失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "shake_get") {
        int32_t traj = 0;
        double amp = 0.0, freq = 0.0, ph = 0.0, decay = 0.0;
        uint32_t seed = 0;
        const int32_t rc = nc_get_clip_shake(ed, ti, cid, &traj, &amp, &freq, &ph,
                                             &decay, &seed);
        if (rc != NC_OK) {
            *out = Err("读取摇晃失败，错误码 " + std::to_string(rc));
            return true;
        }
        *out = Ok("{\"trajectory\":" + std::to_string(traj) +
                  ",\"amplitude\":" + Num(amp) +
                  ",\"frequency\":" + Num(freq) +
                  ",\"phase\":" + Num(ph) +
                  ",\"decay_seconds\":" + Num(decay) +
                  ",\"seed\":" + std::to_string(seed) + "}");
        return true;
    }
    if (op == "beat_sync") {
        /* 音乐卡点落盘：一步完成「检测节拍 → 按节拍切分并写入空轨」。
           素材用 JVecU64 取 id 数组，时长数组另解析（JVecU64 只收整数）。 */
        std::vector<uint64_t> ids;
        if (!JVecU64(a, "asset_ids", &ids) || ids.empty()) {
            *out = Err("没有可用素材");
            return true;
        }
        std::vector<double> secs;
        if (!JVecDouble(a, "usable_seconds", &secs)) {
            *out = Err("素材时长数组缺失");
            return true;
        }
        if (secs.size() < ids.size()) {
            secs.resize(ids.size(), 3.0);  // 兜底：时长缺失时按 3 秒估
        }
        int32_t clips = 0;
        double bpm = 0.0;
        char* warning = nullptr;
        char* error = nullptr;
        const int32_t rc = nc_beat_sync_apply(
            ed,
            static_cast<int32_t>(JInt(a, "target_track", 0)),
            static_cast<int32_t>(JInt(a, "audio_track", -1)),
            ids.data(), secs.data(), static_cast<int32_t>(ids.size()),
            JNum(a, "min_shot_seconds", 0.25),
            static_cast<int32_t>(JInt(a, "prefer_transitions", 1)),
            &clips, &bpm, &warning, &error);
        if (rc != NC_OK) {
            const std::string msg = error ? error : ("卡点失败，错误码 " + std::to_string(rc));
            if (warning) nc_free_string(warning);
            if (error) nc_free_string(error);
            *out = Err(msg);
            return true;
        }
        std::string j = "{\"clips\":" + std::to_string(clips);
        j += ",\"bpm\":" + Num(bpm);
        j += ",\"warning\":\"" + JEsc(warning ? warning : "") + "\"}";
        if (warning) nc_free_string(warning);
        if (error) nc_free_string(error);
        *out = Ok(j);
        return true;
    }
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
    if (op == "nc_keyframe_count") {
        // int32_t nc_keyframe_count(editor, track_index, clip_id, property)
        // property 0..5：不透明度=0 缩放=1 位置X=2 位置Y=3 旋转=4
        const int32_t rc = nc_keyframe_count(
            ed, ti, cid, static_cast<int32_t>(JInt(a, "property", 0)));
        if (rc < 0) {
            *out = Err("读取关键帧数量失败，错误码 " + std::to_string(rc));
            return true;
        }
        *out = Ok("{\"count\":" + std::to_string(rc) + "}");
        return true;
    }
    if (op == "nc_keyframe_get") {
        // int32_t nc_keyframe_get(editor, track_index, clip_id, property, index,
        //                         out_time_us, out_value, out_easing)
        int64_t tUs = 0;
        double val = 0.0;
        int32_t easing = 0;
        const int32_t rc = nc_keyframe_get(
            ed, ti, cid, static_cast<int32_t>(JInt(a, "property", 0)),
            static_cast<int32_t>(JInt(a, "index", 0)), &tUs, &val, &easing);
        if (rc != NC_OK) {
            *out = Err("读取关键帧失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string j = "{\"time_us\":" + std::to_string(tUs);
        j += ",\"value\":" + Num(val);
        j += ",\"easing\":" + std::to_string(easing) + "}";
        *out = Ok(j);
        return true;
    }
    if (op == "nc_remove_keyframe") {
        // int32_t nc_remove_keyframe(editor, track_index, clip_id, property, time_us)
        const int32_t rc = nc_remove_keyframe(
            ed, ti, cid, static_cast<int32_t>(JInt(a, "property", 0)),
            JInt(a, "time_us", 0));
        *out = rc == NC_OK ? Ok("") : Err("删除关键帧失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_set_background_color") {
        // int32_t nc_set_background_color(editor, uint32_t rgba)
        // JS 侧传 0xRRGGBBAA 打包的无符号整数。
        const uint32_t rgba = static_cast<uint32_t>(JInt(a, "rgba", 0));
        const int32_t rc = nc_set_background_color(ed, rgba);
        *out = rc == NC_OK ? Ok("") : Err("设置背景色失败，错误码 " + std::to_string(rc));
        return true;
    }
    if (op == "nc_preview_cache_stats") {
        // int32_t nc_preview_cache_stats(editor, out_hits, out_misses,
        //                                out_reader_opens, out_cached_frames)
        // 供前端渲染按钮显示真实命中率，不做假进度。
        int64_t hits = 0, misses = 0, opens = 0;
        int32_t cached = 0, capacity = 0;
        const int32_t rc = nc_preview_cache_stats(ed, &hits, &misses, &opens, &cached, &capacity);
        if (rc != NC_OK) {
            *out = Err("读取预览缓存统计失败，错误码 " + std::to_string(rc));
            return true;
        }
        std::string j = "{\"frame_hits\":" + std::to_string(hits);
        j += ",\"frame_misses\":" + std::to_string(misses);
        j += ",\"reader_opens\":" + std::to_string(opens);
        j += ",\"cached_frames\":" + std::to_string(cached);
        j += ",\"capacity\":" + std::to_string(capacity) + "}";
        *out = Ok(j);
        return true;
    }
    if (op == "nc_preview_invalidate") {
        nc_preview_invalidate(ed);
        *out = Ok("");
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

    /* 打包成 [w:int32][h:int32][BGRA...] 交给 Kotlin。
       ----------------------------------------------------------------
       **为什么在这里换成 BGRA 而不是交给 Kotlin 逐像素换**：

         · core 输出 RGBA8（0=R 1=G 2=B 3=A）
         · Android Bitmap.Config.ARGB_8888 的 int 是 0xAARRGGBB，
           小端内存字节序是 **B,G,R,A**
         ⇒ Kotlin 若直接 copyPixelsFromBuffer(RGBA) 会红蓝颠倒。
         Kotlin 侧做逐像素换序要 200 万次 boxed Int 操作（1080p），
         实测占预览链路 15~20%；这里用 NEON 一次换 16 像素，
         快一个量级，而且 Kotlin 侧退化成一次 memcpy。

       rgbaToBgraInPlace 在 x86 上自动走标量路径，ARM 上走 NEON，
       两条路径 bit-exact（见 simd.cpp 的注释）。 */
    nairoclip::core::rgbaToBgraInPlace(rgba, static_cast<std::size_t>(w) *
                                                 static_cast<std::size_t>(hgt));

    // 打包成 [w:int32][h:int32][BGRA...] 交给 Kotlin
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
