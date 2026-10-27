#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"
#include <cmath>
using namespace nairoclip::core;
namespace {
Project makeSampleProject() {
    Project p;
    p.name = "示例工程";
    MediaAsset a;
    a.id = AssetId{p.ids.next()};
    a.path = "/videos/a.mp4";
    a.name = "a.mp4";
    a.type = MediaType::Video;
    a.duration = Rational::make(30);
    a.frameRate = fps2997();
    a.width = 1920;
    a.height = 1080;
    p.assets.push_back(a);
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    s.name = "主序列";
    s.frameRate = fps30();
    s.width = 1080;
    s.height = 1920;
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    t.name = "V1";
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.asset = a.id;
    c.start = Rational::make(1, 2);
    c.duration = fromMilliseconds(1500);
    c.sourceIn = Rational::make(2);
    c.speed = Rational::make(3, 2);
    t.clips.push_back(c);
    Marker m;
    m.id = MarkerId{p.ids.next()};
    m.time = Rational::make(5);
    m.name = "高潮";
    m.color = "#FF453A";
    s.markers.push_back(m);
    s.tracks.push_back(t);
    p.sequences.push_back(s);
    return p;
}
}  // namespace
NC_TEST(Serialization, SaveLoadRoundtrip) {
    const Project original = makeSampleProject();
    const std::string text = saveProjectToNclip(original);
    NC_CHECK(!text.empty());
    Project loaded;
    const NclipLoadResult r = loadProjectFromNclip(text, loaded);
    NC_CHECK(r.result.isOk());
    NC_CHECK_EQ(r.fileVersion, 1);
    NC_CHECK_EQ(loaded.name, std::string("示例工程"));
    NC_CHECK_EQ(loaded.assets.size(), std::size_t(1));
    NC_CHECK_EQ(loaded.assets[0].name, std::string("a.mp4"));
    NC_CHECK(loaded.assets[0].duration == Rational::make(30));
    NC_CHECK(loaded.assets[0].frameRate.value == Rational::make(30000, 1001));
    NC_CHECK_EQ(loaded.sequences.size(), std::size_t(1));
    const Sequence& s = loaded.sequences[0];
    NC_CHECK_EQ(s.tracks.size(), std::size_t(1));
    NC_CHECK_EQ(s.tracks[0].clips.size(), std::size_t(1));
    const Clip& c = s.tracks[0].clips[0];
    NC_CHECK(c.start == Rational::make(1, 2));
    NC_CHECK(c.duration == Rational::make(3, 2));
    NC_CHECK(c.sourceIn == Rational::make(2));
    NC_CHECK(c.speed == Rational::make(3, 2));
    NC_CHECK_EQ(s.markers.size(), std::size_t(1));
    NC_CHECK(s.markers[0].name == std::string("高潮"));
}
NC_TEST(Serialization, IdsRestoredForNewEntities) {
    const Project original = makeSampleProject();
    const std::string text = saveProjectToNclip(original);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const std::uint64_t nextId = loaded.ids.next();
    NC_CHECK(nextId > original.ids.peek());
}
NC_TEST(Serialization, RejectsWrongFormat) {
    Project out;
    const NclipLoadResult r = loadProjectFromNclip("{\"format\":\"other\",\"version\":1}", out);
    NC_CHECK(!r.result.isOk());
    NC_CHECK(r.result.code == ErrorCode::InvalidArgument);
}
NC_TEST(Serialization, RejectsFutureVersion) {
    Project out;
    const NclipLoadResult r =
        loadProjectFromNclip("{\"format\":\"nclip\",\"version\":999}", out);
    NC_CHECK(!r.result.isOk());
    NC_CHECK(r.result.code == ErrorCode::Unsupported);
    NC_CHECK_EQ(r.fileVersion, 999);
}
NC_TEST(Serialization, RejectsBadJson) {
    Project out;
    const NclipLoadResult r = loadProjectFromNclip("{broken", out);
    NC_CHECK(!r.result.isOk());
    NC_CHECK(r.result.code == ErrorCode::InvalidArgument);
}
NC_TEST(Serialization, RejectsDanglingAssetReference) {
    // 片段引用了不存在的素材 ID → 拒绝载入
    const std::string text = R"({"format":"nclip","version":1,"project":{"name":"t"},"assets":[],"sequences":[{"id":1,"name":"s","frameRate":[30,1],"width":1920,"height":1080,"tracks":[{"id":2,"type":"video","name":"v","clips":[{"id":3,"asset":999,"start":[0,1],"duration":[1,1],"sourceIn":[0,1],"speed":[1,1]}]}],"markers":[]}]})";
    Project out;
    const NclipLoadResult r = loadProjectFromNclip(text, out);
    NC_CHECK(!r.result.isOk());
}
NC_TEST(Serialization, FailedLoadLeavesTargetUnmodified) {
    Project target;
    target.name = "原工程";
    const NclipLoadResult r = loadProjectFromNclip("not json at all", target);
    NC_CHECK(!r.result.isOk());
    NC_CHECK_EQ(target.name, std::string("原工程"));
}
NC_TEST(Serialization, AnimationRoundtrip) {
    Project p;
    p.name = "动画工程";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    Track t;
    t.id = TrackId{p.ids.next()};
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.start = Rational::make(0);
    c.duration = Rational::make(5);
    Keyframe k1;
    k1.time = Rational::make(0);
    k1.value = 0.0;
    k1.easing = Easing::easeIn();
    Keyframe k2;
    k2.time = Rational::make(2);
    k2.value = 1.0;
    Keyframe k3;
    k3.time = Rational::make(0);
    k3.value = 1.5;
    k3.easing = Easing::bezier(0.2, 0.4, 0.8, 0.6);
    Keyframe k4;
    k4.time = Rational::make(4);
    k4.value = 3.0;
    NC_CHECK(c.animation.curve(ClipProperty::Opacity).insert(k1).isOk());
    NC_CHECK(c.animation.curve(ClipProperty::Opacity).insert(k2).isOk());
    NC_CHECK(c.animation.curve(ClipProperty::Scale).insert(k3).isOk());
    NC_CHECK(c.animation.curve(ClipProperty::Scale).insert(k4).isOk());
    t.clips.push_back(c);
    s.tracks.push_back(t);
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Clip& lc = loaded.sequences[0].tracks[0].clips[0];
    NC_CHECK(lc.animation.hasCurve(ClipProperty::Opacity));
    NC_CHECK(lc.animation.hasCurve(ClipProperty::Scale));
    NC_CHECK_EQ(lc.animation.curveCount(), std::size_t(2));
    const KeyframeTrack* opacity = lc.animation.findCurve(ClipProperty::Opacity);
    NC_CHECK(opacity != nullptr);
    NC_CHECK_EQ(opacity->size(), std::size_t(2));
    NC_CHECK(opacity->keyframes()[0].easing.type == EasingType::EaseIn);
    const KeyframeTrack* scale = lc.animation.findCurve(ClipProperty::Scale);
    NC_CHECK(scale != nullptr);
    NC_CHECK(scale->keyframes()[0].easing.type == EasingType::Bezier);
    NC_CHECK(std::abs(scale->keyframes()[0].easing.c1x - 0.2) < 1e-9);
    NC_CHECK(std::abs(scale->keyframes()[0].easing.c2y - 0.6) < 1e-9);
}
NC_TEST(Serialization, ClipWithoutAnimationOmitsField) {
    Project p;
    p.name = "n";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    Track t;
    t.id = TrackId{p.ids.next()};
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.duration = Rational::make(1);
    t.clips.push_back(c);
    s.tracks.push_back(t);
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    NC_CHECK(text.find("animation") == std::string::npos);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK(loaded.sequences[0].tracks[0].clips[0].animation.empty());
}
NC_TEST(Serialization, SubtitleRoundtrip) {
    Project p;
    p.name = "字幕工程";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    SubtitleCue cue;
    cue.id = SubtitleId{p.ids.next()};
    cue.start = Rational::make(1);
    cue.duration = Rational::make(2);
    cue.text = "你好，世界";
    cue.style.fontSize = 64.0;
    cue.style.color = "#FFD700";
    cue.style.alignment = 2;
    NC_CHECK(s.subtitles.insert(cue).isOk());
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const SubtitleTrack& st = loaded.sequences[0].subtitles;
    NC_CHECK_EQ(st.size(), std::size_t(1));
    NC_CHECK(st.cues()[0].text == std::string("你好，世界"));
    NC_CHECK(st.cues()[0].start == Rational::make(1));
    NC_CHECK(st.cues()[0].duration == Rational::make(2));
    NC_CHECK(std::abs(st.cues()[0].style.fontSize - 64.0) < 1e-9);
    NC_CHECK(st.cues()[0].style.color == std::string("#FFD700"));
    NC_CHECK_EQ(st.cues()[0].style.alignment, 2);
}
NC_TEST(Serialization, TransitionRoundtrip) {
    Project p;
    p.name = "转场工程";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    Track t;
    t.id = TrackId{p.ids.next()};
    Clip c1;
    c1.id = ClipId{p.ids.next()};
    c1.start = Rational::make(0);
    c1.duration = Rational::make(2);
    Clip c2;
    c2.id = ClipId{p.ids.next()};
    c2.start = Rational::make(3);
    c2.duration = Rational::make(2);
    t.clips.push_back(c1);
    t.clips.push_back(c2);
    Transition tr;
    tr.id = TransitionId{p.ids.next()};
    tr.type = TransitionType::WipeLeft;
    tr.duration = Rational::make(3, 2);
    tr.fromClip = c1.id;
    tr.toClip = c2.id;
    tr.name = "左向擦除";
    t.transitions.push_back(tr);
    s.tracks.push_back(t);
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Track& lt = loaded.sequences[0].tracks[0];
    NC_CHECK_EQ(lt.transitions.size(), std::size_t(1));
    NC_CHECK(lt.transitions[0].type == TransitionType::WipeLeft);
    NC_CHECK(lt.transitions[0].duration == Rational::make(3, 2));
    NC_CHECK(lt.transitions[0].fromClip == c1.id);
    NC_CHECK(lt.transitions[0].toClip == c2.id);
    NC_CHECK(lt.transitions[0].name == std::string("左向擦除"));
}
NC_TEST(Serialization, SpeedCurveRoundtrip) {
    Project p;
    p.name = "变速工程";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    Track t;
    t.id = TrackId{p.ids.next()};
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.duration = Rational::make(5);
    SpeedPoint p1;
    p1.sourceTime = Rational::make(0);
    p1.speed = Rational::make(1);
    SpeedPoint p2;
    p2.sourceTime = Rational::make(2);
    p2.speed = Rational::make(4);
    p2.easing = Easing::easeInOut();
    NC_CHECK(c.speedCurve.addPoint(p1).isOk());
    NC_CHECK(c.speedCurve.addPoint(p2).isOk());
    t.clips.push_back(c);
    s.tracks.push_back(t);
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Clip& lc = loaded.sequences[0].tracks[0].clips[0];
    NC_CHECK_EQ(lc.speedCurve.size(), std::size_t(2));
    NC_CHECK(lc.speedCurve.points()[1].speed == Rational::make(4));
    NC_CHECK(lc.speedCurve.points()[1].easing.type == EasingType::EaseInOut);
}
NC_TEST(Serialization, ClipAudioRoundtrip) {
    Project p;
    p.name = "音频工程";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    Track t;
    t.id = TrackId{p.ids.next()};
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.duration = Rational::make(10);
    c.audioGainDb = -7.5;
    c.audioFadeIn = Rational::make(1, 2);
    c.audioFadeOut = Rational::make(3, 2);
    t.clips.push_back(c);
    s.tracks.push_back(t);
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Clip& lc = loaded.sequences[0].tracks[0].clips[0];
    NC_CHECK(std::abs(lc.audioGainDb - (-7.5)) < 1e-9);
    NC_CHECK(lc.audioFadeIn == Rational::make(1, 2));
    NC_CHECK(lc.audioFadeOut == Rational::make(3, 2));
}
NC_TEST(Serialization, EffectsRoundtrip) {
    Project p;
    p.name = "特效工程";
    Sequence s;
    s.id = SequenceId{p.ids.next()};
    Track t;
    t.id = TrackId{p.ids.next()};
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.duration = Rational::make(5);
    Effect e1;
    e1.id = EffectId{p.ids.next()};
    e1.type = EffectType::Contrast;
    e1.value = 1.8;
    Effect e2;
    e2.id = EffectId{p.ids.next()};
    e2.type = EffectType::GaussianBlur;
    e2.value = 12.5;
    e2.enabled = false;
    NC_CHECK(c.effects.add(e1).isOk());
    NC_CHECK(c.effects.add(e2).isOk());
    t.clips.push_back(c);
    s.tracks.push_back(t);
    p.sequences.push_back(s);
    const std::string text = saveProjectToNclip(p);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Clip& lc = loaded.sequences[0].tracks[0].clips[0];
    NC_CHECK_EQ(lc.effects.size(), std::size_t(2));
    NC_CHECK(lc.effects.effects()[0].type == EffectType::Contrast);
    NC_CHECK(std::abs(lc.effects.effects()[0].value - 1.8) < 1e-9);
    NC_CHECK(lc.effects.effects()[1].type == EffectType::GaussianBlur);
    NC_CHECK(lc.effects.effects()[1].enabled == false);
}

NC_TEST(Serialization, RoundTripsAnimationAndSpeedCurve) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.type = MediaType::Video;
    a.duration = Rational::make(60);
    a.path = "/v/a.mp4";
    project.assets.push_back(a);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip c;
    c.asset = project.assets.front().id;
    c.start = Rational::make(0);
    c.duration = Rational::make(5);
    // 关键帧：不透明度 1 → 0
    NC_CHECK(c.animation.curve(ClipProperty::Opacity)
                 .insert(Keyframe{Rational::make(0), 1.0})
                 .isOk());
    NC_CHECK(c.animation.curve(ClipProperty::Opacity)
                 .insert(Keyframe{Rational::make(5), 0.0})
                 .isOk());
    // 速度曲线：1x → 2x
    NC_CHECK(c.speedCurve
                 .addPoint(SpeedPoint{Rational::make(0), Rational::make(1), Easing{}})
                 .isOk());
    NC_CHECK(c.speedCurve
                 .addPoint(SpeedPoint{Rational::make(10), Rational::make(2), Easing{}})
                 .isOk());
    // 音频属性
    c.audioGainDb = -3.0;
    c.audioFadeIn = Rational::make(1, 2);
    c.audioFadeOut = Rational::make(1, 4);
    ClipId clipId;
    NC_CHECK(editor.insertClip(seqId, trackId, c, &clipId).isOk());
    // 特效
    Effect e;
    e.id = EffectId{project.ids.next()};
    e.type = EffectType::Brightness;
    e.value = 0.3;
    NC_CHECK(editor.addEffect(seqId, trackId, clipId, e).isOk());

    // 存读往返
    const std::string saved = saveProjectToNclip(project);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(saved, loaded).result.isOk());
    const Clip& lc = loaded.mainSequence()->tracks[0].clips[0];
    // 动画
    const KeyframeTrack* opacityCurve = lc.animation.findCurve(ClipProperty::Opacity);
    NC_CHECK(opacityCurve != nullptr);
    NC_CHECK_EQ(opacityCurve->keyframes().size(), std::size_t(2));
    NC_CHECK(std::abs(lc.animation.sample(ClipProperty::Opacity, Rational::make(0)) - 1.0) <
             1e-6);
    NC_CHECK(std::abs(lc.animation.sample(ClipProperty::Opacity, Rational::make(5)) - 0.0) <
             1e-6);
    // 速度曲线
    NC_CHECK(!lc.speedCurve.empty());
    NC_CHECK_EQ(lc.speedCurve.size(), std::size_t(2));
    NC_CHECK(std::abs(lc.speedCurve.speedAt(Rational::make(0)) - 1.0) < 1e-6);
    NC_CHECK(std::abs(lc.speedCurve.speedAt(Rational::make(10)) - 2.0) < 1e-6);
    // 音频
    NC_CHECK(std::abs(lc.audioGainDb - (-3.0)) < 1e-9);
    NC_CHECK(lc.audioFadeIn == Rational::make(1, 2));
    NC_CHECK(lc.audioFadeOut == Rational::make(1, 4));
    // 特效
    NC_CHECK_EQ(lc.effects.size(), std::size_t(1));
    NC_CHECK(lc.effects.effects()[0].type == EffectType::Brightness);
    NC_CHECK(std::abs(lc.effects.effects()[0].value - 0.3) < 1e-9);
}

// =============================================================================
// 版本迁移（B0-5）
// =============================================================================
//
// 背景：原实现只拒绝高版本，低版本按当前结构直接读 —— 能否成功取决于
// "新字段是否恰好有默认值"。一旦某次升级忘了给默认值，用户的老工程就打不开，
// 而且是发版后才暴露。
//
// 本组用例验证：缺失字段的旧文件能被加载，且缺失项取合理默认值。

NC_TEST(Serialization, MinimalV1FileLoadsWithDefaults) {
    // 只保留必需字段，其余全部缺失 —— 模拟极早期版本的输出
    const std::string minimal =
        "{\"format\":\"nclip\",\"version\":1,"
        "\"project\":{\"name\":\"老工程\"},"
        "\"assets\":[],"
        "\"sequences\":[]}";
    Project out;
    const NclipLoadResult r = loadProjectFromNclip(minimal, out);
    NC_CHECK(r.result.isOk());
    NC_CHECK_EQ(r.fileVersion, 1);
    NC_CHECK(out.name == "老工程");
    NC_CHECK(out.assets.empty());
    NC_CHECK(out.sequences.empty());
}

NC_TEST(Serialization, V1FileWithoutProxiesLoads) {
    // proxies 是后加的字段（仅在有记录时写入）——缺失必须能正常加载
    const std::string text =
        "{\"format\":\"nclip\",\"version\":1,"
        "\"project\":{\"name\":\"无代理工程\"},"
        "\"assets\":[],"
        "\"sequences\":[]}";
    Project out;
    const NclipLoadResult r = loadProjectFromNclip(text, out);
    NC_CHECK(r.result.isOk());
    NC_CHECK(out.proxies.records.empty());
}

NC_TEST(Serialization, V1MissingProjectNameGetsDefault) {
    // project.name 缺失时应给「未命名项目」，而不是空串
    const std::string text =
        "{\"format\":\"nclip\",\"version\":1,"
        "\"project\":{},"
        "\"assets\":[],"
        "\"sequences\":[]}";
    Project out;
    const NclipLoadResult r = loadProjectFromNclip(text, out);
    NC_CHECK(r.result.isOk());
    NC_CHECK(!out.name.empty());
}

NC_TEST(Serialization, FutureVersionMessageIsActionable) {
    // 高版本拒绝时，提示必须告诉用户"升级应用"，而不是只说"不支持"
    Project out;
    const NclipLoadResult r =
        loadProjectFromNclip("{\"format\":\"nclip\",\"version\":999}", out);
    NC_CHECK(!r.result.isOk());
    NC_CHECK(r.result.code == ErrorCode::Unsupported);
    // 消息中应包含版本号信息，便于用户与客服判断
    NC_CHECK(r.result.message.find("999") != std::string::npos);
}

NC_TEST(Serialization, ZeroAndNegativeVersionRejected) {
    Project out;
    const NclipLoadResult zero =
        loadProjectFromNclip("{\"format\":\"nclip\",\"version\":0}", out);
    NC_CHECK(!zero.result.isOk());
    NC_CHECK(zero.result.code == ErrorCode::InvalidArgument);

    Project out2;
    const NclipLoadResult neg =
        loadProjectFromNclip("{\"format\":\"nclip\",\"version\":-1}", out2);
    NC_CHECK(!neg.result.isOk());
}
