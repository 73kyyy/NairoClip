#include "test_framework.h"
#include "nairoclip/core/export_plan.h"
using namespace nairoclip::core;
namespace {
struct Fixture {
    Project project;
    SequenceId seqId;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 1080;
        seq.height = 1920;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "a.mp4";
        asset.type = MediaType::Video;
        asset.duration = Rational::make(60);
        project.assets.push_back(asset);
    }
    Sequence* seq() { return project.findSequence(seqId); }
    TrackId addTrack(TrackType type, const std::string& name) {
        Track t;
        t.id = TrackId{project.ids.next()};
        t.type = type;
        t.name = name;
        seq()->tracks.push_back(t);
        return seq()->tracks.back().id;
    }
    Track* track(TrackId id) { return seq()->findTrack(id); }
    void addClip(TrackId tid, const Time& start, const Time& duration) {
        Clip c;
        c.id = ClipId{project.ids.next()};
        c.asset = project.assets.front().id;
        c.start = start;
        c.duration = duration;
        track(tid)->clips.push_back(c);
    }
};
}  // namespace
NC_TEST(ExportPlan, CompilesClips) {
    Fixture f;
    const TrackId v1 = f.addTrack(TrackType::Video, "V1");
    f.addClip(v1, Rational::make(0), Rational::make(5));
    f.addClip(v1, Rational::make(5), Rational::make(3));
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    NC_CHECK(exportPlanValid(plan));
    NC_CHECK(plan.duration == Rational::make(8));
    NC_CHECK_EQ(plan.width, 1080);
    NC_CHECK_EQ(plan.height, 1920);
    NC_CHECK_EQ(plan.tracks.size(), std::size_t(1));
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(2));
    NC_CHECK(plan.tracks[0].clips[0].timelineEnd == Rational::make(5));
    NC_CHECK(plan.tracks[0].clips[1].timelineStart == Rational::make(5));
    NC_CHECK(plan.warnings.empty());
}
NC_TEST(ExportPlan, SkipsDisabledClips) {
    Fixture f;
    const TrackId v1 = f.addTrack(TrackType::Video, "V1");
    f.addClip(v1, Rational::make(0), Rational::make(5));
    f.track(v1)->clips[0].enabled = false;
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(0));
    // 注意：Sequence::duration 基于所有片段；计划时长保持序列时长
    NC_CHECK(exportPlanValid(plan));
}
NC_TEST(ExportPlan, WarnsHiddenAndMuted) {
    Fixture f;
    const TrackId v1 = f.addTrack(TrackType::Video, "V1");
    const TrackId a1 = f.addTrack(TrackType::Audio, "A1");
    f.addClip(v1, Rational::make(0), Rational::make(2));
    f.addClip(a1, Rational::make(0), Rational::make(2));
    f.track(v1)->hidden = true;
    f.track(a1)->muted = true;
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    NC_CHECK_EQ(plan.warnings.size(), std::size_t(2));
}
NC_TEST(ExportPlan, WarnsOfflineAsset) {
    Fixture f;
    const TrackId v1 = f.addTrack(TrackType::Video, "V1");
    f.addClip(v1, Rational::make(0), Rational::make(2));
    f.project.assets[0].offline = true;
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    NC_CHECK_EQ(plan.warnings.size(), std::size_t(1));
    NC_CHECK(plan.warnings[0].find("离线") != std::string::npos);
}
NC_TEST(ExportPlan, WarnsMissingAsset) {
    Fixture f;
    const TrackId v1 = f.addTrack(TrackType::Video, "V1");
    f.addClip(v1, Rational::make(0), Rational::make(2));
    f.track(v1)->clips[0].asset = AssetId{999};  // 悬空引用
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    NC_CHECK_EQ(plan.tracks[0].clips.size(), std::size_t(0));
    NC_CHECK_EQ(plan.warnings.size(), std::size_t(1));
}
NC_TEST(ExportPlan, FlagsAnimatedClips) {
    Fixture f;
    const TrackId v1 = f.addTrack(TrackType::Video, "V1");
    f.addClip(v1, Rational::make(0), Rational::make(2));
    Keyframe k;
    k.time = Rational::make(0);
    k.value = 0.5;
    NC_CHECK(f.track(v1)->clips[0].animation.curve(ClipProperty::Opacity).insert(k).isOk());
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    NC_CHECK(plan.tracks[0].clips[0].hasAnimation);
}
NC_TEST(ExportPlan, RejectsInvalidPlan) {
    ExportPlan plan;
    plan.width = 0;  // 非法尺寸
    plan.height = 1080;
    plan.frameRate = fps30();
    NC_CHECK(!exportPlanValid(plan));
}
NC_TEST(ExportPlan, CompilesTransitions) {
    Fixture f;
    const TrackId v1 = f.addTrack(TrackType::Video, "V1");
    f.addClip(v1, Rational::make(0), Rational::make(2));   // [0,2)
    f.addClip(v1, Rational::make(3), Rational::make(2));   // [3,5)
    // 手动追加转场（跳过编辑器校验路径，聚焦编译器）
    Track* track = f.track(v1);
    Transition tr;
    tr.id = TransitionId{f.project.ids.next()};
    tr.type = TransitionType::CrossDissolve;
    tr.duration = Rational::make(1, 2);
    tr.fromClip = track->clips[0].id;
    tr.toClip = track->clips[1].id;
    track->transitions.push_back(tr);
    const ExportPlan plan = compileExportPlan(f.project, *f.seq());
    NC_CHECK_EQ(plan.tracks[0].transitions.size(), std::size_t(1));
    const RenderTransition& rt = plan.tracks[0].transitions[0];
    NC_CHECK(rt.type == TransitionType::CrossDissolve);
    // start = fromClip.end() = 2；end = 2 + 0.5 = 2.5
    NC_CHECK(rt.start == Rational::make(2));
    NC_CHECK(rt.end == Rational::make(5, 2));
    NC_CHECK(rt.fromClip == track->clips[0].id);
}
