#include "nairoclip/core/perf_probe.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

/// 原子开关：默认关闭。环境变量只在首次调用时读一次。
std::atomic<bool>& enabledFlag() {
    static std::atomic<bool> flag{false};
    static std::atomic<bool> initialised{false};
    if (!initialised.exchange(true)) {
        const char* v = std::getenv("NC_PERF");
        flag.store(v != nullptr && v[0] != '0', std::memory_order_relaxed);
    }
    return flag;
}

/// 每阶段的环形缓冲。
struct StageBuffer {
    std::atomic<std::uint64_t> writes{0};
    double samples[kPerfRingSize] = {};

    void push(double us) {
        // 取序号后写入：多线程可能覆盖同一槽位，但统计用途可接受。
        const std::uint64_t i = writes.fetch_add(1, std::memory_order_relaxed);
        samples[i % kPerfRingSize] = us;
    }
};

StageBuffer& bufferFor(PerfStage stage) {
    static StageBuffer buffers[kPerfStageCount];
    const int idx = static_cast<int>(stage);
    const int clamped = (idx >= 0 && idx < kPerfStageCount) ? idx : 0;
    return buffers[clamped];
}

}  // namespace

const char* perfStageName(PerfStage stage) {
    switch (stage) {
        case PerfStage::Decode: return "decode";
        case PerfStage::Fit: return "fit";
        case PerfStage::Effect: return "effect";
        case PerfStage::Composite: return "composite";
        case PerfStage::Transfer: return "transfer";
        default: return "unknown";
    }
}

bool perfEnabled() { return enabledFlag().load(std::memory_order_relaxed); }

void perfSetEnabled(bool on) { enabledFlag().store(on, std::memory_order_relaxed); }

void perfSample(PerfStage stage, double microseconds) {
    if (!perfEnabled()) return;
    bufferFor(stage).push(microseconds);
}

void perfGet(PerfStage stage, double* outAvgMs, double* outP99Ms, double* outMaxMs,
             std::uint64_t* outCount) {
    StageBuffer& buf = bufferFor(stage);
    const std::uint64_t writes = buf.writes.load(std::memory_order_relaxed);
    const std::size_t n =
        static_cast<std::size_t>(std::min<std::uint64_t>(writes, kPerfRingSize));

    if (outCount != nullptr) *outCount = writes;
    if (n == 0) {
        if (outAvgMs != nullptr) *outAvgMs = 0.0;
        if (outP99Ms != nullptr) *outP99Ms = 0.0;
        if (outMaxMs != nullptr) *outMaxMs = 0.0;
        return;
    }

    std::vector<double> v(buf.samples, buf.samples + n);
    std::sort(v.begin(), v.end());

    double sum = 0.0;
    for (double x : v) sum += x;

    if (outAvgMs != nullptr) *outAvgMs = (sum / static_cast<double>(n)) / 1000.0;
    if (outMaxMs != nullptr) *outMaxMs = v.back() / 1000.0;
    if (outP99Ms != nullptr) {
        // p99 下标：n=100 时取第 99 个（0-based 98）; n<100 时退化为最大值。
        std::size_t idx = static_cast<std::size_t>(static_cast<double>(n) * 0.99);
        if (idx >= n) idx = n - 1;
        *outP99Ms = v[idx] / 1000.0;
    }
}

void perfReset() {
    for (int i = 0; i < kPerfStageCount; ++i) {
        StageBuffer& buf = bufferFor(static_cast<PerfStage>(i));
        buf.writes.store(0, std::memory_order_relaxed);
        std::memset(buf.samples, 0, sizeof(buf.samples));
    }
}

double ScopedTimer::nowUs() {
    using clock = std::chrono::steady_clock;
    const auto now = clock::now().time_since_epoch();
    return static_cast<double>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(now).count()) / 1000.0;
}

}  // namespace core
}  // namespace nairoclip
