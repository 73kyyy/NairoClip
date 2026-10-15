#pragma once

#include "nairoclip/core/animation.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <string>
#include <vector>
#include <vector>

namespace nairoclip {
namespace core {

/// 字幕样式（字体、大小、颜色、描边等）。
/// 颜色使用 #RRGGBB / #RRGGBBAA 格式。
struct SubtitleStyle {
    std::string fontFamily = "默认";
    double fontSize = 48.0;         // 画布坐标系点数
    std::string color = "#FFFFFF";  // 填充色
    std::string outlineColor = "#000000";
    double outlineWidth = 2.0;
    std::string backgroundColor = "#00000000";  // 默认透明
    double backgroundPadding = 8.0;
    /// 对齐：0=左 1=中 2=右
    int alignment = 1;
    /// 归一化位置（0..1，画布坐标，锚点取决于 alignment）。
    double anchorX = 0.5;
    double anchorY = 0.9;
};

/// 一条字幕。
struct SubtitleCue {
    SubtitleId id;
    Time start;
    Time duration;
    std::string text;
    SubtitleStyle style;
    bool enabled = true;
    /// 镜头追踪采样点（文档 Part 4-A 3.3「镜头追踪」：文字跟随画面物体）。
    /// 非空时表示该文字已被追踪绑定：anchorX/anchorY 由追踪点插值覆盖。
    /// 语义：时间为**时间轴绝对时间**；坐标为归一化 0..1。
    struct TrackingPoint {
        Time time;
        double centerX = 0.5;
        double centerY = 0.5;
    };
    std::vector<TrackingPoint> tracking;
    /// 追踪应用模式（0 位置 / 1 位置+缩放；文档 3.3「追踪类型」）。
    int trackingMode = 0;
    /// 是否已绑定追踪。
    bool hasTracking() const { return !tracking.empty(); }

    Time end() const { return start + duration; }
    bool contains(const Time& t) const { return start <= t && t < end(); }
    bool valid() const {
        return duration.isPositive() && !start.isNegative();
    }
};

/// 字幕轨道（独立的字幕集合，同一时刻至多一条可见由上层决定，
/// 这里只保证存储有序）。
/// 不变量：按 start 升序；同一条不重叠不强制（允许上层做双语叠放）。
class SubtitleTrack {
public:
    /// 插入字幕（自动按 start 排序）。
    Result insert(SubtitleCue cue);
    bool remove(SubtitleId id);
    void clear();

    bool empty() const { return cues_.empty(); }
    std::size_t size() const { return cues_.size(); }
    const std::vector<SubtitleCue>& cues() const { return cues_; }

    SubtitleCue* findCue(SubtitleId id);
    const SubtitleCue* findCue(SubtitleId id) const;

    /// 查找时刻 t 处所有可见字幕（升序）。
    std::vector<const SubtitleCue*> cuesAt(const Time& t) const;

    bool invariantsHold() const;

private:
    std::vector<SubtitleCue> cues_;
};

/// 把纯文本按行拆成若干条字幕（自动分行的辅助工具）。
/// 返回拆分结果（不含时间信息，调用方填充）。
std::vector<std::string> splitSubtitleText(const std::string& text,
                                           std::size_t maxCharsPerLine);

}  // namespace core
}  // namespace nairoclip
