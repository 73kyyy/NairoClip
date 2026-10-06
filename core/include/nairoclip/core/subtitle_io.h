#pragma once

#include "nairoclip/core/subtitle.h"
#include "nairoclip/core/types.h"

#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 字幕文件交换（SRT / WebVTT）
// =============================================================================
// 用途：导入外部字幕（剪映/PR 用户迁移场景）+ 导出字幕供平台播放器使用。
// 约束：不做样式换算（SRT/VTT 基础样式有限，样式由导入后用户调整）；
//       时间解析容错（接受逗号/点号毫秒分隔符）。

/// 解析 SRT 文本为字幕列表。
/// - 返回解析成功的字幕（text 已 trim；时间已规范化）；
/// - 解析失败的行跳过并计入 warnings。
struct SubtitleParseResult {
    Result result;
    std::vector<SubtitleCue> cues;
    std::vector<std::string> warnings;
};

SubtitleParseResult parseSrt(const std::string& text);

/// 解析 WebVTT 文本为字幕列表。
SubtitleParseResult parseVtt(const std::string& text);

/// 导出为 SRT 文本。
/// - cues 按 start 排序输出；样式忽略（SRT 基础格式）。
std::string exportSrt(const std::vector<SubtitleCue>& cues);

/// 导出为 WebVTT 文本。
std::string exportVtt(const std::vector<SubtitleCue>& cues);

}  // namespace core
}  // namespace nairoclip
