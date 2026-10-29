// =============================================================================
// NairoClip 设计验证桥（Design Harness）
// =============================================================================
// 目的：让 UI/UX 设计稿不是「画出来的假界面」，而是由真实引擎驱动的可交互原型。
//
//   · 所有时间轴数据来自 core 的 C ABI（capi.h），不是前端写死的 JSON；
//   · 预览帧来自 nc_preview_render（真实解码 + 特效/滤镜/蒙版/转场）；
//   · 时间轴胶片条来自 nc_thumbnail_generate（真实解码）；
//   · 音频波形来自 nc_waveform_generate（真实解码 + 桶化）；
//   · 拖拽/裁剪/分割/撤销等操作直接调用 C ABI，改的是真实工程状态；
//   · 导出调用 nc_export_run，写真实 MP4，进度由引擎回调回填。
//
// 仅用于设计评审与平台层对接参考，不进入产品运行时。
//
// 构建：cmake --build build --target nc_design_harness
// 运行：./build/design/tools/nc_design_harness [port] [design_root]
// =============================================================================

#include "nairoclip/core/capi.h"
#include "nairoclip/core/json.h"
#ifdef NAIROCLIP_HAS_FFMPEG
#include "nairoclip/core/media_ffmpeg.h"
#endif

#include <zlib.h>

#include <dirent.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using nairoclip::core::JsonParseError;
using nairoclip::core::JsonValue;
using nairoclip::core::parseJson;
using nairoclip::core::toJsonString;

namespace {

std::string rootDir;
/// 仓库根目录：用于 `/m/...` 这类跨目录静态资源（桌面页引用 mobile/assets）。
std::string repoRoot;

std::string readFile(const std::string& path, bool& ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { ok = false; return {}; }
    std::ostringstream ss;
    ss << in.rdbuf();
    ok = true;
    return ss.str();
}

std::string jsonEscape(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string takeString(char* text) {
    if (text == nullptr) return {};
    std::string out(text);
    nc_free_string(text);
    return out;
}

// ---- PNG 编码（仅依赖 zlib）----

void putBE32(std::string& out, std::uint32_t v) {
    out.push_back(static_cast<char>((v >> 24) & 0xFF));
    out.push_back(static_cast<char>((v >> 16) & 0xFF));
    out.push_back(static_cast<char>((v >> 8) & 0xFF));
    out.push_back(static_cast<char>(v & 0xFF));
}

void putChunk(std::string& out, const char* type, const std::string& data) {
    putBE32(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t crcStart = out.size();
    out += type;
    out += data;
    const uLong crc = crc32(crc32(0L, Z_NULL, 0),
                            reinterpret_cast<const Bytef*>(out.data() + crcStart),
                            static_cast<uInt>(out.size() - crcStart));
    putBE32(out, static_cast<std::uint32_t>(crc));
}

std::string encodePng(const std::uint8_t* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) return {};
    std::string raw;
    raw.reserve(static_cast<std::size_t>(height) * (1 + static_cast<std::size_t>(width) * 3));
    for (int y = 0; y < height; ++y) {
        raw.push_back('\0');
        const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            raw.push_back(static_cast<char>(row[x * 4 + 0]));
            raw.push_back(static_cast<char>(row[x * 4 + 1]));
            raw.push_back(static_cast<char>(row[x * 4 + 2]));
        }
    }
    uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    std::string compressed(bound, '\0');
    if (compress2(reinterpret_cast<Bytef*>(&compressed[0]), &bound,
                  reinterpret_cast<const Bytef*>(raw.data()),
                  static_cast<uLong>(raw.size()), Z_BEST_SPEED) != Z_OK) {
        return {};
    }
    compressed.resize(bound);

    std::string png = "\x89PNG\r\n\x1a\n";
    std::string ihdr;
    putBE32(ihdr, static_cast<std::uint32_t>(width));
    putBE32(ihdr, static_cast<std::uint32_t>(height));
    ihdr.push_back(8);
    ihdr.push_back(2);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    putChunk(png, "IHDR", ihdr);
    putChunk(png, "IDAT", compressed);
    putChunk(png, "IEND", {});
    return png;
}

std::vector<std::uint8_t> resizeRgba(const std::uint8_t* src, int sw, int sh, int targetW) {
    if (src == nullptr || sw <= 0 || sh <= 0) return {};
    if (targetW <= 0 || targetW >= sw) {
        return std::vector<std::uint8_t>(src, src + static_cast<std::size_t>(sw) * sh * 4);
    }
    const int tw = targetW;
    const int th = std::max(1, (sh * tw) / sw);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(tw) * th * 4);
    for (int y = 0; y < th; ++y) {
        const int y0 = y * sh / th;
        const int y1 = std::max(y0 + 1, (y + 1) * sh / th);
        for (int x = 0; x < tw; ++x) {
            const int x0 = x * sw / tw;
            const int x1 = std::max(x0 + 1, (x + 1) * sw / tw);
            unsigned sum[3] = {0, 0, 0};
            unsigned count = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const std::uint8_t* row = src + (static_cast<std::size_t>(sy) * sw + x0) * 4;
                for (int sx = x0; sx < x1; ++sx) {
                    sum[0] += row[0];
                    sum[1] += row[1];
                    sum[2] += row[2];
                    row += 4;
                    ++count;
                }
            }
            std::uint8_t* dst = out.data() + (static_cast<std::size_t>(y) * tw + x) * 4;
            dst[0] = static_cast<std::uint8_t>(sum[0] / count);
            dst[1] = static_cast<std::uint8_t>(sum[1] / count);
            dst[2] = static_cast<std::uint8_t>(sum[2] / count);
            dst[3] = 255;
        }
    }
    return out;
}

// ---- 工程状态 ----

NcEditor* gEditor = nullptr;
std::mutex gMutex;

struct ThumbSet {
    int width = 0;
    int height = 0;
    std::vector<std::vector<std::uint8_t>> images;
};
std::map<std::uint64_t, ThumbSet> gThumbs;

struct WaveSet {
    double peak = 0.0;
    std::vector<double> rms;
};
std::map<std::uint64_t, WaveSet> gWaves;

const char* kProjectName = "未命名项目";

std::string assetPath(const std::string& file) { return rootDir + "/media/" + file; }

void seedProject() {
    gEditor = nc_editor_create();
    if (gEditor == nullptr) {
        std::fprintf(stderr, "错误：nc_editor_create 失败\n");
        std::exit(1);
    }
    /* 正式发布规格：**空工程**。
       不预置任何素材、轨道、片段、字幕、标记 —— 用户从「开始创作」进入后
       自己导入素材。这样既符合真实首启体验，也避免设计稿里的假数据被误当成品。
       画布与帧率给出默认值（1080x1920 / 30fps），这是新建工程的标准初始态。 */
    nc_set_project_name(gEditor, kProjectName);
    nc_set_project_canvas(gEditor, 1080, 1920);
    nc_set_project_frame_rate(gEditor, 30, 1);
}
/* ---- 素材缓存（缩略图 / 波形）----
   引擎（nc_*）非线程安全，解码一次要 10~30 秒。启动时批量预热会把
   /api/state 一起堵死（实测：8888 端口打开要 60s+，客户端以为服务挂了）。
   因此改为 **按需惰性生成**：第一次请求 /api/thumb 或 /api/waveform 时才解码，
   结果写入 gThumbs / gWaves 供后续复用。设计桥是评审宿主，这个延迟可接受。 */

/// 生成某素材的缩略图集（调用方不得持锁）。
bool buildThumbs(std::uint64_t assetId, int count = 8) {
    std::lock_guard<std::mutex> lock(gMutex);
    std::uint8_t* images = nullptr;
    static std::vector<std::int64_t> times(64, 0);
    int32_t got = 0;
    if (nc_thumbnail_generate(gEditor, assetId, count, 200, 112, &images, times.data(), &got) != NC_OK ||
        images == nullptr || got <= 0) {
        return false;
    }
    int w = 0, h = 0;
    nc_thumbnail_size(gEditor, &w, &h);
    ThumbSet set;
    set.width = w;
    set.height = h;
    const std::size_t per = static_cast<std::size_t>(w) * h * 4;
    for (int k = 0; k < got; ++k) {
        set.images.emplace_back(images + per * k, images + per * (k + 1));
    }
    gThumbs[assetId] = std::move(set);
    nc_free_buffer(images);
    return true;
}

/// 生成某素材的波形（调用方不得持锁）。
bool buildWave(std::uint64_t assetId, int buckets = 480) {
    std::lock_guard<std::mutex> lock(gMutex);
    double peak = 0.0;
    int32_t rate = 0;
    int32_t channels = 0;
    if (nc_waveform_generate(gEditor, assetId, buckets, &peak, &rate, &channels) != NC_OK) {
        return false;
    }
    WaveSet set;
    set.peak = peak;
    const int n = nc_waveform_bucket_count(gEditor);
    for (int b = 0; b < n; ++b) {
        double mn = 0.0, mx = 0.0, rms = 0.0;
        if (nc_waveform_bucket_get(gEditor, b, &mn, &mx, &rms) == NC_OK) {
            set.rms.push_back(rms);
        }
    }
    gWaves[assetId] = std::move(set);
    return true;
}

// ---- 状态序列化（全部真实读取 C ABI）----

std::string buildStateJson() {
    std::lock_guard<std::mutex> lock(gMutex);
    JsonValue root = JsonValue::object();

    JsonValue project = JsonValue::object();
    project.set("name", JsonValue::string(takeString(nc_project_name(gEditor))));
    int width = 0, height = 0;
    nc_project_canvas(gEditor, &width, &height);
    project.set("width", JsonValue::integer(width));
    project.set("height", JsonValue::integer(height));
    std::int64_t num = 30, den = 1;
    nc_project_frame_rate(gEditor, &num, &den);
    project.set("fps_num", JsonValue::integer(num));
    project.set("fps_den", JsonValue::integer(den));
    project.set("duration_us", JsonValue::integer(nc_project_duration_us(gEditor)));
    project.set("can_undo", JsonValue::boolean(nc_can_undo(gEditor) == 1));
    project.set("can_redo", JsonValue::boolean(nc_can_redo(gEditor) == 1));
    project.set("undo_count", JsonValue::integer(nc_undo_count(gEditor)));
    project.set("redo_count", JsonValue::integer(nc_redo_count(gEditor)));
    project.set("undo_name", JsonValue::string(
        nc_can_undo(gEditor) == 1 ? takeString(nc_undo_history_name(gEditor, 0)) : std::string()));
    project.set("entitlement", JsonValue::integer(nc_get_entitlement(gEditor)));
    int engineAvailable = 0;
    char* engineVersion = nullptr;
    char* engineNote = nullptr;
    nc_ai_engine_info(gEditor, &engineAvailable, &engineVersion, &engineNote);
    project.set("ai_engine_available", JsonValue::boolean(engineAvailable == 1));
    project.set("ai_engine_version", JsonValue::string(takeString(engineVersion)));
    project.set("ai_engine_note", JsonValue::string(takeString(engineNote)));
    root.set("project", std::move(project));

    JsonValue assets = JsonValue::array();
    const int assetCount = nc_asset_count(gEditor);
    for (int i = 0; i < assetCount; ++i) {
        std::uint64_t id = 0;
        char* name = nullptr;
        char* path = nullptr;
        int32_t type = 0;
        std::int64_t duration = 0;
        if (nc_asset_get(gEditor, i, &id, &name, &path, &type, &duration) != NC_OK) continue;
        JsonValue a = JsonValue::object();
        a.set("id", JsonValue::integer(static_cast<std::int64_t>(id)));
        a.set("name", JsonValue::string(takeString(name)));
        const std::string pathText = takeString(path);
        const std::size_t slash = pathText.find_last_of('/');
        a.set("file", JsonValue::string(slash == std::string::npos ? pathText : pathText.substr(slash + 1)));
        a.set("type", JsonValue::integer(type));
        a.set("duration_us", JsonValue::integer(duration));
        a.set("has_thumbs", JsonValue::boolean(gThumbs.count(id) > 0));
        a.set("has_waveform", JsonValue::boolean(gWaves.count(id) > 0));
        assets.push(std::move(a));
    }
    root.set("assets", std::move(assets));

    JsonValue tracks = JsonValue::array();
    const int trackCount = nc_track_count(gEditor);
    for (int t = 0; t < trackCount; ++t) {
        std::uint64_t trackId = 0;
        char* name = nullptr;
        int32_t type = 0, locked = 0, muted = 0, hidden = 0, solo = 0;
        double gain = 0.0;
        if (nc_track_get(gEditor, t, &trackId, &name, &type, &locked, &muted, &hidden, &solo,
                         &gain) != NC_OK) {
            continue;
        }
        JsonValue track = JsonValue::object();
        track.set("index", JsonValue::integer(t));
        track.set("id", JsonValue::integer(static_cast<std::int64_t>(trackId)));
        track.set("name", JsonValue::string(takeString(name)));
        track.set("type", JsonValue::integer(type));
        track.set("locked", JsonValue::boolean(locked == 1));
        track.set("muted", JsonValue::boolean(muted == 1));
        track.set("hidden", JsonValue::boolean(hidden == 1));
        track.set("solo", JsonValue::boolean(solo == 1));
        track.set("gain_db", JsonValue::number(gain));

        // 转场：UI 需要在时间轴上把已存在的转场画成交界标记
        {
            JsonValue trs = JsonValue::array();
            const int tc = nc_transition_count(gEditor, t);
            for (int ti = 0; ti < tc; ++ti) {
                int32_t ttype = 0;
                int64_t tdur = 0;
                std::uint64_t tfrom = 0, tto = 0;
                char* tname = nullptr;
                if (nc_transition_get(gEditor, t, ti, &ttype, &tdur, &tfrom, &tto, &tname) != NC_OK) {
                    continue;
                }
                JsonValue tr = JsonValue::object();
                // id 用于删除/编辑；nc_transition_get 不下发，改从核心层直读
                // 转场 id：删除/编辑需要，走 nc_transition_get_id
                {
                    std::uint64_t tid = 0;
                    if (nc_transition_get_id(gEditor, t, ti, &tid) == NC_OK) {
                        tr.set("id", JsonValue::integer(static_cast<std::int64_t>(tid)));
                    }
                }
                tr.set("type", JsonValue::integer(ttype));
                tr.set("duration_us", JsonValue::integer(tdur));
                tr.set("from", JsonValue::integer(static_cast<std::int64_t>(tfrom)));
                tr.set("to", JsonValue::integer(static_cast<std::int64_t>(tto)));
                tr.set("name", JsonValue::string(takeString(tname)));
                trs.push(std::move(tr));
            }
            track.set("transitions", std::move(trs));
        }
        // B2-10：高度与折叠状态。UI 需要在折叠态下仍能长按搬动轨道，
        // 因此这两个值必须提供给前端，否则前端只能猜。
        {
            int32_t hpx = 48;
            int32_t col = 0;
            if (nc_track_get_view(gEditor, t, &hpx, &col) == NC_OK) {
                track.set("height_px", JsonValue::integer(hpx));
                track.set("collapsed", JsonValue::boolean(col == 1));
            }
        }

        JsonValue clips = JsonValue::array();
        const int clipCount = nc_clip_count(gEditor, t);
        for (int c = 0; c < clipCount; ++c) {
            std::uint64_t clipId = 0, assetId = 0;
            std::int64_t start = 0, duration = 0, speedNum = 1, speedDen = 1;
            int32_t enabled = 1;
            if (nc_clip_info(gEditor, t, c, &clipId, &start, &duration, &assetId, &speedNum,
                             &speedDen, &enabled) != NC_OK) {
                continue;
            }
            JsonValue clip = JsonValue::object();
            clip.set("id", JsonValue::integer(static_cast<std::int64_t>(clipId)));
            clip.set("index", JsonValue::integer(c));
            clip.set("start_us", JsonValue::integer(start));
            clip.set("duration_us", JsonValue::integer(duration));
            clip.set("asset_id", JsonValue::integer(static_cast<std::int64_t>(assetId)));
            clip.set("speed_num", JsonValue::integer(speedNum));
            clip.set("speed_den", JsonValue::integer(speedDen));
            clip.set("enabled", JsonValue::boolean(enabled == 1));
            double opacity = 1.0;
            nc_get_clip_opacity(gEditor, t, clipId, &opacity);
            clip.set("opacity", JsonValue::number(opacity));
            int32_t style = 0;
            double intensity = 0.0;
            const int hasFilter = nc_get_clip_filter(gEditor, t, clipId, &style, &intensity);
            clip.set("filter_style", JsonValue::integer(hasFilter == 1 ? style : 0));
            clip.set("filter_intensity", JsonValue::number(hasFilter == 1 ? intensity : 0.0));
            clip.set("blend_mode", JsonValue::integer(nc_get_clip_blend_mode(gEditor, t, clipId)));
            clip.set("reversed", JsonValue::boolean(nc_clip_is_reversed(gEditor, t, clipId) == 1));
            clip.set("frozen", JsonValue::boolean(nc_clip_is_frozen(gEditor, t, clipId) == 1));
            clip.set("effect_count", JsonValue::integer(nc_effect_count(gEditor, t, clipId)));
            clip.set("is_compound", JsonValue::boolean(nc_clip_is_compound(gEditor, t, clipId) == 1));
            clips.push(std::move(clip));
        }
        track.set("clips", std::move(clips));
        tracks.push(std::move(track));
    }
    root.set("tracks", std::move(tracks));

    JsonValue subtitles = JsonValue::array();
    const int subtitleCount = nc_subtitle_count(gEditor);
    for (int i = 0; i < subtitleCount; ++i) {
        std::int64_t start = 0, duration = 0;
        char* text = nullptr;
        if (nc_subtitle_get(gEditor, i, &start, &duration, &text) != NC_OK) continue;
        JsonValue cue = JsonValue::object();
        cue.set("start_us", JsonValue::integer(start));
        cue.set("duration_us", JsonValue::integer(duration));
        cue.set("text", JsonValue::string(takeString(text)));
        subtitles.push(std::move(cue));
    }
    root.set("subtitles", std::move(subtitles));

    JsonValue markers = JsonValue::array();
    const int markerCount = nc_marker_count(gEditor);
    for (int i = 0; i < markerCount; ++i) {
        std::uint64_t id = 0;
        std::int64_t time = 0;
        char* name = nullptr;
        char* color = nullptr;
        if (nc_marker_get(gEditor, i, &id, &time, &name, &color) != NC_OK) continue;
        JsonValue m = JsonValue::object();
        m.set("id", JsonValue::integer(static_cast<std::int64_t>(id)));
        m.set("time_us", JsonValue::integer(time));
        m.set("name", JsonValue::string(takeString(name)));
        m.set("color", JsonValue::string(takeString(color)));
        markers.push(std::move(m));
    }
    root.set("markers", std::move(markers));

    return toJsonString(root, false);
}

// ---- 编辑操作（全部走 C ABI；真实进入撤销栈）----

std::string gLastError;
int32_t gLastCode = 0;

bool need(bool cond, const char* message) {
    if (!cond) {
        gLastError = message;
        gLastCode = NC_ERROR_INVALID_ARGUMENT;
    }
    return cond;
}

std::int64_t jint(const JsonValue& v, const char* key, std::int64_t fallback = 0) {
    return v.has(key) ? v.get(key).asInteger(fallback) : fallback;
}
double jnum(const JsonValue& v, const char* key, double fallback = 0.0) {
    return v.has(key) ? v.get(key).asNumber(fallback) : fallback;
}
std::string jstr(const JsonValue& v, const char* key, const char* fallback = "") {
    if (!v.has(key)) return std::string(fallback);
    const std::string& text = v.get(key).asString();
    return text.empty() ? std::string(fallback) : text;
}
bool jbool(const JsonValue& v, const char* key, bool fallback = false) {
    return v.has(key) ? v.get(key).asBool(fallback) : fallback;
}

/* ---- /api/call 通用派发用的取值辅助 ----
   注意：返回 static 容器，避免 &vec[0] 指向已销毁的临时对象。单线程 HTTP 处理，够用。 */
const std::vector<double>& jvecNum(const JsonValue& v, const char* key) {
    static std::vector<double> buf;
    buf.clear();
    if (v.has(key) && v.get(key).isArray()) {
        const JsonValue& arr = v.get(key);
        for (std::size_t i = 0; i < arr.size(); ++i) {
            buf.push_back(arr.at(i).asNumber(0.0));
        }
    }
    return buf;
}

const std::vector<std::int64_t>& jvecInt(const JsonValue& v, const char* key) {
    static std::vector<std::int64_t> buf;
    buf.clear();
    if (v.has(key) && v.get(key).isArray()) {
        const JsonValue& arr = v.get(key);
        for (std::size_t i = 0; i < arr.size(); ++i) {
            buf.push_back(arr.at(i).asInteger(0));
        }
    }
    return buf;
}

const std::vector<std::uint8_t>& jvecByte(const JsonValue& v, const char* key) {
    static std::vector<std::uint8_t> buf;
    buf.clear();
    if (v.has(key) && v.get(key).isArray()) {
        const JsonValue& arr = v.get(key);
        for (std::size_t i = 0; i < arr.size(); ++i) {
            buf.push_back(static_cast<std::uint8_t>(arr.at(i).asInteger(0)));
        }
    }
    return buf;
}

const std::vector<std::uint64_t>& jvecU64(const JsonValue& v, const char* key) {
    static std::vector<std::uint64_t> buf;
    buf.clear();
    if (v.has(key) && v.get(key).isArray()) {
        const JsonValue& arr = v.get(key);
        for (std::size_t i = 0; i < arr.size(); ++i) {
            buf.push_back(static_cast<std::uint64_t>(arr.at(i).asInteger(0)));
        }
    }
    return buf;
}

/// char** 字符串数组 → JSON 数组（最多 n 个；n<=0 时按 nullptr 结尾）。
JsonValue jstrArrayN(char** items, int32_t n) {
    JsonValue out = JsonValue::array();
    if (items == nullptr) return out;
    for (int32_t i = 0; (n > 0 ? i < n : items[i] != nullptr); ++i) {
        if (n <= 0 && items[i] == nullptr) break;
        out.push(JsonValue::string(std::string(items[i])));
    }
    return out;
}

JsonValue jstrArray(char** items) { return jstrArrayN(items, 0); }

const std::vector<float>& jvecF32(const JsonValue& v, const char* key) {
    static std::vector<float> buf;
    buf.clear();
    if (v.has(key) && v.get(key).isArray()) {
        const JsonValue& arr = v.get(key);
        for (std::size_t i = 0; i < arr.size(); ++i) {
            buf.push_back(static_cast<float>(arr.at(i).asNumber(0.0)));
        }
    }
    return buf;
}

const std::vector<std::int32_t>& jvecI32(const JsonValue& v, const char* key) {
    static std::vector<std::int32_t> buf;
    buf.clear();
    if (v.has(key) && v.get(key).isArray()) {
        const JsonValue& arr = v.get(key);
        for (std::size_t i = 0; i < arr.size(); ++i) {
            buf.push_back(static_cast<std::int32_t>(arr.at(i).asInteger(0)));
        }
    }
    return buf;
}

JsonValue jnumArrayF(const float* data, int32_t n, std::int64_t fallback = 0) {
    JsonValue out = JsonValue::array();
    if (data == nullptr) return out;
    const int32_t count = n > 0 ? n : static_cast<int32_t>(fallback);
    for (int32_t i = 0; i < count; ++i) {
        out.push(JsonValue::number(static_cast<double>(data[i])));
    }
    return out;
}

/// 数值数组出参 → JSON 数组（n 优先，n<=0 时用 fallback）。
JsonValue jnumArray(const double* data, int32_t n, std::int64_t fallback = 0) {
    JsonValue out = JsonValue::array();
    if (data == nullptr) return out;
    const int32_t count = n > 0 ? n : static_cast<int32_t>(fallback);
    for (int32_t i = 0; i < count; ++i) {
        out.push(JsonValue::number(data[i]));
    }
    return out;
}

/* ---- 通用 API 直通派发表（自动生成 + 手写补充）----
   放在这里是因为它要用到 jint/jnum/jstr/jvecNum/takeString/encodePng/gEditor 等，
   且必须在 applyEditLocked 之前定义。 */
#include "api_dispatch.inc"
#include "api_dispatch_special.inc"

int32_t applyEditLocked(const JsonValue& req) {
    const std::string op = jstr(req, "op");
    const int32_t track = static_cast<int32_t>(jint(req, "track", -1));
    const std::uint64_t clip = static_cast<std::uint64_t>(jint(req, "clip", 0));
    gLastError.clear();
    gLastCode = 0;

    auto check = [](int32_t code) {
        if (code != NC_OK) {
            gLastCode = code;
            if (gLastError.empty()) {
                gLastError = "操作被引擎拒绝（错误码 " + std::to_string(code) + "）";
            }
            return false;
        }
        return true;
    };

    if (op == "undo") return check(nc_undo(gEditor)) ? NC_OK : gLastCode;
    if (op == "redo") return check(nc_redo(gEditor)) ? NC_OK : gLastCode;
    if (op == "split") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_split_clip(gEditor, track, clip, jint(req, "at_us"))) ? NC_OK : gLastCode;
    }
    if (op == "move") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_move_clip(gEditor, track, clip, jint(req, "start_us"))) ? NC_OK : gLastCode;
    }
    if (op == "move_to_track") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        const int32_t toTrack = static_cast<int32_t>(jint(req, "to_track", -1));
        const std::int64_t at = jint(req, "start_us", 0);
        if (toTrack < 0) { gLastError = "缺少目标轨道"; gLastCode = NC_ERROR_INVALID_ARGUMENT; return gLastCode; }
        return check(nc_move_clip_to_track(gEditor, track, toTrack, clip, at)) ? NC_OK : gLastCode;
    }
    if (op == "trim_start") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_trim_clip_start(gEditor, track, clip, jint(req, "start_us"))) ? NC_OK : gLastCode;
    }
    if (op == "trim_end") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_trim_clip_end(gEditor, track, clip, jint(req, "end_us"))) ? NC_OK : gLastCode;
    }
    if (op == "delete") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_remove_clip(gEditor, track, clip)) ? NC_OK : gLastCode;
    }
    if (op == "ripple_delete") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        const std::uint64_t ids[1] = {clip};
        return check(nc_ripple_delete(gEditor, track, ids, 1)) ? NC_OK : gLastCode;
    }
    if (op == "duplicate") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        if (nc_duplicate_clip(gEditor, track, clip) == 0) { gLastCode = NC_ERROR_INTERNAL; return gLastCode; }
        return NC_OK;
    }
    if (op == "filter") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_filter(gEditor, track, clip, static_cast<int32_t>(jint(req, "style", 0)),
                                        jnum(req, "intensity", 60.0))) ? NC_OK : gLastCode;
    }
    if (op == "opacity") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_opacity(gEditor, track, clip, jnum(req, "value", 1.0))) ? NC_OK : gLastCode;
    }
    if (op == "speed") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_speed(gEditor, track, clip, jint(req, "num", 1), jint(req, "den", 1)))
                   ? NC_OK : gLastCode;
    }
    if (op == "enabled") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_enabled(gEditor, track, clip, jbool(req, "value", true) ? 1 : 0))
                   ? NC_OK : gLastCode;
    }
    if (op == "track_flag") {
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const std::string flag = jstr(req, "flag");
        const int32_t value = jbool(req, "value", false) ? 1 : 0;
        if (flag == "locked") return check(nc_set_track_locked(gEditor, track, value)) ? NC_OK : gLastCode;
        if (flag == "muted") return check(nc_set_track_muted(gEditor, track, value)) ? NC_OK : gLastCode;
        if (flag == "hidden") return check(nc_set_track_hidden(gEditor, track, value)) ? NC_OK : gLastCode;
        if (flag == "solo") return check(nc_set_track_solo(gEditor, track, value)) ? NC_OK : gLastCode;
        gLastError = "未知轨道标志位";
        gLastCode = NC_ERROR_INVALID_ARGUMENT;
        return gLastCode;
    }
    if (op == "track_view") {
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const int32_t hpx = jint(req, "height_px", 48);
        const int32_t col = jbool(req, "collapsed", false) ? 1 : 0;
        return check(nc_set_track_view(gEditor, track, hpx, col)) ? NC_OK : gLastCode;
    }
    if (op == "track_move") {
        // 整条轨道连同其中全部内容（片段/效果/关键帧/转场/调整图层/
        // 静音隐藏锁定标志/轨道增益）一起换位置 —— 它们都在 Track 内部。
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const int32_t to = jint(req, "to_index", -1);
        if (!need(to >= 0, "缺少目标位置")) return gLastCode;
        return check(nc_move_track(gEditor, track, to)) ? NC_OK : gLastCode;
    }
    if (op == "track_gain") {
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        return check(nc_set_track_gain(gEditor, track, jnum(req, "gain_db", 0.0))) ? NC_OK : gLastCode;
    }
    if (op == "rename_track") {
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        return check(nc_rename_track(gEditor, track, jstr(req, "name", "轨道").c_str())) ? NC_OK : gLastCode;
    }
    if (op == "marker_color") {
        return nc_add_marker(gEditor, jint(req, "time_us"), jstr(req, "name", "标记").c_str(),
                             jstr(req, "color", "#F0A03C").c_str()) != 0
                   ? NC_OK : (gLastCode = NC_ERROR_INTERNAL);
    }
    if (op == "add_track") {
        const std::string name = jstr(req, "name", "新建轨道");
        if (nc_add_track(gEditor, name.c_str(), static_cast<int32_t>(jint(req, "type", 0))) == 0) {
            gLastCode = NC_ERROR_INTERNAL;
            return gLastCode;
        }
        return NC_OK;
    }
    if (op == "transition") {
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const std::uint64_t id = nc_add_transition(
            gEditor, track, static_cast<int32_t>(jint(req, "type", 0)), jint(req, "duration_us", 500000),
            static_cast<std::uint64_t>(jint(req, "from", 0)),
            static_cast<std::uint64_t>(jint(req, "to", 0)));
        if (id == 0) { gLastCode = NC_ERROR_INTERNAL; return gLastCode; }
        return NC_OK;
    }
    if (op == "freeze") {
        /* 定格：在播放头处取一帧，作为独立片段插入。
           内核的 nc_freeze_frame 已按「插入编辑」语义实现：
           at_us 落在片段内部时会自动分割并把后续内容顺移，
           因此桥这里不需要预先切分。 */
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const std::uint64_t cid = static_cast<std::uint64_t>(jint(req, "clip", 0));
        if (!need(cid != 0, "缺少片段 id")) return gLastCode;
        const int64_t atUs = jint(req, "at_us", 0);
        const int64_t durUs = jint(req, "duration_us", 2000000);
        if (!need(durUs > 0, "定格时长必须大于 0")) return gLastCode;
        const std::uint64_t newId = nc_freeze_frame(gEditor, track, cid, atUs, durUs);
        if (!need(newId != 0, "定格失败（播放头不在该片段上，或轨道已锁定）")) return gLastCode;
        return NC_OK;
    }
    if (op == "reverse") {
        // 倒放：内核按「时长等价」实现，时长不变
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const std::uint64_t cid = static_cast<std::uint64_t>(jint(req, "clip", 0));
        if (!need(cid != 0, "缺少片段 id")) return gLastCode;
        return check(nc_reverse_clip(gEditor, track, cid)) ? NC_OK : gLastCode;
    }
    if (op == "blend") {
        // 画中画混合模式（0..15，见 nc_blend_mode_count）
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const std::uint64_t cid = static_cast<std::uint64_t>(jint(req, "clip", 0));
        if (!need(cid != 0, "缺少片段 id")) return gLastCode;
        const int32_t mode = static_cast<int32_t>(jint(req, "mode", 0));
        if (!need(mode >= 0 && mode < nc_blend_mode_count(), "混合模式编号超出范围")) {
            return gLastCode;
        }
        return check(nc_set_clip_blend_mode(gEditor, track, cid, mode)) ? NC_OK : gLastCode;
    }
    if (op == "remove_transition") {
        if (!need(track >= 0, "缺少目标轨道")) return gLastCode;
        const std::uint64_t tid = static_cast<std::uint64_t>(jint(req, "transition", 0));
        if (!need(tid != 0, "缺少转场 id")) return gLastCode;
        return check(nc_remove_transition(gEditor, track, tid)) ? NC_OK : gLastCode;
    }
    if (op == "marker") {
        const std::uint64_t id = nc_add_marker(gEditor, jint(req, "time_us"),
                                               jstr(req, "name", "标记").c_str(),
                                               jstr(req, "color", "#C6F24E").c_str());
        if (id == 0) { gLastCode = NC_ERROR_INTERNAL; return gLastCode; }
        return NC_OK;
    }
    if (op == "effect_add") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        const std::uint64_t id = nc_add_effect(gEditor, track, clip,
                                               static_cast<int32_t>(jint(req, "type", 0)),
                                               jnum(req, "value", 0.5));
        if (id == 0) { gLastCode = NC_ERROR_INTERNAL; return gLastCode; }
        return NC_OK;
    }
    if (op == "denoise") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_denoise(gEditor, track, clip,
                                         static_cast<int32_t>(jint(req, "strength", 0))))
                   ? NC_OK : gLastCode;
    }
    if (op == "mask") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_mask(gEditor, track, clip,
                                      static_cast<int32_t>(jint(req, "shape", 0)),
                                      jnum(req, "x", 0.5), jnum(req, "y", 0.5),
                                      jnum(req, "rotation", 0), jnum(req, "width", 1.0),
                                      jnum(req, "height", 1.0), jnum(req, "feather", 0.02),
                                      jnum(req, "radius", 0.0),
                                      static_cast<int32_t>(jint(req, "invert", 0))))
                   ? NC_OK : gLastCode;
    }
    if (op == "chroma") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_chroma_key(gEditor, track, clip,
                                            jstr(req, "key", "#00FF00").c_str(),
                                            jnum(req, "inner", 0.35), jnum(req, "outer", 0.55),
                                            jnum(req, "spill", 0.3),
                                            static_cast<int32_t>(jint(req, "quality", 1)),
                                            static_cast<int32_t>(jint(req, "enabled", 1))))
                   ? NC_OK : gLastCode;
    }
    if (op == "audio") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_set_clip_audio(gEditor, track, clip, jnum(req, "gain_db", 0.0),
                                       jint(req, "fade_in_us", 0), jint(req, "fade_out_us", 0)))
                   ? NC_OK : gLastCode;
    }
    if (op == "speed_curve") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        const JsonValue& times = req.get("times");
        const JsonValue& speeds = req.get("speeds");
        if (!times.isArray() || !speeds.isArray() || times.size() == 0 ||
            times.size() != speeds.size()) {
            gLastCode = NC_ERROR_INVALID_ARGUMENT;
            gLastError = "速度曲线控制点为空或长度不一致";
            return gLastCode;
        }
        std::vector<double> ts;
        std::vector<double> vs;
        std::vector<int32_t> es;
        const int32_t easing = static_cast<int32_t>(jint(req, "easing", 0));
        for (std::size_t i = 0; i < times.size(); ++i) {
            ts.push_back(times.at(i).asNumber());
            vs.push_back(speeds.at(i).asNumber());
            es.push_back(easing);
        }
        return check(nc_set_clip_speed_curve(gEditor, track, clip, ts.data(), vs.data(),
                                             es.data(), static_cast<int32_t>(ts.size())))
                   ? NC_OK : gLastCode;
    }
    if (op == "keyframe") {
        if (!need(track >= 0 && clip != 0, "缺少目标片段")) return gLastCode;
        return check(nc_insert_keyframe(gEditor, track, clip,
                                        static_cast<int32_t>(jint(req, "property", 0)),
                                        jint(req, "time_us", 0),
                                        jnum(req, "value", 1.0)))
                   ? NC_OK : gLastCode;
    }
    if (op == "frame_fit") {
        return check(nc_set_frame_fit(gEditor, static_cast<int32_t>(jint(req, "mode", 1))))
                   ? NC_OK : gLastCode;
    }
    if (op == "canvas") {
        const int w = static_cast<int>(jint(req, "width", 1080));
        const int h = static_cast<int>(jint(req, "height", 1920));
        return check(nc_set_project_canvas(gEditor, w, h)) ? NC_OK : gLastCode;
    }
    if (op == "rename_project") {
        return check(nc_set_project_name(gEditor, jstr(req, "name", "未命名项目").c_str())) ? NC_OK : gLastCode;
    }
    gLastError = "未知操作：" + op;
    gLastCode = NC_ERROR_INVALID_ARGUMENT;
    return gLastCode;
}

// ---- HTTP ----

struct Request {
    std::string method;
    std::string path;
    std::map<std::string, std::string> query;
    std::string body;
};

std::string urlDecode(const std::string& text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(text[i + 1]);
            const int lo = hex(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i] == '+' ? ' ' : text[i]);
    }
    return out;
}

Request parseRequest(const std::string& raw) {
    Request req;
    const std::size_t lineEnd = raw.find("\r\n");
    std::istringstream first(raw.substr(0, lineEnd));
    std::string target;
    first >> req.method >> target;
    const std::size_t q = target.find('?');
    req.path = urlDecode(q == std::string::npos ? target : target.substr(0, q));
    if (q != std::string::npos) {
        std::string query = target.substr(q + 1);
        std::size_t start = 0;
        while (start <= query.size()) {
            const std::size_t amp = query.find('&', start);
            const std::string pair =
                query.substr(start, amp == std::string::npos ? std::string::npos : amp - start);
            const std::size_t eq = pair.find('=');
            if (eq != std::string::npos) {
                req.query[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
            }
            if (amp == std::string::npos) break;
            start = amp + 1;
        }
    }
    const std::size_t sep = raw.find("\r\n\r\n");
    if (sep != std::string::npos) req.body = raw.substr(sep + 4);
    return req;
}

const char* mimeFor(const std::string& path) {
    auto ends = [&path](const char* ext) {
        const std::size_t n = std::strlen(ext);
        return path.size() >= n && path.compare(path.size() - n, n, ext) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".json")) return "application/json; charset=utf-8";
    if (ends(".png")) return "image/png";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".webm")) return "video/webm";
    if (ends(".mp4")) return "video/mp4";
    if (ends(".woff2")) return "font/woff2";
    return "application/octet-stream";
}

struct Response {
    int status = 200;
    std::string contentType = "text/plain; charset=utf-8";
    std::string body;
    std::string extraHeaders;
};

Response jsonResponse(const std::string& body, int status = 200) {
    Response r;
    r.status = status;
    r.contentType = "application/json; charset=utf-8";
    r.body = body;
    r.extraHeaders = "Cache-Control: no-store\r\n";
    return r;
}

Response pngResponse(const std::string& png) {
    Response r;
    if (png.empty()) {
        r.status = 404;
        r.body = "not found";
        return r;
    }
    r.contentType = "image/png";
    r.body = png;
    r.extraHeaders = "Cache-Control: max-age=3600\r\n";
    return r;
}

std::map<std::string, std::string> gFrameCache;
std::mutex gFrameMutex;

// 单帧渲染互斥：core 句柄不支持并发（预览缓存非线程安全），拖拽时串行化。
std::mutex gRenderMutex;


/* 按预设临时渲染一帧（滤镜/特效/蒙版），渲染完立即还原原状态。
   仅用于三级面板宫格缩略图，不影响工程数据。 */
std::string renderFramePng(std::int64_t timeUs, int targetWidth, bool bypassCache,
                           const std::string& cacheTag = std::string());

std::string renderFramePngWith(NcEditor* ed, std::int64_t timeUs, int targetWidth);

/* 转场缩略图：
   用独立 editor 实例搭一个 A|B 迷你序列（两张黏土图），
   临时插入指定转场，渲染转场中点帧，然后销毁实例。

   为什么不用主实例：临时的片段/转场会进撤销栈、可能被自动保存捕获，
   用户按一次撤销看到被自己没做过的改动。独立实例彻底隔离。

   底图为什么用黏土图：用户指定 —— 宫格展示统一用这套抠好的黏土表情，
   与特效/蒙版/滤镜宫格保持同一视觉语汇（参考剪映的宫格预览）。 */
namespace {
/// 黏土底图（相对 repo 根）。
/// 用 1 秒的 mp4 而不是 png：图片素材在 preview.cpp 里没有读帧路径
/// （media reader 只处理容器/视频流），单帧 PNG 会读出空画面。
/// 由 tools/gen_clay_previews.sh 从 faces3/*.png 转出，与宫格图同源。
const char* kClayA = "mobile/static/clay/B_0_0.mp4";   // 最暗（棕）
const char* kClayB = "mobile/static/clay/B_2_0.mp4";   // 最亮（米黄）

std::string resolveRepoFile(const char* rel) {
    if (repoRoot.empty()) return {};
    std::string p = repoRoot + "/" + rel;
    std::FILE* f = std::fopen(p.c_str(), "rb");
    if (f == nullptr) return {};
    std::fclose(f);
    return p;
}
}  // namespace

std::string renderTransitionCard(int type, int width) {
    const std::string a = resolveRepoFile(kClayA);
    const std::string b = resolveRepoFile(kClayB);
    std::fprintf(stderr, "[transcard] repoRoot=%s a=%s b=%s\n", repoRoot.c_str(), a.c_str(), b.c_str());
    if (a.empty() || b.empty()) {
        return {};   // 底图缺失就如实返回空，前端显示占位，不造假
    }
    NcEditor* mini = nc_editor_create();
    if (mini == nullptr) {
        return {};
    }
    // 输出画幅：竖屏 9:16，与移动端预览一致
    nc_set_project_canvas(mini, 1080, 1920);

    const std::uint64_t assetA = nc_import_asset(mini, "clay_a", a.c_str(), 0, 1000000);
    const std::uint64_t assetB = nc_import_asset(mini, "clay_b", b.c_str(), 0, 1000000);
    std::fprintf(stderr, "[transcard] assets=%llu/%llu\n",
                 static_cast<unsigned long long>(assetA), static_cast<unsigned long long>(assetB));
    if (assetA == 0 || assetB == 0) {
        nc_editor_destroy(mini);
        return {};
    }
    const std::uint64_t track = nc_add_track(mini, "card", 0);
    if (track == 0) {
        nc_editor_destroy(mini);
        return {};
    }
    const std::uint64_t cA = nc_insert_clip(mini, 0, assetA, 0, 1000000);
    const std::uint64_t cB = nc_insert_clip(mini, 0, assetB, 1000000, 1000000);
    if (cA == 0 || cB == 0) {
        nc_editor_destroy(mini);
        return {};
    }

    /* 转场区间是 [from->end(), from->end() + duration) —— 从**前一段的结束**
       才开始，不是从交界前开始。所以中点 = 1s + dur/2。
       （早前写成 1s - dur/2 落在区间外，渲染出的是纯 B 段，等于没混。） */
    const std::int64_t kDur = 600000;
    std::string png;
    const std::uint64_t trid = nc_add_transition(mini, 0, type, kDur, cA, cB);
    std::fprintf(stderr, "[transcard] clips=%llu/%llu tr=%llu\n",
                 static_cast<unsigned long long>(cA), static_cast<unsigned long long>(cB),
                 static_cast<unsigned long long>(trid));
    if (trid != 0) {
        /* 也顺手验证一下：交界前（纯 A）与交界后（纯 B）各渲一张，
           用来确认两段素材都能出画；三张里若只有一张有内容，
           就说明某一路 reader 打不开，而不是转场算法的问题。 */
        const std::string prepng = renderFramePngWith(mini, 500000, 96);
        const std::string postpng = renderFramePngWith(mini, 1800000, 96);
        std::fprintf(stderr, "[transcard] pre=%zu post=%zu\n", prepng.size(), postpng.size());
        png = renderFramePngWith(mini, 1000000 + kDur / 2, width);
        std::fprintf(stderr, "[transcard] mid=%lld size=%zu\n",
                     static_cast<long long>(1000000 + kDur / 2), png.size());
    }
    nc_editor_destroy(mini);
    return png;
}

std::string renderPresetPng(const std::string& kind, int style, std::int64_t timeUs, int width) {
    /* 转场宫格不依赖主工程：它用独立实例 + 黏土底图现场合成。
       必须放在「必须有轨道」的检查之前 —— 否则空工程时
       整个转场库都显示不出预览（而转场的预览本来就与工程内容无关）。 */
    if (kind == "transition") {
        return renderTransitionCard(style, width);
    }
    int trackCount = nc_track_count(gEditor);
    if (trackCount <= 0) return {};

    int ti = -1;
    std::uint64_t cid = 0;
    for (int i = 0; i < trackCount; ++i) {
        int type = 0;
        int32_t locked = 0, muted = 0, hidden = 0, solo = 0;
        double gain = 0;
        if (nc_track_get(gEditor, i, nullptr, nullptr, &type, &locked, &muted, &hidden, &solo,
                         &gain) != NC_OK) continue;
        if (type != 0) continue;
        if (nc_clip_count(gEditor, i) <= 0) continue;
        std::uint64_t id = 0, asset = 0;
        std::int64_t st = 0, du = 0, sn = 1, sd = 1;
        int32_t en = 1;
        if (nc_clip_info(gEditor, i, 0, &id, &st, &du, &asset, &sn, &sd, &en) != NC_OK) continue;
        ti = i; cid = id; break;
    }
    if (ti < 0 || cid == 0) return {};

    int32_t oldStyle = 0;
    double oldIntensity = 0;
    nc_get_clip_filter(gEditor, ti, cid, &oldStyle, &oldIntensity);
    std::fprintf(stderr, "[preset] kind=%s style=%d track=%d clip=%llu oldStyle=%d oldInt=%.1f\n",
                 kind.c_str(), style, ti, static_cast<unsigned long long>(cid), oldStyle, oldIntensity);

    std::uint64_t tempEffect = 0;
    if (kind == "filter") {
        nc_set_clip_filter(gEditor, ti, cid, style, 100.0);
    } else if (kind == "effect") {
        nc_set_clip_filter(gEditor, ti, cid, 0, 0.0);
        tempEffect = nc_add_effect(gEditor, ti, cid, style, 0.85);
    } else if (kind == "mask") {
        nc_set_clip_mask(gEditor, ti, cid, style, 0.5, 0.5, 0, 1.0, 1.0, 0.06, 0.0, 0);
    }

    int32_t chkStyle = 0; double chkInt = 0;
    nc_get_clip_filter(gEditor, ti, cid, &chkStyle, &chkInt);
    std::fprintf(stderr, "[preset] after set: style=%d int=%.1f\n", chkStyle, chkInt);
    const std::string tag = kind + std::to_string(style);
    std::string png = renderFramePng(timeUs, width, true, tag);

    /* 精确还原：只删本次新增的那个特效 */
    nc_set_clip_filter(gEditor, ti, cid, oldStyle, oldIntensity);
    if (tempEffect != 0) nc_remove_effect(gEditor, ti, cid, tempEffect);
    if (kind == "mask") nc_set_clip_mask(gEditor, ti, cid, 0, .5, .5, 0, 1, 1, 0, 0, 0);
    return png;
}

/// 用**指定** editor 实例渲染一帧（不查/不写全局缓存）。
/// 转场宫格用：临时实例画的图不能污染主实例的帧缓存。
std::string renderFramePngWith(NcEditor* ed, std::int64_t timeUs, int targetWidth) {
    if (ed == nullptr) {
        return {};
    }
    const std::int64_t bucket = (timeUs / 33333) * 33333;
    std::uint8_t* rgba = nullptr;
    int width = 0, height = 0;
    std::uint64_t clipId = 0;
    double opacity = 1, scale = 1, px = 0, py = 0, rotation = 0;
    int32_t trType = -1;
    double trProgress = 0;
    const int32_t rc = nc_preview_render(ed, bucket, &width, &height, &rgba, &clipId, &opacity,
                                         &scale, &px, &py, &rotation, &trType, &trProgress);
    std::fprintf(stderr, "[framewith] t=%lld rc=%d w=%d h=%d rgba=%p trType=%d prog=%.2f\n",
                 static_cast<long long>(bucket), rc, width, height, static_cast<void*>(rgba),
                 trType, trProgress);
    if (rc != NC_OK || rgba == nullptr) {
        return {};
    }
    const std::vector<std::uint8_t> scaled = resizeRgba(rgba, width, height, targetWidth);
    nc_free_buffer(rgba);
    const int sw = (targetWidth > 0 && targetWidth < width) ? targetWidth : width;
    const int sh = (targetWidth > 0 && targetWidth < width) ? (height * targetWidth) / width : height;
    return encodePng(scaled.data(), sw, sh);
}

std::string renderFramePng(std::int64_t timeUs, int targetWidth, bool bypassCache,
                           const std::string& cacheTag) {
    const std::int64_t bucket = (timeUs / 33333) * 33333;
    const std::string key = std::to_string(bucket) + "@" + std::to_string(targetWidth)
                          + (bypassCache ? "@pre" + cacheTag : "");
    {
        std::lock_guard<std::mutex> lock(gFrameMutex);
        auto it = gFrameCache.find(key);
        if (it != gFrameCache.end()) return it->second;
    }
    std::lock_guard<std::mutex> renderLock(gRenderMutex);
    {
        std::lock_guard<std::mutex> lock(gFrameMutex);
        auto it = gFrameCache.find(key);
        if (it != gFrameCache.end()) return it->second;
    }

    std::uint8_t* rgba = nullptr;
    int width = 0, height = 0;
    std::uint64_t clipId = 0;
    double opacity = 1, scale = 1, px = 0, py = 0, rotation = 0;
    int32_t trType = -1;
    double trProgress = 0;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (nc_preview_render(gEditor, bucket, &width, &height, &rgba, &clipId, &opacity, &scale,
                              &px, &py, &rotation, &trType, &trProgress) != NC_OK ||
            rgba == nullptr) {
            return {};
        }
    }
    const std::vector<std::uint8_t> scaled = resizeRgba(rgba, width, height, targetWidth);
    nc_free_buffer(rgba);
    const int sw = (targetWidth > 0 && targetWidth < width) ? targetWidth : width;
    const int sh = (targetWidth > 0 && targetWidth < width) ? (height * targetWidth) / width : height;
    std::string png = encodePng(scaled.data(), sw, sh);
    {
        std::lock_guard<std::mutex> lock(gFrameMutex);
        if (gFrameCache.size() > 480) gFrameCache.clear();
        gFrameCache[key] = png;
    }
    return png;
}

/// 预取：把 [from, to] 区间按帧桶均匀取 N 个采样点渲染并缓存（后台线程）。
/// 用途：时间轴胶片条 / 拖动前的缓冲。真实渲染，不生成占位图。
void prefetchFrames(std::int64_t fromUs, std::int64_t toUs, int samples, int width) {
    if (toUs <= fromUs || samples <= 0) return;
    for (int i = 0; i < samples; ++i) {
        const std::int64_t t = fromUs + (toUs - fromUs) * i / samples;
        renderFramePng(t, width, false);
    }
}

// ---- 导出（真实 nc_export_run）----

std::atomic<bool> gExporting{false};
std::atomic<double> gExportProgress{0.0};
std::atomic<bool> gExportCancel{false};
std::atomic<std::int64_t> gExportFrames{-1};
std::string gExportPath;
std::string gExportError;

int onExportProgress(double progress) {
    gExportProgress.store(progress);
    return gExportCancel.load() ? 0 : 1;
}

void runExport(std::string path, int presetId, int mode, int attachLabel) {
    int cores = static_cast<int>(std::thread::hardware_concurrency());
    int width = 0, height = 0, fps = 0, downgraded = 0;
    char* presetName = nullptr;
    char* warning = nullptr;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        nc_export_setup_preset(gEditor, presetId, mode, cores, 23, &width, &height, &fps, &downgraded,
                               &presetName, &warning);
        nc_free_string(presetName);
        nc_free_string(warning);
        char* error = nullptr;
        const std::int64_t frames = nc_export_run(gEditor, path.c_str(), 23, 1, attachLabel,
                                                  attachLabel ? "AI 生成" : nullptr,
                                                  &onExportProgress, &error);
        gExportFrames.store(frames);
        gExportError = error != nullptr ? takeString(error) : std::string();
    }
    gExporting.store(false);
}

Response handleApi(const Request& req) {
    if (req.path == "/api/state") return jsonResponse(buildStateJson());

    if (req.path == "/api/presets") {
        JsonValue root = JsonValue::object();
        JsonValue list = JsonValue::array();
        for (int id = 0; id <= 5; ++id) {
            int width = 0, height = 0, fps = 0, requiresPro = 0;
            char* label = nullptr;
            if (nc_export_preset_info(id, &width, &height, &fps, &requiresPro, &label) != NC_OK) continue;
            JsonValue p = JsonValue::object();
            p.set("id", JsonValue::integer(id));
            p.set("label", JsonValue::string(takeString(label)));
            p.set("width", JsonValue::integer(width));
            p.set("height", JsonValue::integer(height));
            p.set("fps", JsonValue::integer(fps));
            p.set("requires_pro", JsonValue::boolean(requiresPro == 1));
            list.push(std::move(p));
        }
        root.set("presets", std::move(list));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/frame") {
        const std::int64_t t = std::stoll(req.query.count("t") ? req.query.at("t") : "0");
        const int width = req.query.count("w") ? std::stoi(req.query.at("w")) : 420;
        return pngResponse(renderFramePng(t, width, false));
    }
    if (req.path == "/api/preset_frame") {
        /* 宫格缩略图：临时给片段套一个预设，渲染一帧，再还原。
           这样滤镜/特效/蒙版的格子是真实画面，而不是占位色块。 */
        const std::string kind = req.query.count("kind") ? req.query.at("kind") : "filter";
        const int style = req.query.count("style") ? std::stoi(req.query.at("style")) : 0;
        const std::int64_t t = req.query.count("t") ? std::stoll(req.query.at("t")) : 1600000;
        const int width = req.query.count("w") ? std::stoi(req.query.at("w")) : 120;
        const std::string png = renderPresetPng(kind, style, t, width);
        return pngResponse(png);
    }
    if (req.path == "/api/prefetch") {
        // 后台真实预渲染（时间轴胶片条/拖动缓冲）；不做任何假进度。
        const std::int64_t from = std::stoll(req.query.count("from") ? req.query.at("from") : "0");
        const std::int64_t to = std::stoll(req.query.count("to") ? req.query.at("to") : "0");
        const int n = req.query.count("n") ? std::stoi(req.query.at("n")) : 24;
        const int w = req.query.count("w") ? std::stoi(req.query.at("w")) : 120;
        std::thread([from, to, n, w] { prefetchFrames(from, to, n, w); }).detach();
        return jsonResponse("{\"ok\":true}");
    }
    if (req.path == "/api/thumb") {
        const std::uint64_t asset = std::stoull(req.query.count("asset") ? req.query.at("asset") : "0");
        const int index = req.query.count("i") ? std::stoi(req.query.at("i")) : 0;
        const int width = req.query.count("w") ? std::stoi(req.query.at("w")) : 120;
        /* 惰性生成：首次请求才解码（10~30s），之后命中缓存 */
        {
            std::lock_guard<std::mutex> lock(gFrameMutex);
            if (gThumbs.find(asset) == gThumbs.end()) {
                // 释放锁再解码，避免阻塞 /api/state
            }
        }
        {
            bool need = false;
            {
                std::lock_guard<std::mutex> lock(gFrameMutex);
                need = gThumbs.find(asset) == gThumbs.end();
            }
            if (need) buildThumbs(asset, 8);
        }
        std::lock_guard<std::mutex> lock(gFrameMutex);
        auto it = gThumbs.find(asset);
        if (it == gThumbs.end() || it->second.images.empty()) return pngResponse({});
        const int count = static_cast<int>(it->second.images.size());
        const int i = std::max(0, std::min(index, count - 1));
        const std::vector<std::uint8_t> scaled =
            resizeRgba(it->second.images[static_cast<std::size_t>(i)].data(), it->second.width,
                       it->second.height, width);
        const int sw = width < it->second.width ? width : it->second.width;
        const int sh = (it->second.height * sw) / it->second.width;
        return pngResponse(encodePng(scaled.data(), sw, sh));
    }
    if (req.path == "/api/waveform") {
        const std::uint64_t asset = std::stoull(req.query.count("asset") ? req.query.at("asset") : "0");
        JsonValue root = JsonValue::object();
        {
            bool need = false;
            {
                std::lock_guard<std::mutex> lock(gFrameMutex);
                need = gWaves.find(asset) == gWaves.end();
            }
            if (need) buildWave(asset, 480);
        }
        std::lock_guard<std::mutex> lock(gFrameMutex);
        auto it = gWaves.find(asset);
        if (it == gWaves.end()) {
            root.set("buckets", JsonValue::array());
            return jsonResponse(toJsonString(root, false));
        }
        root.set("peak", JsonValue::number(it->second.peak));
        JsonValue arr = JsonValue::array();
        for (double v : it->second.rms) arr.push(JsonValue::number(v));
        root.set("buckets", std::move(arr));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/export_status") {
        JsonValue root = JsonValue::object();
        root.set("running", JsonValue::boolean(gExporting.load()));
        root.set("progress", JsonValue::number(gExportProgress.load()));
        root.set("frames", JsonValue::integer(gExportFrames.load()));
        root.set("path", JsonValue::string(gExportPath));
        root.set("error", JsonValue::string(gExportError));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/estimate") {
        // 真实资源预估 + 权益降级判定（nc_export_estimate / nc_would_clamp_export）
        const int presetId = req.query.count("preset") ? std::stoi(req.query.at("preset")) : 1;
        const int mode = req.query.count("mode") ? std::stoi(req.query.at("mode")) : 1;
        const int cores = static_cast<int>(std::thread::hardware_concurrency());
        int width = 0, height = 0, fps = 0, downgraded = 0;
        char* presetName = nullptr;
        char* warning = nullptr;
        int64_t durationUs = 0;
        int64_t queueSeconds = 0;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            durationUs = nc_project_duration_us(gEditor);
            queueSeconds = durationUs;
            nc_export_setup_preset(gEditor, presetId, mode, cores, 23, &width, &height, &fps,
                                   &downgraded, &presetName, &warning);
        }
        int clampWidth = 0, clampHeight = 0;
        if (width > 0 && height > 0) {
            clampWidth = width;
            clampHeight = height;
        } else {
            std::lock_guard<std::mutex> lock(gMutex);
            nc_project_canvas(gEditor, &clampWidth, &clampHeight);
        }
        const int wouldClamp = nc_would_clamp_export(gEditor, clampWidth, clampHeight);
        int64_t sizeBytes = 0;
        double renderSeconds = 0.0;
        char* sizeText = nullptr;
        char* timeText = nullptr;
        nc_export_estimate(width > 0 ? width : clampWidth, height > 0 ? height : clampHeight,
                           fps > 0 ? fps : 30, queueSeconds, 0.0, 1, &sizeBytes, &renderSeconds,
                           &sizeText, &timeText);
        JsonValue root = JsonValue::object();
        root.set("preset", JsonValue::integer(presetId));
        root.set("mode", JsonValue::integer(mode));
        root.set("width", JsonValue::integer(width));
        root.set("height", JsonValue::integer(height));
        root.set("fps", JsonValue::integer(fps));
        root.set("downgraded", JsonValue::boolean(downgraded == 1));
        root.set("would_clamp", JsonValue::boolean(wouldClamp == 1));
        root.set("preset_name", JsonValue::string(takeString(presetName)));
        root.set("warning", JsonValue::string(takeString(warning)));
        root.set("size_bytes", JsonValue::integer(sizeBytes));
        root.set("size_text", JsonValue::string(takeString(sizeText)));
        root.set("render_seconds", JsonValue::number(renderSeconds));
        root.set("time_text", JsonValue::string(takeString(timeText)));
        root.set("duration_us", JsonValue::integer(durationUs));
        root.set("entitlement", JsonValue::integer(nc_get_entitlement(gEditor)));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/ai_features") {
        // 真实门控：nc_ai_feature_access（登录态由「平台层」传入；这里用 query）
        const int loggedIn = req.query.count("logged_in") ? std::stoi(req.query.at("logged_in")) : 1;
        JsonValue root = JsonValue::object();
        JsonValue list = JsonValue::array();
        for (int feature = 1; feature <= 10; ++feature) {
            int32_t needsNetwork = 0, needsPro = 0;
            char* reason = nullptr;
            const int32_t access = nc_ai_feature_access(gEditor, feature, loggedIn, &needsNetwork,
                                                        &needsPro, &reason);
            JsonValue f = JsonValue::object();
            f.set("id", JsonValue::integer(feature));
            f.set("available", JsonValue::boolean(access == 1));
            f.set("requires_network", JsonValue::boolean(needsNetwork == 1));
            f.set("requires_pro", JsonValue::boolean(needsPro == 1));
            f.set("reason", JsonValue::string(takeString(reason)));
            list.push(std::move(f));
        }
        root.set("features", std::move(list));
        root.set("logged_in", JsonValue::boolean(loggedIn == 1));
        int32_t feature = 0, state = 0, retryable = 0;
        double progress = 0.0;
        char* stage = nullptr;
        char* failure = nullptr;
        char* payload = nullptr;
        int32_t taskCode = 0;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            taskCode = nc_ai_task_status(gEditor, &feature, &state, &progress, &stage, &failure,
                                         &retryable, &payload);
        }
        JsonValue task = JsonValue::object();
        task.set("feature", JsonValue::integer(feature));
        task.set("state", JsonValue::integer(state));
        task.set("progress", JsonValue::number(progress));
        task.set("stage", JsonValue::string(takeString(stage)));
        task.set("failure", JsonValue::string(takeString(failure)));
        task.set("retryable", JsonValue::boolean(retryable == 1));
        task.set("payload", JsonValue::string(takeString(payload)));
        task.set("code", JsonValue::integer(taskCode));
        root.set("task", std::move(task));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/models") {
        // 真实模型目录：nc_model_catalog / nc_model_info / nc_model_status / nc_model_access_allowed
        JsonValue root = JsonValue::object();
        JsonValue list = JsonValue::array();
        char** ids = nullptr;
        int32_t count = 0;
        nc_model_catalog(&ids, 0, &count);
        std::vector<std::string> all;
        if (count > 0) {
            ids = new char*[static_cast<std::size_t>(count)];
            for (int i = 0; i < count; ++i) ids[i] = nullptr;
            nc_model_catalog(&ids, count, &count);
            for (int i = 0; i < count; ++i) {
                if (ids[i] != nullptr) all.emplace_back(ids[i]);
            }
            nc_free_string_array(ids, count);
        }
        for (const std::string& id : all) {
            char* name = nullptr;
            char* description = nullptr;
            char* version = nullptr;
            int32_t sizeBytes = 0, requiresPro = 0, capability = 0;
            if (nc_model_info(id.c_str(), &name, &description, &sizeBytes, &version, &requiresPro,
                              &capability) != NC_OK) {
                continue;
            }
            JsonValue m = JsonValue::object();
            m.set("id", JsonValue::string(id));
            m.set("name", JsonValue::string(takeString(name)));
            m.set("description", JsonValue::string(takeString(description)));
            m.set("version", JsonValue::string(takeString(version)));
            m.set("size_bytes", JsonValue::integer(sizeBytes));
            m.set("requires_pro", JsonValue::boolean(requiresPro == 1));
            m.set("capability", JsonValue::integer(capability));
            int32_t status = 0;
            {
                std::lock_guard<std::mutex> lock(gMutex);
                status = nc_model_status(gEditor, id.c_str());
            }
            m.set("status", JsonValue::integer(status));
            m.set("allowed", JsonValue::boolean(nc_model_access_allowed(gEditor, id.c_str()) == 1));
            list.push(std::move(m));
        }
        root.set("models", std::move(list));
        int32_t ready = 0, generating = 0;
        int64_t proxyBytes = 0;
        nc_proxy_usage(gEditor, &ready, &generating, &proxyBytes);
        JsonValue storage = JsonValue::object();
        storage.set("proxy_ready", JsonValue::integer(ready));
        storage.set("proxy_generating", JsonValue::integer(generating));
        storage.set("proxy_bytes", JsonValue::integer(proxyBytes));
        root.set("storage", std::move(storage));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/blend_modes") {
        /* 混合模式库：16 种（kBlendModeCount）。
           名称与顺序取自内核 blend_modes.h，前端不写死。 */
        JsonValue root = JsonValue::object();
        JsonValue list = JsonValue::array();
        const int n = nc_blend_mode_count();
        for (int i = 0; i < n; ++i) {
            const char* nm = nc_blend_mode_name(i);
            if (nm == nullptr || nm[0] == '\0') {
                continue;
            }
            JsonValue it = JsonValue::object();
            it.set("mode", JsonValue::integer(i));
            it.set("name", JsonValue::string(nm));
            list.push(std::move(it));
        }
        root.set("count", JsonValue::integer(static_cast<std::int64_t>(list.size())));
        root.set("items", std::move(list));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/transitions") {
        /* 转场库：34 种类型 + 分类 + 是否支持方向。
           全部来自 nc_transition_*，不在前端写死列表。 */
        JsonValue root = JsonValue::object();
        JsonValue list = JsonValue::array();
        const int n = nc_transition_type_count();
        for (int i = 0; i < n; ++i) {
            JsonValue it = JsonValue::object();
            it.set("type", JsonValue::integer(i));
            it.set("name", JsonValue::string(nc_transition_type_name(i)));
            it.set("category", JsonValue::string(nc_transition_category(i)));
            it.set("directional", JsonValue::boolean(nc_transition_supports_direction(i) == 1));
            list.push(std::move(it));
        }
        root.set("count", JsonValue::integer(n));
        root.set("items", std::move(list));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/media_library") {
        /* 本机素材库（设计桥宿主目录 design/media）。
           时长由 **真实解码探测** 得到：nc_import_asset 需要调用方给出时长，
           设计桥不能猜，因此这里用 FFmpeg 读取器探测每个文件。
           探测失败的文件不列出（不提供不可用的素材）。 */
        JsonValue root = JsonValue::object();
        JsonValue items = JsonValue::array();
        const std::string dir = rootDir + "/media";
        DIR* d = opendir(dir.c_str());
        if (d != nullptr) {
            std::vector<std::string> files;
            while (dirent* entry = readdir(d)) {
                const std::string name = entry->d_name;
                if (name.empty() || name[0] == '.') continue;
                if (name.size() < 4) continue;
                const std::string ext = name.substr(name.size() - 3);
                if (ext == "mp4" || ext == "mov" || ext == "wav" || ext == "m4a" || ext == "mp3") {
                    files.push_back(name);
                }
            }
            closedir(d);
            std::sort(files.begin(), files.end());
#ifdef NAIROCLIP_HAS_FFMPEG
            for (const std::string& file : files) {
                nairoclip::core::FFmpegMediaReader reader;
                const std::string full = dir + "/" + file;
                const nairoclip::core::Result openRes = reader.open(full);
                std::fprintf(stderr, "[media] %s open=%d %s\n", full.c_str(),
                             (int)openRes.isOk(), openRes.message.c_str());
                if (!openRes.isOk()) continue;
                const auto& probe = reader.probe();
                const std::int64_t durationUs = toMicroseconds(probe.duration);
                if (durationUs <= 0) continue;
                JsonValue item = JsonValue::object();
                item.set("name", JsonValue::string(file));
                item.set("path", JsonValue::string(full));
                item.set("duration_us", JsonValue::integer(durationUs));
                // MediaProbeResult 没有 hasVideo 字段，类型看 MediaType：
                // Video=视频，Audio=音频，其余（Unknown）按音频处理并跳过。
                int mediaType = 1;
                if (probe.type == nairoclip::core::MediaType::Video) mediaType = 0;
                else if (probe.type == nairoclip::core::MediaType::Audio) mediaType = 1;
                else continue;                       // 无法判型：不列给用户
                item.set("type", JsonValue::integer(mediaType));
                item.set("width", JsonValue::integer(probe.width));
                item.set("height", JsonValue::integer(probe.height));
                // FrameRate 内嵌的是 value（Rational），不是裸 num/den
                item.set("fps_num", JsonValue::integer(probe.frameRate.value.num));
                item.set("fps_den", JsonValue::integer(probe.frameRate.value.den));
                items.push(std::move(item));
            }
#else
            (void)files;
#endif
        }
        root.set("items", std::move(items));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/projects") {
        // 真实草稿列表：design/out/projects/*.nclip（由 /api/save 写出）
        JsonValue root = JsonValue::object();
        JsonValue list = JsonValue::array();
        const std::string dir = rootDir + "/out/projects";
        DIR* d = opendir(dir.c_str());
        if (d != nullptr) {
            while (dirent* entry = readdir(d)) {
                const std::string name = entry->d_name;
                if (name.size() < 7 || name.compare(name.size() - 6, 6, ".nclip") != 0) continue;
                bool ok = false;
                const std::string text = readFile(dir + "/" + name, ok);
                if (!ok) continue;
                JsonValue item = JsonValue::object();
                item.set("file", JsonValue::string(name));
                // 从 .nclip 中提取工程名与格式版本（真实内容，不猜）
                JsonValue doc;
                JsonParseError err;
                if (parseJson(text, doc, err) && doc.has("project")) {
                    item.set("name", JsonValue::string(doc.get("project").get("name").asString()));
                    item.set("format", JsonValue::integer(doc.get("formatVersion").asInteger()));
                } else {
                    item.set("name", JsonValue::string(name.substr(0, name.size() - 6)));
                }
                item.set("bytes", JsonValue::integer(static_cast<std::int64_t>(text.size())));
                list.push(std::move(item));
            }
            closedir(d);
        }
        root.set("projects", std::move(list));
        return jsonResponse(toJsonString(root, false));
    }
    if (req.path == "/api/save" && req.method == "POST") {
        JsonValue reqJson;
        JsonParseError err;
        if (!parseJson(req.body, reqJson, err)) {
            return jsonResponse("{\"ok\":false,\"message\":\"请求 JSON 解析失败\"}", 400);
        }
        const std::string name = jstr(reqJson, "name", "未命名项目");
        std::string text;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            if (reqJson.has("name")) nc_set_project_name(gEditor, name.c_str());
            text = takeString(nc_project_save(gEditor));
        }
        if (text.empty()) return jsonResponse("{\"ok\":false,\"message\":\"工程序列化失败\"}", 500);
        std::string safe;
        for (char c : name) safe.push_back((c == '/' || c == '\\' || c == ':') ? '_' : c);
        const std::string dir = rootDir + "/out/projects";
        ::mkdir(dir.c_str(), 0775);
        const std::string path = dir + "/" + safe + ".nclip";
        std::ofstream out(path, std::ios::binary);
        if (!out) return jsonResponse("{\"ok\":false,\"message\":\"无法写入草稿目录\"}", 500);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        JsonValue res = JsonValue::object();
        res.set("ok", JsonValue::boolean(true));
        res.set("file", JsonValue::string(safe + ".nclip"));
        res.set("bytes", JsonValue::integer(static_cast<std::int64_t>(text.size())));
        return jsonResponse(toJsonString(res, false));
    }
    if (req.path == "/api/load" && req.method == "POST") {
        JsonValue reqJson;
        JsonParseError err;
        if (!parseJson(req.body, reqJson, err)) {
            return jsonResponse("{\"ok\":false,\"message\":\"请求 JSON 解析失败\"}", 400);
        }
        const std::string file = jstr(reqJson, "file");
        if (file.empty() || file.find("..") != std::string::npos) {
            return jsonResponse("{\"ok\":false,\"message\":\"非法文件名\"}", 400);
        }
        bool ok = false;
        const std::string text = readFile(rootDir + "/out/projects/" + file, ok);
        if (!ok) return jsonResponse("{\"ok\":false,\"message\":\"草稿不存在\"}", 404);
        char* detail = nullptr;
        int32_t code = 0;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            code = nc_project_load(gEditor, text.c_str(), &detail);
        }
        const std::string message = detail != nullptr ? takeString(detail) : std::string();
        {
            std::lock_guard<std::mutex> lock(gFrameMutex);
            gFrameCache.clear();
        }
        std::string out = "{\"ok\":";
        out += code == NC_OK ? "true" : "false";
        out += ",\"code\":" + std::to_string(code);
        out += ",\"message\":\"" + jsonEscape(message) + "\"}";
        return jsonResponse(out);
    }
    /* ---- 通用 API 直通：前端可调用接口清单里的任意 C ABI ----
       POST /api/call  {"fn":"nc_xxx","args":{...}}
       返回 {"ok":bool,"code":int,"message":"","result":{...}} */
    if (req.path == "/api/call" && req.method == "POST") {
        JsonValue reqJson;
        JsonParseError err;
        if (!parseJson(req.body, reqJson, err)) {
            return jsonResponse("{\"ok\":false,\"code\":1,"
                                "\"message\":\"请求 JSON 解析失败\",\"result\":{}}", 400);
        }
        const std::string fn = jstr(reqJson, "fn");
        if (fn.empty()) {
            return jsonResponse("{\"ok\":false,\"code\":1,"
                                "\"message\":\"缺少 fn\",\"result\":{}}", 400);
        }
        const JsonValue args = reqJson.has("args") ? reqJson.get("args") : JsonValue::object();
        ApiCallResult r;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            /* 手写区优先：它可能有更安全的参数默认值（如 chapter_detect）。 */
            found = dispatchApiSpecial(fn, args, r) || dispatchApi(fn, args, r);
        }
        if (!found) {
            return jsonResponse("{\"ok\":false,\"code\":1,\"message\":\"未登记的接口："
                                + jsonEscape(fn) + "\",\"result\":{}}", 404);
        }
        /* 说明：C ABI 的 int32_t 返回有两种语义 ——
             (a) 状态码：0=NC_OK，1..8=错误码；
             (b) 数值结果：如 nc_track_count 返回 3、nc_abi_version 返回 1。
           桥无法从签名区分，因此 **不臆断**：code 原样返回，
           ok 只在「函数未登记 / HTTP 层失败」时为 false。
           调用方按接口清单里的语义自行判断（状态码类看 code==0）。 */
        std::string out = "{\"ok\":true";
        out += ",\"code\":" + std::to_string(r.code);
        out += ",\"message\":\"" + jsonEscape(gLastError) + "\"";
        out += ",\"result\":" + toJsonString(r.result);
        out += "}";
        return jsonResponse(out);
    }
    if (req.path == "/api/edit" && req.method == "POST") {
        JsonValue reqJson;
        JsonParseError err;
        if (!parseJson(req.body, reqJson, err)) {
            return jsonResponse("{\"ok\":false,\"message\":\"请求 JSON 解析失败\"}", 400);
        }
        int32_t code = 0;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            code = applyEditLocked(reqJson);
        }
        std::string out = "{\"ok\":";
        out += code == NC_OK ? "true" : "false";
        out += ",\"code\":" + std::to_string(code);
        out += ",\"message\":\"" + jsonEscape(gLastError) + "\"}";
        return jsonResponse(out);
    }
    if (req.path == "/api/export" && req.method == "POST") {
        if (gExporting.load()) return jsonResponse("{\"ok\":false,\"message\":\"已有导出任务在运行\"}", 409);
        JsonValue reqJson;
        JsonParseError err;
        if (!parseJson(req.body, reqJson, err)) {
            return jsonResponse("{\"ok\":false,\"message\":\"请求 JSON 解析失败\"}", 400);
        }
        gExportCancel.store(false);
        gExportProgress.store(0.0);
        gExportFrames.store(-1);
        gExportError.clear();
        gExportPath = jstr(reqJson, "path", (rootDir + "/out/nairoclip_export.mp4").c_str());
        gExporting.store(true);
        std::thread(runExport, gExportPath, static_cast<int>(jint(reqJson, "preset", 1)),
                    static_cast<int>(jint(reqJson, "mode", 1)),
                    jbool(reqJson, "attach_ai_label", true) ? 1 : 0)
            .detach();
        return jsonResponse("{\"ok\":true}");
    }
    if (req.path == "/api/export_cancel" && req.method == "POST") {
        gExportCancel.store(true);
        return jsonResponse("{\"ok\":true}");
    }
    return jsonResponse("{\"ok\":false,\"message\":\"未知接口\"}", 404);
}

Response handleStatic(const std::string& path) {
    std::string relative = path.empty() ? "/pages/index.html" : path;
    if (relative == "/") relative = "/pages/index.html";
    if (relative.find("..") != std::string::npos) {
        Response r;
        r.status = 403;
        r.body = "forbidden";
        return r;
    }
    bool ok = false;
    std::string data = readFile(rootDir + relative, ok);
    // `/m/...` 映射到仓库根，供桌面页直接引用 mobile/assets 的
    // token/图标/编辑逻辑 —— 单一来源，不做物理复制（复制会失去同步）。
    if (!ok && relative.rfind("/m/", 0) == 0) {
        data = readFile(repoRoot + "/mobile/" + relative.substr(3), ok);
    }
    if (!ok) {
        Response r;
        r.status = 404;
        r.body = "not found: " + relative;
        return r;
    }
    Response r;
    r.contentType = mimeFor(relative);
    r.body = data;
    return r;
}

void sendAll(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}

void serveClient(int fd) {
    std::string raw;
    char buffer[8192];
    while (true) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        raw.append(buffer, static_cast<std::size_t>(n));
        const std::size_t headerEnd = raw.find("\r\n\r\n");
        if (headerEnd == std::string::npos) {
            if (raw.size() > 1u << 20) break;
            continue;
        }
        std::size_t lengthPos = raw.find("Content-Length:");
        if (lengthPos == std::string::npos) lengthPos = raw.find("content-length:");
        if (lengthPos == std::string::npos) break;
        const std::size_t lineEnd = raw.find("\r\n", lengthPos);
        const std::size_t contentLength = static_cast<std::size_t>(
            std::atol(raw.substr(lengthPos + 15, lineEnd - lengthPos - 15).c_str()));
        if (raw.size() >= headerEnd + 4 + contentLength) break;
        if (raw.size() > 8u << 20) break;
    }
    if (raw.empty()) {
        close(fd);
        return;
    }
    const Request req = parseRequest(raw);
    Response res = req.path.rfind("/api/", 0) == 0 ? handleApi(req) : handleStatic(req.path);

    std::ostringstream head;
    head << "HTTP/1.1 " << res.status << " "
         << (res.status == 200 ? "OK" : (res.status == 404 ? "Not Found" : "Error")) << "\r\n";
    head << "Content-Type: " << res.contentType << "\r\n";
    head << "Content-Length: " << res.body.size() << "\r\n";
    head << res.extraHeaders;
    head << "Connection: close\r\n\r\n";
    sendAll(fd, head.str() + res.body);
    close(fd);
}

}  // namespace

int main(int argc, char** argv) {
    const int port = argc > 1 ? std::atoi(argv[1]) : 8787;
    rootDir = argc > 2 ? argv[2] : "design";
    // 仓库根 = rootDir 的父目录。注意 rootDir 常是相对路径（默认 "design"），
    // 此时 find_last_of('/') 会失败，不能直接退化成 "." —— 那会指到
    // 「design 目录内部」，/m/ 就永远 404。
    {
        std::string r = rootDir;
        while (!r.empty() && r.back() == '/') r.pop_back();
        const std::size_t slash = r.find_last_of('/');
        if (slash != std::string::npos) {
            repoRoot = r.substr(0, slash);
        } else if (r == "." ) {
            repoRoot = ".";
        } else {
            // 相对路径且无斜杠：父目录就是当前工作目录
            repoRoot = ".";
        }
        if (repoRoot.empty()) repoRoot = ".";
    }
    bool lanBind = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--lan") == 0) lanBind = true;
    }
    while (!rootDir.empty() && rootDir.back() == '/') rootDir.pop_back();

    seedProject();
    std::fprintf(stderr, "[harness] 工程就绪：%s（%d 条轨道，时长 %.2fs）；后台预热素材缓存…\n",
                 kProjectName, nc_track_count(gEditor), nc_project_duration_us(gEditor) / 1e6);
    // 素材缓存（缩略图/波形）在后台线程预热：HTTP 立即可用，
    // 未就绪时 /api/thumb 返回 404，前端降级为纯色占位（不阻塞交互）。
    /* 说明：启动时不再批量预热素材缓存。
       原因：nc_thumbnail_generate 单次 10~30s，预热线程持锁期间
       /api/state 等所有请求都会排队，客户端表现为「整站卡死」。
       现在改为按需生成：见 handleApi 里的 /api/thumb 与 /api/waveform。 */

    const int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        std::perror("socket");
        return 1;
    }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(lanBind ? INADDR_ANY : INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        return 1;
    }
    listen(server, 64);
    std::fprintf(stderr, "[harness] http://127.0.0.1:%d/  (bind=%s)\n", port,
                 lanBind ? "0.0.0.0 局域网可访问" : "仅本机");
    while (true) {
        const int fd = accept(server, nullptr, nullptr);
        if (fd < 0) continue;
        std::thread(serveClient, fd).detach();
    }
}
