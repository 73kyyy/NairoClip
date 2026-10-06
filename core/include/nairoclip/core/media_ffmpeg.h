#pragma once

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/media_io.h"

#include <memory>

namespace nairoclip {
namespace core {

/// FFmpeg 媒体读取器（真实解码实现）。
///
/// 依赖：libavformat / libavcodec / libavutil / libswscale / libswresample。
/// 构建时通过 NAIROCLIP_HAS_FFMPEG 宏控制是否可用。
///
/// 能力：
///  - 打开容器（MP4/MOV/MKV/WebM/MP3/WAV 等 FFmpeg 支持的格式）
///  - 探测：时长/帧率/分辨率/采样率/声道/编解码器名
///  - 视频：seek 到关键帧后解码，输出 RGBA8 帧
///  - 音频：seek 后解码，输出 float 交错采样块
///
/// 线程约束：单实例单线程使用（与 MediaReader 接口一致）。
class FFmpegMediaReader : public MediaReader {
public:
    FFmpegMediaReader();
    ~FFmpegMediaReader() override;

    FFmpegMediaReader(const FFmpegMediaReader&) = delete;
    FFmpegMediaReader& operator=(const FFmpegMediaReader&) = delete;

    Result open(const std::string& path) override;
    void close() override;
    bool isOpen() const override;
    const MediaProbeResult& probe() const override;
    Result seek(const Time& sourceTime) override;
    bool readVideoFrame(VideoFrame& out) override;
    bool readAudioBlock(AudioBlock& out) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// FFmpeg 读取器工厂。
class FFmpegReaderFactory : public MediaReaderFactory {
public:
    std::unique_ptr<MediaReader> create() override { return std::make_unique<FFmpegMediaReader>(); }
};

/// FFmpeg 是否可用（编译期常量，运行时总是返回 true；用于测试跳过逻辑）。
bool ffmpegAvailable();

}  // namespace core
}  // namespace nairoclip

#endif  // NAIROCLIP_HAS_FFMPEG
