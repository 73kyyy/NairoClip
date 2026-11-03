#include "test_framework.h"
#include "nairoclip/core/preview.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/xform.h"
#include "nairoclip/core/serialization.h"
using namespace nairoclip::core;

namespace {

/// 固定灰度的最小 reader（按路径区分灰度，用于验证「变换后像素怎么变」）。
class GrayReader : public MediaReader {
public:
    Result open(const std::string& path) override {
        if (path.find("upper") != std::string::npos) gray_ = 200;
        else gray_ = 60;
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
    Result seek(const Time& t) override {
        if (!open_) return Result::fail(ErrorCode::InvalidArgument, "未打开");
        if (t.isNegative()) return Result::fail(ErrorCode::OutOfRange, "负时间");
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
    std::uint8_t gray_ = 128;
    bool open_ = false;
    MediaProbeResult probe_;
};

class GrayFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override {
        return std::make_unique<GrayReader>();
    }
};

struct Fx {
    Project project;
    TimelineEditor editor{project};
    SequenceId seqId;
    TrackId track;
    AssetId asset;
    ClipId clip;

    Fx() {
        Sequence seq;
        seq.id = SequenceId{project.ids.next()};
        seq.width = 64;
        seq.height = 48;
        project.sequences.push_back(seq);
        seqId = project.sequences.front().id;

        MediaAsset a;
        a.id = AssetId{project.ids.next()};
        a.path = "/video/base.mp4";
        a.type = MediaType::Video;
        a.duration = Rational::make(60);
        project.assets.push_back(a);
        asset = project.assets.back().id;

        editor.addTrack(seqId, TrackType::Video, "V1", &track);
        Clip c;
        c.asset = asset;
        c.start = Rational::make(0);
        c.duration = Rational::make(2);
        editor.insertClip(seqId, track, c, &clip);
    }
    Clip* clipPtr() {
        Track* t = project.mainSequence()->findTrack(track);
        return t ? t->findClip(clip) : nullptr;
    }
};

}  // namespace

// =============================================================================
// ClipTransform 结构
// =============================================================================

NC_TEST(ClipTransform, DefaultIsNeutral) {
    ClipTransform t;
    NC_CHECK(!t.enabled);          // 默认不启用 → 老工程像素零变化
    NC_CHECK(t.scale == 1.0);
    NC_CHECK(t.posX == 0.0);
    NC_CHECK(t.posY == 0.0);
    NC_CHECK(t.rotation == 0.0);
    NC_CHECK(t.anchor == 4);
}

NC_TEST(ClipTransform, ClampOutOfRange) {
    ClipTransform t;
    t.scale = 999.0;
    t.posX = -99.0;
    t.posY = 99.0;
    t.rotation = 99999.0;
    t.anchor = 42;
    t.clamp();
    NC_CHECK(t.scale == 10.0);
    NC_CHECK(t.posX == -10.0);
    NC_CHECK(t.posY == 10.0);
    NC_CHECK(t.rotation == 3600.0);
    NC_CHECK(t.anchor == 4);
}

NC_TEST(ClipTransform, ClampScaleFloor) {
    ClipTransform t;
    t.scale = 0.0;     // 不能为 0（会让画面消失且无法恢复）
    t.clamp();
    NC_CHECK(t.scale == 0.05);
}

// =============================================================================
// 命令可撤销
// =============================================================================

NC_TEST(SetClipTransform, AppliesAndReverts) {
    Fx f;
    ClipTransform t;
    t.enabled = true;
    t.scale = 0.5;
    t.posX = 0.25;
    NC_CHECK(f.editor.setClipTransform(f.seqId, f.track, f.clip, t).isOk());

    ClipTransform got;
    NC_CHECK(f.editor.getClipTransform(f.project, f.seqId, f.track, f.clip, got));
    NC_CHECK(got.enabled);
    NC_CHECK(std::abs(got.scale - 0.5) < 1e-9);
    NC_CHECK(std::abs(got.posX - 0.25) < 1e-9);

    NC_CHECK(f.editor.undo().isOk());
    ClipTransform back;
    NC_CHECK(f.editor.getClipTransform(f.project, f.seqId, f.track, f.clip, back));
    NC_CHECK(!back.enabled);
    NC_CHECK(back.scale == 1.0);
}

// 幂等：参数不变时不应产生撤销步（面板拖动会连发多次 apply）
NC_TEST(SetClipTransform, IdempotentDoesNotPushUndo) {
    Fx f;
    ClipTransform t;
    t.enabled = true;
    t.scale = 0.6;
    NC_CHECK(f.editor.setClipTransform(f.seqId, f.track, f.clip, t).isOk());
    const std::uint64_t rev1 = f.editor.editRevision();
    NC_CHECK(f.editor.setClipTransform(f.seqId, f.track, f.clip, t).isOk());
    NC_CHECK_EQ(f.editor.editRevision(), rev1);   // 未变化 → revision 不动
}

NC_TEST(SetClipTransform, LockedTrackRejected) {
    Fx f;
    NC_CHECK(f.editor.setTrackLocked(f.seqId, f.track, true).isOk());
    ClipTransform t;
    t.enabled = true;
    const Result r = f.editor.setClipTransform(f.seqId, f.track, f.clip, t);
    NC_CHECK(!r.isOk());
}

// =============================================================================
// 矩阵计算
// =============================================================================

NC_TEST(ClipTransformMatrix, DefaultReturnsIdentity) {
    const Affine2D m = clipTransformMatrix(1.0, 0.0, 0.0, 0.0, 4, 64, 48);
    NC_CHECK(m.isIdentity());
}

NC_TEST(ClipTransformMatrix, ScaleShrinksAroundCenter) {
    // 缩放 0.5：画布中心保持不动，四角向中心收缩
    const Affine2D m = clipTransformMatrix(0.5, 0.0, 0.0, 0.0, 4, 64, 48);
    // 中心 (32,24) 映射后仍应是 (32,24)
    const double cx = m.m[0] * 32 + m.m[1] * 24 + m.m[2];
    const double cy = m.m[3] * 32 + m.m[4] * 24 + m.m[5];
    NC_CHECK(std::abs(cx - 32.0) < 1e-6);
    NC_CHECK(std::abs(cy - 24.0) < 1e-6);
    // 不同值必须产生不同矩阵（防「参数没接上」）
    const Affine2D m2 = clipTransformMatrix(0.8, 0.0, 0.0, 0.0, 4, 64, 48);
    NC_CHECK(std::abs(m.m[0] - m2.m[0]) > 1e-6);
}

NC_TEST(ClipTransformMatrix, TranslationMovesByNormalizedUnit) {
    // posX=1 应右移「一个画布宽」= 64px
    const Affine2D m = clipTransformMatrix(1.0, 1.0, 0.0, 0.0, 4, 64, 48);
    NC_CHECK(std::abs(m.m[2] - 64.0) < 1e-6);
}

NC_TEST(ClipTransformMatrix, RotationChangesMatrix) {
    const Affine2D a = clipTransformMatrix(1.0, 0.0, 0.0, 90.0, 4, 64, 48);
    const Affine2D b = clipTransformMatrix(1.0, 0.0, 0.0, 0.0, 4, 64, 48);
    // 旋转 90° 后 m[0]（cos）应接近 0，与单位阵明显不同
    NC_CHECK(std::abs(a.m[0] - 0.0) < 1e-6);
    NC_CHECK(std::abs(b.m[0] - 1.0) < 1e-6);
}

// =============================================================================
// 渲染真的消费了 transform（G3 的核心断言）
// =============================================================================

NC_TEST(ClipTransformRender, DisabledIsByteIdentical) {
    Fx f;
    GrayFactory factory;
    PreviewRenderer r1(f.project, factory);
    const PreviewFrame before = r1.renderAt(Rational::make(1));

    // 设成「enabled=false 但参数非默认」—— 必须零影响
    ClipTransform t;
    t.enabled = false;
    t.scale = 0.5;
    t.posX = 0.3;
    NC_CHECK(f.editor.setClipTransform(f.seqId, f.track, f.clip, t).isOk());
    PreviewRenderer r2(f.project, factory);
    const PreviewFrame after = r2.renderAt(Rational::make(1));

    NC_CHECK_EQ(before.rgba.size(), after.rgba.size());
    NC_CHECK(std::memcmp(before.rgba.data(), after.rgba.data(), before.rgba.size()) == 0);
}

NC_TEST(ClipTransformRender, EnabledScaleChangesPixels) {
    Fx f;
    GrayFactory factory;
    PreviewRenderer r1(f.project, factory);
    const PreviewFrame before = r1.renderAt(Rational::make(1));

    ClipTransform t;
    t.enabled = true;
    t.scale = 0.5;      // 缩小 → 四角露出背景色
    NC_CHECK(f.editor.setClipTransform(f.seqId, f.track, f.clip, t).isOk());
    PreviewRenderer r2(f.project, factory);
    const PreviewFrame after = r2.renderAt(Rational::make(1));

    NC_CHECK(before.rgba.size() == after.rgba.size());
    // 画面必须真的变了
    NC_CHECK(std::memcmp(before.rgba.data(), after.rgba.data(), before.rgba.size()) != 0);
    // 左上角（原本是素材灰 60）现在应接近背景色（默认黑）
    NC_CHECK(after.rgba[0] < 40);
}

NC_TEST(ClipTransformRender, KeyframeOverridesStaticValue) {
    Fx f;
    ClipTransform t;
    t.enabled = true;
    t.scale = 0.9;      // 静态值：几乎不缩
    NC_CHECK(f.editor.setClipTransform(f.seqId, f.track, f.clip, t).isOk());

    // 打一个 scale=0.3 的关键帧 → 关键帧应覆盖静态值
    Clip* c = f.clipPtr();
    NC_CHECK(c != nullptr);
    Keyframe kf;
    kf.time = Time{};
    kf.value = 0.3;
    kf.easing = Easing::linear();
    NC_CHECK(c->animation.curve(ClipProperty::Scale).insert(kf).isOk());

    GrayFactory factory;
    PreviewRenderer r1(f.project, factory);
    const PreviewFrame frame = r1.renderAt(Rational::make(1));
    // 0.3 缩放后四角大面积露背景
    NC_CHECK(frame.rgba[0] < 40);
}

// =============================================================================
// 序列化往返
// =============================================================================

NC_TEST(ClipTransformSerialization, RoundTripKeepsValues) {
    Fx f;
    ClipTransform t;
    t.enabled = true;
    t.scale = 0.6;
    t.posX = 0.15;
    t.posY = -0.1;
    t.rotation = 5.0;
    t.anchor = 2;
    NC_CHECK(f.editor.setClipTransform(f.seqId, f.track, f.clip, t).isOk());

    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(!text.empty());
    // enabled 时应写出 transform 块
    NC_CHECK(text.find("\"transform\"") != std::string::npos);

    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    ClipTransform got;
    const Track* tr = loaded.mainSequence()->tracks.empty()
                          ? nullptr
                          : &loaded.mainSequence()->tracks.front();
    NC_CHECK(tr != nullptr && !tr->clips.empty());
    got = tr->clips.front().transform;
    NC_CHECK(got.enabled);
    NC_CHECK(std::abs(got.scale - 0.6) < 1e-6);
    NC_CHECK(std::abs(got.posX - 0.15) < 1e-6);
    NC_CHECK(std::abs(got.posY + 0.1) < 1e-6);
    NC_CHECK(std::abs(got.rotation - 5.0) < 1e-6);
    NC_CHECK_EQ(got.anchor, 2);
}

NC_TEST(ClipTransformSerialization, DisabledIsNotWritten) {
    Fx f;
    // 默认（未启用）不应写 transform 块 —— 保持老工程 diff 稳定
    const std::string text = saveProjectToNclip(f.project);
    NC_CHECK(text.find("\"transform\"") == std::string::npos);
}
