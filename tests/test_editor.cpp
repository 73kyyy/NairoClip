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
    // 前置断言失败时不要越界：NC_CHECK 不中断执行，clips[0] 在空 vector 上
    // 是 UB（实测会让整套测试段错误，后续用例结果全部丢失）。
    if (at->clips.size() == 1) {
        NC_CHECK(at->clips[0].start == Rational::make(1));
        NC_CHECK(at->clips[0].duration == Rational::make(3));
        NC_CHECK(at->clips[0].asset == f.assetId);
        NC_CHECK(at->clips[0].id != videoClip);
    }
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
    // 音频轨已有冲突片段（用真实音频素材——B0-7 后视频素材不能放音频轨）
    MediaAsset music;
    music.id = AssetId{f.project.ids.next()};
    music.name = "音乐.wav";
    music.path = "/audio/a.wav";
    music.type = MediaType::Audio;
    music.duration = Rational::make(60);
    f.project.assets.push_back(music);
    Clip existing;
    existing.asset = music.id;
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
NC_TEST(Editor, AssetImportUndoRedo) {
    Fixture f;
    TimelineEditor editor(f.project);
    const std::size_t assetCount = f.project.assets.size();
    MediaAsset a2;
    a2.name = "第二个素材.mp4";
    a2.type = MediaType::Video;
    a2.duration = Rational::make(30);
    const AssetId id = editor.importAsset(a2);
    NC_CHECK(id.isValid());
    NC_CHECK_EQ(f.project.assets.size(), assetCount + 1);
    // 撤销 → 素材移除
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.project.assets.size(), assetCount);
    // 重做 → 恢复
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.project.assets.size(), assetCount + 1);
    NC_CHECK(f.project.findAsset(id) != nullptr);
}
NC_TEST(Editor, RemoveAssetRejectedWhenReferenced) {
    Fixture f;
    TimelineEditor editor(f.project);
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, c).isOk());
    const Result r = editor.removeAsset(f.assetId);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
}
NC_TEST(Editor, RemoveUnreferencedAssetWithUndo) {
    Fixture f;
    TimelineEditor editor(f.project);
    // 导入一个不被引用的素材
    MediaAsset a2;
    a2.name = "未使用.mp4";
    a2.type = MediaType::Video;
    a2.duration = Rational::make(10);
    const AssetId id = editor.importAsset(a2);
    NC_CHECK(id.isValid());
    const std::size_t after = f.project.assets.size();
    // 删除
    NC_CHECK(editor.removeAsset(id).isOk());
    NC_CHECK_EQ(f.project.assets.size(), after - 1);
    // 撤销 → 恢复（且位置正确）
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.project.assets.size(), after);
    NC_CHECK(f.project.findAsset(id) != nullptr);
}
NC_TEST(Editor, GestureMergeCollapsesMoves) {
    Fixture f;
    TimelineEditor editor(f.project);
    // 建片段
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, c, &clipId).isOk());
    const std::size_t before = editor.undoCount();
    // 开启合并（模拟手势拖动开始）
    editor.setMergeEnabled(true);
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, clipId, Rational::make(1)).isOk());
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, clipId, Rational::make(2)).isOk());
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, clipId, Rational::make(3)).isOk());
    editor.setMergeEnabled(false);
    // 三次移动合并为一条历史
    NC_CHECK_EQ(editor.undoCount() - before, std::size_t(1));
    // 最终位置 = 3
    NC_CHECK(findClip(f.project, f.seqId, f.trackId, clipId)->start == Rational::make(3));
    // 一次撤销 → 回到手势起点 0（而非 2）
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(findClip(f.project, f.seqId, f.trackId, clipId)->start == Rational::make(0));
}
NC_TEST(Editor, MergeDisabledKeepsSeparateHistory) {
    Fixture f;
    TimelineEditor editor(f.project);
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    ClipId clipId;
    NC_CHECK(editor.insertClip(f.seqId, f.trackId, c, &clipId).isOk());
    const std::size_t before = editor.undoCount();
    // 未开启合并：两次移动 = 两条历史
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, clipId, Rational::make(1)).isOk());
    NC_CHECK(editor.moveClip(f.seqId, f.trackId, clipId, Rational::make(2)).isOk());
    NC_CHECK_EQ(editor.undoCount() - before, std::size_t(2));
    // 撤销一次 → 回到中间点 1
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(findClip(f.project, f.seqId, f.trackId, clipId)->start == Rational::make(1));
}

// =============================================================================
// 轨道类型校验（B0-7）
// =============================================================================
//
// 背景：轨道类型决定渲染与混音语义。音频片段进视频轨 → 预览黑屏；
// 视频片段进音频轨 → 混音器按 PCM 读取（噪声/崩溃）。此前一律放行。

namespace {

/// 在 Fixture 基础上再挂一条音频轨，并额外准备音频/图片/未知类型素材。
struct TypeFixture {
    Fixture f;
    TrackId audioTrack;
    AssetId audioAsset;
    AssetId imageAsset;
    AssetId unknownAsset;
    explicit TypeFixture() {
        TimelineEditor editor(f.project);
        NC_CHECK(editor.addTrack(f.seqId, TrackType::Audio, "A1", &audioTrack).isOk());
        MediaAsset a;
        a.id = AssetId{f.project.ids.next()};
        a.name = "音乐.wav";
        a.path = "/audio/a.wav";
        a.type = MediaType::Audio;
        a.duration = Rational::make(60);
        f.project.assets.push_back(a);
        audioAsset = a.id;
        MediaAsset img;
        img.id = AssetId{f.project.ids.next()};
        img.name = "封面.png";
        img.path = "/img/c.png";
        img.type = MediaType::Image;
        f.project.assets.push_back(img);
        imageAsset = img.id;
        MediaAsset unk;
        unk.id = AssetId{f.project.ids.next()};
        unk.name = "未探测.bin";
        unk.path = "/x/unknown.bin";
        unk.type = MediaType::Unknown;
        f.project.assets.push_back(unk);
        unknownAsset = unk.id;
    }
};

Clip clipOf(const AssetId& asset, const Time& start, const Time& duration) {
    Clip c;
    c.asset = asset;
    c.start = start;
    c.duration = duration;
    return c;
}

}  // namespace

NC_TEST(Editor, AudioClipRejectedOnVideoTrack) {
    TypeFixture t;
    TimelineEditor editor(t.f.project);
    const Result r = editor.insertClip(t.f.seqId, t.f.trackId,
                                       clipOf(t.audioAsset, Rational::make(0), Rational::make(2)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
    // 消息要说清「是什么放不进哪里」，不能只说"操作失败"
    NC_CHECK(r.message.find("音频") != std::string::npos);
    NC_CHECK(r.message.find("视频轨") != std::string::npos);
    // 未被写入
    NC_CHECK_EQ(t.f.track()->clips.size(), std::size_t(0));
}

NC_TEST(Editor, ImageClipRejectedOnAudioTrack) {
    // 图片没有音轨，放进音频轨混音器读不出 PCM —— 必须拒绝。
    // 注意：视频素材**允许**进音频轨（取声音，见 AudioClipAllowedOnAudioTrack）。
    TypeFixture t;
    TimelineEditor editor(t.f.project);
    const Result r = editor.insertClip(t.f.seqId, t.audioTrack,
                                       clipOf(t.imageAsset, Rational::make(0), Rational::make(2)));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
    NC_CHECK(r.message.find("图片") != std::string::npos);
    NC_CHECK(r.message.find("音频轨") != std::string::npos);
    NC_CHECK_EQ(findTrack(t.f.project, t.f.seqId, t.audioTrack)->clips.size(), std::size_t(0));
}

NC_TEST(Editor, VideoAssetAllowedOnAudioTrackForSoundExtraction) {
    // 带画面的 mp4 通常含音轨，「拖到音频轨取声音」是剪辑软件的标准操作，
    // 音画分离功能本身也依赖它。这条不能被 B0-7 误伤。
    TypeFixture t;
    TimelineEditor editor(t.f.project);
    NC_CHECK(editor
                 .insertClip(t.f.seqId, t.audioTrack,
                             clipOf(t.f.assetId, Rational::make(0), Rational::make(2)))
                 .isOk());
    NC_CHECK_EQ(findTrack(t.f.project, t.f.seqId, t.audioTrack)->clips.size(), std::size_t(1));
}

NC_TEST(Editor, ImageClipAllowedOnVideoTrack) {
    // 图片进视频轨是正常用法（封面/图文），不能误拒
    TypeFixture t;
    TimelineEditor editor(t.f.project);
    NC_CHECK(editor
                 .insertClip(t.f.seqId, t.f.trackId,
                             clipOf(t.imageAsset, Rational::make(0), Rational::make(2)))
                 .isOk());
    NC_CHECK_EQ(t.f.track()->clips.size(), std::size_t(1));
}

NC_TEST(Editor, AudioClipAllowedOnAudioTrack) {
    TypeFixture t;
    TimelineEditor editor(t.f.project);
    NC_CHECK(editor
                 .insertClip(t.f.seqId, t.audioTrack,
                             clipOf(t.audioAsset, Rational::make(0), Rational::make(2)))
                 .isOk());
    NC_CHECK_EQ(findTrack(t.f.project, t.f.seqId, t.audioTrack)->clips.size(), std::size_t(1));
}

NC_TEST(Editor, UnknownTypeClipNotBlockedByTrackType) {
    // 导入后尚未 probe 的素材在真实流程里大量存在。硬拒会让用户
    // 「刚导入就拖不进时间轴」，因此 Unknown 必须放行。
    TypeFixture t;
    TimelineEditor editor(t.f.project);
    NC_CHECK(editor
                 .insertClip(t.f.seqId, t.f.trackId,
                             clipOf(t.unknownAsset, Rational::make(0), Rational::make(2)))
                 .isOk());
    NC_CHECK(editor
                 .insertClip(t.f.seqId, t.audioTrack,
                             clipOf(t.unknownAsset, Rational::make(0), Rational::make(2)))
                 .isOk());
}

NC_TEST(Editor, ClipWithoutAssetRejectedOnSubtitleAndEffectTracks) {
    // 无素材引用的片段（文字/占位）：字幕与特效轨都不接受
    Fixture f;
    TimelineEditor editor(f.project);
    TrackId subTrack;
    NC_CHECK(editor.addTrack(f.seqId, TrackType::Subtitle, "S1", &subTrack).isOk());
    TrackId fxTrack;
    NC_CHECK(editor.addTrack(f.seqId, TrackType::Adjustment, "E1", &fxTrack).isOk());
    Clip bare;
    bare.start = Rational::make(0);
    bare.duration = Rational::make(2);
    NC_CHECK(!editor.insertClip(f.seqId, subTrack, bare).isOk());
    NC_CHECK(!editor.insertClip(f.seqId, fxTrack, bare).isOk());
}

NC_TEST(Editor, MoveClipToWrongTypeTrackRejected) {
    // 拖拽换轨是最容易混拖的路径，必须同样被拦
    TypeFixture t;
    TimelineEditor editor(t.f.project);
    ClipId clipId;
    NC_CHECK(editor
                 .insertClip(t.f.seqId, t.f.trackId,
                             clipOf(t.imageAsset, Rational::make(0), Rational::make(2)), &clipId)
                 .isOk());
    const Result r =
        editor.moveClipToTrack(t.f.seqId, t.f.trackId, t.audioTrack, clipId, Rational::make(0));
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
    // 源轨仍在，目标轨未写入
    NC_CHECK_EQ(t.f.track()->clips.size(), std::size_t(1));
    NC_CHECK_EQ(findTrack(t.f.project, t.f.seqId, t.audioTrack)->clips.size(), std::size_t(0));
}

NC_TEST(Editor, CompoundClipRejectedOnAudioTrack) {
    // 复合片段是渲染结果（含画面），只能进视频轨
    TypeFixture t;
    // 再建一条序列作为嵌套内容
    Sequence nested;
    nested.id = SequenceId{t.f.project.ids.next()};
    nested.name = "嵌套";
    nested.frameRate = fps30();
    t.f.project.sequences.push_back(nested);
    TimelineEditor editor(t.f.project);
    Clip c;
    c.nestedSequence = nested.id;
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    const Result r = editor.insertClip(t.f.seqId, t.audioTrack, c);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::InvalidArgument);
    NC_CHECK(r.message.find("复合片段") != std::string::npos);
}

NC_TEST(Editor, DetachAudioStillWorksOnAudioTrack) {
    // 回归保护：音画分离生成的音频片段沿用视频素材（MediaType::Video），
    // 走的是 allowTrackTypeMismatch 豁免。若有人去掉豁免，此用例会红。
    Fixture f;
    TimelineEditor editor(f.project);
    TrackId audioTrack;
    NC_CHECK(editor.addTrack(f.seqId, TrackType::Audio, "A1", &audioTrack).isOk());
    ClipId videoClip;
    NC_CHECK(editor
                 .insertClip(f.seqId, f.trackId,
                             makeClip(f.assetId, Rational::make(1), Rational::make(3)), &videoClip)
                 .isOk());
    NC_CHECK(editor.detachAudio(f.seqId, f.trackId, videoClip, audioTrack).isOk());
    NC_CHECK_EQ(findTrack(f.project, f.seqId, audioTrack)->clips.size(), std::size_t(1));
}


// =============================================================================
// 轨道重排（B2-5）
// =============================================================================
//
// 背景：视频轨的上下顺序决定遮挡关系（上层盖下层）。此前轨道只能增删、
// 不能改顺序，用户建错顺序后只能删了重建。

namespace {

/// 建 3 条轨道：V1 / V2 / V3，按创建顺序存放。
struct TrackOrderFixture {
    Project project;
    SequenceId seqId;
    TrackId v1, v2, v3;
    explicit TrackOrderFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        TimelineEditor editor(project);
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &v1).isOk());
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V2", &v2).isOk());
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V3", &v3).isOk());
    }
    std::vector<TrackId> order() const {
        std::vector<TrackId> out;
        for (const auto& t : project.sequences[0].tracks) out.push_back(t.id);
        return out;
    }
};

}  // namespace

NC_TEST(Editor, MoveTrackReordersAndUndoes) {
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
    // 把 V1 移到末尾
    NC_CHECK(editor.moveTrack(f.seqId, f.v1, 2).isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v2, f.v3, f.v1}));
    NC_CHECK(f.project.sequences[0].invariantsHold());
    // 撤销 → 还原原始顺序
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
    // 重做 → 再次移动
    NC_CHECK(editor.redo().isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v2, f.v3, f.v1}));
}

NC_TEST(Editor, MoveTrackUpward) {
    // 向上移动是另一个分支（索引变小），单独覆盖
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.moveTrack(f.seqId, f.v3, 0).isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v3, f.v1, f.v2}));
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
}

NC_TEST(Editor, MoveTrackRejectsOutOfRange) {
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(!editor.moveTrack(f.seqId, f.v1, -1).isOk());
    NC_CHECK(!editor.moveTrack(f.seqId, f.v1, 3).isOk());
    NC_CHECK(!editor.moveTrack(f.seqId, f.v1, 100).isOk());
    // 越界不得改动顺序
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
}

NC_TEST(Editor, MoveTrackRejectsLockedTrack) {
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.setTrackLocked(f.seqId, f.v1, true).isOk());
    const Result r = editor.moveTrack(f.seqId, f.v1, 2);
    NC_CHECK(!r.isOk());
    NC_CHECK(r.code == ErrorCode::Locked);
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
}

NC_TEST(Editor, MoveTrackAllowsCrossType) {
    // 跨类型重排允许：顺序只影响渲染层级，不改变轨道语义
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    TimelineEditor editor(project);
    TrackId v, a;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &v).isOk());
    NC_CHECK(editor.addTrack(seqId, TrackType::Audio, "A1", &a).isOk());
    // 音频轨移到视频轨上方
    NC_CHECK(editor.moveTrack(seqId, a, 0).isOk());
    NC_CHECK(project.sequences[0].tracks[0].id == a);
    NC_CHECK(project.sequences[0].tracks[1].id == v);
}

NC_TEST(Editor, MoveTrackToSameIndexKeepsOrder) {
    // 拖回原位：顺序不变，且**不产生历史记录**。
    //
    // 早期这里记录为「已知局限：会产生一条空历史」。2026-10-29 引入
    // Command::isNoOp 后该局限已消除 —— 空历史会让用户按一次撤销
    // 「毫无反应」，观感是撤销坏了，不是可接受的瑕疵。
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    const std::size_t before = editor.undoCount();
    NC_CHECK(editor.moveTrack(f.seqId, f.v2, 1).isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
    // no-op 不入栈：历史条数不变
    NC_CHECK(editor.undoCount() == before);
    // 因此此时没有可撤销的东西
    NC_CHECK(!editor.undo().isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
}

NC_TEST(Editor, MoveTrackMergesConsecutiveDrags) {
    // 手势拖动会连续产生多条 MoveTrackCommand。开启合并后应只留一条历史，
    // 否则逐像素拖动会把撤销栈塞满。中途目标位置以最后一次为准。
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    editor.setMergeEnabled(true);
    const std::size_t before = editor.undoCount();
    NC_CHECK(editor.moveTrack(f.seqId, f.v1, 1).isOk());
    NC_CHECK(editor.moveTrack(f.seqId, f.v1, 2).isOk());
    NC_CHECK(editor.moveTrack(f.seqId, f.v1, 1).isOk());
    NC_CHECK_EQ(editor.undoCount() - before, std::size_t(1));
    NC_CHECK(f.order() == std::vector<TrackId>({f.v2, f.v1, f.v3}));
    // 一次撤销回到拖动前
    NC_CHECK(editor.undo().isOk());
    NC_CHECK(f.order() == std::vector<TrackId>({f.v1, f.v2, f.v3}));
}

// =============================================================================
// 轨道高度与折叠（B2-10）
// =============================================================================

NC_TEST(Editor, TrackHeightDefaultsAndRange) {
    TrackOrderFixture f;
    NC_CHECK_EQ(f.project.sequences[0].tracks[0].heightPx, Track::kDefaultTrackHeight);
    TimelineEditor editor(f.project);
    NC_CHECK(editor.setTrackHeight(f.seqId, f.v1, 120).isOk());
    NC_CHECK_EQ(f.project.sequences[0].tracks[0].heightPx, 120);
    // 边界值可用
    NC_CHECK(editor.setTrackHeight(f.seqId, f.v1, Track::kMinTrackHeight).isOk());
    NC_CHECK(editor.setTrackHeight(f.seqId, f.v1, Track::kMaxTrackHeight).isOk());
    // 越界拒绝，而不是静默截断
    const Result tooSmall = editor.setTrackHeight(f.seqId, f.v1, Track::kMinTrackHeight - 1);
    NC_CHECK(!tooSmall.isOk());
    NC_CHECK(tooSmall.code == ErrorCode::InvalidArgument);
    const Result tooBig = editor.setTrackHeight(f.seqId, f.v1, Track::kMaxTrackHeight + 1);
    NC_CHECK(!tooBig.isOk());
}

NC_TEST(Editor, TrackHeightChangeUndoes) {
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.setTrackHeight(f.seqId, f.v1, 120).isOk());
    NC_CHECK(editor.undo().isOk());
    NC_CHECK_EQ(f.project.sequences[0].tracks[0].heightPx, Track::kDefaultTrackHeight);
    NC_CHECK(editor.redo().isOk());
    NC_CHECK_EQ(f.project.sequences[0].tracks[0].heightPx, 120);
}

NC_TEST(Editor, TrackCollapseKeepsHeight) {
    // 折叠只是显示状态，展开后应恢复原来的高度
    TrackOrderFixture f;
    TimelineEditor editor(f.project);
    NC_CHECK(editor.setTrackHeight(f.seqId, f.v1, 120).isOk());
    NC_CHECK(editor.setTrackHeight(f.seqId, f.v1, 120, true).isOk());
    NC_CHECK(f.project.sequences[0].tracks[0].collapsed);
    NC_CHECK_EQ(f.project.sequences[0].tracks[0].heightPx, 120);
    // 展开
    NC_CHECK(editor.setTrackHeight(f.seqId, f.v1, 120, false).isOk());
    NC_CHECK(!f.project.sequences[0].tracks[0].collapsed);
    NC_CHECK_EQ(f.project.sequences[0].tracks[0].heightPx, 120);
}
