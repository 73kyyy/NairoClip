#include "nairoclip/core/serialization.h"

#include "nairoclip/core/media.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/speed_curve.h"
#include "nairoclip/core/transition_types.h"

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

// ---------------- 动画通道读写 ----------------

const char* easingTypeName(EasingType t) {
    switch (t) {
        case EasingType::Linear: return "linear";
        case EasingType::EaseIn: return "easeIn";
        case EasingType::EaseOut: return "easeOut";
        case EasingType::EaseInOut: return "easeInOut";
        case EasingType::Stepped: return "stepped";
        case EasingType::Bezier: return "bezier";
    }
    return "linear";
}

bool parseEasingType(const std::string& s, EasingType& out) {
    if (s == "linear") { out = EasingType::Linear; return true; }
    if (s == "easeIn") { out = EasingType::EaseIn; return true; }
    if (s == "easeOut") { out = EasingType::EaseOut; return true; }
    if (s == "easeInOut") { out = EasingType::EaseInOut; return true; }
    if (s == "stepped") { out = EasingType::Stepped; return true; }
    if (s == "bezier") { out = EasingType::Bezier; return true; }
    return false;
}

JsonValue writeEasing(const Easing& e) {
    JsonValue o = JsonValue::object();
    o.set("type", JsonValue::string(easingTypeName(e.type)));
    if (e.type == EasingType::Bezier) {
        JsonValue points = JsonValue::array();
        points.push(JsonValue::number(e.c1x));
        points.push(JsonValue::number(e.c1y));
        points.push(JsonValue::number(e.c2x));
        points.push(JsonValue::number(e.c2y));
        o.set("points", std::move(points));
    }
    return o;
}

bool readEasing(const JsonValue& v, Easing& out, std::string& err) {
    if (!v.isObject()) { err = "缓动项应为对象"; return false; }
    if (!parseEasingType(v.get("type").asString(), out.type)) {
        err = "未知缓动类型: " + v.get("type").asString();
        return false;
    }
    if (out.type == EasingType::Bezier) {
        const JsonValue& points = v.get("points");
        if (!points.isArray() || points.size() != 4) {
            err = "贝塞尔缓动缺少 points";
            return false;
        }
        out = Easing::bezier(points.at(0).asNumber(), points.at(1).asNumber(),
                             points.at(2).asNumber(), points.at(3).asNumber());
    }
    return true;
}

JsonValue writeKeyframeTrack(const KeyframeTrack& track) {
    JsonValue arr = JsonValue::array();
    for (const auto& k : track.keyframes()) {
        JsonValue o = JsonValue::object();
        o.set("time", writeRational(k.time));
        o.set("value", JsonValue::number(k.value));
        o.set("easing", writeEasing(k.easing));
        arr.push(std::move(o));
    }
    return arr;
}

bool readKeyframeTrack(const JsonValue& v, KeyframeTrack& out, std::string& err) {
    out.clear();
    if (v.isNull()) return true;
    if (!v.isArray()) { err = "关键帧通道应为数组"; return false; }
    for (std::size_t i = 0; i < v.size(); ++i) {
        const JsonValue& kv = v.at(i);
        if (!kv.isObject()) { err = "关键帧项应为对象"; return false; }
        Keyframe k;
        if (!readRational(kv.get("time"), "time", k.time, err)) return false;
        k.value = kv.get("value").asNumber();
        if (kv.has("easing")) {
            if (!readEasing(kv.get("easing"), k.easing, err)) return false;
        }
        const Result r = out.insert(k);
        if (!r.isOk()) { err = r.message; return false; }
    }
    return true;
}

JsonValue writeClipAnimation(const ClipAnimation& anim) {
    JsonValue o = JsonValue::object();
    const ClipProperty properties[] = {ClipProperty::Opacity, ClipProperty::Scale,
                                       ClipProperty::PositionX, ClipProperty::PositionY,
                                       ClipProperty::Rotation, ClipProperty::Volume};
    const char* names[] = {"opacity", "scale", "positionX", "positionY", "rotation", "volume"};
    for (std::size_t i = 0; i < 6; ++i) {
        const KeyframeTrack* track = anim.findCurve(properties[i]);
        if (track != nullptr && !track->empty()) {
            o.set(names[i], writeKeyframeTrack(*track));
        }
    }
    return o;
}

bool readClipAnimation(const JsonValue& v, ClipAnimation& out, std::string& err) {
    out.clear();
    if (v.isNull()) return true;
    if (!v.isObject()) { err = "animation 应为对象"; return false; }
    const ClipProperty properties[] = {ClipProperty::Opacity, ClipProperty::Scale,
                                       ClipProperty::PositionX, ClipProperty::PositionY,
                                       ClipProperty::Rotation, ClipProperty::Volume};
    const char* names[] = {"opacity", "scale", "positionX", "positionY", "rotation", "volume"};
    for (std::size_t i = 0; i < 6; ++i) {
        if (v.has(names[i])) {
            if (!readKeyframeTrack(v.get(names[i]), out.curve(properties[i]), err)) return false;
        }
    }
    return true;
}

// ---------------- 速度曲线读写 ----------------

JsonValue writeSpeedCurve(const SpeedCurve& curve) {
    JsonValue arr = JsonValue::array();
    for (const auto& p : curve.points()) {
        JsonValue o = JsonValue::object();
        o.set("t", writeRational(p.sourceTime));
        o.set("speed", writeRational(p.speed));
        o.set("easing", writeEasing(p.easing));
        arr.push(std::move(o));
    }
    return arr;
}

bool readSpeedCurve(const JsonValue& v, SpeedCurve& out, std::string& err) {
    out.clear();
    if (v.isNull()) return true;
    if (!v.isArray()) { err = "speedCurve 应为数组"; return false; }
    for (std::size_t i = 0; i < v.size(); ++i) {
        const JsonValue& pv = v.at(i);
        if (!pv.isObject()) { err = "速度点应为对象"; return false; }
        SpeedPoint p;
        if (!readRational(pv.get("t"), "t", p.sourceTime, err)) return false;
        Rational speed;
        if (!readRational(pv.get("speed"), "speed", speed, err)) return false;
        p.speed = std::move(speed);
        if (pv.has("easing")) {
            if (!readEasing(pv.get("easing"), p.easing, err)) return false;
        }
        const Result r = out.addPoint(p);
        if (!r.isOk()) { err = r.message; return false; }
    }
    return true;
}

// ---------------- 特效读写 ----------------

JsonValue writeEffects(const EffectStack& stack) {
    JsonValue arr = JsonValue::array();
    for (const auto& e : stack.effects()) {
        JsonValue o = JsonValue::object();
        o.set("id", JsonValue::integer(static_cast<std::int64_t>(e.id.value)));
        o.set("type", JsonValue::integer(static_cast<std::int64_t>(e.type)));
        o.set("value", JsonValue::number(e.value));
        o.set("enabled", JsonValue::boolean(e.enabled));
        arr.push(std::move(o));
    }
    return arr;
}

bool readEffects(const JsonValue& v, EffectStack& out, std::string& err) {
    out.clear();
    if (v.isNull()) return true;
    if (!v.isArray()) { err = "effects 应为数组"; return false; }
    for (std::size_t i = 0; i < v.size(); ++i) {
        const JsonValue& ev = v.at(i);
        if (!ev.isObject()) { err = "特效项应为对象"; return false; }
        Effect e;
        e.id = EffectId{static_cast<std::uint64_t>(ev.get("id").asInteger())};
        const std::int64_t typeValue = ev.get("type").asInteger();
        if (typeValue < 0 || typeValue > 12) {
            err = "未知特效类型";
            return false;
        }
        e.type = static_cast<EffectType>(typeValue);
        e.value = ev.get("value").asNumber();
        e.enabled = ev.get("enabled").asBool(true);
        const Result r = out.add(e);
        if (!r.isOk()) { err = r.message; return false; }
    }
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
    if (!c.animation.empty()) {
        o.set("animation", writeClipAnimation(c.animation));
    }
    if (!c.speedCurve.empty()) {
        o.set("speedCurve", writeSpeedCurve(c.speedCurve));
    }
    if (!c.effects.empty()) {
        o.set("effects", writeEffects(c.effects));
    }
    if (c.audioGainDb != 0.0 || c.audioFadeIn.isPositive() || c.audioFadeOut.isPositive()) {
        JsonValue audio = JsonValue::object();
        audio.set("gainDb", JsonValue::number(c.audioGainDb));
        audio.set("fadeIn", writeRational(c.audioFadeIn));
        audio.set("fadeOut", writeRational(c.audioFadeOut));
        o.set("audio", std::move(audio));
    }
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
    out.animation.clear();
    if (v.has("animation")) {
        if (!readClipAnimation(v.get("animation"), out.animation, err)) return false;
    }
    out.speedCurve.clear();
    if (v.has("speedCurve")) {
        if (!readSpeedCurve(v.get("speedCurve"), out.speedCurve, err)) return false;
    }
    out.effects.clear();
    if (v.has("effects")) {
        if (!readEffects(v.get("effects"), out.effects, err)) return false;
    }
    out.audioGainDb = 0.0;
    out.audioFadeIn = Time{};
    out.audioFadeOut = Time{};
    if (v.has("audio")) {
        const JsonValue& audio = v.get("audio");
        if (audio.isObject()) {
            out.audioGainDb = audio.get("gainDb").asNumber(0.0);
            if (audio.has("fadeIn")) {
                if (!readRational(audio.get("fadeIn"), "fadeIn", out.audioFadeIn, err)) return false;
            }
            if (audio.has("fadeOut")) {
                if (!readRational(audio.get("fadeOut"), "fadeOut", out.audioFadeOut, err)) return false;
            }
        }
    }
    return true;
}

// ---------------- 转场读写 ----------------

const char* transitionTypeName(TransitionType t) {
    switch (t) {
        case TransitionType::CrossDissolve: return "crossDissolve";
        case TransitionType::FadeToBlack: return "fadeToBlack";
        case TransitionType::FadeFromBlack: return "fadeFromBlack";
        case TransitionType::FlashWhite: return "flashWhite";
        case TransitionType::WipeLeft: return "wipeLeft";
        case TransitionType::WipeRight: return "wipeRight";
        case TransitionType::SlideUp: return "slideUp";
        case TransitionType::Zoom: return "zoom";
    }
    return "crossDissolve";
}

bool parseTransitionType(const std::string& s, TransitionType& out) {
    if (s == "crossDissolve") { out = TransitionType::CrossDissolve; return true; }
    if (s == "fadeToBlack") { out = TransitionType::FadeToBlack; return true; }
    if (s == "fadeFromBlack") { out = TransitionType::FadeFromBlack; return true; }
    if (s == "flashWhite") { out = TransitionType::FlashWhite; return true; }
    if (s == "wipeLeft") { out = TransitionType::WipeLeft; return true; }
    if (s == "wipeRight") { out = TransitionType::WipeRight; return true; }
    if (s == "slideUp") { out = TransitionType::SlideUp; return true; }
    if (s == "zoom") { out = TransitionType::Zoom; return true; }
    return false;
}

JsonValue writeTransition(const Transition& t) {
    JsonValue o = JsonValue::object();
    o.set("id", JsonValue::integer(static_cast<std::int64_t>(t.id.value)));
    o.set("type", JsonValue::string(transitionTypeName(t.type)));
    o.set("duration", writeRational(t.duration));
    o.set("fromClip", JsonValue::integer(static_cast<std::int64_t>(t.fromClip.value)));
    o.set("toClip", JsonValue::integer(static_cast<std::int64_t>(t.toClip.value)));
    if (!t.name.empty()) {
        o.set("name", JsonValue::string(t.name));
    }
    return o;
}

bool readTransition(const JsonValue& v, Transition& out, std::string& err) {
    if (!v.isObject()) { err = "转场项应为对象"; return false; }
    out.id = TransitionId{static_cast<std::uint64_t>(v.get("id").asInteger())};
    if (!parseTransitionType(v.get("type").asString(), out.type)) {
        err = "未知转场类型: " + v.get("type").asString();
        return false;
    }
    if (!readRational(v.get("duration"), "duration", out.duration, err)) return false;
    out.fromClip = ClipId{static_cast<std::uint64_t>(v.get("fromClip").asInteger())};
    out.toClip = ClipId{static_cast<std::uint64_t>(v.get("toClip").asInteger())};
    out.name = v.get("name").asString();
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
    if (!t.transitions.empty()) {
        JsonValue transitions = JsonValue::array();
        for (const auto& tr : t.transitions) {
            transitions.push(writeTransition(tr));
        }
        o.set("transitions", std::move(transitions));
    }
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
    out.transitions.clear();
    const JsonValue& transitions = v.get("transitions");
    if (transitions.isArray()) {
        for (std::size_t i = 0; i < transitions.size(); ++i) {
            Transition tr;
            if (!readTransition(transitions.at(i), tr, err)) return false;
            out.transitions.push_back(std::move(tr));
        }
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

// ---------------- 字幕读写 ----------------

JsonValue writeSubtitleStyle(const SubtitleStyle& style) {
    JsonValue o = JsonValue::object();
    o.set("fontFamily", JsonValue::string(style.fontFamily));
    o.set("fontSize", JsonValue::number(style.fontSize));
    o.set("color", JsonValue::string(style.color));
    o.set("outlineColor", JsonValue::string(style.outlineColor));
    o.set("outlineWidth", JsonValue::number(style.outlineWidth));
    o.set("backgroundColor", JsonValue::string(style.backgroundColor));
    o.set("backgroundPadding", JsonValue::number(style.backgroundPadding));
    o.set("alignment", JsonValue::integer(style.alignment));
    o.set("anchorX", JsonValue::number(style.anchorX));
    o.set("anchorY", JsonValue::number(style.anchorY));
    return o;
}

SubtitleStyle readSubtitleStyle(const JsonValue& v) {
    SubtitleStyle style;
    if (!v.isObject()) return style;
    style.fontFamily = v.get("fontFamily").asString();
    if (style.fontFamily.empty()) style.fontFamily = "默认";
    style.fontSize = v.get("fontSize").asNumber(48.0);
    style.color = v.get("color").asString();
    if (style.color.empty()) style.color = "#FFFFFF";
    style.outlineColor = v.get("outlineColor").asString();
    if (style.outlineColor.empty()) style.outlineColor = "#000000";
    style.outlineWidth = v.get("outlineWidth").asNumber(2.0);
    style.backgroundColor = v.get("backgroundColor").asString();
    if (style.backgroundColor.empty()) style.backgroundColor = "#00000000";
    style.backgroundPadding = v.get("backgroundPadding").asNumber(8.0);
    style.alignment = static_cast<int>(v.get("alignment").asInteger(1));
    style.anchorX = v.get("anchorX").asNumber(0.5);
    style.anchorY = v.get("anchorY").asNumber(0.9);
    return style;
}

JsonValue writeSubtitleTrack(const SubtitleTrack& track) {
    JsonValue arr = JsonValue::array();
    for (const auto& cue : track.cues()) {
        JsonValue o = JsonValue::object();
        o.set("id", JsonValue::integer(static_cast<std::int64_t>(cue.id.value)));
        o.set("start", writeRational(cue.start));
        o.set("duration", writeRational(cue.duration));
        o.set("text", JsonValue::string(cue.text));
        o.set("enabled", JsonValue::boolean(cue.enabled));
        o.set("style", writeSubtitleStyle(cue.style));
        arr.push(std::move(o));
    }
    return arr;
}

bool readSubtitleTrack(const JsonValue& v, SubtitleTrack& out, std::string& err) {
    out.clear();
    if (v.isNull()) return true;
    if (!v.isArray()) { err = "subtitles 应为数组"; return false; }
    for (std::size_t i = 0; i < v.size(); ++i) {
        const JsonValue& cv = v.at(i);
        if (!cv.isObject()) { err = "字幕项应为对象"; return false; }
        SubtitleCue cue;
        cue.id = SubtitleId{static_cast<std::uint64_t>(cv.get("id").asInteger())};
        if (!readRational(cv.get("start"), "start", cue.start, err)) return false;
        if (!readRational(cv.get("duration"), "duration", cue.duration, err)) return false;
        cue.text = cv.get("text").asString();
        cue.enabled = cv.get("enabled").asBool(true);
        cue.style = readSubtitleStyle(cv.get("style"));
        const Result r = out.insert(std::move(cue));
        if (!r.isOk()) { err = r.message; return false; }
    }
    return true;
}

JsonValue writeSequence(const Sequence& s) {
    JsonValue o = JsonValue::object();
    o.set("id", JsonValue::integer(static_cast<std::int64_t>(s.id.value)));
    o.set("name", JsonValue::string(s.name));
    o.set("frameRate", writeFrameRate(s.frameRate));
    o.set("width", JsonValue::integer(s.width));
    o.set("height", JsonValue::integer(s.height));
    // 画幅适配（仅非默认值写入，保持 .nclip 向后兼容与 diff 稳定）。
    if (s.frameFit != FrameFit::Fit) {
        o.set("frameFit", JsonValue::string(frameFitName(s.frameFit)));
    }
    if (s.backgroundRgba != 0x000000FF) {
        o.set("backgroundRgba", JsonValue::integer(static_cast<std::int64_t>(s.backgroundRgba)));
    }
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
    if (!s.subtitles.empty()) {
        o.set("subtitles", writeSubtitleTrack(s.subtitles));
    }
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
    // 画幅适配（缺省 = Fit；非法值降级为默认并继续，保证工程可打开）。
    out.frameFit = FrameFit::Fit;
    if (v.has("frameFit")) {
        const std::string& fitText = v.get("frameFit").asString();
        FrameFit parsed;
        if (frameFitFromString(fitText.c_str(), parsed)) {
            out.frameFit = parsed;
        }
    }
    out.backgroundRgba = 0x000000FF;
    if (v.has("backgroundRgba")) {
        out.backgroundRgba =
            static_cast<std::uint32_t>(v.get("backgroundRgba").asInteger()) & 0xFFFFFFFFu;
    }
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
    out.subtitles.clear();
    if (v.has("subtitles")) {
        if (!readSubtitleTrack(v.get("subtitles"), out.subtitles, err)) return false;
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
                // 特效 ID 也在全局分配器空间内
                for (const auto& e : c.effects.effects()) {
                    if (e.id.value > maxId) maxId = e.id.value;
                }
            }
            // 转场 ID
            for (const auto& tr : t.transitions) {
                if (tr.id.value > maxId) maxId = tr.id.value;
            }
        }
        // 标记 ID
        for (const auto& m : s.markers) {
            if (m.id.value > maxId) maxId = m.id.value;
        }
        // 字幕 ID
        for (const auto& cue : s.subtitles.cues()) {
            if (cue.id.value > maxId) maxId = cue.id.value;
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
