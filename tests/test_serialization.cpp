#include "test_framework.h"
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
