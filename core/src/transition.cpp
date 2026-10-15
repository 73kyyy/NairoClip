#include "nairoclip/core/transition.h"

namespace nairoclip {
namespace core {

const char* transitionName(TransitionType type) {
    switch (type) {
        case TransitionType::CrossDissolve:
            return "交叉溶解";
        case TransitionType::FadeToBlack:
            return "淡入黑场";
        case TransitionType::FadeFromBlack:
            return "从黑场淡入";
        case TransitionType::FlashWhite:
            return "闪白";
        case TransitionType::WipeLeft:
            return "左向擦除";
        case TransitionType::WipeRight:
            return "右向擦除";
        case TransitionType::SlideUp:
            return "上滑";
        case TransitionType::Zoom:
            return "缩放";
        // ---- 文档 Part 4-B 6.1 新增预设（名称逐字）----
        case TransitionType::Cut:
            return "硬切";
        case TransitionType::ColorDissolve:
            return "色彩叠化";
        case TransitionType::BlurDissolve:
            return "模糊叠化";
        case TransitionType::ZoomOut:
            return "拉远";
        case TransitionType::Rotate:
            return "旋转";
        case TransitionType::WhipPan:
            return "甩镜";
        case TransitionType::ZoomPunch:
            return "缩放冲击";
        case TransitionType::Ripple:
            return "波纹";
        case TransitionType::Liquify:
            return "液化";
        case TransitionType::Fisheye:
            return "鱼眼";
        case TransitionType::GlitchWarp:
            return "故障扭曲";
        case TransitionType::RgbSplit:
            return "RGB分离";
        case TransitionType::SignalNoise:
            return "信号干扰";
        case TransitionType::DigitalNoise:
            return "数字噪点";
        case TransitionType::LightSweep:
            return "光晕扫过";
        case TransitionType::LensFlare:
            return "镜头光斑";
        case TransitionType::ExposureFlicker:
            return "曝光闪烁";
        case TransitionType::CircleWipe:
            return "圆形擦除";
        case TransitionType::Blinds:
            return "百叶窗";
        case TransitionType::MosaicSplit:
            return "马赛克分割";
        case TransitionType::Pixelate:
            return "像素化";
        case TransitionType::ParticleDissolve:
            return "粒子消散";
        case TransitionType::SmokeTransition:
            return "烟雾过渡";
        case TransitionType::SparkleGather:
            return "光点聚散";
        case TransitionType::FilmFlicker:
            return "胶片闪烁";
        case TransitionType::OldFilmScratch:
            return "老电影划痕转场";
    }
    return "未知转场";
}

/// 转场分类名（文档 6.1 九个分类 Tab；供 UI 分组展示）。
/// 返回静态字符串；未知类型返回空串。
const char* transitionCategory(TransitionType type) {
    switch (type) {
        case TransitionType::CrossDissolve:
        case TransitionType::FlashWhite:
        case TransitionType::FadeToBlack:
        case TransitionType::Cut:
            return "基础";
        case TransitionType::FadeFromBlack:
        case TransitionType::ColorDissolve:
        case TransitionType::BlurDissolve:
            return "叠化";
        case TransitionType::Zoom:
        case TransitionType::ZoomOut:
        case TransitionType::Rotate:
        case TransitionType::WhipPan:
        case TransitionType::ZoomPunch:
        case TransitionType::SlideUp:
            return "运镜";
        case TransitionType::Ripple:
        case TransitionType::Liquify:
        case TransitionType::Fisheye:
        case TransitionType::GlitchWarp:
            return "扭曲";
        case TransitionType::RgbSplit:
        case TransitionType::SignalNoise:
        case TransitionType::DigitalNoise:
            return "故障";
        case TransitionType::LightSweep:
        case TransitionType::LensFlare:
        case TransitionType::ExposureFlicker:
            return "光效";
        case TransitionType::CircleWipe:
        case TransitionType::Blinds:
        case TransitionType::MosaicSplit:
        case TransitionType::Pixelate:
        case TransitionType::WipeLeft:
        case TransitionType::WipeRight:
            return "几何";
        case TransitionType::ParticleDissolve:
        case TransitionType::SmokeTransition:
        case TransitionType::SparkleGather:
            return "粒子";
        case TransitionType::FilmFlicker:
        case TransitionType::OldFilmScratch:
            return "复古";
    }
    return "";
}

/// 方向选择器适用性（文档 6.3：仅推拉/擦除/百叶窗类转场显示方向选择器）。
bool transitionSupportsDirection(TransitionType type) {
    switch (type) {
        case TransitionType::Zoom:
        case TransitionType::ZoomOut:
        case TransitionType::WipeLeft:
        case TransitionType::WipeRight:
        case TransitionType::SlideUp:
        case TransitionType::CircleWipe:
        case TransitionType::Blinds:
        case TransitionType::MosaicSplit:
        case TransitionType::WhipPan:
        case TransitionType::LightSweep:
            return true;
        default:
            return false;
    }
}

/// 文档 6.2：时长范围 0.1s ~ 2.0s。
bool transitionDurationValid(const Time& duration) {
    const double seconds = duration.toDouble();
    return seconds >= 0.1 - 1e-9 && seconds <= 2.0 + 1e-9;
}

Result validateTransition(const Track& track, const Transition& transition,
                          const std::vector<Transition>& existing) {
    if (!transition.duration.isPositive()) {
        return Result::fail(ErrorCode::InvalidArgument, "转场时长必须大于 0");
    }
    const Clip* from = track.findClip(transition.fromClip);
    const Clip* to = track.findClip(transition.toClip);
    if (from == nullptr || to == nullptr) {
        return Result::fail(ErrorCode::NotFound, "转场引用的片段不存在");
    }
    if (!(from->start < to->start)) {
        return Result::fail(ErrorCode::InvalidArgument, "转场两端片段顺序不正确");
    }
    const Time available = to->start - from->end();
    if (available.isNegative()) {
        return Result::fail(ErrorCode::Overlap, "两端片段在时间上已重叠，无法再插入转场");
    }
    if (available.isPositive() && transition.duration > available) {
        return Result::fail(ErrorCode::OutOfRange, "转场时长超出两片段的时间间隙");
    }
    for (const auto& other : existing) {
        if (other.id == transition.id) {
            continue;
        }
        // 占用规则（专业剪辑语义：允许 A→B→C 转场链）：
        //  1) 同一对片段（任意顺序）已存在转场 → 拒绝（重复）；
        //  2) 同一片段作为两个转场的**起点** → 拒绝（一个片段只能有一个后继转场）；
        //  3) 同一片段作为两个转场的**终点** → 拒绝（一个片段只能有一个前驱转场）。
        // 注意：A→B 与 B→C 是合法的链式关系（B 是先者的终点、后者的起点），
        // 因此不能简单按「片段被任何转场引用」判占用。
        const bool samePair =
            (other.fromClip == transition.fromClip && other.toClip == transition.toClip) ||
            (other.fromClip == transition.toClip && other.toClip == transition.fromClip);
        if (samePair || other.fromClip == transition.fromClip ||
            other.toClip == transition.toClip) {
            return Result::fail(ErrorCode::InvalidArgument, "片段已被其他转场占用");
        }
    }
    return Result::ok();
}

}  // namespace core
}  // namespace nairoclip
