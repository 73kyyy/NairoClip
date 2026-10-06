#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"
using namespace nairoclip::core;
// ===== 边界与防御性测试 =====
NC_TEST(Boundary, HugeTimeValues) {
    // 极大时间值不应溢出崩溃
    const Rational huge = Rational::make(1000000000, 1);  // 10 亿秒
    const Time t{huge};
    NC_CHECK(t.isPositive());
    NC_CHECK(t.toDouble() > 1e8);
}
NC_TEST(Boundary, PrecisionFraction) {
    // 1/3 + 1/3 + 1/3 == 1（有理数精确）
    const Rational third = Rational::make(1, 3);
    const Rational sum = third + third + third;
    NC_CHECK(sum == Rational::make(1));
}
NC_TEST(Boundary, InsertAtVeryLargeTime) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.type = MediaType::Video;
    a.duration = Rational::make(1000000000);
    project.assets.push_back(a);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip c;
    c.asset = a.id;
    c.start = Rational::make(999999999);  // ~10 亿秒起点
    c.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(seqId, trackId, c).isOk());
    const Track* t = findTrack(project, seqId, trackId);
    NC_CHECK_EQ(t->clips.size(), std::size_t(1));
    // 存档读档
    const std::string text = saveProjectToNclip(project);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK(loaded.mainSequence()->tracks[0].clips[0].start == Rational::make(999999999));
}
NC_TEST(Boundary, ZeroDurationClipRejected) {
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
    project.assets.push_back(a);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip c;
    c.asset = a.id;
    c.start = Rational::make(0);
    c.duration = Rational::make(0);
    const Result r = editor.insertClip(seqId, trackId, c);
    NC_CHECK(!r.isOk());
}
