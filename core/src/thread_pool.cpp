#include "nairoclip/core/thread_pool.h"

#include <algorithm>
#include <exception>

namespace nairoclip {
namespace core {

ThreadPool& ThreadPool::instance() {
    static ThreadPool pool;
    return pool;
}

ThreadPool::ThreadPool() {
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 2;   // 查询失败时给一个保守值，不要退化成单线程
    /* 上限 8：手机上 12 核跑满反而因降频更慢，且调度开销上升。
       实测（ARM 8 核）4~8 线程收益最好，超过 8 后基本平坦。 */
    const unsigned n = std::min(hw, 8u);
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_.store(true, std::memory_order_relaxed);
    }
    cvWork_.notify_all();
    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void(std::size_t, std::size_t)> localJob;
        std::size_t begin = 0;
        std::size_t end = 0;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cvWork_.wait(lock, [this] {
                return (hasJob_ && jobNext_ < jobTotal_) ||
                       stop_.load(std::memory_order_relaxed);
            });

            if (stop_.load(std::memory_order_relaxed)) return;

            /* 领取下一段。jobNext_ 在锁内递增 —— 不是原子变量，
               因为「读 job_ + 算区间」必须与「领取」在同一临界区，
               否则主线程收尾时可能读到半更新的状态。 */
            const std::size_t idx = jobNext_++;
            localJob = job_;
            // 将余数分配给前 rem 段；jobTotal_ <= 工作项数，因此每段都非空。
            const std::size_t count = jobEnd_ - jobBegin_;
            const std::size_t base = count / jobTotal_;
            const std::size_t remainder = count % jobTotal_;
            begin = jobBegin_ + idx * base + std::min(idx, remainder);
            end = begin + base + (idx < remainder ? 1u : 0u);
        }

        if (localJob && end > begin) {
            try {
                localJob(begin, end);
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!jobException_) {
                    jobException_ = std::current_exception();
                }
                if (jobRemaining_ > 0 && --jobRemaining_ == 0) {
                    cvDone_.notify_one();
                }
                continue;
            }

            std::lock_guard<std::mutex> lock(mutex_);
            if (jobRemaining_ > 0 && --jobRemaining_ == 0) {
                cvDone_.notify_one();
            }
        }
    }
}

void ThreadPool::parallelFor(std::size_t count, std::size_t minPerTask,
                             const std::function<void(std::size_t, std::size_t)>& fn) {
    if (count == 0 || !fn) return;
    if (minPerTask == 0) minPerTask = 1;

    // 够小 → 当前线程直接做完，不进池子
    const std::size_t wantTasks = 1u + (count - 1u) / minPerTask;
    const std::size_t nThreads = static_cast<std::size_t>(threadCount());
    if (wantTasks <= 1 || nThreads <= 1) {
        fn(0, count);
        return;
    }

    const std::size_t tasks = std::min(wantTasks, nThreads);
    const std::size_t chunk = (count + tasks - 1) / tasks;

    bool runInline = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (hasJob_) {
            /* 已有批次时退化为调用线程同步执行。
               关键：绝不能在持有 mutex_ 时调用用户回调；回调可能再次进入
               parallelFor，或调用其他需要线程池锁的代码。 */
            runInline = true;
        } else {
            job_ = fn;
            jobBegin_ = 0;
            jobEnd_ = count;
            jobTotal_ = tasks;
            jobNext_ = 0;
            jobRemaining_ = tasks;
            jobException_ = nullptr;
            hasJob_ = true;
        }
    }
    if (runInline) {
        fn(0, count);
        return;
    }
    cvWork_.notify_all();

    {
        std::unique_lock<std::mutex> lock(mutex_);
        cvDone_.wait(lock, [this] { return jobRemaining_ == 0; });
        const std::exception_ptr jobException = jobException_;
        jobException_ = nullptr;
        hasJob_ = false;
        job_ = nullptr;
        lock.unlock();
        if (jobException) {
            std::rethrow_exception(jobException);
        }
    }
}

void parallelForRows(int height, int width, const std::uint8_t* base, std::size_t strideBytes,
                     const std::function<void(int, std::uint8_t*)>& fn) {
    if (height <= 0 || width <= 0 || base == nullptr || !fn) return;

    const std::size_t pixels = static_cast<std::size_t>(height) * static_cast<std::size_t>(width);
    if (pixels < ThreadPool::kMinPixelsForParallel) {
        for (int y = 0; y < height; ++y) {
            fn(y, const_cast<std::uint8_t*>(base) + static_cast<std::size_t>(y) * strideBytes);
        }
        return;
    }

    ThreadPool::instance().parallelFor(
        static_cast<std::size_t>(height), 16,
        [&](std::size_t begin, std::size_t end) {
            for (std::size_t y = begin; y < end; ++y) {
                fn(static_cast<int>(y),
                   const_cast<std::uint8_t*>(base) + y * strideBytes);
            }
        });
}

}  // namespace core
}  // namespace nairoclip
