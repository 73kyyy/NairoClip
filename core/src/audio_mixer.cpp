#include "nairoclip/core/audio_mixer.h"

#include "nairoclip/core/export_executor.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

constexpr double kMinGainDb = -60.0;

}  // namespace

AudioMixer::AudioMixer(MixFormat format, MediaReaderFactory& readerFactory)
    : format_(format), readerFactory_(readerFactory) {}

double AudioMixer::dbToLinear(double db) {
    if (db <= kMinGainDb) {
        return 0.0;
    }
    return std::pow(10.0, db / 20.0);
}

bool AudioMixer::prepare(const ExportPlan& plan, const Project& project) {
    clips_.clear();
    readers_.clear();
    duration_ = plan.duration;
    cursor_ = Rational::make(0);

    for (const auto& track : plan.tracks) {
        if (track.type != TrackType::Audio || track.muted) {
            continue;
        }
        for (const auto& renderClip : track.clips) {
            TrackedClip tc;
            tc.clipId = renderClip.clipId;
            tc.asset = renderClip.asset;
            tc.assetPath = renderClip.assetPath;
            tc.timelineStart = renderClip.timelineStart;
            tc.timelineEnd = renderClip.timelineEnd;
            tc.sourceIn = renderClip.sourceIn;
            tc.speed = renderClip.speed;
            tc.linearGain = 1.0;
            // 从工程中查找源片段的音频属性
            const Clip* source = findClip(project, plan.sequenceId, track.trackId,
                                          renderClip.clipId);
            if (source != nullptr) {
                tc.linearGain = dbToLinear(source->audioGainDb);
                tc.fadeIn = source->audioFadeIn;
                tc.fadeOut = source->audioFadeOut;
            }
            clips_.push_back(std::move(tc));
        }
    }
    // 无音频片段：仍可产出静音流（返回 false 提示调用方）
    return !clips_.empty();
}

void AudioMixer::mixInto(std::vector<float>& buffer, const Time& blockStart) {
    const std::int64_t blockFrames = static_cast<std::int64_t>(buffer.size() /
                                                              static_cast<std::size_t>(format_.channels));
    const double secondsPerFrame = 1.0 / format_.sampleRate;

    for (const auto& clip : clips_) {
        // 快速跳过：片段与块无交集
        const Time blockEnd = blockStart + Time{Rational::make(blockFrames, format_.sampleRate)};
        if (clip.timelineEnd <= blockStart || clip.timelineStart >= blockEnd) {
            continue;
        }
        // 确保 reader 已打开
        auto it = readers_.find(clip.asset.value);
        if (it == readers_.end()) {
            auto reader = readerFactory_.create();
            if (!reader->open(clip.assetPath).isOk()) {
                continue;  // 打开失败：跳过该片段（silence）
            }
            it = readers_.emplace(clip.asset.value, std::move(reader)).first;
        }
        MediaReader* reader = it->second.get();

        // 对块内每个采样帧
        for (std::int64_t i = 0; i < blockFrames; ++i) {
            const Time frameTime = blockStart + Time{Rational::make(i, format_.sampleRate)};
            (void)secondsPerFrame;
            if (frameTime < clip.timelineStart || frameTime >= clip.timelineEnd) {
                continue;
            }
            // 时间轴 → 源时间
            const Time sourceTime =
                clip.sourceIn + Time{(frameTime - clip.timelineStart) * clip.speed};
            // 读取源采样（对 Mock 采用 seek+read 一次一帧的简化路径；
            // 真实实现将在平台层以块缓冲 + 插值实现）
            reader->seek(sourceTime);
            AudioBlock sourceBlock;
            if (!reader->readAudioBlock(sourceBlock) || sourceBlock.samples.empty()) {
                continue;
            }
            // 淡入淡出包络（本地时间 = frameTime - timelineStart）
            const Time localTime = frameTime - clip.timelineStart;
            double envelope = 1.0;
            if (clip.fadeIn.isPositive() && localTime < clip.fadeIn) {
                envelope *= localTime.toDouble() / clip.fadeIn.toDouble();
            }
            if (clip.fadeOut.isPositive()) {
                const Time fadeOutStart = clip.timelineEnd - clip.fadeOut;
                if (frameTime > fadeOutStart) {
                    const double remain = (clip.timelineEnd - frameTime).toDouble();
                    envelope *= remain / clip.fadeOut.toDouble();
                }
            }
            if (envelope < 0.0) envelope = 0.0;
            // 求和到输出缓冲（前 min(源声道, 输出声道) 个通道）
            for (int ch = 0; ch < format_.channels; ++ch) {
                const int srcCh = ch < sourceBlock.channels ? ch : 0;
                const float sample =
                    sourceBlock.samples[static_cast<std::size_t>(srcCh)];
                buffer[static_cast<std::size_t>(i) * static_cast<std::size_t>(format_.channels) +
                       static_cast<std::size_t>(ch)] +=
                    sample * static_cast<float>(clip.linearGain * envelope);
            }
        }
    }

    // 硬限幅（防削顶保护）
    for (auto& s : buffer) {
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
    }
}

bool AudioMixer::readBlock(OutputAudioBlock& out) {
    if (!(cursor_ < duration_)) {
        return false;
    }
    out.start = cursor_;
    out.sampleRate = format_.sampleRate;
    out.channels = format_.channels;
    out.samples.assign(static_cast<std::size_t>(blockFrames_) *
                           static_cast<std::size_t>(format_.channels),
                       0.0f);
    mixInto(out.samples, cursor_);
    cursor_ = cursor_ + Time{Rational::make(blockFrames_, format_.sampleRate)};
    return true;
}

Result AudioMixer::seek(const Time& timelineTime) {
    if (timelineTime.isNegative()) {
        return Result::fail(ErrorCode::OutOfRange, "seek 目标为负");
    }
    cursor_ = timelineTime;
    return Result::ok();
}

}  // namespace core
}  // namespace nairoclip
