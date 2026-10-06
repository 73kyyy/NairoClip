#pragma once

#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/subtitle.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 预览渲染器：给定时间轴时刻，返回该帧应显示的画面 + 字幕文本。
///
/// 职责：
///  - 查找时刻处的活跃片段（视频轨）
///  - 时间映射（timeline → source），读取源帧
///  - 附加动画属性（透明度等，供 UI 层应用）
///  - 附加该时刻的字幕文本列表
///
/// 与导出执行器的区别：预览是「按需单帧」，导出是「顺序全帧」。
struct PreviewFrame {
    Time timelineTime;
    int width = 0;
    int height = 0;
    /// RGBA8 像素（空 = 该时刻无画面，UI 显示黑场）。
    std::vector<std::uint8_t> rgba;
    /// 该帧的活动字幕（按轨道顺序）。
    std::vector<std::string> subtitles;
    /// 片段动画快照（透明度等；-1 = 无片段）。
    double opacity = 1.0;
    double scale = 1.0;
    double posX = 0.0;
    double posY = 0.0;
    double rotation = 0.0;
    /// 片段 ID（无片段时 0）。
    std::uint64_t clipId = 0;
};

/// 预览渲染器。
class PreviewRenderer {
public:
    PreviewRenderer(const Project& project, MediaReaderFactory& readerFactory);

    /// 渲染指定时刻的一帧。
    /// - 视频：取第一个未隐藏视频轨在 t 时刻的片段
    /// - 字幕：取序列字幕轨中所有可见字幕
    /// - 动画：应用片段动画通道（透明度/缩放/位移/旋转）
    PreviewFrame renderAt(const Time& timelineTime);

    /// 便捷：只取字幕。
    std::vector<std::string> subtitlesAt(const Time& timelineTime) const;

private:
    const Project& project_;
    MediaReaderFactory& readerFactory_;
    std::map<std::uint64_t, std::unique_ptr<MediaReader>> readers_;
};

}  // namespace core
}  // namespace nairoclip
