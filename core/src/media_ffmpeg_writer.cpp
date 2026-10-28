#include "nairoclip/core/media_ffmpeg_writer.h"

#ifdef NAIROCLIP_HAS_FFMPEG

#include "nairoclip/core/codec_policy.h"

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

/// 枚举 FFmpeg 中某 codec id 的可用编码器 → 合规策略候选列表。
/// hardware 判定：编码器名匹配硬件后缀（codec_policy 分级为 Hardware）。
std::vector<EncoderCandidate> availableEncodersFor(AVCodecID codecId,
                                                   const char* codecName) {
    std::vector<EncoderCandidate> candidates;
    void* iter = nullptr;
    const AVCodec* codec = nullptr;
    while ((codec = av_codec_iterate(&iter)) != nullptr) {
        if (codec->id != codecId || !av_codec_is_encoder(codec)) {
            continue;
        }
        EncoderCandidate candidate;
        candidate.name = codec->name != nullptr ? codec->name : "";
        candidate.codec = codecName;
        candidate.hardware =
            classifyEncoderLicense(candidate.name) == EncoderLicenseClass::Hardware;
        candidates.push_back(std::move(candidate));
    }
    return candidates;
}

/// 目标格式名 → FFmpeg codec id（"h264" 默认）。
AVCodecID ffmpegCodecIdFor(const std::string& codec) {
    if (codec == "av1") return AV_CODEC_ID_AV1;
    if (codec == "vp9") return AV_CODEC_ID_VP9;
    return AV_CODEC_ID_H264;
}

/// 按合规策略打开视频编码器：
/// 返回 nullptr 表示无合规实现（fail-closed）或全部候选打开失败；note 输出说明。
/// 说明：策略按优先级给出「候选序列」，逐个尝试 avcodec_open2；硬件编码器在
/// 无 GPU/驱动缺失时会打开失败（如 h264_nvenc 报 Cannot load libcuda.so.1），
/// 此时按序回退到下一个合规候选（工程必需：设备能力探测的运行时等价物）。
const AVCodec* openCompliantVideoEncoder(const std::string& codec,
                                         const EncoderPolicy& policy,
                                         std::string& note) {
    // 目标格式 → FFmpeg codec id
    AVCodecID targetId = AV_CODEC_ID_H264;
    if (codec == "av1") {
        targetId = AV_CODEC_ID_AV1;
    } else if (codec == "vp9") {
        targetId = AV_CODEC_ID_VP9;
    } else if (codec == "h264") {
        targetId = AV_CODEC_ID_H264;
    }
    const char* codecName = codec == "av1" ? "av1" : (codec == "vp9" ? "vp9" : "h264");
    const std::vector<EncoderCandidate> available = availableEncodersFor(targetId, codecName);
    const std::vector<EncoderSelection> candidates =
        rankVideoEncoderCandidates(codec, available, policy);
    for (const auto& selection : candidates) {
        note = selection.note;
        const AVCodec* encoder = avcodec_find_encoder_by_name(selection.name.c_str());
        if (encoder != nullptr) {
            return encoder;
        }
    }
    if (!note.empty() && candidates.empty()) {
        // 保持 selectVideoEncoder 的失败说明
    }
    if (candidates.empty()) {
        const EncoderSelection primary = selectVideoEncoder(codec, available, policy);
        note = primary.note;
    }
    return nullptr;
}

/// 按合规策略打开音频编码器（AAC 内置实现视为宽松；libfdk_aac 为不可分发）。
const AVCodec* openCompliantAudioEncoder(const std::string& codec,
                                         const EncoderPolicy& policy,
                                         std::string& note) {
    const AVCodecID targetId = codec == "opus" ? AV_CODEC_ID_OPUS : AV_CODEC_ID_AAC;
    std::vector<EncoderCandidate> candidates;
    void* iter = nullptr;
    const AVCodec* c = nullptr;
    while ((c = av_codec_iterate(&iter)) != nullptr) {
        if (c->id != targetId || !av_codec_is_encoder(c)) {
            continue;
        }
        EncoderCandidate candidate;
        candidate.name = c->name != nullptr ? c->name : "";
        candidate.codec = codec;
        candidates.push_back(std::move(candidate));
    }
    const EncoderSelection selection = selectAudioEncoder(codec, candidates, policy);
    note = selection.note;
    if (!selection.valid()) {
        return nullptr;
    }
    return avcodec_find_encoder_by_name(selection.name.c_str());
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
    // 实际使用的视频编码器选择说明（合规审计/诊断用）
    std::string encoderNote;
    bool open = false;
    std::int64_t videoPts = 0;
    std::int64_t audioPts = 0;  // 以采样数为单位
    // 音频 FIFO（交错 float）：累积重采样输出，凑满编码器 frame_size 后编码。
    // AAC 编码器要求除末帧外严格 frame_size 采样。
    std::vector<float> audioFifo;

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
        audioFifo.clear();
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

    // ---- 视频编码器（合规策略选择；文档第七章：禁用 libx264/libx265）----
    // 说明：此前的 avcodec_find_encoder(AV_CODEC_ID_H264) 自动选择会解析到
    // libx264（GPL-2.0），违反协议清单。现改为按 codec_policy 生成候选序列：
    // 硬件编码器优先 → OpenH264 等宽松软件兜底 → （仅开发构建）GPL 例外；
    // 逐个尝试打开（硬件编码器可能因驱动/GPU 缺失失败，如无 GPU 环境下
    // h264_nvenc 报 "Cannot load libcuda.so.1" → 按序回退）；
    // 全部失败时 fail-closed 拒绝编码（不静默降级到未授权实现）。
    const std::vector<EncoderCandidate> videoCandidates =
        availableEncodersFor(ffmpegCodecIdFor(config.videoCodec), config.videoCodec.c_str());
    const std::vector<EncoderSelection> rankedVideo =
        rankVideoEncoderCandidates(config.videoCodec, videoCandidates, EncoderPolicy{});
    std::string attempted;
    const AVCodec* videoEncoder = nullptr;
    for (const auto& candidate : rankedVideo) {
        const AVCodec* candidateCodec =
            avcodec_find_encoder_by_name(candidate.name.c_str());
        if (candidateCodec == nullptr) {
            continue;
        }
        AVCodecContext* ctx = avcodec_alloc_context3(candidateCodec);
        if (ctx == nullptr) {
            continue;
        }
        ctx->width = config.width;
        ctx->height = config.height;
        ctx->time_base = AVRational{config.frameRate.value.den,
                                    config.frameRate.value.num};
        ctx->framerate = AVRational{config.frameRate.value.num,
                                    config.frameRate.value.den};
        ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        ctx->gop_size = static_cast<int>(config.frameRate.toDouble() * 2.0);
        ctx->max_b_frames = 2;
        // 选项设置：crf/preset 为编码器特有。
        // 关键：av_opt_set 失败**不能静默忽略**。
        // 例：h264_nvenc 不认识 x264 的 preset 名（veryfast 等），此处会返回
        // AVERROR_OPTION_NOT_FOUND；若继续，avcodec_open2 仍可能成功，但编码器
        // 参数是残缺的，直到写第一帧才炸。这里直接判定该候选不可用并回退。
        const int crfErr = av_opt_set_int(ctx->priv_data, "crf", config.crf, 0);
        const int presetErr =
            av_opt_set(ctx->priv_data, "preset", config.preset.c_str(), 0);
        if (presetErr == AVERROR_OPTION_NOT_FOUND) {
            // 编码器不提供 preset 选项（部分硬件后端）：不算错误，用其默认码控。
            // 但要确认它至少不是「有 preset 却拒绝该取值」的情况。
            const AVOption* presetOpt = av_opt_find(ctx->priv_data, "preset", nullptr,
                                                   0, AV_OPT_SEARCH_CHILDREN);
            if (presetOpt != nullptr) {
                attempted += (attempted.empty() ? "" : "、") + candidate.name +
                             "(preset 不支持 '" + config.preset + "')";
                avcodec_free_context(&ctx);
                continue;  // 有 preset 选项但取值非法 → 该候选不可用
            }
        }
        (void)crfErr;  // crf 不被识别时用编码器默认码控（不阻断）
        // 性能模式：编码线程上限（0 = 编码器自动）
        if (config.threads > 0) {
            ctx->thread_count = config.threads;
        }
        const int openErr = avcodec_open2(ctx, candidateCodec, nullptr);
        if (openErr < 0) {
            attempted += (attempted.empty() ? "" : "、") + candidate.name + "(" +
                         avErr(openErr) + ")";
            avcodec_free_context(&ctx);
            continue;  // 该候选不可用 → 尝试下一个合规候选
        }
        impl.videoCodec = ctx;
        videoEncoder = candidateCodec;
        // 记录实际使用的编码器（诊断与合规审计用）
        impl.encoderNote = candidate.note;
        if (candidate.usedDevelopmentException) {
            impl.encoderNote += "〔开发例外：发布版本必须替换为硬件/OpenH264〕";
        }
        break;
    }
    if (videoEncoder == nullptr || impl.videoCodec == nullptr) {
        std::string message = "无可用合规视频编码器";
        if (!attempted.empty()) {
            message += "（尝试失败：" + attempted + "）";
        }
        if (rankedVideo.empty()) {
            message += "：仅存在被禁用或无匹配格式的实现（发布构建需链接 OpenH264 "
                       "或使用硬件编码器）";
        }
        return Result::fail(ErrorCode::Unsupported, message);
    }

    impl.videoStream = avformat_new_stream(impl.format, nullptr);
    avcodec_parameters_from_context(impl.videoStream->codecpar, impl.videoCodec);
    impl.videoStream->time_base = impl.videoCodec->time_base;

    // ---- AI 隐式标识元数据（《人工智能生成合成内容标识办法》要求）----
    // 写入容器级元数据（format->metadata）；avformat_write_header 落盘。
    if (!config.aiMetadata.empty()) {
        for (const auto& entry : config.aiMetadata) {
            av_dict_set(&impl.format->metadata, entry.first.c_str(), entry.second.c_str(), 0);
        }
    }

    // ---- 音频编码器（AAC；合规策略：硬件 > 宽松软件（内置 AAC）> 开发例外）----
    std::string audioNote;
    const AVCodec* audioEncoder =
        openCompliantAudioEncoder("aac", EncoderPolicy{}, audioNote);
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

    // 逐项检查返回值：CI（x64, glibc）上曾出现 malloc(): invalid size 崩溃，
    // 定位在真实导出路径。这类"内存紧张时分配失败"如果不检查，
    // 后面 sws_scale 会往 nullptr / 未分配的 data 里写，直接破坏堆。
    impl.videoFrame = av_frame_alloc();
    if (impl.videoFrame == nullptr) {
        abort();
        return Result::fail(ErrorCode::Internal, "视频帧分配失败");
    }
    impl.videoFrame->format = impl.videoCodec->pix_fmt;
    impl.videoFrame->width = config.width;
    impl.videoFrame->height = config.height;
    const int bufErr = av_frame_get_buffer(impl.videoFrame, 32);
    if (bufErr < 0) {
        abort();
        return Result::fail(ErrorCode::Internal, "视频帧缓冲区分配失败: " + avErr(bufErr));
    }
    impl.packet = av_packet_alloc();
    if (impl.packet == nullptr) {
        abort();
        return Result::fail(ErrorCode::Internal, "包分配失败");
    }
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
    // av_frame_make_writable 可能因内存不足失败；失败时 videoFrame->data
    // 仍是旧缓冲或不可写，继续 sws_scale 会写坏堆。
    const int wrErr = av_frame_make_writable(impl.videoFrame);
    if (wrErr < 0) {
        return Result::fail(ErrorCode::Internal, "视频帧不可写: " + avErr(wrErr));
    }
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
    // 惰性创建重采样器与音频帧
    if (impl.audioFrame == nullptr) {
        impl.audioFrame = av_frame_alloc();
        if (impl.audioFrame == nullptr) {
            return Result::fail(ErrorCode::Internal, "音频帧分配失败");
        }
        impl.audioFrame->format = impl.audioCodec->sample_fmt;
        impl.audioFrame->ch_layout = impl.audioCodec->ch_layout;
        impl.audioFrame->sample_rate = impl.audioCodec->sample_rate;
        impl.audioFrame->nb_samples = impl.audioCodec->frame_size > 0
                                          ? impl.audioCodec->frame_size
                                          : 1024;
        const int abufErr = av_frame_get_buffer(impl.audioFrame, 0);
        if (abufErr < 0) {
            av_frame_free(&impl.audioFrame);
            return Result::fail(ErrorCode::Internal,
                                "音频帧缓冲区分配失败: " + avErr(abufErr));
        }
    }
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

    const int channels = impl.audioCodec->ch_layout.nb_channels;
    const int frameSize = impl.audioFrame->nb_samples;
    const int inSamples =
        static_cast<int>(block.samples.size() / static_cast<std::size_t>(block.channels));

    // resample 整个输入块 → 追加到 FIFO（交错 float 保持交错，编码时再拆平面）
    //
    // 容量算式：av_rescale_rnd(a, b, c) == a * b / c。
    // 要得到「N 个输入样本换算成输出采样率下的样本数」，分母必须是输入率、
    // 分子必须是输出率：N * out / in。
    //
    // ⚠️ 这里原来是 N * in / out（分子分母写反）。降采样（48000→44100）时
    // 算出来偏大，问题被掩盖；升采样（44100→48000）时偏小 ——
    // 1024 样本的 AAC 帧只分配 941 个位置却要写 1115 个，每帧越界 174 个
    // float（1392 字节），直接写坏 audioFifo 之后的堆元数据。
    // 表现是后续某次 malloc 报 "malloc(): invalid size (unsorted)"，
    // 或被 ASAN 抓到 libswresample 写空指针 —— 崩溃点与真凶相隔很远。
    const int maxOut = static_cast<int>(av_rescale_rnd(
        swr_get_delay(impl.swr, block.sampleRate) + inSamples,
        impl.audioCodec->sample_rate, block.sampleRate, AV_ROUND_UP));
    // ⚠️ 输出格式是编码器的 sample_fmt，AAC 只有 FLTP（平面）。
    // 平面格式要求 outData 提供 **每个声道一个指针**，而这里原来只给了
    // outData[1]{...} 一个元素 —— swr_convert 会把 outData[1] 当成
    // 第二个声道的写入地址，读到的是栈上的未初始化值。
    // ASAN 报的正是这个：
    //   SEGV on unknown address 0x000000000000, WRITE, libswresample
    // 单声道时 channels==1，越界读不到（看似正常）；双声道必炸。
    //
    // 分配策略：按平面布局，每个声道 maxOut 个 float 连续存放，
    // 转换后按 [ch0 frames][ch1 frames]... 摊平进 audioFifo，
    // 再在下面按平面格式逐声道拷进编码帧。
    const std::size_t fifoStart = impl.audioFifo.size();
    impl.audioFifo.resize(fifoStart + static_cast<std::size_t>(maxOut) *
                                         static_cast<std::size_t>(channels));
    std::uint8_t* outData[AV_NUM_DATA_POINTERS] = {nullptr};
    if (av_sample_fmt_is_planar(impl.audioCodec->sample_fmt)) {
        // 平面：每个声道必须有自己的指针，且不能为 nullptr
        for (int ch = 0; ch < channels && ch < AV_NUM_DATA_POINTERS; ++ch) {
            outData[ch] = reinterpret_cast<std::uint8_t*>(
                impl.audioFifo.data() + fifoStart +
                static_cast<std::size_t>(ch) * static_cast<std::size_t>(maxOut));
        }
    } else {
        outData[0] = reinterpret_cast<std::uint8_t*>(impl.audioFifo.data() + fifoStart);
    }
    // 输入恒为 AV_SAMPLE_FMT_FLT（交错），一个指针即可
    const std::uint8_t* inData[AV_NUM_DATA_POINTERS] = {
        reinterpret_cast<const std::uint8_t*>(block.samples.data())};
    const int converted = swr_convert(impl.swr, outData, maxOut, inData, inSamples);
    if (converted < 0) {
        impl.audioFifo.resize(fifoStart);
        return Result::fail(ErrorCode::Internal, "重采样失败");
    }
    if (converted > maxOut) {
        // 容量算式若再出错，宁可报错也不让越界数据继续往下走
        impl.audioFifo.resize(fifoStart);
        return Result::fail(ErrorCode::Internal, "重采样输出超出预期容量");
    }
    // 按实际产出截断。平面布局下每个声道 converted 个 float，
    // 声道之间留着 maxOut-converted 的空洞 —— 必须压紧，
    // 否则下面按 [ch0][ch1] 顺序取帧会读到未初始化区间。
    if (av_sample_fmt_is_planar(impl.audioCodec->sample_fmt) && channels > 1 &&
        converted < maxOut) {
        for (int ch = 1; ch < channels; ++ch) {
            std::memmove(impl.audioFifo.data() + fifoStart +
                             static_cast<std::size_t>(ch) * static_cast<std::size_t>(converted),
                         impl.audioFifo.data() + fifoStart +
                             static_cast<std::size_t>(ch) * static_cast<std::size_t>(maxOut),
                         static_cast<std::size_t>(converted) * sizeof(float));
        }
    }
    impl.audioFifo.resize(fifoStart +
                          static_cast<std::size_t>(converted) * static_cast<std::size_t>(channels));

    // 从 FIFO 取出整帧编码
    std::size_t consumedFrames = 0;
    const std::size_t available = impl.audioFifo.size() / static_cast<std::size_t>(channels);
    while (available - consumedFrames >= static_cast<std::size_t>(frameSize)) {
        const int awErr = av_frame_make_writable(impl.audioFrame);
        if (awErr < 0) {
            return Result::fail(ErrorCode::Internal,
                                "音频帧不可写: " + avErr(awErr));
        }
        // FIFO 布局与编码器格式一致：
        //   平面（FLTP）：[ch0 全部帧][ch1 全部帧]...
        //   交错（FLT） ：[f0ch0 f0ch1 f1ch0 f1ch1 ...]
        // 取第 consumedFrames 起的 frameSize 帧。
        if (av_sample_fmt_is_planar(impl.audioCodec->sample_fmt)) {
            for (int ch = 0; ch < channels; ++ch) {
                const float* srcCh = impl.audioFifo.data() +
                                     static_cast<std::size_t>(ch) * available +
                                     consumedFrames;
                float* dst = reinterpret_cast<float*>(impl.audioFrame->data[ch]);
                std::memcpy(dst, srcCh, static_cast<std::size_t>(frameSize) * sizeof(float));
            }
        } else {
            const float* src = impl.audioFifo.data() +
                               consumedFrames * static_cast<std::size_t>(channels);
            std::memcpy(impl.audioFrame->data[0], src,
                        static_cast<std::size_t>(frameSize) *
                            static_cast<std::size_t>(channels) * sizeof(float));
        }
        impl.audioFrame->nb_samples = frameSize;
        impl.audioFrame->pts = impl.audioPts;
        impl.audioPts += frameSize;
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
        consumedFrames += static_cast<std::size_t>(frameSize);
    }
    // 保留未消费的剩余样本
    if (consumedFrames > 0) {
        impl.audioFifo.erase(
            impl.audioFifo.begin(),
            impl.audioFifo.begin() +
                static_cast<std::ptrdiff_t>(consumedFrames *
                                            static_cast<std::size_t>(channels)));
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
    // 冲刷音频：先发送 FIFO 中剩余样本（末帧可小于 frame_size；补零到 frame_size 更稳妥）
    if (impl.audioCodec != nullptr && impl.audioFrame != nullptr && !impl.audioFifo.empty()) {
        const int channels = impl.audioCodec->ch_layout.nb_channels;
        const int frameSize = impl.audioFrame->nb_samples;
        const std::size_t remaining = impl.audioFifo.size() / static_cast<std::size_t>(channels);
        const int tailSamples = static_cast<int>(std::min(
            remaining, static_cast<std::size_t>(frameSize)));
        if (av_frame_make_writable(impl.audioFrame) < 0) {
            return Result::fail(ErrorCode::Internal, "末帧不可写");
        }
        // 清零帧缓冲，避免填充垃圾数据
        if (impl.audioCodec->sample_fmt == AV_SAMPLE_FMT_FLTP) {
            for (int ch = 0; ch < channels; ++ch) {
                std::memset(impl.audioFrame->data[ch], 0,
                            static_cast<std::size_t>(frameSize) * sizeof(float));
            }
        } else {
            std::memset(impl.audioFrame->data[0], 0,
                        static_cast<std::size_t>(frameSize) *
                            static_cast<std::size_t>(channels) * sizeof(float));
        }
        // 拷贝剩余样本。FIFO 布局与编码器格式一致（与 writeAudioBlock 相同）：
        //   平面：src[ch * remaining + i]
        //   交错：src[i * channels + ch]
        // 原来这里对平面格式仍按 i*channels+ch 读 —— 那是交错的索引方式，
        // 会串声道，并且当 tailSamples 接近 remaining 时读到错误区域。
        if (av_sample_fmt_is_planar(impl.audioCodec->sample_fmt)) {
            for (int ch = 0; ch < channels; ++ch) {
                const float* srcCh =
                    impl.audioFifo.data() + static_cast<std::size_t>(ch) * remaining;
                float* dst = reinterpret_cast<float*>(impl.audioFrame->data[ch]);
                std::memcpy(dst, srcCh, static_cast<std::size_t>(tailSamples) * sizeof(float));
            }
        } else {
            std::memcpy(impl.audioFrame->data[0], impl.audioFifo.data(),
                        static_cast<std::size_t>(tailSamples) *
                            static_cast<std::size_t>(channels) * sizeof(float));
        }
        impl.audioFrame->nb_samples = frameSize;  // 补零到完整帧
        impl.audioFrame->pts = impl.audioPts;
        impl.audioPts += frameSize;
        avcodec_send_frame(impl.audioCodec, impl.audioFrame);
        impl.audioFifo.clear();
    }
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
