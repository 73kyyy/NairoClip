#include "nairoclip/core/media_ffmpeg.h"

#ifdef NAIROCLIP_HAS_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cmath>
#include <cstring>

namespace nairoclip {
namespace core {

namespace {

std::string avErrorString(int err) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buffer, sizeof(buffer));
    return buffer;
}

/// 取有理数帧率。
/// 策略：以 r_frame_rate（标称帧率）为主；仅当标称帧率无效时才用 avg_frame_rate。
/// 理由：容器量化会让 avg_frame_rate 产生漂移（如 30fps 被记为 900/29），
/// 时间轴映射必须用标称帧率，否则逐帧步进会漂移。
FrameRate frameRateOf(const AVStream* stream) {
    AVRational rate = stream->r_frame_rate;
    if (rate.num <= 0 || rate.den <= 0) {
        rate = stream->avg_frame_rate;
    }
    if (rate.num <= 0 || rate.den <= 0) {
        return fps30();
    }
    return FrameRate::of(rate.num, rate.den);
}

}  // namespace

struct FFmpegMediaReader::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* videoCodec = nullptr;
    AVCodecContext* audioCodec = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int videoStream = -1;
    int audioStream = -1;
    MediaProbeResult probe;
    bool open = false;
    // 当前解码帧缓存（供 readVideoFrame 消费）
    bool frameReady = false;
    Time framePts;
    // 重采样输出格式
    int outSampleRate = 0;
    int outChannels = 0;
    // seek 目标
    Time seekTarget;
    bool afterSeek = false;
    // 解码器冲刷标志（输入 EOF 后发送空包取缓冲帧）
    bool videoEof = false;
    bool audioEof = false;

    void reset() {
        if (format != nullptr) {
            avformat_close_input(&format);
        }
        if (videoCodec != nullptr) {
            avcodec_free_context(&videoCodec);
        }
        if (audioCodec != nullptr) {
            avcodec_free_context(&audioCodec);
        }
        if (sws != nullptr) {
            sws_freeContext(sws);
            sws = nullptr;
        }
        if (swr != nullptr) {
            swr_free(&swr);
        }
        if (frame != nullptr) {
            av_frame_free(&frame);
        }
        if (packet != nullptr) {
            av_packet_free(&packet);
        }
        videoStream = -1;
        audioStream = -1;
        open = false;
        frameReady = false;
    }
};

FFmpegMediaReader::FFmpegMediaReader() : impl_(std::make_unique<Impl>()) {}

FFmpegMediaReader::~FFmpegMediaReader() { close(); }

Result FFmpegMediaReader::open(const std::string& path) {
    close();
    Impl& impl = *impl_;

    int err = avformat_open_input(&impl.format, path.c_str(), nullptr, nullptr);
    if (err < 0) {
        return Result::fail(ErrorCode::NotFound, "无法打开文件: " + avErrorString(err));
    }
    err = avformat_find_stream_info(impl.format, nullptr);
    if (err < 0) {
        close();
        return Result::fail(ErrorCode::Unsupported, "无法读取流信息: " + avErrorString(err));
    }

    // 找最佳视频/音频流
    impl.videoStream = av_find_best_stream(impl.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    impl.audioStream = av_find_best_stream(impl.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (impl.videoStream < 0 && impl.audioStream < 0) {
        close();
        return Result::fail(ErrorCode::Unsupported, "文件中没有可用的音视频流");
    }

    // 打开视频解码器
    if (impl.videoStream >= 0) {
        AVStream* stream = impl.format->streams[impl.videoStream];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec == nullptr) {
            close();
            return Result::fail(ErrorCode::Unsupported, "不支持的视频编解码器");
        }
        impl.videoCodec = avcodec_alloc_context3(codec);
        avcodec_parameters_to_context(impl.videoCodec, stream->codecpar);
        impl.videoCodec->pkt_timebase = stream->time_base;
        err = avcodec_open2(impl.videoCodec, codec, nullptr);
        if (err < 0) {
            close();
            return Result::fail(ErrorCode::Unsupported,
                                "视频解码器打开失败: " + avErrorString(err));
        }
        impl.probe.type = impl.audioStream >= 0 ? MediaType::Video : MediaType::Video;
        impl.probe.width = impl.videoCodec->width;
        impl.probe.height = impl.videoCodec->height;
        impl.probe.frameRate = frameRateOf(stream);
        impl.probe.codec = avcodec_get_name(codec->id);
    }

    // 打开音频解码器
    if (impl.audioStream >= 0) {
        AVStream* stream = impl.format->streams[impl.audioStream];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec != nullptr) {
            impl.audioCodec = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(impl.audioCodec, stream->codecpar);
            impl.audioCodec->pkt_timebase = stream->time_base;
            err = avcodec_open2(impl.audioCodec, codec, nullptr);
            if (err < 0) {
                avcodec_free_context(&impl.audioCodec);
                impl.audioCodec = nullptr;
            } else {
                impl.probe.sampleRate = impl.audioCodec->sample_rate;
                impl.probe.channels = impl.audioCodec->ch_layout.nb_channels;
                if (impl.videoStream < 0) {
                    impl.probe.type = MediaType::Audio;
                }
            }
        }
    }

    // 时长
    if (impl.format->duration > 0) {
        impl.probe.duration = Rational::make(impl.format->duration, AV_TIME_BASE);
    }

    impl.frame = av_frame_alloc();
    impl.packet = av_packet_alloc();
    if (impl.frame == nullptr || impl.packet == nullptr) {
        close();
        return Result::fail(ErrorCode::Internal, "内存分配失败");
    }
    impl.open = true;
    impl.afterSeek = false;
    return Result::ok();
}

void FFmpegMediaReader::close() {
    if (impl_ != nullptr) {
        impl_->reset();
    }
}

bool FFmpegMediaReader::isOpen() const { return impl_ != nullptr && impl_->open; }

const MediaProbeResult& FFmpegMediaReader::probe() const { return impl_->probe; }

Result FFmpegMediaReader::seek(const Time& sourceTime) {
    Impl& impl = *impl_;
    if (!impl.open) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    if (sourceTime.isNegative()) {
        return Result::fail(ErrorCode::OutOfRange, "seek 目标为负");
    }
    const std::int64_t targetUs = toMicroseconds(sourceTime);
    // av_seek_frame 以 AV_TIME_BASE 为单位（微秒）
    const int err = av_seek_frame(impl.format, -1, targetUs, AVSEEK_FLAG_BACKWARD);
    if (err < 0) {
        return Result::fail(ErrorCode::OutOfRange, "seek 失败: " + avErrorString(err));
    }
    if (impl.videoCodec != nullptr) {
        avcodec_flush_buffers(impl.videoCodec);
    }
    if (impl.audioCodec != nullptr) {
        avcodec_flush_buffers(impl.audioCodec);
    }
    impl.seekTarget = sourceTime;
    impl.afterSeek = true;
    impl.frameReady = false;
    impl.videoEof = false;
    impl.audioEof = false;
    return Result::ok();
}

bool FFmpegMediaReader::readVideoFrame(VideoFrame& out) {
    Impl& impl = *impl_;
    if (!impl.open || impl.videoStream < 0) {
        return false;
    }
    AVStream* stream = impl.format->streams[impl.videoStream];

    // 循环解码直到拿到目标帧
    while (true) {
        const int err = avcodec_receive_frame(impl.videoCodec, impl.frame);
        if (err == 0) {
            // 计算帧时间戳（秒 → 微秒有理数）
            const std::int64_t pts = impl.frame->best_effort_timestamp;
            const std::int64_t tsUs = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
            const Time frameTime = Rational::make(tsUs, AV_TIME_BASE);
            // seek 后跳过 target 之前的帧
            if (impl.afterSeek && frameTime + timeAtFrame(impl.probe.frameRate, 0) < impl.seekTarget) {
                av_frame_unref(impl.frame);
                continue;
            }
            impl.afterSeek = false;
            // 转换为 RGBA8
            const int width = impl.frame->width;
            const int height = impl.frame->height;
            if (width <= 0 || height <= 0) {
                av_frame_unref(impl.frame);
                continue;
            }
            const AVPixelFormat srcFormat = static_cast<AVPixelFormat>(impl.frame->format);
            impl.sws = sws_getCachedContext(impl.sws, width, height, srcFormat, width, height,
                                            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
                                            nullptr);
            if (impl.sws == nullptr) {
                av_frame_unref(impl.frame);
                return false;
            }
            out.timestamp = frameTime;
            out.width = width;
            out.height = height;
            out.stride = width * 4;
            out.pixels.resize(static_cast<std::size_t>(out.stride) *
                              static_cast<std::size_t>(height));
            std::uint8_t* dst[4] = {out.pixels.data(), nullptr, nullptr, nullptr};
            const int dstStride[4] = {out.stride, 0, 0, 0};
            sws_scale(impl.sws, impl.frame->data, impl.frame->linesize, 0, height, dst, dstStride);
            av_frame_unref(impl.frame);
            return true;
        }
        if (err == AVERROR(EAGAIN)) {
            // 需要喂包
            if (impl.videoEof) {
                return false;  // 已冲刷完解码器
            }
            const int readErr = av_read_frame(impl.format, impl.packet);
            if (readErr < 0) {
                // 输入 EOF：冲刷解码器（发送空包）以取出缓冲帧
                impl.videoEof = true;
                avcodec_send_packet(impl.videoCodec, nullptr);
                continue;
            }
            if (impl.packet->stream_index == impl.videoStream) {
                const int sendErr = avcodec_send_packet(impl.videoCodec, impl.packet);
                if (sendErr < 0 && sendErr != AVERROR(EAGAIN)) {
                    av_packet_unref(impl.packet);
                    return false;
                }
            }
            av_packet_unref(impl.packet);
            continue;
        }
        return false;  // 其他错误 / EOF
    }
}

bool FFmpegMediaReader::readAudioBlock(AudioBlock& out) {
    Impl& impl = *impl_;
    if (!impl.open || impl.audioStream < 0 || impl.audioCodec == nullptr) {
        return false;
    }
    AVStream* stream = impl.format->streams[impl.audioStream];
    const int outSampleRate =
        impl.probe.sampleRate > 0 ? impl.probe.sampleRate : 48000;
    const int outChannels = impl.probe.channels > 0 ? impl.probe.channels : 2;

    while (true) {
        const int err = avcodec_receive_frame(impl.audioCodec, impl.frame);
        if (err == 0) {
            // 设置重采样器（swr_alloc_set_opts2 就地初始化传入的指针）
            AVChannelLayout outLayout;
            av_channel_layout_default(&outLayout, outChannels);
            swr_free(&impl.swr);
            const int allocErr = swr_alloc_set_opts2(
                &impl.swr, &outLayout, AV_SAMPLE_FMT_FLT, outSampleRate,
                &impl.audioCodec->ch_layout, impl.audioCodec->sample_fmt,
                impl.audioCodec->sample_rate, 0, nullptr);
            av_channel_layout_uninit(&outLayout);
            if (allocErr < 0 || impl.swr == nullptr || swr_init(impl.swr) < 0) {
                av_frame_unref(impl.frame);
                return false;
            }
            const int maxOutSamples = static_cast<int>(
                av_rescale_rnd(swr_get_delay(impl.swr, impl.audioCodec->sample_rate) +
                                   impl.frame->nb_samples,
                               impl.audioCodec->sample_rate, outSampleRate, AV_ROUND_UP));
            out.sampleRate = outSampleRate;
            out.channels = outChannels;
            out.samples.resize(static_cast<std::size_t>(maxOutSamples) *
                               static_cast<std::size_t>(outChannels));
            std::uint8_t* outData[1] = {reinterpret_cast<std::uint8_t*>(out.samples.data())};
            const int converted = swr_convert(impl.swr, outData, maxOutSamples,
                                              const_cast<const std::uint8_t**>(impl.frame->data),
                                              impl.frame->nb_samples);
            if (converted < 0) {
                av_frame_unref(impl.frame);
                return false;
            }
            out.samples.resize(static_cast<std::size_t>(converted) *
                               static_cast<std::size_t>(outChannels));
            // 时间戳
            const std::int64_t pts = impl.frame->best_effort_timestamp;
            const std::int64_t tsUs = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
            out.start = Rational::make(tsUs, AV_TIME_BASE);
            av_frame_unref(impl.frame);
            return true;
        }
        if (err == AVERROR(EAGAIN)) {
            if (impl.audioEof) {
                return false;
            }
            const int readErr = av_read_frame(impl.format, impl.packet);
            if (readErr < 0) {
                impl.audioEof = true;
                avcodec_send_packet(impl.audioCodec, nullptr);
                continue;
            }
            if (impl.packet->stream_index == impl.audioStream) {
                const int sendErr = avcodec_send_packet(impl.audioCodec, impl.packet);
                if (sendErr < 0 && sendErr != AVERROR(EAGAIN)) {
                    av_packet_unref(impl.packet);
                    return false;
                }
            }
            av_packet_unref(impl.packet);
            continue;
        }
        return false;
    }
}

bool ffmpegAvailable() { return true; }

}  // namespace core
}  // namespace nairoclip

#endif  // NAIROCLIP_HAS_FFMPEG
