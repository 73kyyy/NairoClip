#include "nairoclip/core/media_io.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <utility>

namespace nairoclip {
namespace core {

namespace {

std::uint16_t readLe16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t readLe32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

struct WavPcmReader::Impl {
    std::ifstream file;
    MediaProbeResult probe;
    std::uint64_t dataOffset = 0;
    std::uint64_t dataBytes = 0;
    std::uint64_t totalFrames = 0;
    std::uint64_t cursorFrame = 0;
    std::uint16_t formatTag = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint16_t blockAlign = 0;
    bool open = false;
};

WavPcmReader::WavPcmReader() : impl_(std::make_unique<Impl>()) {}
WavPcmReader::~WavPcmReader() { close(); }

Result WavPcmReader::open(const std::string& path) {
    close();
    if (path.empty()) return Result::fail(ErrorCode::InvalidArgument, "WAV 路径为空");
    Impl& st = *impl_;
    st.file.open(path, std::ios::binary);
    if (!st.file) return Result::fail(ErrorCode::NotFound, "无法打开 WAV 文件");

    std::uint8_t riff[12]{};
    st.file.read(reinterpret_cast<char*>(riff), sizeof(riff));
    if (st.file.gcount() != static_cast<std::streamsize>(sizeof(riff)) ||
        std::memcmp(riff, "RIFF", 4) != 0 || std::memcmp(riff + 8, "WAVE", 4) != 0) {
        close();
        return Result::fail(ErrorCode::Unsupported, "不是 RIFF/WAVE 文件");
    }

    bool haveFmt = false;
    bool haveData = false;
    std::uint64_t fileEnd = 0;
    st.file.seekg(0, std::ios::end);
    const auto endPos = st.file.tellg();
    if (endPos < 0) {
        close();
        return Result::fail(ErrorCode::InvalidArgument, "无法读取 WAV 文件长度");
    }
    fileEnd = static_cast<std::uint64_t>(endPos);
    st.file.seekg(12, std::ios::beg);

    while (st.file) {
        const auto chunkPos = st.file.tellg();
        if (chunkPos < 0 || static_cast<std::uint64_t>(chunkPos) + 8U > fileEnd) break;
        std::uint8_t chunk[8]{};
        st.file.read(reinterpret_cast<char*>(chunk), sizeof(chunk));
        if (st.file.gcount() != static_cast<std::streamsize>(sizeof(chunk))) break;
        const std::uint32_t chunkSize = readLe32(chunk + 4);
        const auto payloadPos = st.file.tellg();
        if (payloadPos < 0) {
            close();
            return Result::fail(ErrorCode::Unsupported, "WAV chunk 位置无效");
        }
        const std::uint64_t chunkStart = static_cast<std::uint64_t>(payloadPos);
        if (chunkStart > fileEnd || chunkSize > fileEnd - chunkStart) {
            close();
            return Result::fail(ErrorCode::Unsupported, "WAV chunk 超出文件边界");
        }
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (chunkSize < 16U) {
                close();
                return Result::fail(ErrorCode::Unsupported, "WAV fmt chunk 长度无效");
            }
            std::uint8_t fmt[40]{};
            const auto toRead = static_cast<std::streamsize>(std::min<std::uint32_t>(chunkSize, sizeof(fmt)));
            st.file.read(reinterpret_cast<char*>(fmt), toRead);
            if (st.file.gcount() < 16) {
                close();
                return Result::fail(ErrorCode::Unsupported, "WAV fmt chunk 不完整");
            }
            st.formatTag = readLe16(fmt);
            const std::uint16_t channels = readLe16(fmt + 2);
            const std::uint32_t sampleRate = readLe32(fmt + 4);
            st.blockAlign = readLe16(fmt + 12);
            st.bitsPerSample = readLe16(fmt + 14);
            if (st.formatTag == 0xFFFE) {
                if (chunkSize < 40U || toRead < 40 ||
                    readLe16(fmt + 16) < 22U ||
                    readLe16(fmt + 18) != st.bitsPerSample) {
                    close();
                    return Result::fail(ErrorCode::Unsupported,
                                        "WAV extensible fmt chunk 参数无效");
                }
                static constexpr std::uint8_t kPcmGuid[16] = {
                    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
                static constexpr std::uint8_t kFloatGuid[16] = {
                    0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
                    0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
                if (std::memcmp(fmt + 24, kPcmGuid, sizeof(kPcmGuid)) == 0) {
                    st.formatTag = 1;
                } else if (std::memcmp(fmt + 24, kFloatGuid, sizeof(kFloatGuid)) == 0) {
                    st.formatTag = 3;
                } else {
                    close();
                    return Result::fail(ErrorCode::Unsupported,
                                        "WAV extensible 子格式不是 PCM/IEEE float");
                }
            }
            const bool pcmInteger = st.formatTag == 1 &&
                (st.bitsPerSample == 8 || st.bitsPerSample == 16 ||
                 st.bitsPerSample == 24 || st.bitsPerSample == 32);
            const bool pcmFloat = st.formatTag == 3 && st.bitsPerSample == 32;
            if ((!pcmInteger && !pcmFloat) || channels == 0 || channels > 8 ||
                sampleRate == 0 || sampleRate > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
                st.blockAlign != channels * (st.bitsPerSample / 8U)) {
                close();
                return Result::fail(ErrorCode::Unsupported,
                                    "WAV 仅支持 PCM 8/16/24/32-bit 或 float32");
            }
            st.probe.type = MediaType::Audio;
            st.probe.sampleRate = static_cast<int>(sampleRate);
            st.probe.channels = static_cast<int>(channels);
            st.probe.codec = pcmFloat ? "pcm_f32le" : "pcm_s" + std::to_string(st.bitsPerSample) + "le";
            haveFmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            st.dataOffset = chunkStart;
            st.dataBytes = chunkSize;
            haveData = true;
        }
        const std::uint64_t next = chunkStart + chunkSize + (chunkSize & 1U);
        if (next > fileEnd) break;
        st.file.seekg(static_cast<std::streamoff>(next), std::ios::beg);
    }

    if (!haveFmt || !haveData || st.blockAlign == 0 || st.probe.sampleRate <= 0) {
        close();
        return Result::fail(ErrorCode::Unsupported, "WAV 缺少有效 fmt 或 data chunk");
    }
    st.totalFrames = st.dataBytes / st.blockAlign;
    if (st.totalFrames == 0) {
        close();
        return Result::fail(ErrorCode::Unsupported, "WAV 音频数据为空");
    }
    st.probe.duration = Rational::make(static_cast<std::int64_t>(st.totalFrames),
                                       st.probe.sampleRate);
    st.cursorFrame = 0;
    st.open = true;
    return Result::ok();
}

void WavPcmReader::close() {
    if (!impl_) return;
    if (impl_->file.is_open()) impl_->file.close();
    // std::ifstream::close() 不会自动清除此前 read/seek 产生的 failbit。
    // Reader 被复用时必须 clear()，否则一次非 WAV 探测失败会污染后续 WAV open。
    impl_->file.clear();
    impl_->probe = {};
    impl_->dataOffset = impl_->dataBytes = impl_->totalFrames = impl_->cursorFrame = 0;
    impl_->formatTag = impl_->bitsPerSample = impl_->blockAlign = 0;
    impl_->open = false;
}

bool WavPcmReader::isOpen() const { return impl_ && impl_->open; }
const MediaProbeResult& WavPcmReader::probe() const { return impl_->probe; }

Result WavPcmReader::seek(const Time& sourceTime) {
    if (!isOpen()) return Result::fail(ErrorCode::InvalidArgument, "WAV 尚未打开");
    if (sourceTime.isNegative()) return Result::fail(ErrorCode::OutOfRange, "WAV seek 时间为负");
    const double seconds = sourceTime.toDouble();
    const long double frame = static_cast<long double>(seconds) * impl_->probe.sampleRate;
    impl_->cursorFrame = std::min<std::uint64_t>(
        impl_->totalFrames, frame >= static_cast<long double>(impl_->totalFrames)
                                ? impl_->totalFrames
                                : static_cast<std::uint64_t>(frame));
    return Result::ok();
}

bool WavPcmReader::readVideoFrame(VideoFrame&) { return false; }

bool WavPcmReader::readAudioBlock(AudioBlock& out) {
    if (!isOpen() || impl_->cursorFrame >= impl_->totalFrames) return false;
    Impl& st = *impl_;
    constexpr std::uint64_t kFramesPerBlock = 1024;
    const std::uint64_t frames = std::min(kFramesPerBlock, st.totalFrames - st.cursorFrame);
    const std::uint64_t byteCount = frames * st.blockAlign;
    if (byteCount > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) return false;
    std::vector<std::uint8_t> raw(static_cast<std::size_t>(byteCount));
    const std::uint64_t offset = st.dataOffset + st.cursorFrame * st.blockAlign;
    st.file.clear();
    st.file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    st.file.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    if (st.file.gcount() != static_cast<std::streamsize>(raw.size())) return false;

    const std::size_t channels = static_cast<std::size_t>(st.probe.channels);
    const std::size_t bytesPerSample = st.bitsPerSample / 8U;
    out.start = Rational::make(static_cast<std::int64_t>(st.cursorFrame), st.probe.sampleRate);
    out.sampleRate = st.probe.sampleRate;
    out.channels = st.probe.channels;
    out.samples.resize(static_cast<std::size_t>(frames) * channels);
    for (std::size_t i = 0; i < out.samples.size(); ++i) {
        const std::uint8_t* p = raw.data() + i * bytesPerSample;
        float sample = 0.0f;
        if (st.formatTag == 3) {
            std::uint32_t bits = readLe32(p);
            std::memcpy(&sample, &bits, sizeof(sample));
            if (!std::isfinite(sample)) sample = 0.0f;
        } else if (st.bitsPerSample == 8) {
            sample = (static_cast<int>(p[0]) - 128) / 128.0f;
        } else if (st.bitsPerSample == 16) {
            const std::uint32_t rawValue = readLe16(p);
            const std::int32_t value = rawValue >= 0x8000U
                                           ? static_cast<std::int32_t>(rawValue) - 0x10000
                                           : static_cast<std::int32_t>(rawValue);
            sample = static_cast<float>(value / 32768.0);
        } else if (st.bitsPerSample == 24) {
            std::int32_t value = static_cast<std::int32_t>(p[0]) |
                (static_cast<std::int32_t>(p[1]) << 8) |
                (static_cast<std::int32_t>(p[2]) << 16);
            if ((value & 0x800000) != 0) value -= 0x1000000;
            sample = static_cast<float>(value / 8388608.0);
        } else {
            const std::uint32_t rawValue = readLe32(p);
            const std::int64_t value = rawValue >= 0x80000000U
                                           ? static_cast<std::int64_t>(rawValue) - 0x100000000LL
                                           : static_cast<std::int64_t>(rawValue);
            sample = static_cast<float>(static_cast<double>(value) / 2147483648.0);
        }
        out.samples[i] = std::max(-1.0f, std::min(1.0f, sample));
    }
    st.cursorFrame += frames;
    return true;
}

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
    std::uint8_t gray = config_.grayValue;
    if (config_.grayByTime) {
        const long ms = static_cast<long>(out.timestamp.toDouble() * 1000.0);
        gray = static_cast<std::uint8_t>((ms / 100) & 0xFF);
    }
    out.pixels.assign(static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(out.height),
                      gray);
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
