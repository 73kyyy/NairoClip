#include "pixel_baseline.h"
#include "test_framework.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/media_io.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace nairoclip::core;
using nairoclip::test::FrameDigest;
using nairoclip::test::fnv1a64;

namespace {

/// 确定性读取器：每帧是随 (frameIndex, x, y) 变化的图案。
/// 用图案而不是纯色——纯色图无法发现"采样位置错了"，因为每处都一样。
class PatternReader : public MediaReader {
public:
    explicit PatternReader(int w, int h) : width_(w), height_(h) {
        probe_.type = MediaType::Video;
        probe_.duration = Rational::make(10);
        probe_.frameRate = fps30();
        probe_.width = w;
        probe_.height = h;
    }
    Result open(const std::string&) override {
        open_ = true;
        frame_ = 0;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    /// 真实的 MediaReader（FFmpeg 实现）在 seek 后，下一次 readVideoFrame 返回
    /// **该时间对应的那一帧**。mock 必须遵循同一语义，否则导出器每次 seek 后
    /// 都读到第 0 帧——所有输出帧内容相同，基线就变成了假通过。
    /// （这个坑在写基线时真踩过：30 帧指纹全相同。）
    Result seek(const Time& t) override {
        // 按帧率换算目标帧号
        frame_ = static_cast<int>(t.toDouble() * 30.0 + 0.5);
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame& out) override {
        if (!open_ || frame_ >= 60) {
            return false;
        }
        out.timestamp = Rational::make(frame_);
        out.width = width_;
        out.height = height_;
        out.stride = width_ * 4;
        out.pixels.resize(static_cast<std::size_t>(width_) * height_ * 4);
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                const std::size_t i = (static_cast<std::size_t>(y) * width_ + x) * 4;
                out.pixels[i + 0] = static_cast<std::uint8_t>((x * 7 + frame_ * 13) & 0xFF);
                out.pixels[i + 1] = static_cast<std::uint8_t>((y * 11 + frame_ * 3) & 0xFF);
                out.pixels[i + 2] = static_cast<std::uint8_t>((x + y + frame_) & 0xFF);
                out.pixels[i + 3] = 255;
            }
        }
        ++frame_;
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    int width_;
    int height_;
    MediaProbeResult probe_;
    bool open_ = false;
    int frame_ = 0;
};

class PatternFactory : public MediaReaderFactory {
public:
    explicit PatternFactory(int w, int h) : w_(w), h_(h) {}
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<PatternReader>(w_, h_);
    }

private:
    int w_, h_;
};

/// 跑一次导出，收集每一帧的指纹。
std::vector<FrameDigest> renderDigests(const Project& project, int w, int h) {
    std::vector<FrameDigest> out;
    const Sequence* seq = project.mainSequence();
    if (seq == nullptr) {
        return out;
    }
    const ExportPlan plan = compileExportPlan(project, *seq);
    PatternFactory factory(w, h);
    ExportExecutorOptions opts;
    executeExport(
        plan, factory,
        [&out](const OutputVideoFrame& f) {
            FrameDigest d;
            d.timestampMicros = static_cast<std::int64_t>(f.timestamp.toDouble() * 1000000.0);
            d.width = f.width;
            d.height = f.height;
            d.hash = fnv1a64(f.rgba.data(), f.rgba.size());
            out.push_back(d);
        },
        [](const OutputAudioBlock&) {}, nullptr, opts, &project);
    return out;
}

struct BaselineFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId v1;
    AssetId assetId;
    explicit BaselineFixture(int w = 64, int h = 64) {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = w;
        seq.height = h;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/video/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = a.id;
        NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &v1).isOk());
    }
};

}  // namespace

// =============================================================================
// 像素基线（B2-6 前置设施）
// =============================================================================
//
// 这套断言的作用不是「测某个功能」，而是**证明渲染路径是确定性的**。
// 有了它，改合成主链路时才能对照"改前改后逐像素一致"。
// 没有它，改 B2-6 只能用"编译通过、看起来对"冒充验证。

NC_TEST(RenderBaseline, FramesActuallyDifferAcrossTime) {
    // 这条是前两条的"地基"：如果所有帧的像素都相同，那么
    // 「两次导出一致」和「加滤镜后不一致」都无法证明渲染真的在按时间取样。
    //
    // 用带时间变化的读取器（每帧 R 通道 = frameIndex * 10）。
    // 若导出真的逐帧取样，30 帧里至少应有 2 个不同指纹。
    BaselineFixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());

    const std::vector<FrameDigest> frames = renderDigests(f.project, 64, 64);
    NC_CHECK_EQ(frames.size(), std::size_t(30));
    std::vector<std::uint64_t> uniq;
    for (const auto& d : frames) {
        bool seen = false;
        for (auto h : uniq) {
            if (h == d.hash) {
                seen = true;
                break;
            }
        }
        if (!seen) uniq.push_back(d.hash);
    }
    if (uniq.size() < 2) {
        ::nairoclip::test::reportFailure(
            __FILE__, __LINE__,
            "30 帧只有 " + std::to_string(uniq.size()) +
                " 种指纹：渲染未按时间推进，基线不可信");
    }
    // 时间戳必须真的在推进
    NC_CHECK(frames[1].timestampMicros > frames[0].timestampMicros);
}

NC_TEST(RenderBaseline, SpecificKnownRegionIsStable) {
    // 把「关键区域」的像素值钉死：整帧指纹只能告诉你「变了」，
    // 这条能告诉你「变成了什么」。用于 B2-6 这类改动的对照。
    //
    // 期望值来自当前实现（改动前的正确输出）。若这条变红，说明
    // 合成链路的行为变了——此时必须判断：是修 bug 还是引入回归。
    BaselineFixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());

    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    PatternFactory factory(64, 64);
    int grabbed = 0;
    std::uint8_t sample[4] = {0, 0, 0, 0};
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& fr) {
            if (grabbed == 0 && !fr.rgba.empty()) {
                sample[0] = fr.rgba[0];
                sample[1] = fr.rgba[1];
                sample[2] = fr.rgba[2];
                sample[3] = fr.rgba[3];
            }
            ++grabbed;
        },
        [](const OutputAudioBlock&) {}, nullptr, ExportExecutorOptions{}, &f.project);

    NC_CHECK_EQ(grabbed, 30);
    // 图案：pixels[0] = (x*7 + frame*13) & 0xFF，x=0, frame=0 → 0
    NC_CHECK_EQ(static_cast<int>(sample[0]), 0);
    // pixels[1] = (y*11 + frame*3) & 0xFF，y=0, frame=0 → 0
    NC_CHECK_EQ(static_cast<int>(sample[1]), 0);
    // pixels[2] = (x + y + frame) & 0xFF → 0
    NC_CHECK_EQ(static_cast<int>(sample[2]), 0);
    NC_CHECK_EQ(static_cast<int>(sample[3]), 255);
}

NC_TEST(RenderBaseline, ExportIsDeterministic) {
    // 同一条路径跑两次，指纹序列必须完全相同。
    // 这条一旦失败，说明渲染里有未受控的随机/时间/顺序依赖，
    // 后续任何像素比对都不可信。
    BaselineFixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c).isOk());

    const std::vector<FrameDigest> run1 = renderDigests(f.project, 64, 64);
    const std::vector<FrameDigest> run2 = renderDigests(f.project, 64, 64);

    // 帧数必须 == 时长 × 帧率（1s @ 30fps = 30 帧）。
    // 只断言 !empty() 不够：空序列的两次比对会"一致"，
    // 那种通过是假安全——这里必须钉死具体帧数。
    NC_CHECK_EQ(run1.size(), std::size_t(30));
    NC_CHECK_EQ(run1.size(), run2.size());
    const std::string diff = nairoclip::test::diffDigests(run1, run2);
    if (!diff.empty()) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__, "两次导出不一致: " + diff);
    }
}

NC_TEST(RenderBaseline, DigestDetectsSinglePixelChange) {
    // 自检：基线工具本身必须能发现单像素改动。
    // 否则「比对通过」可能只是因为比对不灵敏——那是假安全。
    std::vector<std::uint8_t> frame(16 * 16 * 4, 128);
    const std::uint64_t before = fnv1a64(frame.data(), frame.size());
    frame[100] = 129;  // 改一个字节
    const std::uint64_t after = fnv1a64(frame.data(), frame.size());
    NC_CHECK(before != after);
}

NC_TEST(RenderBaseline, DiffReportsFrameCountMismatch) {
    std::vector<FrameDigest> a(3), b(5);
    const std::string diff = nairoclip::test::diffDigests(a, b);
    NC_CHECK(!diff.empty());
    NC_CHECK(diff.find("帧数不同") != std::string::npos);
}

NC_TEST(RenderBaseline, DiffReportsFirstDifferingFrame) {
    std::vector<FrameDigest> a(4), b(4);
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i].hash = 100 + i;
        b[i].hash = 100 + i;
    }
    b[2].hash = 999;
    const std::string diff = nairoclip::test::diffDigests(a, b);
    NC_CHECK(!diff.empty());
    NC_CHECK(diff.find("第 2 帧") != std::string::npos);
}

NC_TEST(RenderBaseline, IdenticalDigestsReportNoDiff) {
    std::vector<FrameDigest> a(3), b(3);
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i].hash = b[i].hash = i * 7 + 1;
        a[i].timestampMicros = b[i].timestampMicros = static_cast<std::int64_t>(i) * 33333;
        a[i].width = b[i].width = 64;
        a[i].height = b[i].height = 64;
    }
    NC_CHECK(nairoclip::test::diffDigests(a, b).empty());
}

NC_TEST(RenderBaseline, FilterChangesOutputPixels) {
    // 正向验证：基线确实"看得见"渲染变化。
    // 加一个滤镜，指纹必须变——若不变，说明基线对真实改动也不敏感，
    // 那么它就不能用来证明 B2-6 的"无调整图层时输出一致"。
    BaselineFixture f;
    Clip c;
    c.asset = f.assetId;
    c.start = Rational::make(0);
    c.duration = Rational::make(1);
    ClipId cid;
    NC_CHECK(f.editor.insertClip(f.seqId, f.v1, c, &cid).isOk());

    const std::vector<FrameDigest> plain = renderDigests(f.project, 64, 64);
    NC_CHECK(f.editor.setClipFilter(f.seqId, f.v1, cid, FilterStyle::BlackWhite, 100.0).isOk());
    const std::vector<FrameDigest> filtered = renderDigests(f.project, 64, 64);

    NC_CHECK_EQ(plain.size(), std::size_t(30));
    NC_CHECK_EQ(plain.size(), filtered.size());
    NC_CHECK(nairoclip::test::diffDigests(plain, filtered) != std::string());
}
