#include "nairoclip/core/media_ffmpeg_writer.h"

#ifdef NAIROCLIP_HAS_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <cstdio>
#include <cstring>

namespace nairoclip {
namespace core {

namespace {

std::string avErr(int err) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buffer, sizeof(buffer));
    return buffer;
}

}  // namespace

struct FFmpegMediaWriter::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* videoCodec = nullptr;
    AVCodecContext* audioCodec = nullptr;
    AVStream* videoStream = nullptr;
    AVStream* audioStream = nullptr;
    SwsContext* sws = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* videoFrame = nullptr;
    AVFrame* audioFrame = nullptr;
    AVPacket* packet = nullptr;
    WriterConfig config;
    std::string path;
    bool open = false;
    std::int64_t videoPts = 0;
    std::int64_t audioPts = 0;  // 以采样数为单位

    void reset() {
        if (videoCodec != nullptr) avcodec_free_context(&videoCodec);
        if (audioCodec != nullptr) avcodec_free_context(&audioCodec);
        if (sws != nullptr) {
            sws_freeContext(sws);
            sws = nullptr;
        }
        if (swr != nullptr) swr_free(&swr);
        if (videoFrame != nullptr) av_frame_free(&videoFrame);
        if (audioFrame != nullptr) av_frame_free(&audioFrame);
        if (packet != nullptr) av_packet_free(&packet);
        // 注意：avformat_free_context 不写 trailer；abort 场景直接关闭
        if (format != nullptr) {
            if (!(format->oformat->flags & AVFMT_NOFILE) && format->pb != nullptr) {
                avio_closep(&format->pb);
            }
            avformat_free_context(format);
            format = nullptr;
        }
        videoStream = nullptr;
        audioStream = nullptr;
        open = false;
    }
};

FFmpegMediaWriter::FFmpegMediaWriter() : impl_(std::make_unique<Impl>()) {}
FFmpegMediaWriter::~FFmpegMediaWriter() {
    if (impl_ != nullptr && impl_->open) {
        abort();  // 未 finish 的写入器：丢弃
    }
}

Result FFmpegMediaWriter::open(const std::string& path, const WriterConfig& config) {
    abort();
    Impl& impl = *impl_;
    impl.config = config;
    impl.path = path;

    int err = avformat_alloc_output_context2(&impl.format, nullptr, nullptr, path.c_str());
    if (err < 0 || impl.format == nullptr) {
        return Result::fail(ErrorCode::Internal, "无法创建输出上下文");
    }

    // ---- 视频编码器（H.264）----
    const AVCodec* videoEncoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (videoEncoder == nullptr) {
        return Result::fail(ErrorCode::Unsupported, "H.264 编码器不可用");
    }
    impl.videoCodec = avcodec_alloc_context3(videoEncoder);
    impl.videoCodec->width = config.width;
    impl.videoCodec->height = config.height;
    impl.videoCodec->time_base = AVRational{config.frameRate.value.den,
                                            config.frameRate.value.num};
    impl.videoCodec->framerate = AVRational{config.frameRate.value.num,
                                            config.frameRate.value.den};
    impl.videoCodec->pix_fmt = AV_PIX_FMT_YUV420P;
    impl.videoCodec->gop_size = static_cast<int>(config.frameRate.toDouble() * 2.0);
    impl.videoCodec->max_b_frames = 2;
    av_opt_set_int(impl.videoCodec->priv_data, "crf", config.crf, 0);
    av_opt_set(impl.videoCodec->priv_data, "preset", config.preset.c_str(), 0);

    impl.videoStream = avformat_new_stream(impl.format, nullptr);
    err = avcodec_open2(impl.videoCodec, videoEncoder, nullptr);
    if (err < 0) {
        return Result::fail(ErrorCode::Unsupported, "视频编码器打开失败: " + avErr(err));
    }
    avcodec_parameters_from_context(impl.videoStream->codecpar, impl.videoCodec);
    impl.videoStream->time_base = impl.videoCodec->time_base;

    // ---- 音频编码器（AAC）----
    const AVCodec* audioEncoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (config.includeAudio && audioEncoder != nullptr) {
        impl.audioCodec = avcodec_alloc_context3(audioEncoder);
        impl.audioCodec->sample_rate = config.audioSampleRate;
        AVChannelLayout layout;
        av_channel_layout_default(&layout, config.audioChannels);
        impl.audioCodec->ch_layout = layout;
        impl.audioCodec->sample_fmt = audioEncoder->sample_fmts != nullptr
                                          ? audioEncoder->sample_fmts[0]
                                          : AV_SAMPLE_FMT_FLTP;
        impl.audioCodec->bit_rate = config.audioBitrate;
        impl.audioCodec->time_base = AVRational{1, config.audioSampleRate};

        impl.audioStream = avformat_new_stream(impl.format, nullptr);
        err = avcodec_open2(impl.audioCodec, audioEncoder, nullptr);
        if (err < 0) {
            avcodec_free_context(&impl.audioCodec);
            impl.audioCodec = nullptr;
            impl.audioStream = nullptr;
        } else {
            avcodec_parameters_from_context(impl.audioStream->codecpar, impl.audioCodec);
            impl.audioStream->time_base = impl.audioCodec->time_base;
        }
    }

    if (!(impl.format->oformat->flags & AVFMT_NOFILE)) {
        err = avio_open(&impl.format->pb, path.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) {
            return Result::fail(ErrorCode::OutOfRange, "无法打开输出文件: " + avErr(err));
        }
    }
    err = avformat_write_header(impl.format, nullptr);
    if (err < 0) {
        return Result::fail(ErrorCode::Internal, "写文件头失败: " + avErr(err));
    }

    impl.videoFrame = av_frame_alloc();
    impl.videoFrame->format = impl.videoCodec->pix_fmt;
    impl.videoFrame->width = config.width;
    impl.videoFrame->height = config.height;
    av_frame_get_buffer(impl.videoFrame, 32);
    impl.packet = av_packet_alloc();
    impl.open = true;
    return Result::ok();
}

Result FFmpegMediaWriter::writeVideoFrame(const OutputVideoFrame& frame) {
    Impl& impl = *impl_;
    if (!impl.open) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    if (frame.rgba.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "帧数据为空");
    }
    // RGBA → YUV420P
    impl.sws = sws_getCachedContext(impl.sws, frame.width, frame.height, AV_PIX_FMT_RGBA,
                                    impl.config.width, impl.config.height, AV_PIX_FMT_YUV420P,
                                    SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (impl.sws == nullptr) {
        return Result::fail(ErrorCode::Internal, "缩放器创建失败");
    }
    av_frame_make_writable(impl.videoFrame);
    const std::uint8_t* src[4] = {frame.rgba.data(), nullptr, nullptr, nullptr};
    const int srcStride[4] = {frame.width * 4, 0, 0, 0};
    sws_scale(impl.sws, src, srcStride, 0, frame.height, impl.videoFrame->data,
              impl.videoFrame->linesize);
    impl.videoFrame->pts = impl.videoPts++;

    const int err = avcodec_send_frame(impl.videoCodec, impl.videoFrame);
    if (err < 0) {
        return Result::fail(ErrorCode::Internal, "视频帧编码失败: " + avErr(err));
    }
    while (true) {
        const int recvErr = avcodec_receive_packet(impl.videoCodec, impl.packet);
        if (recvErr == AVERROR(EAGAIN) || recvErr == AVERROR_EOF) {
            break;
        }
        if (recvErr < 0) {
            return Result::fail(ErrorCode::Internal, "视频包接收失败: " + avErr(recvErr));
        }
        av_packet_rescale_ts(impl.packet, impl.videoCodec->time_base,
                             impl.videoStream->time_base);
        impl.packet->stream_index = impl.videoStream->index;
        const int writeErr = av_interleaved_write_frame(impl.format, impl.packet);
        av_packet_unref(impl.packet);
        if (writeErr < 0) {
            return Result::fail(ErrorCode::Internal, "写视频包失败: " + avErr(writeErr));
        }
    }
    return Result::ok();
}

Result FFmpegMediaWriter::writeAudioBlock(const OutputAudioBlock& block) {
    Impl& impl = *impl_;
    if (!impl.open) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    if (impl.audioCodec == nullptr || block.samples.empty()) {
        return Result::ok();  // 无音频轨或空块：静默忽略
    }
    if (impl.audioFrame == nullptr) {
        impl.audioFrame = av_frame_alloc();
        impl.audioFrame->format = impl.audioCodec->sample_fmt;
        impl.audioFrame->ch_layout = impl.audioCodec->ch_layout;
        impl.audioFrame->sample_rate = impl.audioCodec->sample_rate;
        impl.audioFrame->nb_samples = impl.audioCodec->frame_size > 0
                                          ? impl.audioCodec->frame_size
                                          : 1024;
        av_frame_get_buffer(impl.audioFrame, 0);
    }
    // 重采样：float 交错 → 编码器采样格式
    if (impl.swr == nullptr) {
        AVChannelLayout inLayout;
        av_channel_layout_default(&inLayout, block.channels);
        const int allocErr = swr_alloc_set_opts2(
            &impl.swr, &impl.audioCodec->ch_layout, impl.audioCodec->sample_fmt,
            impl.audioCodec->sample_rate, &inLayout, AV_SAMPLE_FMT_FLT, block.sampleRate, 0,
            nullptr);
        av_channel_layout_uninit(&inLayout);
        if (allocErr < 0 || impl.swr == nullptr || swr_init(impl.swr) < 0) {
            return Result::fail(ErrorCode::Internal, "音频重采样器创建失败");
        }
    }
    const int frameSize = impl.audioFrame->nb_samples;
    const std::uint8_t* inData[1] = {reinterpret_cast<const std::uint8_t*>(block.samples.data())};
    const int inSamples =
        static_cast<int>(block.samples.size() / static_cast<std::size_t>(block.channels));
    int consumed = 0;
    while (consumed < inSamples) {
        av_frame_make_writable(impl.audioFrame);
        const int outSamples = swr_convert(impl.swr, impl.audioFrame->data, frameSize, inData,
                                           inSamples - consumed);
        if (outSamples <= 0) {
            break;
        }
        impl.audioFrame->nb_samples = outSamples;
        impl.audioFrame->pts = impl.audioPts;
        impl.audioPts += outSamples;
        const int err = avcodec_send_frame(impl.audioCodec, impl.audioFrame);
        if (err < 0) {
            return Result::fail(ErrorCode::Internal, "音频帧编码失败: " + avErr(err));
        }
        while (true) {
            const int recvErr = avcodec_receive_packet(impl.audioCodec, impl.packet);
            if (recvErr == AVERROR(EAGAIN) || recvErr == AVERROR_EOF) break;
            if (recvErr < 0) {
                return Result::fail(ErrorCode::Internal, "音频包接收失败");
            }
            av_packet_rescale_ts(impl.packet, impl.audioCodec->time_base,
                                 impl.audioStream->time_base);
            impl.packet->stream_index = impl.audioStream->index;
            const int writeErr = av_interleaved_write_frame(impl.format, impl.packet);
            av_packet_unref(impl.packet);
            if (writeErr < 0) {
                return Result::fail(ErrorCode::Internal, "写音频包失败");
            }
        }
        consumed += outSamples;  // 简化：假设一次转换消费完
        if (outSamples < frameSize) {
            break;  // 输入耗尽
        }
    }
    return Result::ok();
}

Result FFmpegMediaWriter::finish() {
    Impl& impl = *impl_;
    if (!impl.open) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    // 冲刷视频
    avcodec_send_frame(impl.videoCodec, nullptr);
    while (true) {
        const int recvErr = avcodec_receive_packet(impl.videoCodec, impl.packet);
        if (recvErr == AVERROR(EAGAIN) || recvErr == AVERROR_EOF) break;
        if (recvErr < 0) break;
        av_packet_rescale_ts(impl.packet, impl.videoCodec->time_base,
                             impl.videoStream->time_base);
        impl.packet->stream_index = impl.videoStream->index;
        av_interleaved_write_frame(impl.format, impl.packet);
        av_packet_unref(impl.packet);
    }
    // 冲刷音频
    if (impl.audioCodec != nullptr) {
        avcodec_send_frame(impl.audioCodec, nullptr);
        while (true) {
            const int recvErr = avcodec_receive_packet(impl.audioCodec, impl.packet);
            if (recvErr == AVERROR(EAGAIN) || recvErr == AVERROR_EOF) break;
            if (recvErr < 0) break;
            av_packet_rescale_ts(impl.packet, impl.audioCodec->time_base,
                                 impl.audioStream->time_base);
            impl.packet->stream_index = impl.audioStream->index;
            av_interleaved_write_frame(impl.format, impl.packet);
            av_packet_unref(impl.packet);
        }
    }
    // 写文件尾
    const int err = av_write_trailer(impl.format);
    if (err < 0) {
        return Result::fail(ErrorCode::Internal, "写文件尾失败");
    }
    // 关闭
    if (!(impl.format->oformat->flags & AVFMT_NOFILE) && impl.format->pb != nullptr) {
        avio_closep(&impl.format->pb);
    }
    impl.reset();
    return Result::ok();
}

void FFmpegMediaWriter::abort() {
    if (impl_ == nullptr || !impl_->open) {
        return;
    }
    impl_->reset();
    // 删除半成品文件（由调用方控制是否删除；这里保守地保留，
    // 因为某些平台下删除行为需要用户确认）。
}

bool FFmpegMediaWriter::isOpen() const { return impl_ != nullptr && impl_->open; }

}  // namespace core
}  // namespace nairoclip

#endif  // NAIROCLIP_HAS_FFMPEG
