#include "test_framework.h"
#include "nairoclip/core/project_doctor.h"
using namespace nairoclip::core;
NC_TEST(ProjectDoctor, HealthyProjectNoIssues) {
    Project p;
    Sequence seq;
    seq.id = SequenceId{p.ids.next()};
    p.sequences.push_back(seq);
    MediaAsset a;
    a.id = AssetId{p.ids.next()};
    a.type = MediaType::Video;
    a.duration = Rational::make(60);
    p.assets.push_back(a);
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.asset = a.id;
    c.start = Rational::make(0);
    c.duration = Rational::make(5);
    t.clips.push_back(c);
    p.sequences[0].tracks.push_back(t);
    const HealthReport report = checkProjectHealth(p);
    NC_CHECK(report.healthy());
    NC_CHECK_EQ(report.errorCount(), std::size_t(0));
}
NC_TEST(ProjectDoctor, DetectsDanglingAsset) {
    Project p;
    Sequence seq;
    seq.id = SequenceId{p.ids.next()};
    p.sequences.push_back(seq);
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.asset = AssetId{999};  // 悬空
    c.duration = Rational::make(5);
    t.clips.push_back(c);
    p.sequences[0].tracks.push_back(t);
    const HealthReport report = checkProjectHealth(p);
    NC_CHECK(!report.healthy());
    NC_CHECK_EQ(report.errorCount(), std::size_t(1));
    NC_CHECK_EQ(report.issues[0].code, std::string("dangling-asset"));
}
NC_TEST(ProjectDoctor, RepairsDanglingAssetByDisabling) {
    Project p;
    Sequence seq;
    seq.id = SequenceId{p.ids.next()};
    p.sequences.push_back(seq);
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.asset = AssetId{999};
    c.duration = Rational::make(5);
    t.clips.push_back(c);
    p.sequences[0].tracks.push_back(t);
    const RepairReport report = repairProject(p);
    // 片段被禁用但保留
    NC_CHECK_EQ(p.sequences[0].tracks[0].clips.size(), std::size_t(1));
    NC_CHECK(!p.sequences[0].tracks[0].clips[0].enabled);
    NC_CHECK(!report.actions.empty());
    // 修复后重新检查：悬空片段停用后不再作为问题（导出时被跳过）
    // 注意：检查器仍会报告（因为引用仍悬空），我们验证动作已执行
}
NC_TEST(ProjectDoctor, RepairsOverlapByShifting) {
    Project p;
    Sequence seq;
    seq.id = SequenceId{p.ids.next()};
    p.sequences.push_back(seq);
    MediaAsset a;
    a.id = AssetId{p.ids.next()};
    a.type = MediaType::Video;
    a.duration = Rational::make(60);
    p.assets.push_back(a);
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    Clip c1;
    c1.id = ClipId{p.ids.next()};
    c1.asset = a.id;
    c1.start = Rational::make(0);
    c1.duration = Rational::make(5);
    Clip c2;
    c2.id = ClipId{p.ids.next()};
    c2.asset = a.id;
    c2.start = Rational::make(3);  // 与 c1 重叠
    c2.duration = Rational::make(2);
    t.clips.push_back(c1);
    t.clips.push_back(c2);
    p.sequences[0].tracks.push_back(t);
    const HealthReport before = checkProjectHealth(p);
    NC_CHECK(!before.healthy());
    repairProject(p);
    // 重叠修复：c2 移到 c1 之后
    NC_CHECK(p.sequences[0].tracks[0].clips[1].start == Rational::make(5));
    const HealthReport after = checkProjectHealth(p);
    NC_CHECK(after.healthy());
}
NC_TEST(ProjectDoctor, RemovesNonPositiveDuration) {
    Project p;
    Sequence seq;
    seq.id = SequenceId{p.ids.next()};
    p.sequences.push_back(seq);
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    Clip c;
    c.id = ClipId{p.ids.next()};
    c.duration = Rational::make(0);  // 非法
    t.clips.push_back(c);
    p.sequences[0].tracks.push_back(t);
    repairProject(p);
    NC_CHECK(p.sequences[0].tracks[0].clips.empty());
}
NC_TEST(ProjectDoctor, RemovesDanglingTransition) {
    Project p;
    Sequence seq;
    seq.id = SequenceId{p.ids.next()};
    p.sequences.push_back(seq);
    Track t;
    t.id = TrackId{p.ids.next()};
    t.type = TrackType::Video;
    Transition tr;
    tr.id = TransitionId{p.ids.next()};
    tr.fromClip = ClipId{100};  // 不存在
    tr.toClip = ClipId{101};
    t.transitions.push_back(tr);
    p.sequences[0].tracks.push_back(t);
    repairProject(p);
    NC_CHECK(p.sequences[0].tracks[0].transitions.empty());
}
NC_TEST(ProjectDoctor, AdvancesIdAllocator) {
    Project p;
    p.ids.restore(5);  // 人为落后
    Sequence seq;
    seq.id = SequenceId{100};  // 大于分配器
    p.sequences.push_back(seq);
    repairProject(p);
    NC_CHECK(p.ids.peek() >= 100);
}
NC_TEST(ProjectDoctor, NoSequencesWarning) {
    Project p;
    const HealthReport report = checkProjectHealth(p);
    NC_CHECK_EQ(report.warningCount(), std::size_t(1));
    NC_CHECK_EQ(report.issues[0].code, std::string("no-sequences"));
}
