#pragma once

/* =============================================================================
   固定大小线程池（P3）
   -----------------------------------------------------------------------------
   用途：把「逐像素」类工作按**行条带**切分到多核。

   为什么按行切而不按层：同一帧的多层合成有严格的 alpha 叠加顺序，
   层与层之间无法并行；但每一层内部的像素之间**完全独立**，
   按行切分既无依赖、又对缓存友好（每线程连续访问一段内存）。

   为什么不用第三方库：依赖越少越好。这里只需要「提交 N 个任务并等全部完成」
   这一个语义，条件变量 + 计数器 50 行就够，引一个 TBB/OpenMP 不值得。

   ⚠️ **不保证任务执行顺序**。调用方必须保证各任务写的内存区间不重叠。

   ⚠️ **不可重入**：任务里再调 parallelFor 会死锁（线程池线程被占满后
   等自己）。这一条写在接口注释里，因为它是最容易踩的坑。
   ============================================================================= */

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace nairoclip {
namespace core {

class ThreadPool {
public:
    /// 进程级单例（懒初始化）。线程数 = hardware_concurrency，至少 1。
    static ThreadPool& instance();

    /// 线程数（实际创建的，不是 hardware_concurrency 的返回值）。
    int threadCount() const { return static_cast<int>(workers_.size()); }

    /// 并行执行 fn(begin, end)，把 [0, count) 切成若干段。
    ///
    /// - count 是**工作项数**（通常是行数），不是像素数；
    /// - minPerTask：每段至少处理多少项。低于它就不切分 ——
    ///   小图（< 64K 像素，约 128 行 1080p 以下）走单线程，
    ///   否则任务调度开销反超计算收益；
    /// - 阻塞直到全部完成；异常不跨线程传播（任务里不要抛）。
    ///
    /// 段划分：`n = min(threadCount, ceil(count / minPerTask))`，
    /// 按 n 等分连续区间。等分后余数给最后一段，保证不丢行。
    void parallelFor(std::size_t count, std::size_t minPerTask,
                     const std::function<void(std::size_t, std::size_t)>& fn);

    /// 建议的「够大才并行」阈值（像素数）。低于它单线程更快。
    static constexpr std::size_t kMinPixelsForParallel = 64u * 1024u;

    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    ThreadPool();

    void workerLoop();

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cvWork_;
    std::condition_variable cvDone_;
    std::function<void(std::size_t, std::size_t)> job_;
    std::size_t jobBegin_ = 0;
    std::size_t jobEnd_ = 0;
    std::size_t jobChunk_ = 0;
    std::size_t jobTotal_ = 0;      // 本次 parallelFor 的总段数
    std::size_t jobNext_ = 0;       // 下一个待领取的段号（原子递增）
    std::size_t jobRemaining_ = 0;  // 未完成的段数
    std::atomic<bool> stop_{false};
    bool hasJob_ = false;
};

/// 便捷封装：按行并行处理一张 RGBA 图。
///
/// `fn(y, rowPtr)` 处理第 y 行；像素数低于阈值时在当前线程同步执行。
/// 这是大多数逐像素效果需要的最小接口。
void parallelForRows(int height, int width, const std::uint8_t* base, std::size_t strideBytes,
                     const std::function<void(int, std::uint8_t*)>& fn);

}  // namespace core
}  // namespace nairoclip
