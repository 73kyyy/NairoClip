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
#include <cstdlib>
#include <cstdio>
#include <vector>

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

namespace {

/// 解码路径的堆检查点（诊断用；NC_MEDIA_HEAP_PROBE=1 打开）。
/// 已确证拆到 readVideoFrame 这一层：reader.open 之后堆还是好的，
/// readVideoFrame 返回后第一次 malloc 就崩。这里把解码拆成四段。
bool mediaProbeEnabled() {
    static const bool enabled = [] {
        const char* v = std::getenv("NC_MEDIA_HEAP_PROBE");
        return v != nullptr && v[0] != '0';
    }();
    return enabled;
}

void mediaHeapCheck(const char* where) {
    if (!mediaProbeEnabled()) return;
    constexpr int kN = 122;
    void* blocks[kN];
    std::size_t sizes[kN];
    int n = 0;
    for (int i = 0; i < 96; ++i, ++n) sizes[n] = 16 + static_cast<std::size_t>(i) * 21;
    for (int i = 0; i < 24; ++i, ++n) sizes[n] = 2048 + static_cast<std::size_t>(i) * 2600;
    sizes[n++] = 1048;
    sizes[n++] = 2048;
    for (int i = 0; i < n; ++i) {
        blocks[i] = std::malloc(sizes[i]);
        if (blocks[i] != nullptr) std::memset(blocks[i], 0xCD, sizes[i]);
    }
    for (int i = 0; i < n; ++i) std::free(blocks[i]);
    std::printf("[诊断] readVideoFrame: %s 后堆 OK\n", where);
    std::fflush(stdout);
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
    /// 音频 seek 后「待跳过」标志：readAudioBlock 丢弃目标之前的音频帧
    /// （与 readVideoFrame 的 seek 跳过逻辑对称；见 seek 注释）。
    bool audioAfterSeek = false;
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
        // 状态标志复位（close 后重新 open 时不得残留）
        seekTarget = Time{};
        afterSeek = false;
        audioAfterSeek = false;
        videoEof = false;
        audioEof = false;
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
        if (impl.videoCodec == nullptr) {
            close();
            return Result::fail(ErrorCode::Internal, "视频解码器上下文分配失败");
        }
        // 返回值必须检查：失败时 codecpar 未被拷贝，后面 open2 会用
        // 一个字段全空的 context，行为依赖布局（正是难查的那类问题）。
        if (avcodec_parameters_to_context(impl.videoCodec, stream->codecpar) < 0) {
            close();
            return Result::fail(ErrorCode::Unsupported, "视频流参数复制失败");
        }
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

    // 打开音频解码器（可选路径：任何一步失败都只是「没有音频」，不致命）
    if (impl.audioStream >= 0) {
        AVStream* stream = impl.format->streams[impl.audioStream];
        const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
        if (codec != nullptr) {
            impl.audioCodec = avcodec_alloc_context3(codec);
            // 返回值必须逐个检查：失败后继续用 nullptr context 是
            // 未定义行为，且崩不崩取决于布局，极难定位。
            if (impl.audioCodec != nullptr &&
                avcodec_parameters_to_context(impl.audioCodec, stream->codecpar) < 0) {
                avcodec_free_context(&impl.audioCodec);
                impl.audioCodec = nullptr;
            }
            if (impl.audioCodec != nullptr) {
                impl.audioCodec->pkt_timebase = stream->time_base;
                if (avcodec_open2(impl.audioCodec, codec, nullptr) < 0) {
                    avcodec_free_context(&impl.audioCodec);
                    impl.audioCodec = nullptr;
                }
            }
            if (impl.audioCodec != nullptr) {
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
    // 定位策略（修复「MP4/AAC 音频 seek 内容错位」缺陷）：
    //   容器同时含音频+视频时，按 stream_index = -1 定位在 MP4 上只会移动到
    //   「视频关键帧」，音频解复用器位置不会移动到目标附近——实测含视频的
    //   MP4（AAC）：seek(3s) 后音频块仍从 0s 读出（对照：WAV/PCM 正常）。
    //   因此存在音频流时按「音频流索引」定位（时间戳经流时间基换算，向后
    //   对齐到帧边界），视频读取由 readVideoFrame 的 seek 跳过逻辑保证时间
    //   正确；后续 readAudioBlock 再按 seekTarget 裁掉目标之前的样本，使
    //   首个音频块**采样精确**地自目标开始。
    //   无音频流（纯视频）时退回 stream_index = -1（行为不变）。
    int seekIndex = -1;
    std::int64_t seekTimestamp = targetUs;
    if (impl.audioStream >= 0) {
        const AVStream* audioStream = impl.format->streams[impl.audioStream];
        seekIndex = impl.audioStream;
        seekTimestamp = av_rescale_q(targetUs, AV_TIME_BASE_Q, audioStream->time_base);
    }
    const int err = av_seek_frame(impl.format, seekIndex, seekTimestamp, AVSEEK_FLAG_BACKWARD);
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
    impl.audioAfterSeek = impl.audioStream >= 0;
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
            mediaHeapCheck("receive_frame 之后");
            impl.sws = sws_getCachedContext(impl.sws, width, height, srcFormat, width, height,
                                            AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr,
                                            nullptr);
            if (impl.sws == nullptr) {
                av_frame_unref(impl.frame);
                return false;
            }
            mediaHeapCheck("sws_getCachedContext 之后");
            out.timestamp = frameTime;
            out.width = width;
            out.height = height;
            out.stride = width * 4;
            const std::size_t frameBytes =
                static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(height);
            out.pixels.resize(frameBytes);
            /* sws_scale 必须写到一块**尾部有余量**的缓冲上。
               ----------------------------------------------------------------
               sws_scale 的 SIMD 路径按 8/16 字节块写入，最后一行的末尾会越过
               stride*height 若干字节。若直接写进恰好 frameBytes 的 vector，
               就是一次堆越界写。valgrind 实测（1080x1920 导出后解码）：

                 Invalid write of size 8
                 Address 0x2012a040 is 0 bytes after a block of size 8,294,400
                   at sws_scale (libswscale.so.7.5.100)
                   by readVideoFrame (media_ffmpeg.cpp:350)

               因此这里用一块独立的临时缓冲（frameBytes + 64，取 FFmpeg 自身的
               AV_INPUT_BUFFER_PADDING_SIZE 惯例），转换完再拷回 out.pixels。
               不把 padding 塞进 out.pixels 的原因：多处调用方（含
               test_media_ffmpeg / test_media_io 的 CHECK_EQ）按
               pixels.size() == stride*height 断言帧大小，改变它会造成
               语义破坏。代价是一次 frameBytes 的拷贝，换取缓冲区契约不变。 */
            constexpr std::size_t kTailPadding = 64;
            std::vector<std::uint8_t> scratch(frameBytes + kTailPadding, 0);
            std::uint8_t* dst[4] = {scratch.data(), nullptr, nullptr, nullptr};
            const int dstStride[4] = {out.stride, 0, 0, 0};
            sws_scale(impl.sws, impl.frame->data, impl.frame->linesize, 0, height, dst, dstStride);
            std::memcpy(out.pixels.data(), scratch.data(), frameBytes);
            mediaHeapCheck("sws_scale 之后");
            av_frame_unref(impl.frame);
            mediaHeapCheck("av_frame_unref 之后");
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
            // 容量算式：输出率放分子、输入率放分母（N * out / in）。
            // ⚠️ 原来写反成 N * in / out：升采样时会算小，
            // swr_convert 直接写过 out.samples 的缓冲区。
            // 与 media_ffmpeg_writer 里同类错误同源，一并修正。
            const int maxOutSamples = static_cast<int>(
                av_rescale_rnd(swr_get_delay(impl.swr, impl.audioCodec->sample_rate) +
                                   impl.frame->nb_samples,
                               outSampleRate, impl.audioCodec->sample_rate, AV_ROUND_UP));
            out.sampleRate = outSampleRate;
            out.channels = outChannels;
            out.samples.resize(static_cast<std::size_t>(maxOutSamples) *
                               static_cast<std::size_t>(outChannels));
            std::uint8_t* outData[1] = {reinterpret_cast<std::uint8_t*>(out.samples.data())};
            const int converted = swr_convert(impl.swr, outData, maxOutSamples,
                                              const_cast<const std::uint8_t**>(impl.frame->data),
                                              impl.frame->nb_samples);
            // 兜底：即使容量算式将来再出错，也不让越界结果往下传。
            if (converted > maxOutSamples) {
                av_frame_unref(impl.frame);
                return false;
            }
            if (converted < 0) {
                av_frame_unref(impl.frame);
                return false;
            }
            out.samples.resize(static_cast<std::size_t>(converted) *
                               static_cast<std::size_t>(outChannels));
            // 时间戳
            const std::int64_t pts = impl.frame->best_effort_timestamp;
            const std::int64_t tsUs = av_rescale_q(pts, stream->time_base, AV_TIME_BASE_Q);
            if (impl.audioAfterSeek) {
                // seek 后对齐（采样精确）：
                // 音频解码帧的起点通常早于 seek 目标（编解码器帧边界，AAC 最长
                // 约 23ms），且可能从更早处开始（按音频流索引向后定位）。
                // 规则：1) 完全位于目标之前的帧整体丢弃；
                //       2) 与目标相交的帧裁掉目标之前的样本，
                //          使产出块自目标时间采样精确开始（混音器按源块
                //          第 0 样本与源时间对齐，必须无偏移）。
                const int frameRate =
                    impl.frame->sample_rate > 0 ? impl.frame->sample_rate : outSampleRate;
                const std::int64_t frameDurationUs =
                    static_cast<std::int64_t>(impl.frame->nb_samples) * 1000000LL / frameRate;
                const std::int64_t targetUs = toMicroseconds(impl.seekTarget);
                if (tsUs + frameDurationUs <= targetUs) {
                    av_frame_unref(impl.frame);
                    continue;  // 整帧在目标之前：丢弃，继续解下一帧
                }
                if (tsUs < targetUs && converted > 0) {
                    const std::int64_t skip = static_cast<std::int64_t>(
                        static_cast<double>(targetUs - tsUs) * outSampleRate / 1000000.0 + 0.5);
                    if (skip >= converted) {
                        av_frame_unref(impl.frame);
                        continue;  // 舍入后无剩余样本：整帧丢弃
                    }
                    if (skip > 0) {
                        out.samples.erase(out.samples.begin(),
                                          out.samples.begin() +
                                              static_cast<std::ptrdiff_t>(skip) * outChannels);
                    }
                    out.start = impl.seekTarget;
                } else {
                    out.start = Rational::make(tsUs, AV_TIME_BASE);
                }
                impl.audioAfterSeek = false;
            } else {
                out.start = Rational::make(tsUs, AV_TIME_BASE);
            }
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
