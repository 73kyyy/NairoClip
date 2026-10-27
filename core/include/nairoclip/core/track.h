#pragma once

#include "nairoclip/core/clip.h"
#include "nairoclip/core/transition_types.h"

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
    /// 当前状态：**尚未有渲染代码消费它**，创建后放入的内容不会生效。
    /// 因此 `validateClipPlacement` 拒绝任何 Clip 落入（见 B0-7），
    /// UI 侧也应禁止用户创建，等 B2-6 落地后再开放。
    Adjustment,
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