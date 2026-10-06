#include "test_framework.h"
#include "nairoclip/core/serialization.h"
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
