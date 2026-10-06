#pragma once

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/media_writer.h"

#include <memory>

namespace nairoclip {
namespace core {

/// FFmpeg 媒体写入器（真实编码：H.264 + AAC → MP4）。
///
/// 通过 libavcodec（编码器）+ libswscale（RGBA→YUV420P）+
/// libswresample（float→AAC 采样格式）实现。
class FFmpegMediaWriter : public MediaWriter {
public:
    FFmpegMediaWriter();
    ~FFmpegMediaWriter() override;

    FFmpegMediaWriter(const FFmpegMediaWriter&) = delete;
    FFmpegMediaWriter& operator=(const FFmpegMediaWriter&) = delete;

    Result open(const std::string& path, const WriterConfig& config) override;
    Result writeVideoFrame(const OutputVideoFrame& frame) override;
    Result writeAudioBlock(const OutputAudioBlock& block) override;
    Result finish() override;
    void abort() override;
    bool isOpen() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace core
}  // namespace nairoclip

#endif  // NAIROCLIP_HAS_FFMPEG
