#include "pixel_baseline.h"
#include "test_framework.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/serialization.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace nairoclip::core;
using nairoclip::test::fnv1a64;

namespace {

/// 每帧内容随时间明显变化的读取器（frameIndex 编码进像素）。
/// seek 语义与真实 FFmpeg 一致：seek 后读到的是目标时间的帧。
class TimeVaryingReader : public MediaReader {
public:
    TimeVaryingReader() {
        probe_.type = MediaType::Video;
        probe_.duration = Rational::make(10);
        probe_.frameRate = fps30();
        probe_.width = 16;
        probe_.height = 16;
    }
    Result open(const std::string&) override {
        open_ = true;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time& t) override {
        currentFrame_ = static_cast<int>(t.toDouble() * 30.0 + 0.5);
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame& out) override {
        out.timestamp = Rational::make(currentFrame_);
        out.width = 16;
        out.height = 16;
        out.stride = 64;
        out.pixels.assign(16 * 16 * 4, 0);
        // 帧号直接编进像素：不同帧 → 不同像素，冻帧若泄漏就会立刻暴露
        const std::uint8_t v = static_cast<std::uint8_t>((currentFrame_ * 9 + 30) & 0xFF);
        for (int i = 0; i < 16 * 16; ++i) {
            out.pixels[static_cast<std::size_t>(i) * 4 + 0] = v;
            out.pixels[static_cast<std::size_t>(i) * 4 + 1] = v;
            out.pixels[static_cast<std::size_t>(i) * 4 + 2] = v;
            out.pixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
        }
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    MediaProbeResult probe_;
    bool open_ = false;
    int currentFrame_ = 0;
};

class Factory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<TimeVaryingReader>();
    }
};

struct Fx {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId v1;
    AssetId assetId;
    Fx() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 16;
        seq.height = 16;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/v/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = a.id;
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &v1).isOk());
    }
    /// 返回每帧首像素的值（用于判断"是否停在同一个源帧"）。
    std::vector<int> renderFirstPixels() {
        std::vector<int> out;
        const ExportPlan plan = compileExportPlan(project, *project.mainSequence());
        Factory factory;
        executeExport(
            plan, factory,
            [&out](const OutputVideoFrame& f) {
                out.push_back(f.rgba.empty() ? -1 : static_cast<int>(f.rgba[0]));
            },
            [](const OutputAudioBlock&) {}, nullptr, ExportExecutorOptions{}, &project);
        return out;
    }
};

}  // namespace

// =============================================================================
// 定格/冻帧（B3-2）
// =============================================================================
//
// 验收标准（文档原文）：**导出后逐帧比对，冻帧段像素完全一致**。
//
// 旧实现用 speed = 1/1000000 模拟定格。它在本仓库的 mock reader 下侥幸通过，
// 但语义是巧合：任何把"源时间→帧号"取整方式改变的实现，都会让定格悄悄失效。

NC_TEST(FreezeFrame, FrozenSegmentHasIdenticalPixels) {
    Fx f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    c.freeze = true;
    c.sourceIn = Rational::make(1, 2);  // 定格在源 0.5 秒处
    const std::size_t frameCount = 30;
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());

    const std::vector<int> pixels = f.renderFirstPixels();
    NC_CHECK_EQ(pixels.size(), frameCount);
    if (pixels.empty()) {
        return;
    }
    // 逐帧比对：全部等于首帧
    for (std::size_t i = 1; i < pixels.size(); ++i) {
        if (pixels[i] != pixels[0]) {
            ::nairoclip::test::reportFailure(
                __FILE__, __LINE__,
                "冻帧段第 " + std::to_string(i) + " 帧与首帧不同: " +
                    std::to_string(pixels[i]) + " != " + std::to_string(pixels[0]));
            break;
        }
    }
}

NC_TEST(FreezeFrame, NonFrozenClipDoesVaryAcrossFrames) {
    // 反向验证：不冻帧的同一片段必须逐帧变化。
    // 若不变，说明上面的「冻帧一致」可能只是因为整个管道不推进——
    // 那是假通过。
    Fx f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    c.sourceIn = Rational::make(1, 2);
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());

    const std::vector<int> pixels = f.renderFirstPixels();
    NC_CHECK_EQ(pixels.size(), std::size_t(30));
    int distinct = 0;
    for (std::size_t i = 1; i < pixels.size(); ++i) {
        if (pixels[i] != pixels[i - 1]) {
            ++distinct;
        }
    }
    if (distinct == 0) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "未冻帧的片段也逐帧相同 —— 对照失效，冻帧断言不可信");
    }
}

NC_TEST(FreezeFrame, FreezeUsesSourceIn) {
    // 定格应停在 sourceIn 指定的那一帧，而不是第 0 帧。
    // 两个不同 sourceIn 的定格片段，像素应不同。
    Fx f;
    Clip a;
    a.asset = f.assetId;
    a.start = Rational::make(0);
    a.duration = Rational::make(1, 5);
    a.freeze = true;
    a.sourceIn = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, a).isOk());
    const std::vector<int> atOne = f.renderFirstPixels();

    Fx g;
    Clip b;
    b.asset = g.assetId;
    b.start = Rational::make(0);
    b.duration = Rational::make(1, 5);
    b.freeze = true;
    b.sourceIn = Rational::make(2);
    NC_CHECK(g.editor.insertClip(g.seqId, g.v1, b).isOk());
    const std::vector<int> atTwo = g.renderFirstPixels();

    // 先确认两段都真的出了帧，再索引——否则断言失败后会越界崩溃，
    // 把「用例失败」升级成「整套测试段错误」（此前踩过）。
    if (atOne.empty() || atTwo.empty()) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__, "没有输出帧，无法比较");
        return;
    }
    // 不同源位置 → 不同帧内容
    NC_CHECK(atOne[0] != atTwo[0]);
}

NC_TEST(FreezeFrame, EmptyFreezeLongSegmentStaysFrozen) {
    // 长定格（10 秒 = 300 帧）仍必须完全静止。
    // 旧实现用 speed=1/1e6，长定格下源时间会漂移约 1e-5 秒——
    // 看似无害，但若 reader 按"四舍五入到帧"取样，恰好在帧边界上会跳帧。
    Fx f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(10);
    c.freeze = true;
    c.sourceIn = Rational::make(1, 2);
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());

    const std::vector<int> pixels = f.renderFirstPixels();
    NC_CHECK_EQ(pixels.size(), std::size_t(300));
    for (std::size_t i = 1; i < pixels.size(); ++i) {
        if (pixels[i] != pixels[0]) {
            ::nairoclip::test::reportFailure(
                __FILE__, __LINE__,
                "长定格第 " + std::to_string(i) + " 帧发生变化");
            break;
        }
    }
}

NC_TEST(FreezeFrame, ClipValidWithFreeze) {
    // 定格片段必须是合法片段（freeze 不破坏 valid()）
    Clip c;
    c.asset = AssetId{1};
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    c.sourceIn = Rational::make(0);
    c.speed = Rational::make(1);
    c.freeze = true;
    NC_CHECK(c.valid());
    // sourceTimeAt 恒返回 sourceIn
    NC_CHECK(c.sourceTimeAt(Rational::make(0)) == Rational::make(0));
    NC_CHECK(c.sourceTimeAt(Rational::make(1)) == Rational::make(0));
    NC_CHECK(c.sourceTimeAt(Rational::make(19, 10)) == Rational::make(0));
}

NC_TEST(FreezeFrame, FreezeOverridesReverseAndCurve) {
    // 定格与倒放/速度曲线互斥：freeze 优先
    Clip c;
    c.asset = AssetId{1};
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    c.sourceIn = Rational::make(3);
    c.speed = Rational::make(1);
    c.freeze = true;
    c.reversed = true;  // 同时设了倒放
    NC_CHECK(c.sourceTimeAt(Rational::make(1, 2)) == Rational::make(3));
    NC_CHECK(c.sourceTimeAt(Rational::make(3, 2)) == Rational::make(3));
}

NC_TEST(FreezeFrame, SerializationRoundtrip) {
    Fx f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(2);
    c.sourceIn = Rational::make(1, 2);
    c.freeze = true;
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());

    Project out;
    const NclipLoadResult r = loadProjectFromNclip(saveProjectToNclip(f.project), out);
    NC_CHECK(r.result.isOk());
    NC_CHECK(out.sequences[0].tracks[0].clips[0].freeze);
    NC_CHECK(out.sequences[0].tracks[0].clips[0].sourceIn == Rational::make(1, 2));
}

NC_TEST(FreezeFrame, NotFrozenOmitsField) {
    Fx f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"freeze\"") == std::string::npos);
}
