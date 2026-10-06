#pragma once

#include "nairoclip/core/export_executor.h"
#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/media_writer.h"
#include "nairoclip/core/project.h"

#include <functional>
#include <memory>
#include <string>

namespace nairoclip {
namespace core {

// =============================================================================
// 导出运行器（Export Runner）
// =============================================================================
// 职责：把「导出计划 + 读取器工厂 + 写入器工厂」组装成一次完整导出：
//
//   ExportPlan ──► executeExport ──► videoSink/audioSink ──► MediaWriter
//        ▲                                                        │
//   MediaReaderFactory（解码）                            finish/abort
//
// 平台差异：Android 用 MediaCodec 写入器，桌面用 FFmpeg 写入器，
// 接口一致（MediaWriter）。

/// 导出运行配置。
struct ExportRunConfig {
    std::string outputPath;
    WriterConfig writer;
    ExportExecutorOptions executor;
};

/// 导出运行结果。
struct ExportRunResult {
    Result result;
    ExportStats stats;
    bool fileWritten = false;
};

/// 进度回调：progress ∈ [0,1]，返回 false 取消。
using ExportProgressFn = std::function<bool(double)>;

/// 执行完整导出。
/// - plan：由 compileExportPlan 生成；
/// - readerFactory：解码器工厂（如 FFmpegReaderFactory）；
/// - writerFactory：编码器工厂（如 FFmpegMediaWriter）；
/// - 失败/取消时写入器 abort()，成功时 finish()。
/// project：可选（提供时启用真实音频混音；为 nullptr 时音频走简化路径）。
ExportRunResult runExport(const ExportPlan& plan, MediaReaderFactory& readerFactory,
                          MediaWriterFactory& writerFactory, const ExportRunConfig& config,
                          const ExportProgressFn& progress = nullptr,
                          const Project* project = nullptr);

}  // namespace core
}  // namespace nairoclip
