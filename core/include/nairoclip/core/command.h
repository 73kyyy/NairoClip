#pragma once

#include "nairoclip/core/project.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 编辑命令：apply 执行、revert 还原。
/// 所有修改工程的操作必须实现为命令，由 UndoStack 统一驱动，
/// 保证撤销/重做的一致性。
class Command {
public:
    virtual ~Command() = default;

    virtual Result apply(Project& project) = 0;
    virtual Result revert(Project& project) = 0;

    /// 展示名称（历史面板用，如「分割片段」）。
    virtual std::string name() const = 0;
};

/// 撤销栈：线性历史 + 游标。
class UndoStack {
public:
    /// 执行命令并入栈。apply 失败时命令不入栈，工程保持原样。
    Result push(Project& project, std::unique_ptr<Command> command);

    Result undo(Project& project);
    Result redo(Project& project);

    bool canUndo() const { return cursor_ > 0; }
    bool canRedo() const { return cursor_ < commands_.size(); }

    std::size_t undoCount() const { return cursor_; }
    std::size_t redoCount() const { return commands_.size() - cursor_; }
    std::size_t size() const { return commands_.size(); }

    /// 历史面板展示：已应用命令的名称（按时间顺序）。
    std::vector<std::string> undoHistoryNames() const;

    void clear();

private:
    std::vector<std::unique_ptr<Command>> commands_;
    std::size_t cursor_ = 0;  // commands_[0, cursor) 处于「已应用」状态
};

}  // namespace core
}  // namespace nairoclip