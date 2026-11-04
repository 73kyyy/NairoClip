#include "nairoclip/core/crash_handler.h"

#include <cstdio>
#include <cstring>
#include <mutex>

#if defined(__unix__) || defined(__APPLE__) || defined(__ANDROID__)
#define NC_HAS_POSIX_SIGNAL 1
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
/* backtrace 是 glibc 扩展，**Android NDK 没有 <execinfo.h>**。
   ---------------------------------------------------------------------------
   实测：Android 交叉编译报
     error: no member named 'backtrace' in the global namespace
   所以必须用 __has_include 检测，并在没有时降级。

   降级后的日志仍然有用：信号名 + 故障地址 + 时间。
   故障地址能区分「空指针 0x0」「野指针」「栈溢出」，已足够定位方向。
   完整调用栈需要另外集成 libunwind 或 Crashpad（见 §8.7 的 P2 项）。 */
/* backtrace 的可用性判定（踩过两个坑，都记录在此）
   ---------------------------------------------------------------------------
   坑 1：Android NDK **有** <execinfo.h>，但里面写着
         `int backtrace(...) __INTRODUCED_IN(33);`
         —— 符号只在 API 33+ 存在。我们 minSdk = 24，
         链接期或编译期都会失败。

   坑 2：只看 __has_include 不够。头文件在，符号不在。

   正确判据（三者同时满足）：
     · 有 <execinfo.h>
     · 不是 Android，或 Android API ≥ 33
     · 不是 musl（musl 也没有 backtrace）

   降级后日志仍含「信号名 + 故障地址 + 时间」，足以判断崩溃类型
   （空指针 / 野指针 / 栈溢出）。完整栈需 libunwind 或 Crashpad。 */
#if defined(__has_include)
#if __has_include(<execinfo.h>)
#define NC_EXECINFO_HEADER 1
#else
#define NC_EXECINFO_HEADER 0
#endif
#else
#define NC_EXECINFO_HEADER 0
#endif

#if NC_EXECINFO_HEADER && defined(__ANDROID__) && (__ANDROID_API__ < 33)
#define NC_HAS_BACKTRACE 0
#elif NC_EXECINFO_HEADER
#define NC_HAS_BACKTRACE 1
#include <execinfo.h>
#else
#define NC_HAS_BACKTRACE 0
#endif
#else
#define NC_HAS_POSIX_SIGNAL 0
#endif

namespace nairoclip {
namespace core {

#if NC_HAS_POSIX_SIGNAL

namespace {

/* ---------------------------------------------------------------- 全局状态 */

/* 全部用固定数组而非 std::string ——
   信号处理器里不能用会分配内存的类型。 */
char g_logPath[1024] = {0};
volatile sig_atomic_t g_installed = 0;
volatile sig_atomic_t g_inHandler = 0;

std::mutex g_installMutex;

/* 备用栈：栈溢出导致的 SIGSEGV 在原栈上无法执行处理器。
   64KB 足够 backtrace + write 使用。 */
constexpr std::size_t kAltStackSize = 64 * 1024;
char g_altStack[kAltStackSize];

/* ---------------------------------------------------------------- 安全写 */

/// async-signal-safe 的写字符串（不用 strlen —— 它虽然不是最危险的，
/// 但自己数长度最保险）。
void safeWrite(int fd, const char* s) {
    if (s == nullptr) return;
    std::size_t n = 0;
    while (s[n] != '\0' && n < 4096) ++n;
    if (n > 0) {
        ssize_t ignored = ::write(fd, s, n);
        (void)ignored;
    }
}

/// async-signal-safe 的写整数（不用 snprintf）。
void safeWriteInt(int fd, long long v) {
    char buf[24];
    int i = sizeof(buf) - 1;
    buf[i] = '\0';
    const bool neg = v < 0;
    unsigned long long u = neg ? static_cast<unsigned long long>(-(v + 1)) + 1ULL
                               : static_cast<unsigned long long>(v);
    if (u == 0) {
        buf[--i] = '0';
    } else {
        while (u > 0 && i > 1) {
            buf[--i] = static_cast<char>('0' + (u % 10ULL));
            u /= 10ULL;
        }
    }
    if (neg && i > 0) buf[--i] = '-';
    ssize_t ignored = ::write(fd, buf + i, static_cast<std::size_t>(sizeof(buf) - 1 - i));
    (void)ignored;
}

const char* signalName(int sig) {
    switch (sig) {
        case SIGSEGV: return "SIGSEGV (段错误)";
        case SIGABRT: return "SIGABRT (abort)";
        case SIGFPE: return "SIGFPE (算术异常)";
        case SIGBUS: return "SIGBUS (总线错误)";
        case SIGILL: return "SIGILL (非法指令)";
        default: return "未知信号";
    }
}

/* ---------------------------------------------------------------- 处理器 */

void crashHandler(int sig, siginfo_t* info, void* /*context*/) {
    /* 防重入：处理过程中再崩（极可能发生，因为写文件也要调系统调用），
       直接 _exit，不要递归。 */
    if (g_inHandler != 0) {
        _exit(128 + sig);
    }
    g_inHandler = 1;

    /* 只保留异步信号安全的操作：open / write / close / backtrace_symbols_fd */
    const int fd = ::open(g_logPath[0] != '\0' ? g_logPath : "./crash.log",
                          O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        safeWrite(fd, "=== NairoClip 崩溃报告 ===\n信号: ");
        safeWrite(fd, signalName(sig));
        safeWrite(fd, " [");
        safeWriteInt(fd, sig);
        safeWrite(fd, "]\n");

        /* 故障地址：对 SIGSEGV 极有价值 ——
           能立刻区分「空指针（0x0）」「野指针（0xdeadbeef）」
           「栈溢出（接近栈顶地址）」 */
        if (info != nullptr) {
            safeWrite(fd, "故障地址: 0x");
            /* 用十六进制输出，自己实现（snprintf 不安全） */
            char hex[2 + sizeof(void*) * 2 + 1];
            int i = 0;
            hex[i++] = '0';
            hex[i++] = 'x';
            const unsigned long long addr =
                reinterpret_cast<unsigned long long>(info->si_addr);
            const int digits = static_cast<int>(sizeof(void*) * 2);
            for (int d = digits - 1; d >= 0; --d) {
                const int nib = static_cast<int>((addr >> (d * 4)) & 0xF);
                hex[i++] = static_cast<char>(nib < 10 ? '0' + nib : 'a' + (nib - 10));
            }
            hex[i] = '\0';
            safeWrite(fd, hex);
            safeWrite(fd, "\n");
        }

        safeWrite(fd, "\n--- 调用栈 ---\n");
#if NC_HAS_BACKTRACE
        /* backtrace_symbols_fd 是 async-signal-safe 的；
           而 backtrace_symbols（返回 char**）会 malloc，不能用。 */
        void* frames[64];
        const int n = ::backtrace(frames, 64);
        if (n > 0) {
            ::backtrace_symbols_fd(frames, n, fd);
        }
#else
        /* Android 走到这里。不是错误 —— 是平台能力限制。
           日志仍然包含信号名与故障地址，足以判断崩溃类型。 */
        safeWrite(fd, "（本平台无 execinfo.h，无法取调用栈）\n");
        safeWrite(fd, "可用的信息：信号类型 + 故障地址。\n");
        safeWrite(fd, "需要完整栈请集成 libunwind 或 Crashpad（见 §8.7）。\n");
#endif
        safeWrite(fd, "\n--- 说明 ---\n");
        safeWrite(fd, "地址可用 addr2line 或 llvm-symbolizer 还原：\n");
        safeWrite(fd, "  addr2line -Cfipe <libnairoclip_core.a 中的 .o> 0x<地址>\n");
        safeWrite(fd, "注意：release 构建若 strip 过，符号可能不完整。\n");

        ::close(fd);
    }

    /* **不恢复**：写完就走。
       出了段错误后进程状态已不可信，继续跑只会引发二次崩溃，
       而且第二现场会覆盖第一现场。 */
    _exit(128 + sig);
}

}  // namespace

bool installCrashHandler(const char* crashDir) {
    std::lock_guard<std::mutex> lock(g_installMutex);
    if (g_installed != 0) return true;

    /* 日志路径：目录 + 固定文件名。
       不用时间戳 —— 每次崩溃覆盖上一次即可，历史报告价值不高，
       而时间戳会让文件名无法预测、UI 不好找。 */
    if (crashDir != nullptr && crashDir[0] != '\0') {
        std::snprintf(g_logPath, sizeof(g_logPath), "%s/crash.log", crashDir);
    } else {
        std::snprintf(g_logPath, sizeof(g_logPath), "./crash.log");
    }

    /* 备用栈：SIGSEGV 用 SA_ONSTACK 时走这块内存 */
    stack_t ss;
    std::memset(&ss, 0, sizeof(ss));
    ss.ss_sp = g_altStack;
    ss.ss_size = kAltStackSize;
    ss.ss_flags = 0;
    if (::sigaltstack(&ss, nullptr) != 0) {
        return false;
    }

    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crashHandler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    ::sigemptyset(&sa.sa_mask);

    const int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL};
    bool ok = true;
    for (int sig : signals) {
        if (::sigaction(sig, &sa, nullptr) != 0) {
            ok = false;
        }
    }

    if (ok) g_installed = 1;
    return ok;
}

void uninstallCrashHandler() {
    std::lock_guard<std::mutex> lock(g_installMutex);
    if (g_installed == 0) return;
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    const int signals[] = {SIGSEGV, SIGABRT, SIGFPE, SIGBUS, SIGILL};
    for (int sig : signals) {
        ::sigaction(sig, &sa, nullptr);
    }
    stack_t ss;
    std::memset(&ss, 0, sizeof(ss));
    ss.ss_flags = SS_DISABLE;
    ::sigaltstack(&ss, nullptr);
    g_installed = 0;
}

const char* crashLogPath() { return g_logPath; }

#else  // 非 POSIX 平台

bool installCrashHandler(const char* /*crashDir*/) { return false; }
void uninstallCrashHandler() {}
const char* crashLogPath() { return ""; }

#endif

}  // namespace core
}  // namespace nairoclip
