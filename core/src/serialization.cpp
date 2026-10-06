#include "nairoclip/core/serialization.h"

#include "nairoclip/core/media.h"

namespace nairoclip {
namespace core {

namespace {

// ---------------- 基础类型读写 ----------------

JsonValue writeRational(const Rational& r) {
    JsonValue arr = JsonValue::array();
    arr.push(JsonValue::integer(r.num));
    arr.push(JsonValue::integer(r.den));
    return arr;
}

bool readRational(const JsonValue& v, const char* field, Rational& out, std::string& err) {
    if (!v.isArray() || v.size() != 2 || !v.at(0).isInteger() || !v.at(1).isInteger()) {
        err = std::string("字段 ") + field + " 应为 [分子, 分母] 数组";
        return false;
    }
    out = Rational::make(v.at(0).asInteger(), v.at(1).asInteger());
    return true;
}

JsonValue writeFrameRate(const FrameRate& fps) {
    return writeRational(fps.value);
}

bool readFrameRate(const JsonValue& v, FrameRate& out, std::string& err) {
    Rational r;
    if (!readRational(v, "frameRate", r, err)) return false;
    out.value = r;
    return true;
}

const char* mediaTypeName(MediaType t) {
    switch (t) {
        case MediaType::Video: return "video";
        case MediaType::Audio: return "audio";
        case MediaType::Image: return "image";
        case MediaType::Unknown: return "unknown";
    }
    return "unknown";
}

bool parseMediaType(const std::string& s, MediaType& out) {
    if (s == "video") { out = MediaType::Video; return true; }
    if (s == "audio") { out = MediaType::Audio; return true; }
    if (s == "image") { out = MediaType::Image; return true; }
    if (s == "unknown") { out = MediaType::Unknown; return true; }
    return false;
}

const char* trackTypeName(TrackType t) {
    switch (t) {
        case TrackType::Video: return "video";
        case TrackType::Audio: return "audio";
        case TrackType::Subtitle: return "subtitle";
        case TrackType::Effect: return "effect";
    }
    return "video";
}

bool parseTrackType(const std::string& s, TrackType& out) {
    if (s == "video") { out = TrackType::Video; return true; }
    if (s == "audio") { out = TrackType::Audio; return true; }
    if (s == "subtitle") { out = TrackType::Subtitle; return true; }
    if (s == "effect") { out = TrackType::Effect; return true; }
    return false;
}

// ---------------- 实体读写 ----------------

JsonValue writeAsset(const MediaAsset& a) {
    JsonValue o = JsonValue::object();
    o.set("id", JsonValue::integer(static_cast<std::int64_t>(a.id.value)));
    o.set("path", JsonValue::string(a.path));
    o.set("name", JsonValue::string(a.name));
    o.set("type", JsonValue::string(mediaTypeName(a.type)));
    o.set("duration", writeRational(a.duration));
    o.set("frameRate", writeFrameRate(a.frameRate));
    o.set("width", JsonValue::integer(a.width));
    o.set("height", JsonValue::integer(a.height));
    o.set("sampleRate", JsonValue::integer(a.sampleRate));
    o.set("channels", JsonValue::integer(a.channels));
    o.set("offline", JsonValue::boolean(a.offline));
    return o;
}

bool readAsset(const JsonValue& v, MediaAsset& out, std::string& err) {
    if (!v.isObject()) { err = "素材项应为对象"; return false; }
    out.id = AssetId{static_cast<std::uint64_t>(v.get("id").asInteger())};
    out.path = v.get("path").asString();
    out.name = v.get("name").asString();
    if (!parseMediaType(v.get("type").asString(), out.type)) {
        err = "未知素材类型: " + v.get("type").asString();
        return false;
    }
    if (!readRational(v.get("duration"), "duration", out.duration, err)) return false;
    out.frameRate.value = Rational::make(30);
    if (v.has("frameRate")) {
        if (!readFrameRate(v.get("frameRate"), out.frameRate, err)) return false;
    }
    out.width = static_cast<int>(v.get("width").asInteger());
    out.height = static_cast<int>(v.get("height").asInteger());
    out.sampleRate = static_cast<int>(v.get("sampleRate").asInteger());
    out.channels = static_cast<int>(v.get("channels").asInteger());
    out.offline = v.get("offline").asBool(false);
    return true;
}

JsonValue writeClip(const Clip& c) {
    JsonValue o = JsonValue::object();
    o.set("id", JsonValue::integer(static_cast<std::int64_t>(c.id.value)));
    o.set("asset", JsonValue::integer(static_cast<std::int64_t>(c.asset.value)));
    o.set("start", writeRational(c.start));
    o.set("duration", writeRational(c.duration));
    o.set("sourceIn", writeRational(c.sourceIn));
    o.set("speed", writeRational(c.speed));
    o.set("enabled", JsonValue::boolean(c.enabled));
    return o;
}

bool readClip(const JsonValue& v, Clip& out, std::string& err) {
    if (!v.isObject()) { err = "片段项应为对象"; return false; }
    out.id = ClipId{static_cast<std::uint64_t>(v.get("id").asInteger())};
    out.asset = AssetId{static_cast<std::uint64_t>(v.get("asset").asInteger())};
    if (!readRational(v.get("start"), "start", out.start, err)) return false;
    if (!readRational(v.get("duration"), "duration", out.duration, err)) return false;
    if (!readRational(v.get("sourceIn"), "sourceIn", out.sourceIn, err)) return false;
    out.speed = Rational::make(1);
    if (v.has("speed")) {
        if (!readRational(v.get("speed"), "speed", out.speed, err)) return false;
    }
    out.enabled = v.get("enabled").asBool(true);
    return true;
}

JsonValue writeTrack(const Track& t) {
    JsonValue o = JsonValue::object();
    o.set("id", JsonValue::integer(static_cast<std::int64_t>(t.id.value)));
    o.set("type", JsonValue::string(trackTypeName(t.type)));
    o.set("name", JsonValue::string(t.name));
    o.set("locked", JsonValue::boolean(t.locked));
    o.set("muted", JsonValue::boolean(t.muted));
    o.set("hidden", JsonValue::boolean(t.hidden));
    JsonValue clips = JsonValue::array();
    for (const auto& c : t.clips) {
        clips.push(writeClip(c));
    }
    o.set("clips", std::move(clips));
    return o;
}

bool readTrack(const JsonValue& v, Track& out, std::string& err) {
    if (!v.isObject()) { err = "轨道项应为对象"; return false; }
    out.id = TrackId{static_cast<std::uint64_t>(v.get("id").asInteger())};
    if (!parseTrackType(v.get("type").asString(), out.type)) {
        err = "未知轨道类型: " + v.get("type").asString();
        return false;
    }
    out.name = v.get("name").asString();
    out.locked = v.get("locked").asBool(false);
    out.muted = v.get("muted").asBool(false);
    out.hidden = v.get("hidden").asBool(false);
    const JsonValue& clips = v.get("clips");
    if (!clips.isArray()) { err = "轨道缺少 clips 数组"; return false; }
    out.clips.clear();
    for (std::size_t i = 0; i < clips.size(); ++i) {
        Clip c;
        if (!readClip(clips.at(i), c, err)) return false;
        out.clips.push_back(std::move(c));
    }
    return true;
}

JsonValue writeMarker(const Marker& m) {
    JsonValue o = JsonValue::object();
    o.set("id", JsonValue::integer(static_cast<std::int64_t>(m.id.value)));
    o.set("time", writeRational(m.time));
    o.set("name", JsonValue::string(m.name));
    o.set("color", JsonValue::string(m.color));
    return o;
}

bool readMarker(const JsonValue& v, Marker& out, std::string& err) {
    if (!v.isObject()) { err = "标记项应为对象"; return false; }
    out.id = MarkerId{static_cast<std::uint64_t>(v.get("id").asInteger())};
    if (!readRational(v.get("time"), "time", out.time, err)) return false;
    out.name = v.get("name").asString();
    out.color = v.get("color").asString();
    return true;
}

JsonValue writeSequence(const Sequence& s) {
    JsonValue o = JsonValue::object();
    o.set("id", JsonValue::integer(static_cast<std::int64_t>(s.id.value)));
    o.set("name", JsonValue::string(s.name));
    o.set("frameRate", writeFrameRate(s.frameRate));
    o.set("width", JsonValue::integer(s.width));
    o.set("height", JsonValue::integer(s.height));
    JsonValue tracks = JsonValue::array();
    for (const auto& t : s.tracks) {
        tracks.push(writeTrack(t));
    }
    o.set("tracks", std::move(tracks));
    JsonValue markers = JsonValue::array();
    for (const auto& m : s.markers) {
        markers.push(writeMarker(m));
    }
    o.set("markers", std::move(markers));
    return o;
}

bool readSequence(const JsonValue& v, Sequence& out, std::string& err) {
    if (!v.isObject()) { err = "序列项应为对象"; return false; }
    out.id = SequenceId{static_cast<std::uint64_t>(v.get("id").asInteger())};
    out.name = v.get("name").asString();
    out.frameRate = FrameRate::of(30);
    if (v.has("frameRate")) {
        if (!readFrameRate(v.get("frameRate"), out.frameRate, err)) return false;
    }
    out.width = static_cast<int>(v.get("width").asInteger());
    out.height = static_cast<int>(v.get("height").asInteger());
    const JsonValue& tracks = v.get("tracks");
    if (!tracks.isArray()) { err = "序列缺少 tracks 数组"; return false; }
    out.tracks.clear();
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        Track t;
        if (!readTrack(tracks.at(i), t, err)) return false;
        out.tracks.push_back(std::move(t));
    }
    const JsonValue& markers = v.get("markers");
    out.markers.clear();
    if (markers.isArray()) {
        for (std::size_t i = 0; i < markers.size(); ++i) {
            Marker m;
            if (!readMarker(markers.at(i), m, err)) return false;
            out.markers.push_back(std::move(m));
        }
    }
    return true;
}

/// 校验载入后的工程引用完整性（片段引用的素材必须存在；ID 不重复）。
bool validateReferences(const Project& project, std::string& err) {
    for (const auto& seq : project.sequences) {
        for (const auto& track : seq.tracks) {
            for (const auto& clip : track.clips) {
                if (!clip.valid()) {
                    err = "存在非法片段（时长或速度异常）";
                    return false;
                }
                if (clip.asset.isValid() && project.findAsset(clip.asset) == nullptr) {
                    err = "片段引用了不存在的素材";
                    return false;
                }
            }
            if (!track.invariantsHold()) {
                err = "轨道不变量被破坏（排序或重叠）";
                return false;
            }
        }
    }
    return true;
}

/// 计算所有实体 ID 的最大值，用于恢复分配器。
uint64_t maxIdOf(const Project& project) {
    std::uint64_t maxId = 0;
    for (const auto& a : project.assets) {
        if (a.id.value > maxId) maxId = a.id.value;
    }
    for (const auto& s : project.sequences) {
        if (s.id.value > maxId) maxId = s.id.value;
        for (const auto& t : s.tracks) {
            if (t.id.value > maxId) maxId = t.id.value;
            for (const auto& c : t.clips) {
                if (c.id.value > maxId) maxId = c.id.value;
            }
        }
        for (const auto& m : s.markers) {
            if (m.id.value > maxId) maxId = m.id.value;
        }
    }
    return maxId;
}

}  // namespace

std::string saveProjectToNclip(const Project& project) {
    JsonValue root = JsonValue::object();
    root.set("format", JsonValue::string("nclip"));
    root.set("version", JsonValue::integer(kNclipFormatVersion));
    JsonValue meta = JsonValue::object();
    meta.set("name", JsonValue::string(project.name));
    root.set("project", std::move(meta));
    JsonValue assets = JsonValue::array();
    for (const auto& a : project.assets) {
        assets.push(writeAsset(a));
    }
    root.set("assets", std::move(assets));
    JsonValue sequences = JsonValue::array();
    for (const auto& s : project.sequences) {
        sequences.push(writeSequence(s));
    }
    root.set("sequences", std::move(sequences));
    return toJsonString(root, true);
}

NclipLoadResult loadProjectFromNclip(const std::string& text, Project& out) {
    NclipLoadResult result;
    JsonValue root;
    JsonParseError parseError;
    if (!parseJson(text, root, parseError)) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "JSON 解析失败: " + parseError.message);
        return result;
    }
    if (!root.isObject()) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "根节点应为对象");
        return result;
    }
    if (root.get("format").asString() != "nclip") {
        result.result = Result::fail(ErrorCode::InvalidArgument, "文件格式标识不是 nclip");
        return result;
    }
    const int version = static_cast<int>(root.get("version").asInteger());
    result.fileVersion = version;
    if (version <= 0) {
        result.result = Result::fail(ErrorCode::InvalidArgument, "缺少版本号");
        return result;
    }
    if (version > kNclipFormatVersion) {
        result.result = Result::fail(ErrorCode::Unsupported, "文件版本高于当前程序支持的版本");
        return result;
    }
    Project loaded;
    loaded.name = root.get("project").get("name").asString();
    if (loaded.name.empty()) {
        loaded.name = "未命名项目";
    }
    std::string err;
    const JsonValue& assets = root.get("assets");
    if (assets.isArray()) {
        for (std::size_t i = 0; i < assets.size(); ++i) {
            MediaAsset a;
            if (!readAsset(assets.at(i), a, err)) {
                result.result = Result::fail(ErrorCode::InvalidArgument, err);
                return result;
            }
            loaded.assets.push_back(std::move(a));
        }
    }
    const JsonValue& sequences = root.get("sequences");
    if (sequences.isArray()) {
        for (std::size_t i = 0; i < sequences.size(); ++i) {
            Sequence s;
            if (!readSequence(sequences.at(i), s, err)) {
                result.result = Result::fail(ErrorCode::InvalidArgument, err);
                return result;
            }
            loaded.sequences.push_back(std::move(s));
        }
    }
    if (!validateReferences(loaded, err)) {
        result.result = Result::fail(ErrorCode::InvalidArgument, err);
        return result;
    }
    loaded.ids.restore(maxIdOf(loaded));
    out = std::move(loaded);
    result.result = Result::ok();
    return result;
}

}  // namespace core
}  // namespace nairoclip
