#include "nairoclip/core/export_runner.h"

namespace nairoclip {
namespace core {

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
    const Result openResult = writer->open(config.outputPath, writerConfig);
    if (!openResult.isOk()) {
        result.result = openResult;
        return result;
    }

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
    const Result finishResult = writer->finish();
    if (!finishResult.isOk()) {
        writer->abort();
        result.result = finishResult;
        return result;
    }
    result.fileWritten = true;
    result.result = Result::ok();
    return result;
}

}  // namespace core
}  // namespace nairoclip
