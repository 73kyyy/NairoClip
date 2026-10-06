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
