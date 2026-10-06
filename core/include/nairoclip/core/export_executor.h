#pragma once

#include "nairoclip/core/export_plan.h"
#include "nairoclip/core/media_io.h"
#include "nairoclip/core/project.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// OutputVideoFrame / OutputAudioBlock 已移至 media_io.h（基础类型）。

/// 导出执行器：把 ExportPlan 逐帧/逐块驱动渲染，
/// 通过回调把输出交给平台编码器（FFmpeg/MediaCodec）。
///
/// 职责边界：
///  - 执行器负责「读源 → 时间映射 → 组装输出」；
///  - 具体解码由注入的 MediaReaderFactory 完成（平台层提供）；
///  - 具体编码由 FrameSink / AudioSink 回调完成（平台层提供）。
///
/// 进度回调返回 false 表示取消（用于用户取消导出）。
struct ExportExecutorOptions {
    /// 是否输出视频（false 时只跑音频/字幕校验路径）。
    bool outputVideo = true;
    /// 是否输出音频。
    bool outputAudio = true;
    /// 视频编码帧率覆盖（0 = 用序列帧率）。
    int overrideFps = 0;
    /// 最大输出帧数（0 = 不限制；测试用保护）。
    std::int64_t maxFrames = 0;
};

/// 执行统计。
struct ExportStats {
    std::int64_t framesWritten = 0;
    std::int64_t audioBlocksWritten = 0;
    std::int64_t clipsVisited = 0;
    std::int64_t clipsSkipped = 0;
    std::vector<std::string> warnings;
    bool cancelled = false;
};

/// 执行导出。
/// - plan 由 compileExportPlan 生成；
/// - readerFactory 用于打开每个素材（同一素材重复打开由执行器缓存）；
/// - videoSink 每输出一帧调用一次；audioSink 每输出一块调用一次；
/// - progress（0.0-1.0）返回 false 取消导出。
ExportStats executeExport(
    const ExportPlan& plan, MediaReaderFactory& readerFactory,
    const std::function<void(const OutputVideoFrame&)>& videoSink,
    const std::function<void(const OutputAudioBlock&)>& audioSink,
    const std::function<bool(double)>& progress = nullptr,
    const ExportExecutorOptions& options = {});

}  // namespace core
}  // namespace nairoclip
