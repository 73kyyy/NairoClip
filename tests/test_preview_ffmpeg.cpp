#include "test_framework.h"

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/editor.h"
#include "nairoclip/core/media_ffmpeg.h"
#include "nairoclip/core/preview.h"
#include <cmath>
#include <fstream>
using namespace nairoclip::core;
// =============================================================================
// 预览器真实解码测试：验证预览路径能驱动 FFmpeg 解码真实文件
// =============================================================================
// 链路：工程（引用真实 mp4）→ PreviewRenderer.renderAt → 真实像素

namespace {
bool testMediaExists() {
    std::ifstream f("/tmp/media_gen/test_av.mp4");
    return f.good();
}
struct Fixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId trackId;
    AssetId assetId;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 160;
        seq.height = 120;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/tmp/media_gen/test_av.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(3);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &trackId);
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(2);
        editor.insertClip(seqId, trackId, c);
    }
};
}  // namespace
NC_TEST(PreviewFFmpeg, RendersRealPixels) {
    if (!testMediaExists()) return;
    Fixture f;
    FFmpegReaderFactory factory;
    PreviewRenderer preview(f.project, factory);
    const PreviewFrame frame = preview.renderAt(Rational::make(1));
    // 尺寸来自序列
    NC_CHECK_EQ(frame.width, 160);
    NC_CHECK_EQ(frame.height, 120);
    // 像素非空
    NC_CHECK(!frame.rgba.empty());
    // 检查有真实内容（testsrc 彩条：颜色丰富）
    bool sawColor = false;
    for (std::size_t i = 0; i + 3 < frame.rgba.size(); i += 4) {
        if (frame.rgba[i] > 60 || frame.rgba[i + 1] > 60 || frame.rgba[i + 2] > 60) {
            sawColor = true;
            break;
        }
    }
    NC_CHECK(sawColor);
}
#endif
