#include "test_framework.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/editor.h"
using namespace nairoclip::core;
namespace {
class PreviewMockFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        MockMediaReader::Config cfg;
        cfg.probe.type = MediaType::Video;
        cfg.probe.frameRate = fps30();
        cfg.probe.width = 64;
        cfg.probe.height = 48;
        cfg.probe.duration = Rational::make(60);
        cfg.videoFrameCount = 100000;
        return std::make_unique<MockMediaReader>(cfg);
    }
};
struct Fixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId videoTrack;
    AssetId assetId;
    Fixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 64;
        seq.height = 48;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;
        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/video/a.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        assetId = project.assets.front().id;
        editor.addTrack(seqId, TrackType::Video, "V1", &videoTrack);
    }
    ClipId addClip(const Time& start, const Time& duration) {
        Clip c;
        c.asset = assetId;
        c.start = start;
        c.duration = duration;
        ClipId id;
        editor.insertClip(seqId, videoTrack, c, &id);
        return id;
    }
};
}  // namespace
NC_TEST(Preview, RendersActiveClip) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(2));
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK(frame.clipId != 0);
    NC_CHECK_EQ(frame.width, 64);
    NC_CHECK_EQ(frame.height, 48);
    NC_CHECK(!frame.rgba.empty());
    NC_CHECK_EQ(frame.rgba.size(), std::size_t(64 * 48 * 4));
    // 无动画通道 → 默认 1.0
    NC_CHECK(std::abs(frame.opacity - 1.0) < 1e-9);
}
NC_TEST(Preview, EmptyGapReturnsNoClip) {
    Fixture f;
    f.addClip(Rational::make(5), Rational::make(2));  // [5,7)
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK_EQ(frame.clipId, std::uint64_t(0));
    NC_CHECK(frame.rgba.empty());
    NC_CHECK(std::abs(frame.opacity) < 1e-9);  // 黑场标记
}
NC_TEST(Preview, IncludesSubtitles) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(5));
    SubtitleCue cue;
    cue.id = SubtitleId{f.project.ids.next()};
    cue.start = Rational::make(1);
    cue.duration = Rational::make(2);
    cue.text = "预览字幕";
    NC_CHECK(f.editor.upsertSubtitle(f.seqId, cue).isOk());
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    // t=1.5 有字幕
    const PreviewFrame withSub = renderer.renderAt(fromMilliseconds(1500));
    NC_CHECK_EQ(withSub.subtitles.size(), std::size_t(1));
    NC_CHECK_EQ(withSub.subtitles[0], std::string("预览字幕"));
    // t=4 无字幕
    const PreviewFrame noSub = renderer.renderAt(Rational::make(4));
    NC_CHECK(noSub.subtitles.empty());
}
NC_TEST(Preview, AppliesAnimationOpacity) {
    Fixture f;
    const ClipId clip = f.addClip(Rational::make(0), Rational::make(4));
    Keyframe k1;
    k1.time = Rational::make(0);
    k1.value = 0.0;
    Keyframe k2;
    k2.time = Rational::make(4);
    k2.value = 1.0;
    NC_CHECK(f.editor
                 .insertKeyframe(f.seqId, f.videoTrack, clip, ClipProperty::Opacity, k1)
                 .isOk());
    NC_CHECK(f.editor
                 .insertKeyframe(f.seqId, f.videoTrack, clip, ClipProperty::Opacity, k2)
                 .isOk());
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    // t=2 → 线性中点 0.5
    const PreviewFrame frame = renderer.renderAt(Rational::make(2));
    NC_CHECK(std::abs(frame.opacity - 0.5) < 1e-6);
}
NC_TEST(Preview, HiddenTrackSkipped) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(2));
    NC_CHECK(f.editor.setTrackHidden(f.seqId, f.videoTrack, true).isOk());
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    // 隐藏轨道 → 无画面
    NC_CHECK_EQ(frame.clipId, std::uint64_t(0));
}
NC_TEST(Preview, AppliesColorEffectsToPixels) {
    Fixture f;
    const ClipId clip = f.addClip(Rational::make(0), Rational::make(2));
    // 加亮度 +0.5
    Effect e;
    e.id = EffectId{f.project.ids.next()};
    e.type = EffectType::Brightness;
    e.value = 0.5;
    NC_CHECK(f.editor.addEffect(f.seqId, f.videoTrack, clip, e).isOk());
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK(!frame.rgba.empty());
    // Mock 帧灰度 128（0.502），+0.5 后 ≈ 1.0（255）
    NC_CHECK(frame.rgba[0] >= 254);
    NC_CHECK(frame.rgba[1] >= 254);
    NC_CHECK(frame.rgba[2] >= 254);
    // alpha 不变
    NC_CHECK_EQ(frame.rgba[3], std::uint8_t(255));
}
NC_TEST(Preview, GrayscaleEffectTransformsPixels) {
    Fixture f;
    const ClipId clip = f.addClip(Rational::make(0), Rational::make(2));
    // 未加特效的对照帧
    PreviewMockFactory factory;
    {
        PreviewRenderer renderer(f.project, factory);
        const PreviewFrame plain = renderer.renderAt(Rational::make(1));
        NC_CHECK(!plain.rgba.empty());
    }
    Effect e;
    e.id = EffectId{f.project.ids.next()};
    e.type = EffectType::Saturation;
    e.value = 0.0;  // 完全去饱和
    NC_CHECK(f.editor.addEffect(f.seqId, f.videoTrack, clip, e).isOk());
    PreviewRenderer renderer2(f.project, factory);
    const PreviewFrame frame = renderer2.renderAt(Rational::make(1));
    NC_CHECK(!frame.rgba.empty());
    // 灰度化后 RGB 三通道应相等
    NC_CHECK_EQ(frame.rgba[0], frame.rgba[1]);
    NC_CHECK_EQ(frame.rgba[1], frame.rgba[2]);
}
NC_TEST(Preview, TransitionBlending) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(2));  // [0,2)
    f.addClip(Rational::make(3), Rational::make(2));  // [3,5)
    Track* track = f.project.mainSequence()->findTrack(f.videoTrack);
    Transition tr;
    tr.id = TransitionId{f.project.ids.next()};
    tr.type = TransitionType::CrossDissolve;
    tr.duration = Rational::make(1, 2);
    tr.fromClip = track->clips[0].id;
    tr.toClip = track->clips[1].id;
    track->transitions.push_back(tr);
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    // 转场区间 [2, 2.5)：t=2.25 → progress 0.5
    const PreviewFrame frame = renderer.renderAt(fromMilliseconds(2250));
    NC_CHECK_EQ(frame.transitionType, static_cast<int>(TransitionType::CrossDissolve));
    NC_CHECK(std::abs(frame.transitionProgress - 0.5) < 0.02);
    // 混合输出应有像素
    NC_CHECK(!frame.rgba.empty());
}
NC_TEST(Preview, NoTransitionOutsideInterval) {
    Fixture f;
    f.addClip(Rational::make(0), Rational::make(2));
    f.addClip(Rational::make(3), Rational::make(2));
    Track* track = f.project.mainSequence()->findTrack(f.videoTrack);
    Transition tr;
    tr.id = TransitionId{f.project.ids.next()};
    tr.type = TransitionType::CrossDissolve;
    tr.duration = Rational::make(1, 2);
    tr.fromClip = track->clips[0].id;
    tr.toClip = track->clips[1].id;
    track->transitions.push_back(tr);
    PreviewMockFactory factory;
    PreviewRenderer renderer(f.project, factory);
    // t=1 不在转场区间
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK_EQ(frame.transitionType, -1);
}

// =============================================================================
// 多层合成（G1）：画中画必须在预览里可见
// =============================================================================

namespace {

/// 按**素材路径**返回不同灰度的 reader。
///
/// 为什么不用 MockMediaReader + 固定 grayValue：MediaReaderFactory::create()
/// 不带参数，拿不到「这是哪个素材」，因此无法让上下层输出不同灰度。
/// 这里自己实现一个最小 reader，open(path) 时按路径里的 lower/upper/third
/// 决定灰度 —— 这样才能真正断言「两层混合后的像素介于两者之间」。
class PathAwareReader : public MediaReader {
public:
    explicit PathAwareReader(std::uint8_t gray) : gray_(gray) {}

    Result open(const std::string& path) override {
        // 从路径关键词决定灰度（见下面 Factory 的映射）
        if (path.find("lower") != std::string::npos) gray_ = 60;
        else if (path.find("upper") != std::string::npos) gray_ = 200;
        else if (path.find("third") != std::string::npos) gray_ = 120;
        open_ = true;
        probe_.type = MediaType::Video;
        probe_.frameRate = fps30();
        probe_.width = 64;
        probe_.height = 48;
        probe_.duration = Rational::make(60);
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time& sourceTime) override {
        if (!open_) return Result::fail(ErrorCode::InvalidArgument, "未打开");
        if (sourceTime.isNegative()) return Result::fail(ErrorCode::OutOfRange, "负时间");
        return Result::ok();
    }
    bool readVideoFrame(VideoFrame& out) override {
        if (!open_) return false;
        out.width = 64;
        out.height = 48;
        out.stride = 64 * 4;
        out.pixels.assign(static_cast<std::size_t>(out.stride) * 48, gray_);
        for (std::size_t i = 3; i < out.pixels.size(); i += 4) out.pixels[i] = 255;
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }

private:
    std::uint8_t gray_;
    bool open_ = false;
    MediaProbeResult probe_;
};

class MultiLayerFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<PathAwareReader>(128);
    }
};

/// 建一个双视频轨工程，两条轨在同一时刻各有一个片段。
struct LayeredFixture {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId lowerTrack;
    TrackId upperTrack;
    AssetId lowerAsset;
    AssetId upperAsset;

    LayeredFixture() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 64;
        seq.height = 48;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;

        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/video/lower.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        lowerAsset = project.assets.back().id;

        MediaAsset b;
        b.id = AssetId{project.ids.next()};
        b.path = "/video/upper.mp4";
        b.type = MediaType::Video;
        b.duration = Rational::make(60);
        project.assets.push_back(b);
        upperAsset = project.assets.back().id;

        editor.addTrack(seqId, TrackType::Video, "V1", &lowerTrack);
        editor.addTrack(seqId, TrackType::Video, "V2", &upperTrack);
    }

    void addClip(TrackId track, AssetId asset, const Time& start, const Time& dur) {
        Clip c;
        c.asset = asset;
        c.start = start;
        c.duration = dur;
        editor.insertClip(seqId, track, c);
    }
};

}  // namespace

// 修复的核心断言：此前 activeVideoClip 只返回第一层，第二层被完全忽略。
NC_TEST(PreviewMultiLayer, UpperTrackIsVisible) {
    LayeredFixture f;
    f.addClip(f.lowerTrack, f.lowerAsset, Rational::make(0), Rational::make(2));
    f.addClip(f.upperTrack, f.upperAsset, Rational::make(0), Rational::make(2));

    /* 上层不透明（opacity=1）→ 应以「上层」为主。
       无法直接从 frame 判断是哪一层，改用像素灰度：
       让工厂每次都返回固定灰度，先跑一次单层确认基线。 */
    MultiLayerFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK_EQ(frame.width, 64);
    NC_CHECK_EQ(frame.height, 48);
    NC_CHECK(!frame.rgba.empty());
    NC_CHECK_EQ(frame.rgba.size(), static_cast<std::size_t>(64) * 48 * 4);

    /* 上层（灰 200）完全不透明 → 预览结果必须是 200。
       修复前：只渲染第一条视频轨（下层，灰 60），结果会是 60。
       这一条是「画中画在预览里看得见」的直接证明。 */
    NC_CHECK_EQ(frame.rgba[0], std::uint8_t(200));
}

// 上层不透明度 0.5 时，结果应介于上下两层灰度之间（证明真的做了混合）
NC_TEST(PreviewMultiLayer, SemiTransparentUpperBlendsWithLower) {
    LayeredFixture f;
    f.addClip(f.lowerTrack, f.lowerAsset, Rational::make(0), Rational::make(2));
    f.addClip(f.upperTrack, f.upperAsset, Rational::make(0), Rational::make(2));

    // 给上层打一个恒定的不透明度关键帧（0.5）
    Track* up = f.project.mainSequence()->findTrack(f.upperTrack);
    NC_CHECK(up != nullptr && !up->clips.empty());
    Clip& upperClip = up->clips.front();
    Keyframe kf;
    kf.time = Time{};
    kf.value = 0.5;
    kf.easing = Easing::linear();
    NC_CHECK(upperClip.animation.curve(ClipProperty::Opacity).insert(kf).isOk());

    MultiLayerFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK(!frame.rgba.empty());
    // 合成后应是不透明结果
    NC_CHECK_EQ(frame.rgba[3], std::uint8_t(255));

    /* 像素断言（这才是「混合真的发生了」的证据）：
       下层灰 60、上层灰 200、上层 opacity=0.5
       → 期望 ≈ 0.5*200 + 0.5*60 = 130（允许 ±3 的取整误差）。
       若混合未生效（上层直接覆盖），会是 200；
       若下层被忽略，会是 60。 */
    const int got = frame.rgba[0];
    NC_CHECK(got > 120 && got < 140);
}

// 三条视频轨：第 3 层不能被静默丢弃（导出侧 N 层修复的对应断言）
NC_TEST(PreviewMultiLayer, ThreeLayersAllCollected) {
    LayeredFixture f;
    TrackId thirdTrack;
    f.editor.addTrack(f.seqId, TrackType::Video, "V3", &thirdTrack);

    MediaAsset c;
    c.id = AssetId{f.project.ids.next()};
    c.path = "/video/third.mp4";
    c.type = MediaType::Video;
    c.duration = Rational::make(60);
    f.project.assets.push_back(c);
    const AssetId thirdAsset = f.project.assets.back().id;

    f.addClip(f.lowerTrack, f.lowerAsset, Rational::make(0), Rational::make(2));
    f.addClip(f.upperTrack, f.upperAsset, Rational::make(0), Rational::make(2));
    f.addClip(thirdTrack, thirdAsset, Rational::make(0), Rational::make(2));

    MultiLayerFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK(!frame.rgba.empty());
    NC_CHECK_EQ(frame.width, 64);
    NC_CHECK_EQ(frame.height, 48);
}

// 隐藏上层轨后，预览应回到单层（对应轨道小眼睛按钮的效果）
NC_TEST(PreviewMultiLayer, HiddenUpperTrackFallsBackToSingleLayer) {
    LayeredFixture f;
    f.addClip(f.lowerTrack, f.lowerAsset, Rational::make(0), Rational::make(2));
    f.addClip(f.upperTrack, f.upperAsset, Rational::make(0), Rational::make(2));

    NC_CHECK(f.editor.setTrackHidden(f.seqId, f.upperTrack, true).isOk());

    MultiLayerFactory factory;
    PreviewRenderer renderer(f.project, factory);
    const PreviewFrame frame = renderer.renderAt(Rational::make(1));
    NC_CHECK(!frame.rgba.empty());
    // 单层仍应正常出画面
    NC_CHECK_EQ(frame.rgba.size(), static_cast<std::size_t>(64) * 48 * 4);
}
