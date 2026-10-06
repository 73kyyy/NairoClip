#pragma once

#include "nairoclip/core/json.h"
#include "nairoclip/core/project.h"

#include <string>

namespace nairoclip {
namespace core {

/// .nclip 工程文件格式版本。
/// 版本历史：
///   1 - 初始版本：工程/序列/轨道/片段/素材全量存储
constexpr int kNclipFormatVersion = 1;

/// 序列化错误码扩展说明：
///   Unsupported - 文件版本高于当前支持版本
///   InvalidArgument - 结构不完整/类型错误/引用悬空
struct NclipLoadResult {
    Result result;
    int fileVersion = 0;  // 成功时为实际读取的版本
};

/// 将工程序列化为 .nclip 文本（JSON）。
std::string saveProjectToNclip(const Project& project);

/// 从 .nclip 文本载入工程。失败时 out 不被修改。
NclipLoadResult loadProjectFromNclip(const std::string& text, Project& out);

}  // namespace core
}  // namespace nairoclip
