#include "nairoclip/core/media_ffmpeg.h"

#ifdef NAIROCLIP_HAS_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/display.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mastering_display_metadata.h>
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

/// 读取容器的显示旋转角（顺时针，0/90/180/270）。
///
/// 来源优先级：
///   1. 流级 display matrix 侧数据（手机竖拍 MP4 走这条，最常见）
///   2. stream->metadata 的 "rotate" 键（老文件/某些封装）
///   3. 都没有 → 0
///
/// 为什么不用 av_display_rotation_get 直接用：它返回的是「逆时针弧度 +
/// 需要取负」的约定，容易搞反。这里统一归一化到 [0,360) 的顺时针角度，
/// 并且把非 90 倍数（少数设备会写 89.9 之类）四舍五入到最近的 90 倍数 ——
/// 只支持正交旋转：任意角度的旋转需要重采样，那是一块独立的几何变换工作，
/// 不属于「读元数据」的范畴，强行支持会引入插值糊边。
int displayRotationOf(const AVStream* stream) {
    double deg = 0.0;
    bool found = false;

    const AVPacketSideData* sd =
        av_packet_side_data_get(stream->codecpar->coded_side_data,
                                stream->codecpar->nb_coded_side_data,
                                AV_PKT_DATA_DISPLAYMATRIX);
    if (sd != nullptr && sd->size >= 9 * sizeof(std::int32_t)) {
        // av_display_rotation_get 返回逆时针角度；取负得到顺时针。
        deg = -av_display_rotation_get(reinterpret_cast<const std::int32_t*>(sd->data));
        found = true;
    }

    if (!found) {
        const AVDictionaryEntry* tag = av_dict_get(stream->metadata, "rotate", nullptr, 0);
        if (tag != nullptr && tag->value != nullptr) {
            deg = std::atof(tag->value);
            found = true;
        }
    }

    if (!found || !std::isfinite(deg)) return 0;

    // 归一化到 [0, 360)
    double norm = std::fmod(deg, 360.0);
    if (norm < 0) norm += 360.0;
    // 吸附到最近的 90 度
    int quarter = static_cast<int>(std::lround(norm / 90.0)) % 4;
    if (quarter < 0) quarter += 4;
    return quarter * 90;
}

/// 把 src（RGBA8）按顺时针 deg 度旋转，写入 dst。
/// 90/270 时宽高互换。deg 必须是 0/90/180/270。
void rotateRgba(const std::uint8_t* src, int sw, int sh, int sStride,
                std::uint8_t* dst, int dw, int dh, int dStride, int deg) {
    if (deg == 0) {
        for (int y = 0; y < sh; ++y) {
            std::memcpy(dst + static_cast<std::size_t>(y) * dStride,
                        src + static_cast<std::size_t>(y) * sStride,
                        static_cast<std::size_t>(sw) * 4);
        }
        return;
    }
    for (int y = 0; y < sh; ++y) {
        const std::uint8_t* row = src + static_cast<std::size_t>(y) * sStride;
        for (int x = 0; x < sw; ++x) {
            const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
            int dx = 0;
            int dy = 0;
            switch (deg) {
                case 90:   // 顺时针 90：(x,y) -> (sh-1-y, x)
                    dx = sh - 1 - y;
                    dy = x;
                    break;
                case 180:  // (x,y) -> (sw-1-x, sh-1-y)
                    dx = sw - 1 - x;
                    dy = sh - 1 - y;
                    break;
                case 270:  // 顺时针 270：(x,y) -> (y, sw-1-x)
                    dx = y;
                    dy = sw - 1 - x;
                    break;
                default:
                    break;
            }
            std::memcpy(dst + static_cast<std::size_t>(dy) * dStride +
                        static_cast<std::size_t>(dx) * 4, p, 4);
        }
    }
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
    /// 重采样器上次构建时的输入/输出参数（用于判断能否复用，见 readAudioBlock）。
    int swrSrcRate = -1;
    int swrSrcFmt = -1;
    int swrOutRate = -1;
    int swrOutChannels = -1;
    // seek 目标
    Time seekTarget;
    bool afterSeek = false;
    /// 音频 seek 后「待跳过」标志：readAudioBlock 丢弃目标之前的音频帧
    /// （与 readVideoFrame 的 seek 跳过逻辑对称；见 seek 注释）。
    bool audioAfterSeek = false;
    /// 容器显示旋转角（0/90/180/270）。open 时读一次，readVideoFrame 每帧应用。
    int rotationDeg = 0;

    /* ---- 顺序访问快路径（PD）----
       目标：把「每输出 1 帧就 seek + flush + 从关键帧重解 125 帧」
       降为「每输出 1 帧只解 1 帧」。

       生效条件（全部满足才走快路径）：
         1. seqHint.active
         2. |seek目标 - hint.nextExpected| <= tolerance
         3. 解码器没 EOF（EOF 后必须 seek 才能回到前面）
       任一不满足 → 清标志，走原 seek 路径，行为与改动前完全一致。 */
    SequentialHint seqHint;
    /// 真正执行 av_seek_frame 的次数（验收断言用）。
    long seekCount = 0;
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
        rotationDeg = 0;
        seqHint = SequentialHint{};
        seekCount = 0;
        swrSrcRate = -1;
        swrSrcFmt = -1;
        swrOutRate = -1;
        swrOutChannels = -1;
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
        /* 解码器多线程（PD 第 4 条）。
           原来直接 avcodec_open2(..., nullptr)，thread_count 保持库默认
           （通常 1）—— 多核完全闲置。多核机解码段耗时理论上可降到 1/N。

           thread_count = 0 → FFmpeg 自选（= CPU 核数）；
           thread_type = FF_THREAD_FRAME → 帧级并行（H.264/HEVC 最有效）。
           不用 FF_THREAD_SLICE：帧级并行对 GOP 内依赖的处理更简单，
           且延迟更可控（切片并行需要等同一帧的所有切片，首帧延迟更高）。

           注意：帧级并行会让解码器**内部缓存若干帧**，因此 seek 后
           不能假设「下一次 receive 就是目标帧」—— 这点由 readVideoFrame
           里既有的 afterSeek 跳过逻辑处理（按时间戳过滤，不看序号）。 */
        impl.videoCodec->thread_count = 0;
        impl.videoCodec->thread_type = FF_THREAD_FRAME;
        err = avcodec_open2(impl.videoCodec, codec, nullptr);
        if (err < 0) {
            close();
            return Result::fail(ErrorCode::Unsupported,
                                "视频解码器打开失败: " + avErrorString(err));
        }
        impl.probe.type = impl.audioStream >= 0 ? MediaType::Video : MediaType::Video;
        /* 读容器旋转角。手机竖拍的 MP4 编码尺寸是 1920x1080（横向），
           真正的朝向写在 display matrix 侧数据里。解码器不会自动应用 ——
           不读它，竖拍视频在预览和导出里都会横过来。

           读到的角度同时用于：
             · probe.width/height 报「显示尺寸」（竖拍 = 1080x1920）
             · readVideoFrame 转完 RGBA 后实际旋转像素
           所以下游拿到的帧永远已经是正确朝向，不需要各自再处理一次
           （多处各自处理必然漂移，这是历来最典型的一类 bug）。 */
        impl.rotationDeg = displayRotationOf(stream);
        impl.probe.rotationDeg = impl.rotationDeg;
        const bool swap = (impl.rotationDeg == 90 || impl.rotationDeg == 270);
        impl.probe.width = swap ? impl.videoCodec->height : impl.videoCodec->width;
        impl.probe.height = swap ? impl.videoCodec->width : impl.videoCodec->height;
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

void FFmpegMediaReader::suggestSequential(const SequentialHint& hint) {
    Impl& impl = *impl_;
    impl.seqHint = hint;
    /* 关闭提示时顺手清 EOF 标志？不能清 —— EOF 是解码器真实状态，
       清了会让 readVideoFrame 继续在已耗尽的解码器上取帧（永远 EAGAIN）。
       交给下一次真 seek 去复位。 */
}

long FFmpegMediaReader::seekCount() const { return impl_->seekCount; }

Result FFmpegMediaReader::seek(const Time& sourceTime) {
    Impl& impl = *impl_;
    if (!impl.open) {
        return Result::fail(ErrorCode::InvalidArgument, "未打开");
    }
    if (sourceTime.isNegative()) {
        return Result::fail(ErrorCode::OutOfRange, "seek 目标为负");
    }
    const std::int64_t targetUs = toMicroseconds(sourceTime);

    /* ---- 顺序访问快路径（PD）----
       命中判定：调用方提示「接下来要 t」，而我们现在就要 t —— 说明解码器
       已经停在正确位置，无需 seek。

       为什么必须检查 videoEof：EOF 之后 avcodec_receive_frame 会一直返回
       AVERROR_EOF，不 seek 就永远拿不到帧。此时必须走慢路径。

       为什么必须检查 afterSeek：上一帧读取过程中可能已消费了目标之后的帧
       （readVideoFrame 返回时就 av_frame_unref 了，但解码器内部位置仍在前）。
       保守起见，只要 afterSeek 为 true 就说明位置已不可信，走慢路径。 */
    if (impl.seqHint.active && !impl.videoEof) {
        const double dtSec = std::fabs(
            static_cast<double>(targetUs - toMicroseconds(impl.seqHint.nextExpected)) / 1e6);
        if (dtSec <= impl.seqHint.tolerance) {
            /* 位置可用：只更新「期望的下一帧」标记，不动解码器。
               afterSeek = false 是关键 —— 否则 readVideoFrame 会把
               接下来的正常帧当成「seek 前的旧帧」丢掉。 */
            impl.seekTarget = sourceTime;
            impl.afterSeek = false;
            impl.frameReady = false;
            return Result::ok();
        }
        /* 超容差（跳章/倒放/随机跳转）→ 提示失效，往下走真 seek */
        impl.seqHint.active = false;
    }

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
    ++impl.seekCount;
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

            /* YUV → RGBA 的色彩空间对齐。
               ------------------------------------------------------------------
               不设的话 sws 默认按 BT.601 的系数转换。但 720p 及以上（以及
               绝大多数手机视频）实际是 BT.709 —— 用 601 转 709 素材，
               红色会偏橙、整体泛灰，肉眼一眼能看出「颜色不对」。

               判据优先级：
                 1. 码流自带的 color_space（最准，容器里写了就用它）
                 2. 否则按分辨率推断：height >= 720 → 709，否则 601
                    （这是业界通行的启发式，FFmpeg/Chrome 都这么做）

               同时对齐 range：手机录像多为 limited(16-235)，
               电脑录屏/合成素材多为 full(0-255)。用错 range 会「发白发灰」
               或「死黑死白」。 */
            int swsFlags = SWS_BILINEAR | SWS_FULL_CHR_H_INT | SWS_ACCURATE_RND;
            const int* coeffs = nullptr;
            int dstRange = 0;
            const int srcRange = (impl.frame->color_range == AVCOL_RANGE_JPEG) ? 1 : 0;

            const AVColorSpace cs = impl.frame->colorspace;
            bool useBt709 = false;
            if (cs == AVCOL_SPC_BT709) {
                useBt709 = true;
            } else if (cs == AVCOL_SPC_BT470BG || cs == AVCOL_SPC_SMPTE170M) {
                useBt709 = false;
            } else {
                useBt709 = (height >= 720);
            }
            const int* coeff = useBt709 ? sws_getCoefficients(SWS_CS_ITU709)
                                        : sws_getCoefficients(SWS_CS_ITU601);
            coeffs = coeff;
            dstRange = 0;   // 输出 RGBA 用 limited，与后续滤镜链一致
            sws_setColorspaceDetails(impl.sws, coeff, srcRange, coeff, dstRange,
                                     0, 1 << 16, 1 << 16);
            (void)swsFlags;

            out.timestamp = frameTime;
            /* 旋转后的显示尺寸：90/270 时宽高互换。
               这里就换掉，下游一律按 out.width/height 走，
               不需要知道「源是横的但显示是竖的」这回事。 */
            const bool swap = (impl.rotationDeg == 90 || impl.rotationDeg == 270);
            const int dispW = swap ? height : width;
            const int dispH = swap ? width : height;
            out.width = dispW;
            out.height = dispH;
            out.stride = dispW * 4;
            const std::size_t frameBytes =
                static_cast<std::size_t>(out.stride) * static_cast<std::size_t>(dispH);
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
            /* 两步走：先 sws 转到「编码尺寸」的 RGBA，再按需旋转到显示尺寸。
               为什么不直接让 sws 输出旋转后的尺寸：sws 只做缩放/色彩转换，
               不做旋转；想一步到位得用 sws_rotate 或滤镜图，代价大得多。
               正交旋转是一次 memcpy 级别的像素搬运，自己做更可控。 */
            const std::size_t rawBytes =
                static_cast<std::size_t>(width) * 4 * static_cast<std::size_t>(height);
            std::vector<std::uint8_t> scratch(rawBytes + kTailPadding, 0);
            std::uint8_t* dst[4] = {scratch.data(), nullptr, nullptr, nullptr};
            const int rawStride = width * 4;
            const int dstStride[4] = {rawStride, 0, 0, 0};
            sws_scale(impl.sws, impl.frame->data, impl.frame->linesize, 0, height, dst, dstStride);
            if (impl.rotationDeg != 0) {
                rotateRgba(scratch.data(), width, height, rawStride,
                           out.pixels.data(), dispW, dispH, out.stride, impl.rotationDeg);
            } else {
                std::memcpy(out.pixels.data(), scratch.data(), frameBytes);
            }
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
            /* 重采样器：**参数变化时才重建**，不要每帧重建。
               ------------------------------------------------------------
               原实现每解一帧就 swr_free + swr_alloc_set_opts2 + swr_init。
               swr_init 会重算整张滤波器系数表（重采样有几十个抽头，
               48k→44.1k 这类非整数比更贵），而输入格式在整条流里
               通常**恒定不变** —— 每帧重建纯属浪费。

               复用与 sws_getCachedContext 同一范式：
                 输入/输出参数没变 → 直接用；变了才 free + 重建。

               ⚠️ 注意：seek 后必须**保留** swr（不能随 flush 一起清）。
               avcodec_flush_buffers 只清解码器内部状态，不改变采样格式；
               重采样器里的 delay 由 swr_convert 自身维护，重建反而会丢掉
               已缓冲的样本，导致 seek 后开头出现轻微爆音。 */
            const bool swrMatches =
                impl.swr != nullptr &&
                impl.swrSrcRate == impl.audioCodec->sample_rate &&
                impl.swrSrcFmt == impl.audioCodec->sample_fmt &&
                impl.swrOutRate == outSampleRate &&
                impl.swrOutChannels == outChannels;

            if (!swrMatches) {
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
                impl.swrSrcRate = impl.audioCodec->sample_rate;
                impl.swrSrcFmt = impl.audioCodec->sample_fmt;
                impl.swrOutRate = outSampleRate;
                impl.swrOutChannels = outChannels;
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
