#include "test_framework.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/serialization.h"
#include <chrono>
#include <cstdio>
#include <string>
using namespace nairoclip::core;
namespace {
/// 大规模工程压力测试：1000 片段插入 + 500 次撤销 + 500 次重做 + 存档读档。
NC_TEST(Stress, ThousandClipsUndoRedoSaveLoad) {
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.type = MediaType::Video;
    a.duration = Rational::make(100000);
    project.assets.push_back(a);
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    const auto t0 = std::chrono::steady_clock::now();
    // 1000 个不重叠片段，每个 100ms、间隙 5ms
    Time cursor = Rational::make(0);
    const Time step = fromMilliseconds(105);
    const Time duration = fromMilliseconds(100);
    for (int i = 0; i < 1000; ++i) {
        Clip c;
        c.asset = a.id;
        c.start = cursor;
        c.duration = duration;
        const Result r = editor.insertClip(seqId, trackId, c);
        NC_CHECK(r.isOk());
        cursor = cursor + step;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const Track* track = findTrack(project, seqId, trackId);
    NC_CHECK(track != nullptr);
    NC_CHECK_EQ(track->clips.size(), std::size_t(1000));
    NC_CHECK(track->invariantsHold());
    // 500 撤销 + 500 重做
    for (int i = 0; i < 500; ++i) {
        NC_CHECK(editor.undo().isOk());
    }
    NC_CHECK_EQ(findTrack(project, seqId, trackId)->clips.size(), std::size_t(500));
    for (int i = 0; i < 500; ++i) {
        NC_CHECK(editor.redo().isOk());
    }
    NC_CHECK_EQ(findTrack(project, seqId, trackId)->clips.size(), std::size_t(1000));
    const auto t2 = std::chrono::steady_clock::now();
    // 存档读档
    const std::string text = saveProjectToNclip(project);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK_EQ(loaded.mainSequence()->tracks[0].clips.size(), std::size_t(1000));
    const auto t3 = std::chrono::steady_clock::now();
    const auto ms = [](auto from, auto to) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
    };
    std::printf("  [stress] insert1000=%lldms undo/redo1000=%lldms saveload=%lldms\n",
                static_cast<long long>(ms(t0, t1)),
                static_cast<long long>(ms(t1, t2)),
                static_cast<long long>(ms(t2, t3)));
    // 性能底线（宽松阈值，防止 CI 抖动误报）：全部操作 < 10s
    NC_CHECK(ms(t0, t3) < 10000);
}
}  // namespace
