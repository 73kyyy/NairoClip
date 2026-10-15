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
    /// 若设置了覆盖名（setDisplayName），返回覆盖名（事务名等用户视角名称）。
    std::string name() const {
        return displayNameOverride_.empty() ? nameImpl() : displayNameOverride_;
    }

    /// 设置展示名覆盖（用于事务命名：把底层命令名替换为用户操作名）。
    void setDisplayName(std::string displayName) {
        displayNameOverride_ = std::move(displayName);
    }

    /// 合并钩子：默认可与同类型命令合并（手势拖动场景）。
    /// 返回 true 时，push 的新命令会调用 mergeFrom 并替代原命令。
    virtual bool canMergeWith(const Command& other) const {
        (void)other;
        return false;
    }
    /// 从后续命令吸收状态（canMergeWith 返回 true 时调用）。
    virtual void mergeFrom(const Command& other) { (void)other; }

protected:
    /// 子类实现的原始名称（默认由 name() 转发）。
    virtual std::string nameImpl() const = 0;

private:
    std::string displayNameOverride_;
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

    /// ---- 事务（手势合并）----
    /// 开始事务：后续 push 的命令合并为单条历史记录。
    /// 嵌套 begin 计数，最外层 end 时成形。
    void beginTransaction(const std::string& name);
    /// 结束事务：把事务期间命令合并为一条历史记录。
    /// 返回是否形成了分组（事务期间无命令则返回 false）。
    bool endTransaction(Project& project);
    bool inTransaction() const { return transactionDepth_ > 0; }

    /// 合并策略：连续 push 且栈顶命令 canMergeWith 时合并（如拖动微调）。
    void setMergeEnabled(bool enabled) { mergeEnabled_ = enabled; }
    bool mergeEnabled() const { return mergeEnabled_; }

    /// 丢弃最近一次 push（用于手势取消：先还原再弹出）。
    Result dropLast(Project& project);

    /// 设置最大历史深度（文档 Part 5(续) 第32节「最大可撤销步数 20 步」）。
    /// - maxSteps <= 0 = 不限制（默认）；
    /// - 超出深度时最早记录被移除（不再可撤销）；
    /// - 缩小时立即裁剪至目标深度（保留最近记录）。
    void setMaxDepth(std::size_t maxSteps);
    std::size_t maxDepth() const { return maxDepth_; }

private:
    Result pushInternal(Project& project, std::unique_ptr<Command> command);
    /// 按 maxDepth_ 裁剪历史（在 push 后调用）。
    void trimToMaxDepth();
    std::vector<std::unique_ptr<Command>> commands_;
    std::size_t cursor_ = 0;  // commands_[0, cursor) 处于「已应用」状态
    /// 最大历史深度（0 = 不限制）。
    std::size_t maxDepth_ = 0;

    // 事务状态
    std::size_t transactionDepth_ = 0;
    std::string transactionName_;
    std::vector<std::unique_ptr<Command>> transactionCommands_;
    bool mergeEnabled_ = false;
};

}  // namespace core
}  // namespace nairoclip