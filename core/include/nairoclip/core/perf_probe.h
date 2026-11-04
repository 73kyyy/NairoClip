#pragma once

/* =============================================================================
   性能埋点（P0）
   -----------------------------------------------------------------------------
   目的：**先测量，再优化。** 没有分阶段计时就无法判断瓶颈，
   容易把时间花在不是瓶颈的地方。

   五段（与 PERF_XFORM_PLAN.md §0 的分段一致）：
     decode    解码 + sws 转换（含 seek）
     fit       画幅适配 / 几何变换重采样
     effect    特效、滤镜、LUT、蒙版、抠像
     composite 多层合成
     transfer  传给平台层（PNG/Base64 或 SurfaceView blit）

   设计要点：
     · **默认关闭**。埋点本身有成本（clock_gettime + 原子操作），
       不能常态开着。用环境变量 NC_PERF=1 或 C ABI 开关启用。
     · 关闭时代价 = 一次 bool 判断，可以忽略。
     · 环形缓冲记录最近 N 个样本，导出 avg / p99 / count。
       不用累加和算 avg —— 平均会被极端值带偏，p99 才能反映卡顿。
     · 线程安全：写入用原子序号 + 无锁环形写；读取时可能读到
       半新半旧，但统计用途可以接受（p99 不需要严格一致）。
   ============================================================================= */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace nairoclip {
namespace core {

/// 五个计时阶段。顺序与文档一致，不要重排（C ABI 按下标传）。
enum class PerfStage : int {
    Decode = 0,
    Fit = 1,
    Effect = 2,
    Composite = 3,
    Transfer = 4,
    Count = 5
};

constexpr int kPerfStageCount = static_cast<int>(PerfStage::Count);
constexpr std::size_t kPerfRingSize = 256;  // 每阶段保留最近 256 个样本

const char* perfStageName(PerfStage stage);

/// 是否启用埋点（读 NC_PERF 环境变量；只解析一次）。
bool perfEnabled();

/// 程序化开关（供测试与 C ABI 使用）。优先级高于环境变量。
void perfSetEnabled(bool on);

/// 采样一个样本（微秒）。
void perfSample(PerfStage stage, double microseconds);

/// 取统计结果。任何 out 指针可为 nullptr。
void perfGet(PerfStage stage, double* outAvgMs, double* outP99Ms, double* outMaxMs,
             std::uint64_t* outCount);

/// 清空所有统计。
void perfReset();

/* ---------------------------------------------------------------- 作用域计时 */
/// RAII 计时器。perfEnabled() 为 false 时构造/析构都只是一次 bool 判断。
class ScopedTimer {
public:
    explicit ScopedTimer(PerfStage stage)
        : stage_(stage), enabled_(perfEnabled()), startUs_(enabled_ ? nowUs() : 0.0) {}

    ~ScopedTimer() {
        if (enabled_) {
            perfSample(stage_, nowUs() - startUs_);
        }
    }

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    static double nowUs();
    PerfStage stage_;
    bool enabled_;
    double startUs_;
};

/// 便捷宏。作用域结束时自动采样。
#define NC_PERF_SCOPE(stage) ::nairoclip::core::ScopedTimer _nc_perf_timer_##__LINE__(stage)

}  // namespace core
}  // namespace nairoclip
