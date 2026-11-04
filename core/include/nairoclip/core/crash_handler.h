#pragma once

/* =============================================================================
   崩溃捕获（§8.1 第 3 条）
   -----------------------------------------------------------------------------
   目的：**让崩溃留下证据。**

   现状（2026-11-04 实测）：core 里零 `signal()` / `sigaction` / `backtrace`。
   一崩进程直接死，什么都不留 —— 排查只能靠「用户描述 + 猜」。

   ## 设计原则（三条，都是踩过的坑）

   1. **只记录，不恢复。**
      出了 SIGSEGV 之后进程状态已不可信（堆可能已损坏），
      任何「修复后继续跑」的尝试都会引发二次崩溃，且丢失第一现场。
      正确做法：写完栈立刻 `_exit()`。

   2. **信号处理器里只能用 async-signal-safe 函数。**
      `malloc` / `printf` / `std::string` / `std::vector` 都**不能用** ——
      它们内部可能加锁，而崩溃时锁可能正被持有，会死锁。
      只能用 `open` / `write` / `close` / `backtrace_symbols_fd`。

   3. **栈要预先分配好。**
      用 `sigaltstack` 给 SIGSEGV 单独的栈 —— 栈溢出导致的崩溃
      在原栈上无法执行处理器（这正是最常见的一类崩溃）。

   ## 不做的事

   · 不做 minidump（那是 Crashpad 的范畴，见 §8.7 的 P2 项）
   · 不做符号化（用 `addr2line` 事后处理，运行时做需要 libunwind）
   · 不上传（本地落盘即可，用户可主动导出）
   ============================================================================= */

namespace nairoclip {
namespace core {

/// 安装崩溃处理器（SIGSEGV / SIGABRT / SIGFPE / SIGBUS / SIGILL）。
///
/// @param crashDir 崩溃日志目录（必须已存在且可写）。
///                 传 nullptr 用当前工作目录。
/// @return 是否全部安装成功。
///
/// 幂等：重复调用只安装一次（第二次直接返回 true）。
bool installCrashHandler(const char* crashDir = nullptr);

/// 卸载处理器（仅测试用 —— 生产环境不该卸载）。
void uninstallCrashHandler();

/// 崩溃日志的**预期**路径（不检查是否存在）。
/// 供 UI「导出崩溃报告」使用；未安装时返回空串。
const char* crashLogPath();

}  // namespace core
}  // namespace nairoclip
