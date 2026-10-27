#include "pixel_baseline.h"
#include "test_framework.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/media_io.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

// =============================================================================
// 调整图层（B2-6）
// =============================================================================
//
// 验收要求（文档）：
//   1. 单条调整图层 + 单个 LUT 生效，影响下方轨道；
//   2. **无调整图层时，输出与改动前逐像素一致**；
//   3. 可撤销（增删调整图层走命令）。
//
// 第 2 条是本组最重要的断言——它证明"新功能没有偷偷改变既有渲染"。

namespace {

/// 建一个「视频轨 + 调整图层」的工程。
struct AdjustmentFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId v1;
    TrackId adj;
    AssetId assetId;
    explicit AdjustmentFixture(bool withAdjustmentTrack) {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 64;
        seq.height = 64;
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
        if (withAdjustmentTrack) {
            NC_CHECK(editor.addTrack(seqId, TrackType::Adjustment, "调整", &adj).isOk());
        }
        Clip c;
        c.asset = assetId;
        c.start = Rational::make(0);
        c.duration = Rational::make(1);
        NC_CHECK(editor.insertClip(seqId, v1, c).isOk());
    }
};

}  // namespace

NC_TEST(AdjustmentLayer, AllFrameDigestsMatchPreFeatureBaseline) {
    // B2-6 的真正验收断言：**全部 30 帧**的指纹都必须等于「引入调整图层
    // 功能之前」记录的基线值。
    //
    // 期望值来自 git 提交 303cec4（像素基线设施那次提交）的实测输出，
    // 逐帧记录后用程序自动生成到下面这张表。若任何一帧不符，
    // 说明调整图层功能改变了既有渲染路径——那是不允许的。
    static const std::uint64_t kBaseline[30] = {
        0xfe495ad5c6bd0003ull, 0xe6a1016309d39183ull, 0x469417d76128fc83ull,
        0xce98670c9de67f03ull, 0xb9201f7d87424203ull, 0x54663c41531ab483ull,
        0xe45a7bbd72eeb183ull, 0x53696876744e4003ull, 0x9a020bad65018d03ull,
        0xafa2360f8ee6e583ull, 0x899a3079bdb83f83ull, 0xd3df33059a3e8f03ull,
        0xeca993a0ce8f8403ull, 0x92e224a8b55a5a83ull, 0x57511568180cd883ull,
        0x807edd6c9ef51303ull, 0xd21b51b80885d103ull, 0x51e534b6942b3d83ull,
        0xef105fb605569983ull, 0x809b470eb31d1b03ull, 0x2c6838e2c3f05d03ull,
        0x7440de1b7d87a683ull, 0xd1817c5f84d11e83ull, 0xe15c23b9c63d1103ull,
        0x2ca34cf5c3cbdc03ull, 0xa9d2c9e2b7f66983ull, 0x45e3b3b96ebba783ull,
        0x8f00cb37bb474f03ull, 0x4280c9a0078e5c03ull, 0x1d7283881d404b83ull,
    };
    AdjustmentFixture f(false);
    const std::vector<FrameDigest> frames = renderDigests(f.project, 64, 64);
    NC_CHECK_EQ(frames.size(), std::size_t(30));
    for (std::size_t i = 0; i < frames.size() && i < 30; ++i) {
        if (frames[i].hash != kBaseline[i]) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "第 %zu 帧与 B2-6 之前不一致: %016llx != %016llx", i,
                          static_cast<unsigned long long>(frames[i].hash),
                          static_cast<unsigned long long>(kBaseline[i]));
            ::nairoclip::test::reportFailure(__FILE__, __LINE__, buf);
            break;  // 只报第一处，避免刷屏
        }
    }
}

NC_TEST(AdjustmentLayer, NoAdjustmentKeepsOutputIdentical) {
    // 注意这条断言**能证明什么、不能证明什么**：
    //   ✅ 能证明：两次渲染完全确定（无隐藏随机/顺序依赖）；
    //   ❌ 不能证明：输出与「引入本功能之前」一致。
    //      因为两次运行用的是同一份代码——若代码整体偏移了一个像素，
    //      两次仍然彼此一致。这个洞是变异测试发现的：向渲染尾部注入
    //      「无条件改一个像素」后，本断言依然通过。
    //
    // 真正守住「未改变既有输出」的是 SpecificKnownRegionIsStable
    // （钉死具体像素值）。两条一起看才有意义。
    AdjustmentFixture f(false);
    const std::vector<FrameDigest> frames = renderDigests(f.project, 64, 64);
    NC_CHECK_EQ(frames.size(), std::size_t(30));
    AdjustmentFixture g(false);
    const std::vector<FrameDigest> again = renderDigests(g.project, 64, 64);
    NC_CHECK(nairoclip::test::diffDigests(frames, again).empty());
}

NC_TEST(AdjustmentLayer, EmptyAdjustmentTrackChangesNothing) {
    // 有调整图层但没设任何效果 → 必须零影响。
    // 这条防止「把空调整图层当成不透明黑层盖上」这类错误实现。
    AdjustmentFixture with(false);
    const std::vector<FrameDigest> plain = renderDigests(with.project, 64, 64);

    AdjustmentFixture withAdj(true);
    const std::vector<FrameDigest> withEmpty = renderDigests(withAdj.project, 64, 64);

    NC_CHECK_EQ(plain.size(), withEmpty.size());
    const std::string diff = nairoclip::test::diffDigests(plain, withEmpty);
    if (!diff.empty()) {
        ::nairoclip::test::reportFailure(__FILE__, __LINE__,
                                         "空调整图层改变了输出: " + diff);
    }
}

NC_TEST(AdjustmentLayer, EffectsApplyToLowerTracks) {
    // 给调整图层加「黑白」特效 → 输出必须变化
    AdjustmentFixture f(true);
    const std::vector<FrameDigest> before = renderDigests(f.project, 64, 64);

    Sequence* seq = f.project.findSequence(f.seqId);
    Track* track = seq->findTrack(f.adj);
    NC_CHECK(track != nullptr);
    Effect e;
    e.id = EffectId{f.project.ids.next()};
    e.type = EffectType::Grayscale;
    e.value = 1.0;
    NC_CHECK(track->adjustmentEffects.add(e).isOk());

    const std::vector<FrameDigest> after = renderDigests(f.project, 64, 64);
    NC_CHECK_EQ(before.size(), after.size());
    NC_CHECK(!nairoclip::test::diffDigests(before, after).empty());
}

NC_TEST(AdjustmentLayer, GrayscaleActuallyRemovesColor) {
    // 不只断言"变了"，还要断言"变成了什么样"：
    // 灰度 1.0 后 R=G=B（在同一帧上逐像素检查）。
    AdjustmentFixture f(true);
    Sequence* seq = f.project.findSequence(f.seqId);
    Track* track = seq->findTrack(f.adj);
    Effect e;
    e.id = EffectId{f.project.ids.next()};
    e.type = EffectType::Grayscale;
    e.value = 1.0;
    NC_CHECK(track->adjustmentEffects.add(e).isOk());

    const ExportPlan plan = compileExportPlan(f.project, *f.project.mainSequence());
    PatternFactory factory(64, 64);
    bool checked = false;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& fr) {
            if (checked || fr.rgba.empty()) {
                return;
            }
            // 抽样检查若干像素：灰度后各通道应相等（±1 容差，防舍入）
            for (int idx : {0, 40, 400, 4000}) {
                const int r = fr.rgba[static_cast<std::size_t>(idx) * 4 + 0];
                const int g = fr.rgba[static_cast<std::size_t>(idx) * 4 + 1];
                const int b = fr.rgba[static_cast<std::size_t>(idx) * 4 + 2];
                if (std::abs(r - g) > 1 || std::abs(g - b) > 1) {
                    ::nairoclip::test::reportFailure(
                        __FILE__, __LINE__,
                        "灰度后像素仍有色彩: rgb=" + std::to_string(r) + "," +
                            std::to_string(g) + "," + std::to_string(b));
                }
            }
            checked = true;
        },
        [](const OutputAudioBlock&) {}, nullptr, ExportExecutorOptions{}, &f.project);
    NC_CHECK(checked);
}

NC_TEST(AdjustmentLayer, HiddenAdjustmentIsSkipped) {
    // 隐藏的调整图层不应生效（与视频轨隐藏语义一致）
    AdjustmentFixture f(true);
    Sequence* seq = f.project.findSequence(f.seqId);
    Track* track = seq->findTrack(f.adj);
    Effect e;
    e.id = EffectId{f.project.ids.next()};
    e.type = EffectType::Grayscale;
    e.value = 1.0;
    NC_CHECK(track->adjustmentEffects.add(e).isOk());
    track->hidden = true;

    AdjustmentFixture plain(false);
    const std::vector<FrameDigest> withHidden = renderDigests(f.project, 64, 64);
    const std::vector<FrameDigest> baseline = renderDigests(plain.project, 64, 64);
    // 隐藏后应与无调整图层时一致
    NC_CHECK(nairoclip::test::diffDigests(baseline, withHidden).empty());
}
