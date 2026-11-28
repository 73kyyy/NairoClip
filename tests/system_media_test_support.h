#pragma once

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/media_ffmpeg_writer.h"

#include <cstdio>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace nairoclip::core::test_support {

// Probe the exact system encoder path used by FFmpegMediaWriter, including
// pixel-format acceptance and one real frame submission. CI hosts often expose
// VAAPI/QSV encoder names without a usable device; name enumeration alone is
// not proof that the encoder can actually export. Never enable a bundled
// software encoder to make an integration test pass.
inline bool hasUsableSystemVideoEncoder(const std::string& codec, int width, int height,
                                        FrameRate frameRate = fps30()) {
    if (codec.empty() || width <= 0 || height <= 0 ||
        static_cast<std::size_t>(width) >
            std::numeric_limits<std::size_t>::max() /
                static_cast<std::size_t>(height) / 4U) {
        return false;
    }

    static std::uint64_t probeIndex = 0;
    const std::string path = "/tmp/media_gen/nc_system_encoder_probe_" +
                              std::to_string(++probeIndex) + ".mp4";
    WriterConfig config;
    config.width = width;
    config.height = height;
    config.frameRate = frameRate;
    config.videoCodec = codec;
    config.includeAudio = false;

    FFmpegMediaWriter writer;
    const Result opened = writer.open(path, config);
    if (!opened.isOk()) {
        std::printf("[SKIP] 系统视频编码器不可用（%s）：%s\\n",
                    codec.c_str(), opened.message.c_str());
        writer.abort();
        std::remove(path.c_str());
        return false;
    }

    OutputVideoFrame frame;
    frame.timestamp = Rational::make(0);
    frame.width = width;
    frame.height = height;
    frame.rgba.assign(static_cast<std::size_t>(width) *
                          static_cast<std::size_t>(height) * 4U,
                      0U);
    for (std::size_t alpha = 3; alpha < frame.rgba.size(); alpha += 4) {
        frame.rgba[alpha] = 255U;
    }

    const Result written = writer.writeVideoFrame(frame);
    const Result finished = written.isOk() ? writer.finish() : written;
    const bool usable = written.isOk() && finished.isOk();
    if (!usable) {
        const Result& failure = written.isOk() ? finished : written;
        std::printf("[SKIP] 系统视频编码器不能完成当前帧配置（%s）：%s\\n",
                    codec.c_str(), failure.message.c_str());
    }
    writer.abort();
    std::remove(path.c_str());
    return usable;
}

}  // namespace nairoclip::core::test_support

#endif  // NAIROCLIP_HAS_FFMPEG
