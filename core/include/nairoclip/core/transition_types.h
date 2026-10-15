#pragma once

#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <string>

namespace nairoclip {
namespace core {

/// 转场类型（文档 Part 4-B 第 6.1 节「分类 Tab + 预设网格」）。
/// 分类与预设（逐字）：
///   基础：叠化、闪白、闪黑、硬切
///   叠化：交叉叠化、色彩叠化、模糊叠化
///   运镜：推近、拉远、旋转、甩镜、缩放冲击
///   扭曲：波纹、液化、鱼眼、故障扭曲
///   故障：RGB分离、信号干扰、数字噪点
///   光效：光晕扫过、镜头光斑、曝光闪烁
///   几何：圆形擦除、百叶窗、马赛克分割、像素化
///   粒子：粒子消散、烟雾过渡、光点聚散
///   复古：胶片闪烁、老电影划痕转场
///
/// 值分配约定（**仅追加，不重排**——保证 .nclip 向后兼容）：
///   0..7 = 既有 8 种（交叉溶解/淡入黑场/从黑场淡入/闪白/左向擦除/
///          右向擦除/上滑/缩放）；
///   8..31 = 文档新增预设（追加区）。
enum class TransitionType {
    // ---- 既有 8 种（0..7；值不可变更）----
    CrossDissolve = 0,  ///< 交叉溶解（基础「叠化」）
    FadeToBlack = 1,    ///< 淡入黑场（基础「闪黑」）
    FadeFromBlack = 2,  ///< 从黑场淡入（叠化变体）
    FlashWhite = 3,     ///< 闪白（基础）
    WipeLeft = 4,       ///< 左向擦除（几何类方向变体）
    WipeRight = 5,      ///< 右向擦除（几何类方向变体）
    SlideUp = 6,        ///< 上滑（运镜类方向变体）
    Zoom = 7,           ///< 缩放（运镜「推近」）
    // ---- 新增（8..31；文档 Part 4-B 6.1 预设清单）----
    Cut = 8,               ///< 硬切（基础；无过渡语义，占位）
    ColorDissolve = 9,     ///< 色彩叠化（叠化）
    BlurDissolve = 10,     ///< 模糊叠化（叠化）
    ZoomOut = 11,          ///< 拉远（运镜）
    Rotate = 12,           ///< 旋转（运镜）
    WhipPan = 13,          ///< 甩镜（运镜）
    ZoomPunch = 14,        ///< 缩放冲击（运镜）
    Ripple = 15,           ///< 波纹（扭曲）
    Liquify = 16,          ///< 液化（扭曲）
    Fisheye = 17,          ///< 鱼眼（扭曲）
    GlitchWarp = 18,       ///< 故障扭曲（扭曲）
    RgbSplit = 19,         ///< RGB分离（故障）
    SignalNoise = 20,      ///< 信号干扰（故障）
    DigitalNoise = 21,     ///< 数字噪点（故障）
    LightSweep = 22,       ///< 光晕扫过（光效）
    LensFlare = 23,        ///< 镜头光斑（光效）
    ExposureFlicker = 24,  ///< 曝光闪烁（光效）
    CircleWipe = 25,       ///< 圆形擦除（几何）
    Blinds = 26,           ///< 百叶窗（几何）
    MosaicSplit = 27,      ///< 马赛克分割（几何）
    Pixelate = 28,         ///< 像素化（几何）
    ParticleDissolve = 29, ///< 粒子消散（粒子）
    SmokeTransition = 30,  ///< 烟雾过渡（粒子）
    SparkleGather = 31,    ///< 光点聚散（粒子）
    FilmFlicker = 32,      ///< 胶片闪烁（复古）
    OldFilmScratch = 33,   ///< 老电影划痕转场（复古）
};

/// 转场参数（纯数据类型，不依赖 Track/Clip，供 Track 存储与校验共用）。
struct Transition {
    TransitionId id;
    TransitionType type = TransitionType::CrossDissolve;
    Time duration = Rational::make(1, 2);  // 转场时长，必须 > 0
    /// 所属轨道内的片段对：从前片段 A 到后片段 B。
    ClipId fromClip;
    ClipId toClip;
    std::string name;  // 展示名（如「交叉溶解」）
};

}  // namespace core
}  // namespace nairoclip
