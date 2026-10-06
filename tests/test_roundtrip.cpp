#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"
#include <cmath>
#include <string>
using namespace nairoclip::core;
namespace {
/// 构造确定性的伪随机工程（LCG），用于「编辑操作序列 → 存档 → 读档」一致性验证。
struct Lcg {
    std::uint64_t state;
    explicit Lcg(std::uint64_t seed) : state(seed) {}
    std::uint32_t next() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return static_cast<std::uint32_t>(state >> 33);
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }
};
}  // namespace
NC_TEST(Roundtrip, RandomEditSequenceSurvivesSaveLoad) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    // 素材
    AssetId assetId;
    {
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.name = "asset.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(1000);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
    }
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    // 随机插入 20 个不重叠片段
    Lcg rng(20260706);
    Time cursor = Rational::make(0);
    int inserted = 0;
    for (int i = 0; i < 20; ++i) {
        const int durationMs = rng.range(500, 3000);
        const Time duration = fromMilliseconds(durationMs);
        Clip c;
        c.asset = assetId;
        c.start = cursor;
        c.duration = duration;
        const Result r = editor.insertClip(seqId, trackId, c);
        NC_CHECK(r.isOk());
        ++inserted;
        cursor = cursor + duration + fromMilliseconds(rng.range(0, 500));
    }
    NC_CHECK_EQ(inserted, 20);
    // 随机撤销若干次并重做
    const int undoSteps = rng.range(1, 5);
    for (int i = 0; i < undoSteps; ++i) {
        NC_CHECK(editor.undo().isOk());
    }
    for (int i = 0; i < undoSteps; ++i) {
        NC_CHECK(editor.redo().isOk());
    }
    // 存档 → 读档
    const std::string text = saveProjectToNclip(project);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    const Sequence* originalSeq = project.mainSequence();
    const Sequence* loadedSeq = loaded.mainSequence();
    NC_CHECK(originalSeq != nullptr && loadedSeq != nullptr);
    NC_CHECK(originalSeq->duration() == loadedSeq->duration());
    NC_CHECK_EQ(originalSeq->tracks[0].clips.size(), loadedSeq->tracks[0].clips.size());
    for (std::size_t i = 0; i < originalSeq->tracks[0].clips.size(); ++i) {
        const Clip& a = originalSeq->tracks[0].clips[i];
        const Clip& b = loadedSeq->tracks[0].clips[i];
        NC_CHECK(a.id == b.id);
        NC_CHECK(a.start == b.start);
        NC_CHECK(a.duration == b.duration);
        NC_CHECK(a.sourceIn == b.sourceIn);
        NC_CHECK(a.speed == b.speed);
    }
}
NC_TEST(Roundtrip, SaveIsDeterministic) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const std::string a = saveProjectToNclip(project);
    const std::string b = saveProjectToNclip(project);
    NC_CHECK_EQ(a, b);
}
