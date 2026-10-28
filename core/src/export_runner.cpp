#include "nairoclip/core/export_runner.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace nairoclip {
namespace core {

namespace {

/// 导出流程内的堆检查钩子（仅诊断用，默认关闭）。
///
/// 为什么放在这里：已确证 nc_export_run **返回时**堆已经坏了，
/// 但外层探针无法区分是 open / executeExport / finish 哪一步。
/// 由环境变量 NC_EXPORT_HEAP_PROBE=1 打开；打开后每个阶段之间
/// malloc/free 一批覆盖各尺寸档位的块，迫使 glibc 复核 chunk header。
///
/// 生产路径下这个函数只做一次 getenv（静态缓存），开销可忽略。
bool probeEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("NC_EXPORT_HEAP_PROBE");
        return v != nullptr && v[0] != '0';
    }();
    return enabled;
}

void exportHeapCheck(const char* where) {
    if (!probeEnabled()) return;
    constexpr int kN = 122;
    void* blocks[kN];
    std::size_t sizes[kN];
    int n = 0;
    for (int i = 0; i < 96; ++i, ++n) sizes[n] = 16 + static_cast<std::size_t>(i) * 21;
    for (int i = 0; i < 24; ++i, ++n) sizes[n] = 2048 + static_cast<std::size_t>(i) * 2600;
    sizes[n++] = 1048;
    sizes[n++] = 2048;
    for (int i = 0; i < n; ++i) {
        blocks[i] = std::malloc(sizes[i]);
        if (blocks[i] != nullptr) std::memset(blocks[i], 0xCD, sizes[i]);
    }
    for (int i = 0; i < n; ++i) std::free(blocks[i]);
    std::printf("[诊断] runExport: %s 后堆 OK\n", where);
    std::fflush(stdout);
}

}  // namespace

ExportRunResult runExport(const ExportPlan& plan, MediaReaderFactory& readerFactory,
                          MediaWriterFactory& writerFactory, const ExportRunConfig& config,
                          const ExportProgressFn& progress, const Project* project) {
    ExportRunResult result;

    // 0) 会员门控（文档 3.10）：免费/过期会员导出时强制降级分辨率 + 附加水印。
    //    - 降级只缩不放；仅当需要降级时复制计划（不影响常规路径）；
    //    - 水印经执行器逐帧应用（几何按最终输出帧尺寸计算）。
    const ExportPlan* renderPlan = &plan;
    ExportPlan clampedPlan;
    int clampedWidth = plan.width;
    int clampedHeight = plan.height;
    const bool resolutionClamped = clampSizeToShortSide(
        plan.width, plan.height, config.policy.maxExportShortSide, clampedWidth, clampedHeight);
    if (resolutionClamped) {
        clampedPlan = plan;
        clampedPlan.width = clampedWidth;
        clampedPlan.height = clampedHeight;
        renderPlan = &clampedPlan;
    }
    ExportExecutorOptions executorOptions = config.executor;
    if (config.policy.watermark.enabled) {
        // 门控水印优先于调用方自带的水印规格（门控是产品约束，不可被覆盖）
        executorOptions.watermark = config.policy.watermark;
    }

    // 1) 打开写入器
    auto writer = writerFactory.create();
    if (writer == nullptr) {
        result.result = Result::fail(ErrorCode::Internal, "写入器创建失败");
        return result;
    }
    WriterConfig writerConfig = config.writer;
    if (writerConfig.width <= 0) {
        writerConfig.width = renderPlan->width;
    }
    if (writerConfig.height <= 0) {
        writerConfig.height = renderPlan->height;
    }
    if (resolutionClamped) {
        // 门控降级生效：输出尺寸以门控结果为准（避免帧与文件尺寸不一致）
        writerConfig.width = clampedWidth;
        writerConfig.height = clampedHeight;
    }
    if (writerConfig.frameRate.toDouble() <= 0.0) {
        writerConfig.frameRate = renderPlan->frameRate;
    }
    exportHeapCheck("writer 创建后/未 open");
    const Result openResult = writer->open(config.outputPath, writerConfig);
    if (!openResult.isOk()) {
        result.result = openResult;
        return result;
    }

    exportHeapCheck("writer.open 之后");
    // 2) 驱动导出执行器
    bool sinkFailed = false;
    Result sinkError;
    result.stats = executeExport(
        *renderPlan, readerFactory,
        [&](const OutputVideoFrame& frame) {
            if (sinkFailed) return;
            const Result r = writer->writeVideoFrame(frame);
            if (!r.isOk()) {
                sinkFailed = true;
                sinkError = r;
            }
        },
        [&](const OutputAudioBlock& block) {
            if (sinkFailed) return;
            const Result r = writer->writeAudioBlock(block);
            if (!r.isOk()) {
                sinkFailed = true;
                sinkError = r;
            }
        },
        progress, executorOptions, project);

    exportHeapCheck("executeExport 之后");
    // 2a) 门控说明（供 UI 提示；如「免费版已降级 1080p 并附加水印」）
    if (resolutionClamped) {
        result.stats.warnings.push_back("会员门控：导出分辨率已降级为 " +
                                        std::to_string(clampedWidth) + "x" +
                                        std::to_string(clampedHeight));
    }
    if (executorOptions.watermark.enabled) {
        result.stats.warnings.push_back("会员门控：已附加导出水印");
    }

    // 3) 收尾
    if (result.stats.cancelled) {
        writer->abort();
        result.result = Result::fail(ErrorCode::Cancelled, "导出已取消");
        return result;
    }
    if (sinkFailed) {
        writer->abort();
        result.result = sinkError;
        return result;
    }
    exportHeapCheck("finish 之前");
    const Result finishResult = writer->finish();
    exportHeapCheck("finish 之后");
    if (!finishResult.isOk()) {
        writer->abort();
        result.result = finishResult;
        return result;
    }
    result.fileWritten = true;
    result.result = Result::ok();
    exportHeapCheck("runExport 返回前");
    return result;
}

}  // namespace core
}  // namespace nairoclip
