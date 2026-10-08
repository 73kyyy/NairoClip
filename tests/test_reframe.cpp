#include "test_framework.h"
#include "nairoclip/core/reframe.h"

#include <cmath>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 构造图像：指定区域为高细节/高对比的「主体」，其余为平坦背景。
/// 主体是位于 (cx, cy)（归一化）的方形棋盘纹理（高梯度）。
std::vector<std::uint8_t> makeImageWithSubject(int w, int h, double subjectX, double subjectY,
                                               double subjectSize = 0.15) {
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4, 0);
    // 背景：平坦灰色
    for (std::size_t i = 0; i + 3 < img.size(); i += 4) {
        img[i] = 128;
        img[i + 1] = 128;
        img[i + 2] = 128;
        img[i + 3] = 255;
    }
    const int cx = static_cast<int>(subjectX * w);
    const int cy = static_cast<int>(subjectY * h);
    const int half = static_cast<int>(subjectSize * std::min(w, h) / 2.0);
    for (int y = cy - half; y <= cy + half; ++y) {
        for (int x = cx - half; x <= cx + half; ++x) {
            if (x < 0 || y < 0 || x >= w || y >= h) continue;
            // 棋盘（亮暗交替）→ 高梯度
            const bool bright = ((x / 2) + (y / 2)) % 2 == 0;
            const std::size_t idx = (static_cast<std::size_t>(y) * w + x) * 4;
            const std::uint8_t value = bright ? 240 : 20;
            img[idx] = value;
            img[idx + 1] = value;
            img[idx + 2] = value;
        }
    }
    return img;
}

/// 平坦纯色图（无主体）。
std::vector<std::uint8_t> makeFlatImage(int w, int h, std::uint8_t value) {
    std::vector<std::uint8_t> img(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t i = 0; i + 3 < img.size(); i += 4) {
        img[i] = value;
        img[i + 1] = value;
        img[i + 2] = value;
        img[i + 3] = 255;
    }
    return img;
}

/// 按序列生成显著性（用于轨迹测试）。
std::vector<FrameSaliency> makeSaliencyRun(const std::vector<std::pair<double, double>>& centers,
                                           double timestampStep = 1.0 / 30.0) {
    std::vector<FrameSaliency> result;
    for (std::size_t i = 0; i < centers.size(); ++i) {
        FrameSaliency s;
        s.timestamp = Rational::make(static_cast<std::int64_t>(i * timestampStep * 1000), 1000);
        s.centerX = centers[i].first;
        s.centerY = centers[i].second;
        s.energy = 0.8;
        s.hasSubject = true;
        result.push_back(s);
    }
    return result;
}

}  // namespace

// =============================================================================
// 显著性估计（真实像素分析）
// =============================================================================

NC_TEST(Reframe, SaliencyFindsSubjectPosition) {
    // 主体在左侧 (0.25, 0.5)
    const int w = 160;
    const int h = 120;
    const auto img = makeImageWithSubject(w, h, 0.25, 0.5);
    ReframeConfig config;
    config.minEnergy = 0.05;
    FrameSaliency out;
    NC_CHECK(computeFrameSaliency(img.data(), w, h, w * 4, nullptr, 0, config,
                                  Rational::make(0), out));
    NC_CHECK(out.hasSubject);
    NC_CHECK(out.energy > 0.05);
    // 重心应接近主体位置
    NC_CHECK(std::fabs(out.centerX - 0.25) < 0.12);
    NC_CHECK(std::fabs(out.centerY - 0.5) < 0.15);
}

NC_TEST(Reframe, SaliencyFindsRightSubject) {
    const int w = 160;
    const int h = 120;
    const auto img = makeImageWithSubject(w, h, 0.75, 0.4);
    ReframeConfig config;
    config.minEnergy = 0.05;
    FrameSaliency out;
    NC_CHECK(computeFrameSaliency(img.data(), w, h, w * 4, nullptr, 0, config,
                                  Rational::make(0), out));
    NC_CHECK(out.hasSubject);
    NC_CHECK(std::fabs(out.centerX - 0.75) < 0.12);
    NC_CHECK(std::fabs(out.centerY - 0.4) < 0.15);
}

NC_TEST(Reframe, FlatImageHasLowEnergy) {
    const int w = 160;
    const int h = 120;
    const auto img = makeFlatImage(w, h, 128);
    ReframeConfig config;
    FrameSaliency out;
    NC_CHECK(computeFrameSaliency(img.data(), w, h, w * 4, nullptr, 0, config,
                                  Rational::make(0), out));
    // 平坦图：低能量 → 无主体
    NC_CHECK(!out.hasSubject);
}

NC_TEST(Reframe, SaliencyRejectsBadArgs) {
    ReframeConfig config;
    FrameSaliency out;
    std::vector<std::uint8_t> tiny(16, 0);
    NC_CHECK(!computeFrameSaliency(nullptr, 16, 16, 64, nullptr, 0, config, Rational::make(0),
                                   out));
    NC_CHECK(!computeFrameSaliency(tiny.data(), 0, 16, 64, nullptr, 0, config, Rational::make(0),
                                   out));
    NC_CHECK(!computeFrameSaliency(tiny.data(), 16, -1, 64, nullptr, 0, config, Rational::make(0),
                                   out));
    // stride 过小
    NC_CHECK(!computeFrameSaliency(tiny.data(), 16, 16, 16, nullptr, 0, config, Rational::make(0),
                                   out));
}

NC_TEST(Reframe, MotionContributesToSaliency) {
    // 两帧之间主体移动：带前一帧时能量更高（运动项）
    const int w = 120;
    const int h = 90;
    const auto frame1 = makeImageWithSubject(w, h, 0.3, 0.5, 0.2);
    const auto frame2 = makeImageWithSubject(w, h, 0.4, 0.5, 0.2);
    ReframeConfig config;
    config.minEnergy = 0.0;
    FrameSaliency withoutPrev;
    FrameSaliency withPrev;
    computeFrameSaliency(frame2.data(), w, h, w * 4, nullptr, 0, config, Rational::make(0),
                         withoutPrev);
    computeFrameSaliency(frame2.data(), w, h, w * 4, frame1.data(), w * 4, config,
                         Rational::make(0), withPrev);
    NC_CHECK(withPrev.energy >= withoutPrev.energy);
}

// =============================================================================
// 轨迹规划（平滑 + 速度限制 + 边界钳制）
// =============================================================================

NC_TEST(Reframe, PlanProducesKeyframesPerFrame) {
    const auto saliency = makeSaliencyRun({{0.5, 0.5}, {0.5, 0.5}, {0.5, 0.5}});
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1080, 1920, ReframeConfig{});
    NC_CHECK(track.valid());
    NC_CHECK_EQ(track.keyframes.size(), std::size_t(3));
    NC_CHECK_EQ(track.srcWidth, 1920);
    NC_CHECK_EQ(track.srcHeight, 1080);
}

NC_TEST(Reframe, PlanClampsWindowInsideSource) {
    // 主体在最左：窗口不应越界（中心 >= 半宽）
    const auto saliency = makeSaliencyRun({{0.0, 0.5}, {0.0, 0.5}, {0.0, 0.5}, {0.0, 0.5}});
    ReframeConfig config;
    config.maxPanSpeedPerSecond = 100.0;  // 不限制，验证钳制
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1080, 1920, config);
    NC_CHECK(track.valid());
    // 9:16 目标放入 16:9 源：以源高为限 → 窗口高 = 1080（全高），
    // 窗口宽 = 1080 × (1080/1920) = 607.5 → 归一化半宽 = 607.5/2/1920 = 0.1582
    const double halfW = 607.5 / 2.0 / 1920.0;
    for (const auto& kf : track.keyframes) {
        NC_CHECK(kf.centerX >= halfW - 1e-6);
        NC_CHECK(kf.centerX <= 1.0 - halfW + 1e-6);
    }
    NC_CHECK(track.keyframes.front().centerX < 0.35);  // 应靠左
}

NC_TEST(Reframe, PlanSmoothsJitter) {
    // 交替抖动的中心 → 平滑后应显著收敛
    std::vector<std::pair<double, double>> centers;
    for (int i = 0; i < 30; ++i) {
        centers.emplace_back(i % 2 == 0 ? 0.3 : 0.7, 0.5);
    }
    const auto saliency = makeSaliencyRun(centers);
    ReframeConfig config;
    config.trackingSensitivity = 0.2;       // 低灵敏度 → 强平滑
    config.maxPanSpeedPerSecond = 100.0;    // 关闭速度限制以单独验证平滑
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1920, 1080, config);
    NC_CHECK(track.valid());
    // 平滑后相邻帧位移应远小于原始抖动（0.4）
    double maxJump = 0.0;
    for (std::size_t i = 1; i < track.keyframes.size(); ++i) {
        maxJump = std::max(maxJump,
                           std::fabs(track.keyframes[i].centerX - track.keyframes[i - 1].centerX));
    }
    NC_CHECK(maxJump < 0.15);
}

NC_TEST(Reframe, PlanLimitsPanSpeed) {
    // 主体突然从最左跳到最右 → 速度限制让窗口平滑移动
    std::vector<std::pair<double, double>> centers;
    for (int i = 0; i < 10; ++i) centers.emplace_back(0.15, 0.5);
    for (int i = 0; i < 10; ++i) centers.emplace_back(0.85, 0.5);
    const auto saliency = makeSaliencyRun(centers);
    ReframeConfig config;
    config.trackingSensitivity = 1.0;    // 高灵敏度 → 小平滑窗口
    config.maxPanSpeedPerSecond = 0.5;   // 0.5 画布宽/秒 → 每帧 0.5/30 ≈ 0.0167
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1920, 1080, config);
    NC_CHECK(track.valid());
    // 每帧位移不应超过 maxStep（加少量容差）
    const double maxStep = 0.5 / 30.0 + 1e-6;
    for (std::size_t i = 1; i < track.keyframes.size(); ++i) {
        const double dx = std::fabs(track.keyframes[i].centerX - track.keyframes[i - 1].centerX);
        NC_CHECK(dx <= maxStep + 0.02);
    }
}

NC_TEST(Reframe, PlanNoSubjectFallsBackToCenter) {
    std::vector<FrameSaliency> saliency;
    for (int i = 0; i < 5; ++i) {
        FrameSaliency s;
        s.timestamp = Rational::make(i, 30);
        s.centerX = 0.9;  // 无主体时这些值应被忽略
        s.centerY = 0.9;
        s.energy = 0.01;
        s.hasSubject = false;
        saliency.push_back(s);
    }
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1080, 1920, ReframeConfig{});
    NC_CHECK(track.valid());
    NC_CHECK(!track.note.empty());  // 应给出「未检测到主体」诊断
    // 退化为居中（允许边界钳制差异）
    NC_CHECK(std::fabs(track.keyframes.front().centerX - 0.5) < 0.1);
}

NC_TEST(Reframe, PlanRejectsEmptyOrBadSizes) {
    const ReframeTrack empty = planReframe({}, 1920, 1080, 1080, 1920, ReframeConfig{});
    NC_CHECK(!empty.valid());
    const auto saliency = makeSaliencyRun({{0.5, 0.5}});
    NC_CHECK(!planReframe(saliency, 0, 1080, 1080, 1920, ReframeConfig{}).valid());
    NC_CHECK(!planReframe(saliency, 1920, 1080, 0, 1920, ReframeConfig{}).valid());
}

// =============================================================================
// 轨迹查询与裁剪窗口
// =============================================================================

NC_TEST(Reframe, AtInterpolatesBetweenKeyframes) {
    std::vector<FrameSaliency> saliency;
    FrameSaliency a;
    a.timestamp = Rational::make(0);
    a.centerX = 0.3;
    a.centerY = 0.5;
    a.energy = 1.0;
    a.hasSubject = true;
    FrameSaliency b;
    b.timestamp = Rational::make(1);
    b.centerX = 0.7;
    b.centerY = 0.5;
    b.energy = 1.0;
    b.hasSubject = true;
    saliency.push_back(a);
    saliency.push_back(b);
    ReframeConfig config;
    config.maxPanSpeedPerSecond = 100.0;
    config.trackingSensitivity = 1.0;  // 最小平滑
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1920, 1080, config);
    NC_CHECK(track.valid());
    ReframeKeyframe mid;
    NC_CHECK(reframeAt(track, Rational::make(1, 2), mid));
    // 中点应在两端之间
    NC_CHECK(mid.centerX > track.keyframes.front().centerX - 1e-9);
    NC_CHECK(mid.centerX < track.keyframes.back().centerX + 1e-9);
}

NC_TEST(Reframe, AtClampsOutsideRange) {
    const auto saliency = makeSaliencyRun({{0.4, 0.5}, {0.6, 0.5}});
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1920, 1080, ReframeConfig{});
    ReframeKeyframe before;
    NC_CHECK(reframeAt(track, Rational::make(-5), before));
    NC_CHECK(before.centerX == track.keyframes.front().centerX);
    ReframeKeyframe after;
    NC_CHECK(reframeAt(track, Rational::make(100), after));
    NC_CHECK(after.centerX == track.keyframes.back().centerX);
}

NC_TEST(Reframe, AtRejectsEmptyTrack) {
    const ReframeTrack empty;
    ReframeKeyframe out;
    NC_CHECK(!reframeAt(empty, Rational::make(0), out));
}

NC_TEST(Reframe, CropWindowFitsInsideSource) {
    const auto saliency = makeSaliencyRun({{0.1, 0.1}, {0.9, 0.9}});
    ReframeConfig config;
    config.maxPanSpeedPerSecond = 100.0;
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1080, 1920, config);
    NC_CHECK(track.valid());
    // 9:16 目标 → 窗口 607x1080（源内最大）
    CropWindow window;
    NC_CHECK(cropWindowAt(track, Rational::make(0), 1080, 1920, window));
    NC_CHECK_EQ(window.height, 1080);
    NC_CHECK(window.width <= 1920);
    NC_CHECK(window.x >= 0);
    NC_CHECK(window.y >= 0);
    NC_CHECK(window.x + window.width <= 1920);
    NC_CHECK(window.y + window.height <= 1080);
}

NC_TEST(Reframe, CropWindowRejectsBadArgs) {
    const auto saliency = makeSaliencyRun({{0.5, 0.5}});
    const ReframeTrack track = planReframe(saliency, 1920, 1080, 1080, 1920, ReframeConfig{});
    CropWindow window;
    NC_CHECK(!cropWindowAt(track, Rational::make(0), 0, 1920, window));
    const ReframeTrack empty;
    NC_CHECK(!cropWindowAt(empty, Rational::make(0), 1080, 1920, window));
}

// =============================================================================
// 真实渲染（裁剪 + 缩放）
// =============================================================================

NC_TEST(Reframe, RenderCropsToTargetSize) {
    const int sw = 160;
    const int sh = 120;
    // 主体在左侧
    const auto img = makeImageWithSubject(sw, sh, 0.25, 0.5, 0.3);
    FrameSaliency s1;
    s1.timestamp = Rational::make(0);
    s1.centerX = 0.25;
    s1.centerY = 0.5;
    s1.energy = 1.0;
    s1.hasSubject = true;
    const auto saliency = std::vector<FrameSaliency>{s1};
    ReframeConfig config;
    config.maxPanSpeedPerSecond = 100.0;
    const ReframeTrack track = planReframe(saliency, sw, sh, sh, sh, config);  // 1:1 目标
    NC_CHECK(track.valid());
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(sh) * sh * 4, 0);
    NC_CHECK(reframeRenderFrameAt(track, Rational::make(0), img.data(), sw, sh, sw * 4,
                                  dst.data(), sh, sh));
    // 输出尺寸正确
    NC_CHECK_EQ(dst.size(), static_cast<std::size_t>(sh) * sh * 4);
    // 输出应有内容（非全黑：主体区域被裁入）
    bool anyContent = false;
    for (std::size_t i = 0; i < dst.size() && !anyContent; i += 4) {
        if (dst[i] > 30) anyContent = true;
    }
    NC_CHECK(anyContent);
}

NC_TEST(Reframe, RenderRejectsMismatchedSource) {
    const auto saliency = makeSaliencyRun({{0.5, 0.5}});
    const ReframeTrack track = planReframe(saliency, 160, 120, 120, 120, ReframeConfig{});
    std::vector<std::uint8_t> src(static_cast<std::size_t>(160) * 120 * 4, 128);
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(120) * 120 * 4, 0);
    // 源尺寸与轨迹不符 → 拒绝
    NC_CHECK(!reframeRenderFrameAt(track, Rational::make(0), src.data(), 100, 120, 100 * 4,
                                   dst.data(), 120, 120));
    NC_CHECK(!reframeRenderFrameAt(track, Rational::make(0), nullptr, 160, 120, 160 * 4,
                                   dst.data(), 120, 120));
    NC_CHECK(!reframeRenderFrameAt(track, Rational::make(0), src.data(), 160, 120, 160 * 4,
                                   nullptr, 120, 120));
}

// =============================================================================
// 方向判定与配置
// =============================================================================

NC_TEST(Reframe, DirectionLandscapeToPortrait) {
    NC_CHECK(reframeDirection(1920, 1080, 1080, 1920) == ReframeDirection::LandscapeToPortrait);
    NC_CHECK(reframeDirection(1920, 1080, 1080, 1920) == ReframeDirection::LandscapeToPortrait);
    const ReframeDirection d = reframeDirection(1920, 1080, 1080, 1920);
    NC_CHECK_EQ(std::string(reframeDirectionName(d)), std::string("横转竖"));
}

NC_TEST(Reframe, DirectionPortraitToLandscape) {
    const ReframeDirection d = reframeDirection(1080, 1920, 1920, 1080);
    NC_CHECK(d == ReframeDirection::PortraitToLandscape);
    NC_CHECK_EQ(std::string(reframeDirectionName(d)), std::string("竖转横"));
}

NC_TEST(Reframe, DirectionSameOrientation) {
    NC_CHECK(reframeDirection(1920, 1080, 1920, 1080) == ReframeDirection::SameOrientation);
    NC_CHECK(reframeDirection(1920, 1080, 1280, 720) == ReframeDirection::SameOrientation);
    NC_CHECK(reframeDirection(0, 0, 1920, 1080) == ReframeDirection::SameOrientation);
}

NC_TEST(Reframe, ConfigSensitivityMapping) {
    ReframeConfig low;
    low.trackingSensitivity = 0.0;
    ReframeConfig high;
    high.trackingSensitivity = 1.0;
    // 低灵敏度 → 长窗口（更平滑）；高灵敏度 → 短窗口（更跟手）
    NC_CHECK(low.smoothingSeconds() > high.smoothingSeconds());
    NC_CHECK(low.smoothingSeconds() <= low.maxSmoothingSeconds + 1e-9);
    NC_CHECK(high.smoothingSeconds() >= high.minSmoothingSeconds - 1e-9);
    // 非法配置
    ReframeConfig bad;
    bad.trackingSensitivity = 1.5;
    NC_CHECK(!bad.valid());
}

// =============================================================================
// 序列化与导出集成（智能重构图持久化 + 真实渲染路径）
// =============================================================================

#include "nairoclip/core/editor.h"
#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/serialization.h"

namespace {

/// 生成 16:9 源（左半红/右半蓝），验证重构图裁剪方向。
std::vector<std::uint8_t> wideRedBlue(int w, int h) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4,
                                     0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t idx =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                 static_cast<std::size_t>(x)) * 4;
            if (x < w / 2) {
                pixels[idx] = 255;  // 左红
            } else {
                pixels[idx + 2] = 255;  // 右蓝
            }
            pixels[idx + 3] = 255;
        }
    }
    return pixels;
}

/// Mock 读取器：120x48 源（宽幅），按帧返回红蓝图。
class WideReader : public MediaReader {
public:
    WideReader() {
        probe_.type = MediaType::Video;
        probe_.frameRate = fps30();
        probe_.width = 120;
        probe_.height = 48;
        probe_.duration = Rational::make(10);
        image_ = wideRedBlue(120, 48);
    }
    Result open(const std::string& p) override {
        if (p.empty()) return Result::fail(ErrorCode::InvalidArgument, "路径为空");
        open_ = true;
        return Result::ok();
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    const MediaProbeResult& probe() const override { return probe_; }
    Result seek(const Time&) override { return open_ ? Result::ok()
                                                     : Result::fail(ErrorCode::InvalidArgument,
                                                                    "未打开"); }
    bool readVideoFrame(VideoFrame& o) override {
        if (!open_) return false;
        o.timestamp = Rational::make(0);
        o.width = 120;
        o.height = 48;
        o.stride = 120 * 4;
        o.pixels = image_;
        return true;
    }
    bool readAudioBlock(AudioBlock&) override { return false; }
private:
    MediaProbeResult probe_;
    bool open_ = false;
    std::vector<std::uint8_t> image_;
};

class WideFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<WideReader>(); }
};

}  // namespace

NC_TEST(ReframeSer, RoundtripPersistsTrack) {
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 48;
    seq.height = 120;  // 9:16 竖屏目标
    // 构造轨迹：主体在左侧
    const auto saliency = makeSaliencyRun({{0.25, 0.5}, {0.25, 0.5}, {0.3, 0.5}});
    seq.reframe = planReframe(saliency, 120, 48, 48, 120, ReframeConfig{});
    NC_CHECK(seq.reframe.valid());
    project.sequences.push_back(seq);

    const std::string text = saveProjectToNclip(project);
    Project loaded;
    const NclipLoadResult result = loadProjectFromNclip(text, loaded);
    NC_CHECK(result.result.isOk());
    const ReframeTrack& track = loaded.sequences[0].reframe;
    NC_CHECK(track.valid());
    NC_CHECK_EQ(track.srcWidth, 120);
    NC_CHECK_EQ(track.srcHeight, 48);
    NC_CHECK_EQ(track.keyframes.size(), std::size_t(3));
    NC_CHECK(std::fabs(track.keyframes[1].centerX - seq.reframe.keyframes[1].centerX) < 1e-9);
    NC_CHECK(std::fabs(track.keyframes[1].centerY - seq.reframe.keyframes[1].centerY) < 1e-9);
}

NC_TEST(ReframeSer, DisabledOmittedFromFile) {
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    project.sequences.push_back(seq);
    const std::string text = saveProjectToNclip(project);
    NC_CHECK(text.find("reframe") == std::string::npos);
    Project loaded;
    NC_CHECK(loadProjectFromNclip(text, loaded).result.isOk());
    NC_CHECK(!loaded.sequences[0].reframe.valid());
}

NC_TEST(ReframeSer, ExportPlanCarriesReframe) {
    Project project;
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.width = 48;
    seq.height = 120;
    const auto saliency = makeSaliencyRun({{0.3, 0.5}});
    seq.reframe = planReframe(saliency, 120, 48, 48, 120, ReframeConfig{});
    project.sequences.push_back(seq);
    const ExportPlan plan = compileExportPlan(project, project.sequences[0]);
    NC_CHECK(plan.reframe.valid());
    NC_CHECK_EQ(plan.reframe.srcWidth, 120);
}

NC_TEST(ReframeSer, ExportExecutorAppliesReframeCrop) {
    // 120x48 源（左红右蓝）→ 48x120 画布（9:16）：
    // 重构图应裁出源左侧（主体在 x=0.25）并放大，输出以红色为主。
    Project project;
    TimelineEditor editor(project);
    Sequence seq;
    seq.id = SequenceId{project.ids.next()};
    seq.frameRate = fps30();
    seq.width = 48;
    seq.height = 120;
    project.sequences.push_back(seq);
    const SequenceId seqId = project.sequences.front().id;
    MediaAsset asset;
    asset.id = AssetId{project.ids.next()};
    asset.path = "/video/wide.mp4";
    asset.type = MediaType::Video;
    asset.duration = Rational::make(10);
    project.assets.push_back(asset);
    const AssetId assetId = project.assets.front().id;
    TrackId trackId;
    NC_CHECK(editor.addTrack(seqId, TrackType::Video, "V1", &trackId).isOk());
    Clip clip;
    clip.asset = assetId;
    clip.start = Rational::make(0);
    clip.duration = Rational::make(1);
    NC_CHECK(editor.insertClip(seqId, trackId, clip).isOk());
    // 设置重构图轨迹（主体靠左）
    const auto saliency = makeSaliencyRun({{0.25, 0.5}, {0.25, 0.5}});
    project.sequences.front().reframe = planReframe(saliency, 120, 48, 48, 120, ReframeConfig{});

    const ExportPlan plan = compileExportPlan(project, project.sequences.front());
    NC_CHECK(plan.reframe.valid());
    WideFactory factory;
    int checked = 0;
    executeExport(
        plan, factory,
        [&](const OutputVideoFrame& frame) {
            if (checked == 0) {
                NC_CHECK_EQ(frame.width, 48);
                NC_CHECK_EQ(frame.height, 120);
                // 裁剪源左侧区域 → 中心像素应偏红
                const std::size_t idx =
                    (static_cast<std::size_t>(60) * 48 + 24) * 4;
                NC_CHECK(frame.rgba[idx] > 100);  // 红分量
                NC_CHECK(frame.rgba[idx + 2] < 100);  // 蓝分量低
            }
            ++checked;
        },
        nullptr, nullptr, ExportExecutorOptions{}, &project);
    NC_CHECK(checked > 0);
}
