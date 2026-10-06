#include "nairoclip/core/media_io.h"

#include <cmath>

namespace nairoclip {
namespace core {

MockMediaReader::MockMediaReader(Config config) : config_(std::move(config)) {}

Result MockMediaReader::open(const std::string& path) {
    if (config_.failOpen) {
        return Result::fail(ErrorCode::NotFound, "Mock 配置为打开失败");
    }
    if (path.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "路径为空");
    }
    open_ = true;
    ++openCount_;
    lastPath_ = path;
    nextVideoFrame_ = 0;
    nextAudioBlock_ = 0;
    seekBase_ = Rational::make(0);
    return Result::ok();
}

void MockMediaReader::close() { open_ = false; }

Result MockMediaReader::seek(const Time& sourceTime) {
    if (!open_) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    if (sourceTime.isNegative()) {
        return Result::fail(ErrorCode::OutOfRange, "seek 目标为负");
    }
    seekBase_ = sourceTime;
    nextVideoFrame_ = 0;
    nextAudioBlock_ = 0;
    return Result::ok();
}

bool MockMediaReader::readVideoFrame(VideoFrame& out) {
    if (!open_ || nextVideoFrame_ >= config_.videoFrameCount) {
        return false;
    }
    out.timestamp = seekBase_ + timeAtFrame(config_.probe.frameRate, nextVideoFrame_);
    out.width = config_.probe.width > 0 ? config_.probe.width : 16;
    out.height = config_.probe.height > 0 ? config_.probe.height : 16;
    out.stride = out.width * 4;
    out.pixels.assign(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(out.height),
                      config_.grayValue);
    // 设置 alpha 通道为 255
    for (std::size_t i = 3; i < out.pixels.size(); i += 4) {
        out.pixels[i] = 255;
    }
    ++nextVideoFrame_;
    return true;
}

bool MockMediaReader::readAudioBlock(AudioBlock& out) {
    if (!open_ || nextAudioBlock_ >= config_.audioBlockCount) {
        return false;
    }
    const int sampleRate = config_.probe.sampleRate > 0 ? config_.probe.sampleRate : 48000;
    const int channels = config_.probe.channels > 0 ? config_.probe.channels : 2;
    const int frames = config_.audioBlockFrames > 0 ? config_.audioBlockFrames : 1024;
    out.start = seekBase_ +
                Time{Rational::make(nextAudioBlock_ * frames, sampleRate)};
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels));
    // 生成 440Hz 正弦波（测试参考信号）：相位跟随绝对时间，
    // 保证 seek 后波形相位连续（与真实解码器一致）。
    const double baseSeconds = out.start.toDouble();
    for (int i = 0; i < frames; ++i) {
        const double t = baseSeconds + static_cast<double>(i) / sampleRate;
        const float v = static_cast<float>(0.25 * std::sin(2.0 * 3.141592653589793 * 440.0 * t));
        for (int ch = 0; ch < channels; ++ch) {
            out.samples[static_cast<std::size_t>(i) * static_cast<std::size_t>(channels) +
                        static_cast<std::size_t>(ch)] = v;
        }
    }
    ++nextAudioBlock_;
    return true;
}

}  // namespace core
}  // namespace nairoclip
