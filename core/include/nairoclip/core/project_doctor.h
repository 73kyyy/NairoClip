#pragma once

#include "nairoclip/core/project.h"

#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 工程健康检查与修复（Project Doctor）
// =============================================================================
// 用途：
//  - 打开文件后的自检（防止损坏工程导致崩溃）
//  - 用户手动「修复工程」（容错恢复）
//  - QA 工具链
//
// 检查项：
//  1. 悬空引用：片段引用的素材不存在
//  2. 轨道不变量：片段重叠/负时长/负起点
//  3. 转场一致性：引用片段缺失、时长为负
//  4. 字幕不变量
//  5. 序列时长与片段不一致（信息级）
//  6. ID 分配器回退（保证新 ID 不冲突）

/// 问题严重级别。
enum class HealthLevel {
    Info,     // 信息（不影响使用）
    Warning,  // 警告（建议修复）
    Error,    // 错误（必须修复才能正常导出）
};

/// 单个问题描述。
struct HealthIssue {
    HealthLevel level = HealthLevel::Info;
    std::string code;     // 稳定代码（如 "dangling-asset"）
    std::string message;  // 人类可读描述
    /// 定位（可选）：
    std::uint64_t sequenceId = 0;
    std::uint64_t trackId = 0;
    std::uint64_t clipId = 0;
};

/// 健康报告。
struct HealthReport {
    std::vector<HealthIssue> issues;

    std::size_t errorCount() const;
    std::size_t warningCount() const;
    bool healthy() const { return errorCount() == 0; }
};

/// 修复动作描述（修复做了什么）。
struct RepairAction {
    std::string code;
    std::string description;
    std::size_t affected = 0;
};

/// 修复报告。
struct RepairReport {
    std::vector<RepairAction> actions;
    HealthReport after;  // 修复后的重新检查
};

/// 检查工程健康状态（只读）。
HealthReport checkProjectHealth(const Project& project);

/// 修复工程（原地修改）。
/// 修复策略（保守，不删除用户数据）：
///  - 悬空素材引用片段 → 禁用（enabled=false）并保留（用户可重新链接素材）
///  - 重叠片段 → 截断后到的片段（保留先到的）
///  - 负起点 → 钳制到 0
///  - 非正时长 → 删除该片段（无法表达）
///  - 悬空转场 → 删除转场
///  - ID 分配器 → 推进到大于所有现有实体 ID
RepairReport repairProject(Project& project);

}  // namespace core
}  // namespace nairoclip
