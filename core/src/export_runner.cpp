#include "nairoclip/core/export_runner.h"

namespace nairoclip {
namespace core {

ExportRunResult runExport(const ExportPlan& plan, MediaReaderFactory& readerFactory,
                          MediaWriterFactory& writerFactory, const ExportRunConfig& config,
                          const ExportProgressFn& progress, const Project* project) {
    ExportRunResult result;

    // 1) 打开写入器
    auto writer = writerFactory.create();
    if (writer == nullptr) {
        result.result = Result::fail(ErrorCode::Internal, "写入器创建失败");
        return result;
    }
    WriterConfig writerConfig = config.writer;
    if (writerConfig.width <= 0) {
        writerConfig.width = plan.width;
    }
    if (writerConfig.height <= 0) {
        writerConfig.height = plan.height;
    }
    if (writerConfig.frameRate.toDouble() <= 0.0) {
        writerConfig.frameRate = plan.frameRate;
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
        plan, readerFactory,
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
        progress, config.executor, project);

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
