#pragma once

#include "nairoclip/core/animation.h"
#include "nairoclip/core/clip.h"
#include "nairoclip/core/effects.h"
#include "nairoclip/core/transition_types.h"
#include "nairoclip/core/xform.h"

#include <string>
#include <vector>

namespace nairoclip {
namespace core {

enum class TrackType {
    Video,
    Audio,
    Subtitle,
    /// 调整图层容器（B0-8 由 `Effect` 更名而来；渲染实现在 B2-6）。
    ///
    /// 数值固定为 3 —— 老工程文件与 C ABI 的 `track_type == 3` 都映射到这里，
    /// **不可改变数值**，否则历史工程会轨道错位。
    ///
    /// 调整图层容器（B0-8 由 `Effect` 更名而来）。
    ///
    /// 数值固定为 3 —— 老工程文件与 C ABI 的 `track_type == 3` 都映射到这里，
    /// **不可改变数值**，否则历史工程会轨道错位。
    ///
    /// 渲染已实现（B2-6 + §4.7）：effects / LUT / xforms / opacity /
    /// 关键帧全部可用。`validateClipPlacement` 仍拒绝 Clip 落入 ——
    /// 调整图层的参数挂在 Track 上（`Track::adjustment*`），
    /// 不需要也不应该有 Clip。
    Adjustment,

    /// 文本轨（§4.6 文本素材化）。
    ///
    /// **数值必须是 4**，追加在末尾 —— 插入中间会让历史工程的
    /// 轨道类型全部错位（3 变成 Text、4 变成 Adjustment）。
    ///
    /// 与其他轨的区别：只接受文本片段（`Clip::text.has_value()`），
    /// 不接受任何媒体素材。
    Text,
};

/// 音频自动闪避（Auto Ducking）设置——轨道级属性。
///
/// 产品对应（文档第 2/3 部分）：
///   「🎵音频 → 自动闪避 → 开关/灵敏度/压低幅度滑块」；
///   时间轴音乐轨显示「自动压低区间」、混音台高亮联动关系。
///
/// 语义：本轨作为「被压低轨」（通常是音乐/背景乐），
/// 当同一序列中存在「触发轨」（通常是主视频轨人声/旁白）的有效声音时，
/// 本轨音量自动降低 `duckAmountDb`，语音结束后平滑恢复。
struct AutoDuckSettings {
    /// 是否启用（UI 开关）。
    bool enabled = false;
    /// 触发灵敏度 0..1：越高越容易触发压低（低电平语音也可触发）。
    /// 映射到语音活动检测阈值：threshold = 0.02 + (1 - sensitivity) × 0.18。
    double sensitivity = 0.5;
    /// 压低幅度（dB，正数表示压低多少；文档要求 0..-18dB 量级）。
    double duckAmountDb = 12.0;
    /// 启动时间（秒）：检测到语音后多久开始压低（防误触发，语气词等）。
    double attackSeconds = 0.08;
    /// 恢复时间（秒）：语音结束后多久恢复原音量（避免抽吸感）。
    double releaseSeconds = 0.35;
    /// 保留静音间隙（秒）：语音间隙短于该值时保持压低（避免句中频繁恢复）。
    double holdSeconds = 0.25;

    /// 参数是否合法（供 UI 校验与命令拒绝）。
    bool valid() const {
        return sensitivity >= 0.0 && sensitivity <= 1.0 && duckAmountDb >= 0.0 &&
               duckAmountDb <= 24.0 && attackSeconds >= 0.0 && attackSeconds <= 2.0 &&
               releaseSeconds >= 0.0 && releaseSeconds <= 3.0 && holdSeconds >= 0.0 &&
               holdSeconds <= 2.0;
    }
    /// 语音活动阈值（由灵敏度映射）。
    double voiceThreshold() const {
        return 0.02 + (1.0 - sensitivity) * 0.18;
    }
};

/// 自动压低区间（供时间轴绘制「自动压低区间」与混音台高亮联动）。
struct DuckRegion {
    Time start;              // 压低区间起点
    Time end;                // 压低区间终点
    double gain = 1.0;       // 区间内的目标线性增益（已含压低幅度）
};

/// 单条轨道。不变量：clips 按 start 升序且互不重叠。
struct Track {
    TrackId id;
    TrackType type = TrackType::Video;
    std::string name;
    bool locked = false;
    bool muted = false;
    bool hidden = false;
    /// 独奏（混音台）——文档第 3 部分「每条轨道独立静音(Mute)/独奏(Solo)」。
    /// 语义：任一轨道 solo 时，只有 solo 的音频轨参与混音（视频/字幕轨不受影响）。
    bool solo = false;
    /// 轨道增益（dB；0 = 原始电平；合法范围 [-60, +12]）。
    /// 混音台的分贝数值直接输入即修改此值（轨道级总音量，与片段级增益相乘）。
    double gainDb = 0.0;
    std::vector<Clip> clips;
    /// 转场列表（作用于 clips 中相邻的片段对）。
    std::vector<Transition> transitions;
    /// 音频自动闪避设置（仅音频轨有效；默认关闭）。
    AutoDuckSettings autoDuck;
    /// 调整图层的特效栈（B2-6）。
    ///
    /// 语义：仅 `type == TrackType::Adjustment` 时有意义。合成时把**下方已
    /// 合成好的画面**当作输入应用这些特效，再继续向上叠加。
    /// 空 = 无效果（此时调整图层完全不影响输出，可安全存在）。
    ///
    /// 为什么不复用 `Clip::effects`：调整图层没有片段——它作用于「下方所有
    /// 轨道」，而不是某一帧素材。挂在轨道上才符合它的作用域。
    EffectStack adjustmentEffects;
    /// 调整图层的 LUT 路径与强度（B2-6；空 = 不应用）。
    std::string adjustmentLutPath;
    double adjustmentLutStrength = 1.0;

    /* ---- 4.7 调整图层全功能 ----
       用户原话：「调整图层相当于一个调节的图层，可以在里面加任何功能，
       之后应用到它下面的所有轨道。」

       原来只有 effects + LUT，用户无法对整帧做几何变换或调透明度。
       补齐三项，使其功能真正与普通片段「平行」：
         · xforms     —— 8 项几何变换作用于**已合成的整帧**
         · opacity    —— 整帧 alpha
         · animation  —— 关键帧（独立实例，不与任何 Clip 共用）
    */

    /// 8 项几何变换（作用于下方所有轨道合成后的整帧）。
    XformStack adjustmentXforms;
    /// 整帧不透明度。1.0 = 不变；0.0 = 完全透明（等于关闭这层）。
    double adjustmentOpacity = 1.0;
    /// 关键帧动画。与 ClipAnimation 同构但是**独立实例** ——
    /// 调整图层没有 Clip，共用一份会导致两处写入互相覆盖。
    ClipAnimation adjustmentAnimation;

    /// 是否为「有实际效果的」调整图层。
    ///
    /// 判据要与渲染路径一致：只要有一项会产生变化就算有。
    /// 漏判会导致「用户设了效果但渲染被跳过」——这类 bug 表现为
    /// 「调了半天没反应」，最难查。因此宁可多重算一次也不漏。
    bool hasAdjustment() const {
        if (type != TrackType::Adjustment) return false;
        if (!adjustmentEffects.empty()) return true;
        if (!adjustmentLutPath.empty()) return true;
        if (adjustmentXforms.anyEnabled()) return true;
        if (adjustmentOpacity < 0.999) return true;
        if (!adjustmentAnimation.empty()) return true;
        return false;
    }
    /// 轨道在时间轴上的显示高度（像素，B2-10）。
    ///
    /// 为什么放核心层：用户调好的布局要跨会话保留，必须跟随工程序列化。
    /// 取值范围 [kMinTrackHeight, kMaxTrackHeight]，越界由 setTrackHeight 拒绝
    /// 而不是静默截断——静默截断会让用户「拖了半天突然跳回去」。
    int heightPx = kDefaultTrackHeight;
    /// 是否折叠为仅标题栏（B2-10）。折叠时 heightPx 保留，展开后恢复。
    bool collapsed = false;

    static constexpr int kMinTrackHeight = 24;
    static constexpr int kDefaultTrackHeight = 48;
    static constexpr int kMaxTrackHeight = 200;

    /// 高度是否在合法范围。
    bool heightValid() const {
        return heightPx >= kMinTrackHeight && heightPx <= kMaxTrackHeight;
    }

    /// 独奏状态是否有效（音频轨语义）。
    bool soloActive() const { return solo; }
    /// 增益是否合法。
    bool gainValid() const { return gainDb >= -60.0 && gainDb <= 12.0; }

    const Clip* findClip(ClipId clipId) const;
    Clip* findClip(ClipId clipId);
    const Clip* clipAt(const Time& t) const;
    int indexOf(ClipId clipId) const;

    /// 校验不变量：排序 + 无重叠 + 各片段合法。
    bool invariantsHold() const;

    /// 目标区间是否与现有片段重叠（ignore 用于忽略自身）。
    bool hasOverlap(const Time& start, const Time& end, ClipId ignore = ClipId{}) const;

    void sortClips();
};

}  // namespace core
}  // namespace nairoclip