#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

struct CompoundFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    CompoundFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.frameRate = fps30();
        seq.width = 1920;
        seq.height = 1080;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/videos/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
    }
    /// 添加片段并返回 ID。
    ClipId addClip(double start, double duration) {
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(static_cast<std::int64_t>(start * 1000), 1000);
        c.duration = Rational::make(static_cast<std::int64_t>(duration * 1000), 1000);
        ClipId id;
        editor.insertClip(seqId, trackId, c, &id);
        return id;
    }
    const Track* track() const { return findTrack(project, seqId, trackId); }
};

}  // namespace

// =============================================================================
// 创建复合片段（文档「右键菜单：创建复合片段(选中多个片段打组)」）
// =============================================================================

NC_TEST(Compound, CreatePacksClipsIntoNestedSequence) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    const ClipId c2 = f.addClip(1.0, 1.0);
    const ClipId c3 = f.addClip(2.0, 1.0);
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(1));

    SequenceId nestedId;
    ClipId compoundClipId;
    const Result r = f.editor.createCompoundClip(f.seqId, f.trackId, {c1, c2}, "复合片段A",
                                                 &nestedId, &compoundClipId);
    NC_CHECK(r.isOk());
    // 嵌套序列已创建
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(2));
    const Sequence* nested = f.project.findSequence(nestedId);
    NC_CHECK(nested != nullptr);
    NC_CHECK_EQ(nested->name, std::string("复合片段A"));
    // 内部包含 2 个片段（相对时间从 0 开始）
    NC_CHECK_EQ(nested->tracks.size(), std::size_t(1));
    NC_CHECK_EQ(nested->tracks[0].clips.size(), std::size_t(2));
    NC_CHECK(nested->tracks[0].clips[0].start == Rational::make(0));
    NC_CHECK(nested->tracks[0].clips[1].start == Rational::make(1));
    // 原轨道：复合片段 + 剩余 c3
    const Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(2));
    const Clip* compound = track->findClip(compoundClipId);
    NC_CHECK(compound != nullptr);
    NC_CHECK(compound->isCompound());
    NC_CHECK(compound->nestedSequence == nestedId);
    NC_CHECK(compound->asset.value == 0);  // 复合片段不引用素材
    NC_CHECK(compound->start == Rational::make(0));
    NC_CHECK(compound->duration == Rational::make(2));  // 覆盖 [0,2)
    NC_CHECK(track->invariantsHold());
}

NC_TEST(Compound, CreateUndoRestoresOriginal) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    const ClipId c2 = f.addClip(1.0, 1.0);
    const std::size_t undoBefore = f.editor.undoCount();
    SequenceId nestedId;
    NC_CHECK(f.editor.createCompoundClip(f.seqId, f.trackId, {c1, c2}, "组", &nestedId).isOk());
    // 撤销：还原两个片段、删除嵌套序列
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(1));
    const Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(2));
    NC_CHECK(track->findClip(c1) != nullptr);
    NC_CHECK(track->findClip(c2) != nullptr);
    NC_CHECK(track->invariantsHold());
    // 重做：恢复复合片段
    NC_CHECK(f.editor.redo().isOk());
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(2));
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(1));
    NC_CHECK(f.track()->clips[0].isCompound());
    (void)undoBefore;
}

NC_TEST(Compound, CreateRejectsNonContiguousSelection) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    f.addClip(1.0, 1.0);  // 中间未选中
    const ClipId c3 = f.addClip(2.0, 1.0);
    const Result r = f.editor.createCompoundClip(f.seqId, f.trackId, {c1, c3}, "组");
    NC_CHECK(!r.isOk());  // 必须连续
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(1));  // 无副作用
    NC_CHECK_EQ(f.track()->clips.size(), std::size_t(3));
}

NC_TEST(Compound, CreateRejectsEmptySelection) {
    CompoundFixture f;
    f.addClip(0.0, 1.0);
    NC_CHECK(!f.editor.createCompoundClip(f.seqId, f.trackId, {}, "组").isOk());
}

NC_TEST(Compound, CreateRejectsMissingClip) {
    CompoundFixture f;
    f.addClip(0.0, 1.0);
    const Result r = f.editor.createCompoundClip(f.seqId, f.trackId, {ClipId{9999}}, "组");
    NC_CHECK(!r.isOk());
}

NC_TEST(Compound, CreateRejectsLockedTrack) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.trackId, true).isOk());
    NC_CHECK(!f.editor.createCompoundClip(f.seqId, f.trackId, {c1}, "组").isOk());
}

// =============================================================================
// 解除复合片段（文档「解除复合片段(还原为独立轨道)」）
// =============================================================================

NC_TEST(Compound, UnwrapRestoresInnerClips) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    const ClipId c2 = f.addClip(1.0, 1.0);
    f.addClip(3.0, 1.0);  // c3 在后方（不受影响）
    SequenceId nestedId;
    ClipId compoundClipId;
    NC_CHECK(f.editor
                 .createCompoundClip(f.seqId, f.trackId, {c1, c2}, "组", &nestedId, &compoundClipId)
                 .isOk());
    // 解除
    NC_CHECK(f.editor.unwrapCompoundClip(f.seqId, f.trackId, compoundClipId).isOk());
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(1));  // 嵌套序列已删除
    const Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(3));  // 还原 2 个 + 原 c3
    NC_CHECK(track->clips[0].start == Rational::make(0));
    NC_CHECK(track->clips[1].start == Rational::make(1));
    NC_CHECK(track->clips[2].start == Rational::make(3));
    NC_CHECK(track->invariantsHold());
    // 还原的片段引用了原素材（不是复合片段）
    NC_CHECK(!track->clips[0].isCompound());
    NC_CHECK(track->clips[0].asset == f.assetId);
}

NC_TEST(Compound, UnwrapUndoRestoresCompound) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    const ClipId c2 = f.addClip(1.0, 1.0);
    SequenceId nestedId;
    ClipId compoundClipId;
    NC_CHECK(f.editor
                 .createCompoundClip(f.seqId, f.trackId, {c1, c2}, "组", &nestedId, &compoundClipId)
                 .isOk());
    NC_CHECK(f.editor.unwrapCompoundClip(f.seqId, f.trackId, compoundClipId).isOk());
    // 撤销解除：恢复复合片段与嵌套序列
    NC_CHECK(f.editor.undo().isOk());
    NC_CHECK_EQ(f.project.sequences.size(), std::size_t(2));
    const Track* track = f.track();
    NC_CHECK_EQ(track->clips.size(), std::size_t(1));
    NC_CHECK(track->clips[0].isCompound());
}

NC_TEST(Compound, UnwrapRejectsNormalClip) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    NC_CHECK(!f.editor.unwrapCompoundClip(f.seqId, f.trackId, c1).isOk());
}

NC_TEST(Compound, UnwrapRejectsMissingClip) {
    CompoundFixture f;
    f.addClip(0.0, 1.0);
    NC_CHECK(!f.editor.unwrapCompoundClip(f.seqId, f.trackId, ClipId{9999}).isOk());
}

// =============================================================================
// 序列化（复合片段持久化）
// =============================================================================

NC_TEST(Compound, SerializationRoundtrip) {
    CompoundFixture f;
    const ClipId c1 = f.addClip(0.0, 1.0);
    const ClipId c2 = f.addClip(1.0, 1.0);
    SequenceId nestedId;
    ClipId compoundClipId;
    NC_CHECK(f.editor
                 .createCompoundClip(f.seqId, f.trackId, {c1, c2}, "复合片段A", &nestedId,
                                     &compoundClipId)
                 .isOk());

    const std::string text = saveProjectToNclip(f.project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    // 嵌套序列与复合片段都应恢复
    NC_CHECK_EQ(loaded.sequences.size(), std::size_t(2));
    const Sequence* nested = loaded.findSequence(nestedId);
    NC_CHECK(nested != nullptr);
    NC_CHECK_EQ(nested->tracks[0].clips.size(), std::size_t(2));
    const Track* mainTrack = loaded.sequences[0].findTrack(f.trackId);
    NC_CHECK(mainTrack != nullptr);
    const Clip* compound = mainTrack->findClip(compoundClipId);
    NC_CHECK(compound != nullptr);
    NC_CHECK(compound->isCompound());
    NC_CHECK(compound->nestedSequence == nestedId);
    NC_CHECK_EQ(compound->compoundName, std::string("复合片段A"));
}

NC_TEST(Compound, DanglingNestedSequenceRejected) {
    // 复合片段引用不存在的嵌套序列：载入必须失败（防御性校验）
    const std::string text = R"({"format":"nclip","version":1,"project":{"name":"t"},)"
                             R"("assets":[],"sequences":[{"id":1,"name":"s","frameRate":[30,1],)"
                             R"("width":1920,"height":1080,"tracks":[{"id":2,"type":"video",)"
                             R"("name":"v","clips":[{"id":3,"asset":0,"nestedSequence":99,)"
                             R"("start":[0,1],"duration":[1,1],"sourceIn":[0,1],"speed":[1,1]})"
                             R"(]}],"markers":[]}]})";
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(!result.result.isOk());
}

NC_TEST(Compound, DefaultClipOmitsNestedFields) {
    CompoundFixture f;
    f.addClip(0.0, 1.0);
    const std::string text = saveProjectToNclip(f.project);
    // 普通片段不应写入嵌套字段（保持文件简洁与向后兼容）
    NC_CHECK(text.find("nestedSequence") == std::string::npos);
    NC_CHECK(text.find("compoundName") == std::string::npos);
}
