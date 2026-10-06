#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include <cmath>
using namespace nairoclip::core;
namespace {
struct Fixture {
    Project project;
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    Fixture() {
        project.name = "测试工程";
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "测试视频.mp4";
        asset.type = MediaType::Video;
        asset.duration = Rational::make(60);
        asset.width = 1920;
        asset.height = 1080;
        project.assets.push_back(asset);
        assetId = project.assets.front().id;
        TimelineEditor editor(project);
        TrackId id;
        const Result r = editor.addTrack(seqId, TrackType::Video, "视频轨 1", &id);
        NC_CHECK(r.isOk());
        trackId = id;
    }
    Track* track() { return findTrack(project, seqId, trackId); }
};
Clip makeClip(const AssetId& asset, const Time& start, const Time& duration,
              const Time& sourceIn = Rational::make(0),
              const Rational& speed = Rational::make(1)) {
    Clip c;
    c.asset = asset;
    c.start = start;
    c.duration = duration;
    c.sourceIn = sourceIn;
    c.speed = speed;
    return c;
}
}  // namespace
NC_TEST(Editor, InsertAndQuery) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    const Result r = editor.insertClip(
        f.seqId, f.trackId, makeClip(f.assetId, Rational::make(0), Rational::make(5)), &clipId);
    NC_CHECK(r.isOk());
    NC_CHECK(clipId.isValid());
    Track* track = f.track();
    NC_CHECK(track != nullptr);
    NC_CHECK_EQ(track->clips.size(), std::size_t(1));
    NC_CHECK(track->clips[0].end() == Rational::make(5));
    NC_CHECK(track->invariantsHold());
}
NC_TEST(Editor, InsertOverlapRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)))
                 .isOk());
    const Result r = editor.insertClip(
        f.seqId, f.trackId, makeClip(f.assetId, Rational::make(3), Rational::make(5)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
    NC_CHECK_EQ(editor.undoCount(), std::size_t(1));
}
NC_TEST(Editor, SourceRangeRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    const Result r = editor.insertClip(
        f.seqId, f.trackId,
        makeClip(f.assetId, Rational::make(0), Rational::make(10), Rational::make(55)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
}
NC_TEST(Editor, UndoRedoInsert) {
    Fixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)))
                 .isOk());
    NC_CHECK_EQ(editor.undoCount(), std::size_t(1));
    NC_CHECK(editor.canUndo());
    NC_CHECK(!editor.canRedo());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(0));
    NC_CHECK(editor.canRedo());
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
    NC_CHECK(f.track()->clips[0].start == Rational::make(0));
}
NC_TEST(Editor, SplitClip) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(4)).isOk());
    Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(2));
    NC_CHECK(track->invariantsHold());
    NC_CHECK(track->clips[0].end() == Rational::make(4));
    NC_CHECK(track->clips[1].start == Rational::make(4));
    NC_CHECK(track->clips[1].sourceIn == Rational::make(4));
    NC_CHECK(editor.undo().isOk());
    track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(1));
    NC_CHECK(track->clips[0].end() == Rational::make(10));
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(2));
}
NC_TEST(Editor, SplitRejectsOutside) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(!editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(0)).isOk());
    NC_CHECK(!editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(10)).isOk());
    NC_CHECK(!editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(12)).isOk());
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
}
NC_TEST(Editor, MoveClip) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(2)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(5), Rational::make(2)), &b)
                 .isOk());
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, a, Rational::make(3)).isOk());
    NC_CHECK(f.track()->clips[0].start == Rational::make(3));
    const Result r = editor.moveClip(f.seqId, f.trackId, a, Rational::make(4));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
    NC_CHECK(f.track()->clips[0].start == Rational::make(3));
    NC_CHECK(!editor.moveClip(f.seqId, f.trackId, a, Rational::make(-1)).isOk());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.track()->clips[0].start == Rational::make(0));
}
NC_TEST(Editor, TrimStartAdjustsSourceIn) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(2), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.trimClipStart(f.seqId, f.trackId, clipId, Rational::make(5)).isOk());
    Track* track = f.track();
    NC_CHECK(track->clips[0].start == Rational::make(5));
    NC_CHECK(track->clips[0].duration == Rational::make(7));
    NC_CHECK(track->clips[0].sourceIn == Rational::make(3));
    NC_CHECK(editor.undo().isOk());
    track = f.track();
    NC_CHECK(track->clips[0].start == Rational::make(2));
    NC_CHECK(track->clips[0].sourceIn == Rational::make(0));
    NC_CHECK(track->clips[0].duration == Rational::make(10));
}
NC_TEST(Editor, TrimStartBeyondSourceRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(10), Rational::make(5)),
                               &clipId)
                 .isOk());
    const Result r = editor.trimClipStart(f.seqId, f.trackId, clipId, Rational::make(8));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
}
NC_TEST(Editor, TrimEnd) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.trimClipEnd(f.seqId, f.trackId, clipId, Rational::make(6)).isOk());
    NC_CHECK(f.track()->clips[0].duration == Rational::make(6));
    const Result r = editor.trimClipEnd(f.seqId, f.trackId, clipId, Rational::make(61));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::OutOfRange);
    NC_CHECK(f.track()->clips[0].duration == Rational::make(6));
}
NC_TEST(Editor, SetSpeedKeepsSourceRange) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(10)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(2)).isOk());
    Track* track = f.track();
    NC_CHECK(track->clips[0].duration == Rational::make(5));
    NC_CHECK(track->clips[0].sourceOut() == Rational::make(10));
    NC_CHECK(track->clips[0].speed == Rational::make(2));
    NC_CHECK(editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(1, 2)).isOk());
    track = f.track();
    NC_CHECK(track->clips[0].duration == Rational::make(20));
    NC_CHECK(track->clips[0].sourceOut() == Rational::make(10));
    NC_CHECK(!editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(20)).isOk());
    NC_CHECK(!editor.setClipSpeed(f.seqId, f.trackId, clipId, Rational::make(0)).isOk());
}
NC_TEST(Editor, SpeedOverlapRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(5)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(5), Rational::make(5)), &b)
                 .isOk());
    const Result r = editor.setClipSpeed(f.seqId, f.trackId, a, Rational::make(1, 2));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
    NC_CHECK(f.track()->clips[0].duration == Rational::make(5));
}
NC_TEST(Editor, RemoveClipAndUndoOrder) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId a;
    ClipId b;
    ClipId c;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(2)), &a)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(4), Rational::make(2)), &b)
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(8), Rational::make(2)), &c)
                 .isOk());
    NC_CHECK(editor.removeClip(f.seqId, f.trackId, b).isOk());
    Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(2));
    NC_CHECK(track->findClip(b) == nullptr);
    NC_CHECK(editor.undo().isOk());
    track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(3));
    NC_CHECK(track->clips[1].id == b);
    NC_CHECK(track->invariantsHold());
}
NC_TEST(Editor, RedoTruncatedByNewCommand) {
    Fixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(2)))
                 .isOk());
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(4), Rational::make(2)))
                 .isOk());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(editor.canRedo());
    NC_CHECK_EQ(editor.redoCount(), std::size_t(1));
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(8), Rational::make(2)))
                 .isOk());
    NC_CHECK(!editor.canRedo());
    NC_CHECK_EQ(editor.redoCount(), std::size_t(0));
    NC_CHECK_EQ(editor.undoCount(), std::size_t(2));
}
NC_TEST(Editor, HistoryNames) {
    Fixture f;
    TimelineEditor editor(f.project);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId,
                               makeClip(f.assetId, Rational::make(0), Rational::make(4)),
                               &clipId)
                 .isOk());
    NC_CHECK(editor.splitClip(f.seqId, f.trackId, clipId, Rational::make(2)).isOk());
    const auto names = editor.undoHistoryNames();
    NC_CHECK_EQ(names.size(), std::size_t(2));
    NC_CHECK_EQ(names[0], std::string("添加片段"));
    NC_CHECK_EQ(names[1], std::string("分割片段"));
}
NC_TEST(Editor, LockedTrackRejected) {
    Fixture f;
    TimelineEditor editor(f.project);
    f.track()->locked = true;
    const Result r = editor.insertClip(
        f.seqId, f.trackId, makeClip(f.assetId, Rational::make(0), Rational::make(2)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Locked);
}
NC_TEST(Editor, ImportAsset) {
    Fixture f;
    TimelineEditor editor(f.project);
    MediaAsset asset;
    asset.name = "音乐.mp3";
    asset.path = "/tmp/music.mp3";
    asset.type = MediaType::Audio;
    asset.duration = Rational::make(120);
    const AssetId id = editor.importAsset(asset);
    NC_CHECK(id.isValid());
    NC_CHECK(f.project.findAsset(id) != nullptr);
    NC_CHECK(f.project.findAsset(id)->name == std::string("音乐.mp3"));
}
NC_TEST(Editor, UpsertSubtitleWithUndo) {
    Fixture f;
    TimelineEditor editor(f.project);
    SubtitleCue cue;
    cue.id = SubtitleId{f.project.ids.next()};
    cue.start = Rational::make(1);
    cue.duration = Rational::make(2);
    cue.text = "你好";
    NC_CHECK(editor.upsertSubtitle(f.seqId, cue).isOk());
    NC_CHECK_EQ(f.project.mainSequence()->subtitles.size(), std::size_t(1));
    // 替换同 ID
    cue.text = "你好，世界";
    NC_CHECK(editor.upsertSubtitle(f.seqId, cue).isOk());
    NC_CHECK_EQ(f.project.mainSequence()->subtitles.size(), std::size_t(1));
    NC_CHECK(f.project.mainSequence()->subtitles.cues()[0].text == std::string("你好，世界"));
    // 撤销替换 → 恢复旧文本
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.project.mainSequence()->subtitles.cues()[0].text == std::string("你好"));
    // 撤销新增 → 空
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.project.mainSequence()->subtitles.empty());
    // 重做两次 → 最新文本
    NC_CHECK(editor.redo().isOk());
    NC_CHECK(editor.redo().isOk());
    NC_CHECK(f.project.mainSequence()->subtitles.cues()[0].text == std::string("你好，世界"));
    const auto names = editor.undoHistoryNames();
    NC_CHECK_EQ(names.size(), std::size_t(2));
    NC_CHECK_EQ(names[0], std::string("添加字幕"));
}
NC_TEST(Editor, RemoveSubtitleWithUndo) {
    Fixture f;
    TimelineEditor editor(f.project);
    SubtitleCue cue;
    cue.id = SubtitleId{f.project.ids.next()};
    cue.start = Rational::make(0);
    cue.duration = Rational::make(1);
    cue.text = "删除测试";
    NC_CHECK(editor.upsertSubtitle(f.seqId, cue).isOk());
    NC_CHECK(editor.removeSubtitle(f.seqId, cue.id).isOk());
    NC_CHECK(f.project.mainSequence()->subtitles.empty());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.project.mainSequence()->subtitles.size(), std::size_t(1));
    NC_CHECK(f.project.mainSequence()->subtitles.cues()[0].text == std::string("删除测试"));
}
NC_TEST(Editor, RemoveMissingSubtitleFails) {
    Fixture f;
    TimelineEditor editor(f.project);
    const Result r = editor.removeSubtitle(f.seqId, SubtitleId{999});
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::NotFound);
    NC_CHECK_EQ(editor.undoCount(), std::size_t(0));
}
NC_TEST(Editor, TransactionGroupsCommands) {
    Fixture f;
    TimelineEditor editor(f.project);
    f.project.ids.next();
    editor.beginTransaction("批量移动");
    NC_CHECK(editor.inTransaction());
    // 事务内执行 3 次操作
    ClipId id1;
    {
        Clip c;
        c.asset = f.project.assets.front().id;
        c.start = Rational::make(0);
        c.duration = Rational::make(1);
        NC_CHECK(editor.insertClip(f.seqId, f.trackId, c, &id1).isOk());
    }
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, id1, Rational::make(2)).isOk());
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, id1, Rational::make(4)).isOk());
    // 事务内不入历史
    NC_CHECK_EQ(editor.undoCount(), std::size_t(0));
    const bool grouped = editor.endTransaction();
    NC_CHECK(grouped);  // 3 条命令合并
    NC_CHECK(!editor.inTransaction());
    NC_CHECK_EQ(editor.undoCount(), std::size_t(1));
    const auto names = editor.undoHistoryNames();
    NC_CHECK_EQ(names[0], std::string("批量移动"));
    // 一次撤销全部回退
    NC_CHECK(editor.undo().isOk());
    const Track* t = findTrack(f.project, f.seqId, f.trackId);
    NC_CHECK_EQ(t->clips.size(), std::size_t(0));
    // 重做全部恢复
    NC_CHECK(editor.redo().isOk());
    t = findTrack(f.project, f.seqId, f.trackId);
    NC_CHECK_EQ(t->clips.size(), std::size_t(1));
    NC_CHECK(t->clips[0].start == Rational::make(4));
}
NC_TEST(Editor, EmptyTransactionNoHistory) {
    Fixture f;
    TimelineEditor editor(f.project);
    editor.beginTransaction("空事务");
    const bool grouped = editor.endTransaction();
    NC_CHECK(!grouped);
    NC_CHECK_EQ(editor.undoCount(), std::size_t(0));
}
NC_TEST(Editor, NestedTransaction) {
    Fixture f;
    TimelineEditor editor(f.project);
    editor.beginTransaction("外层");
    editor.beginTransaction("内层");
    Clip c;
    c.asset = f.project.assets.front().id;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, c).isOk());
    NC_CHECK(!editor.endTransaction());  // 内层结束不封装
    NC_CHECK(editor.inTransaction());
    // 再执行一个操作
    Clip c2;
    c2.asset = f.project.assets.front().id;
    c2.start = Rational::make(2);
    c2.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, c2).isOk());
    NC_CHECK(editor.endTransaction());  // 外层结束封装
    NC_CHECK_EQ(editor.undoCount(), std::size_t(1));
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, f.trackId)->clips.size(), std::size_t(0));
}
NC_TEST(Editor, TrackPropertyCommandsWithUndo) {
    Fixture f;
    TimelineEditor editor(f.project);
    // 重命名
    NC_CHECK(editor.renameTrack(f.seqId, f.trackId, "主视频").isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, f.trackId)->name, std::string("主视频"));
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, f.trackId)->name, std::string("视频轨 1"));
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, f.trackId)->name, std::string("主视频"));
    // 锁定
    NC_CHECK(editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    NC_CHECK(findTrack(f.project, f.seqId, f.trackId)->locked);
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(!findTrack(f.project, f.seqId, f.trackId)->locked);
    // 静音
    NC_CHECK(editor.setTrackMuted(f.seqId, f.trackId, true).isOk());
    NC_CHECK(findTrack(f.project, f.seqId, f.trackId)->muted);
    // 隐藏
    NC_CHECK(editor.setTrackHidden(f.seqId, f.trackId, true).isOk());
    NC_CHECK(findTrack(f.project, f.seqId, f.trackId)->hidden);
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(!findTrack(f.project, f.seqId, f.trackId)->hidden);
}
NC_TEST(Editor, LockedTrackRejectsEdits) {
    Fixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    const Result r = editor.insertClip(f.seqId, f.trackId, c);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Locked);
}
NC_TEST(Editor, DetachAudioCreatesAudioClip) {
    Fixture f;
    TimelineEditor editor(f.project);
    // 建音频轨
    TrackId audioTrack;
    NC_CHECK(editor.addTrack(f.seqId, TrackType::Audio, "A1", &audioTrack).isOk());
    // 插入视频片段
    Clip vc;
    vc.asset = f.assetId;
    vc.start = Rational::make(1);
    vc.duration = Rational::make(3);
    ClipId videoClip;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, vc, &videoClip).isOk());
    // 音画分离
    NC_CHECK(editor.detachAudio(f.seqId, f.trackId, videoClip, audioTrack).isOk());
    const Track* at = findTrack(f.project, f.seqId, audioTrack);
    NC_CHECK_EQ(at->clips.size(), std::size_t(1));
    NC_CHECK(at->clips[0].start == Rational::make(1));
    NC_CHECK(at->clips[0].duration == Rational::make(3));
    NC_CHECK(at->clips[0].asset == f.assetId);
    NC_CHECK(at->clips[0].id != videoClip);
    // 撤销 → 音频消失
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, audioTrack)->clips.size(), std::size_t(0));
    // 重做 → 恢复
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, audioTrack)->clips.size(), std::size_t(1));
}
NC_TEST(Editor, DetachAudioRejectsOverlap) {
    Fixture f;
    TimelineEditor editor(f.project);
    TrackId audioTrack;
    NC_CHECK(editor.addTrack(f.seqId, TrackType::Audio, "A1", &audioTrack).isOk());
    Clip vc;
    vc.asset = f.assetId;
    vc.start = Rational::make(0);
    vc.duration = Rational::make(3);
    ClipId videoClip;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, vc, &videoClip).isOk());
    // 音频轨已有冲突片段
    Clip existing;
    existing.asset = f.assetId;
    existing.start = Rational::make(1);
    existing.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(f.seqId, audioTrack, existing).isOk());
    const Result r = editor.detachAudio(f.seqId, f.trackId, videoClip, audioTrack);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Overlap);
}
NC_TEST(Editor, MarkerCommandsWithUndo) {
    Fixture f;
    TimelineEditor editor(f.project);
    Marker m;
    m.id = MarkerId{f.project.ids.next()};
    m.time = Rational::make(3);
    m.name = "高潮";
    m.color = "#FF453A";
    NC_CHECK(editor.upsertMarker(f.seqId, m).isOk());
    NC_CHECK_EQ(f.project.mainSequence()->markers.size(), std::size_t(1));
    NC_CHECK(f.project.mainSequence()->markers[0].name == std::string("高潮"));
    // 替换
    m.name = "高潮2";
    NC_CHECK(editor.upsertMarker(f.seqId, m).isOk());
    NC_CHECK_EQ(f.project.mainSequence()->markers.size(), std::size_t(1));
    NC_CHECK(f.project.mainSequence()->markers[0].name == std::string("高潮2"));
    // 撤销替换
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.project.mainSequence()->markers[0].name == std::string("高潮"));
    // 撤销添加
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.project.mainSequence()->markers.size(), std::size_t(0));
    // 重做两次
    NC_CHECK(editor.redo().isOk());
    NC_CHECK(editor.redo().isOk());
    NC_CHECK(f.project.mainSequence()->markers[0].name == std::string("高潮2"));
    // 删除
    NC_CHECK(editor.removeMarker(f.seqId, m.id).isOk());
    NC_CHECK_EQ(f.project.mainSequence()->markers.size(), std::size_t(0));
    // 撤销删除
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.project.mainSequence()->markers.size(), std::size_t(1));
}
NC_TEST(Editor, ClipAudioProperties) {
    Fixture f;
    TimelineEditor editor(f.project);
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(10);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, c, &clipId).isOk());
    // 设置增益 -6dB + 淡入 1s + 淡出 2s
    NC_CHECK(editor.setClipAudio(f.seqId, f.trackId, clipId, -6.0, Rational::make(1),
                                 Rational::make(2))
                 .isOk());
    const Clip* clip = findClip(f.project, f.seqId, f.trackId, clipId);
    NC_CHECK(clip->audioGainDb == -6.0);
    NC_CHECK(clip->audioFadeIn == Rational::make(1));
    NC_CHECK(clip->audioFadeOut == Rational::make(2));
    NC_CHECK(clip->audioValid());
    // 淡入中点系数 = 0.5
    NC_CHECK(std::abs(clip->audioFadeFactor(fromMilliseconds(500)) - 0.5) < 1e-6);
    // 中段 = 1.0
    NC_CHECK(std::abs(clip->audioFadeFactor(Rational::make(5)) - 1.0) < 1e-6);
    // 淡出中段（t=9，距终点 1s，淡出 2s）→ 0.5
    NC_CHECK(std::abs(clip->audioFadeFactor(Rational::make(9)) - 0.5) < 1e-6);
    // 撤销
    NC_CHECK(editor.undo().isOk());
    clip = findClip(f.project, f.seqId, f.trackId, clipId);
    NC_CHECK(clip->audioGainDb == 0.0);
    NC_CHECK(clip->audioFadeIn == Time{});
}
NC_TEST(Editor, ClipAudioRejectsInvalid) {
    Fixture f;
    TimelineEditor editor(f.project);
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(3);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, c, &clipId).isOk());
    // 增益超范围 → 拒绝
    const Result r1 = editor.setClipAudio(f.seqId, f.trackId, clipId, 20.0, Time{}, Time{});
    NC_CHECK(!r1.isOk());
    // 淡入+淡出超出片段时长 → 拒绝
    const Result r2 = editor.setClipAudio(f.seqId, f.trackId, clipId, 0.0, Rational::make(2),
                                          Rational::make(2));
    NC_CHECK(!r2.isOk());
    // 失败后工程未被修改、未入历史
    const Clip* clip = findClip(f.project, f.seqId, f.trackId, clipId);
    NC_CHECK(clip->audioGainDb == 0.0);
    NC_CHECK_EQ(editor.undoCount(), std::size_t(1));  // 只有 insert
}
