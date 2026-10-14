#include "nairoclip/core/audio_mixer.h"

#include "nairoclip/core/auto_duck.h"
#include "nairoclip/core/export_executor.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

constexpr double kMinGainDb = -60.0;
/// 降噪流历史保留帧数（源采样率；供小块回退时直接恢复，不必重置重建）。
constexpr long long kDenoiseHistoryRetainSeconds = 1;
/// 降噪流前跳重置阈值（源采样率秒数）：请求位置超过「已产出前沿」该距离时
/// 重置重建（直接 seek 到目标，避免长时间空转处理）；正常连续播放不触发。
constexpr long long kDenoiseForwardResetSeconds = 2;

}  // namespace

std::vector<DuckRegion> AudioMixer::duckRegions(TrackId trackId) const {
    for (const auto& dupPlan : duckPlans_) {
        if (dupPlan.trackId == trackId) {
            return dupPlan.regions;
        }
    }
    return {};
}

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
    duckPlans_.clear();
    denoiseStates_.clear();
    denoiseReaders_.clear();
    duration_ = plan.duration;
    cursor_ = Rational::make(0);
    masterGain_ = 1.0;
    hasSolo_ = false;

    // 混音台：先扫描是否存在独奏轨（决定静音过滤策略）
    for (const auto& track : plan.tracks) {
        if (track.type != TrackType::Audio) {
            continue;
        }
        const Track* sourceTrack = findTrack(project, plan.sequenceId, track.trackId);
        if (sourceTrack != nullptr && sourceTrack->solo) {
            hasSolo_ = true;
            break;
        }
    }
    // 主输出增益（序列级）
    const Sequence* sequence = project.findSequence(plan.sequenceId);
    if (sequence != nullptr) {
        masterGain_ = dbToLinear(sequence->masterGainDb);
    }

    for (const auto& track : plan.tracks) {
        if (track.type != TrackType::Audio) {
            continue;
        }
        const Track* sourceTrack = findTrack(project, plan.sequenceId, track.trackId);
        // 混音台过滤：静音优先排除；有独奏时非独奏轨排除
        if (track.muted) {
            continue;
        }
        if (hasSolo_ && (sourceTrack == nullptr || !sourceTrack->solo)) {
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
            tc.trackId = track.trackId;
            tc.ducked = false;
            // 从工程中查找源片段的音频属性
            const Clip* source = findClip(project, plan.sequenceId, track.trackId,
                                          renderClip.clipId);
            if (source != nullptr) {
                tc.linearGain = dbToLinear(source->audioGainDb);
                tc.fadeIn = source->audioFadeIn;
                tc.fadeOut = source->audioFadeOut;
                // 降噪（文档「🎵音频 → 降噪 → 开关/强度(弱中强)」）
                tc.denoise = source->denoise;
            }
            // 轨道级混音台控制
            if (sourceTrack != nullptr) {
                tc.trackGain = dbToLinear(sourceTrack->gainDb);
                if (sourceTrack->autoDuck.enabled) {
                    tc.ducked = true;
                }
            }
            clips_.push_back(std::move(tc));
        }
    }

    // 自动闪避：分析触发轨语音活动 → 为被压低轨生成压低区间
    buildDuckPlans(plan, project);

    // 无音频片段：仍可产出静音流（返回 false 提示调用方）
    return !clips_.empty();
}

bool AudioMixer::trackAudible(TrackId trackId) const {
    for (const auto& clip : clips_) {
        if (clip.trackId == trackId) {
            return true;
        }
    }
    return false;
}

void AudioMixer::buildDuckPlans(const ExportPlan& plan, const Project& project) {
    // 收集所有启用自动闪避的音频轨（被压低轨）
    struct DuckTarget {
        TrackId trackId;
        AutoDuckSettings settings;
    };
    std::vector<DuckTarget> targets;
    for (const auto& track : plan.tracks) {
        if (track.type != TrackType::Audio || track.muted) {
            continue;
        }
        const Track* sourceTrack = findTrack(project, plan.sequenceId, track.trackId);
        if (sourceTrack != nullptr && sourceTrack->autoDuck.enabled &&
            sourceTrack->autoDuck.valid()) {
            targets.push_back(DuckTarget{track.trackId, sourceTrack->autoDuck});
        }
    }
    if (targets.empty()) {
        return;
    }

    // 触发源：除被压低轨之外的其他音频轨（旁白/人声通常在视频轨或专用音频轨）。
    // 说明：视频轨自带音频（人声）在本工程中通过音频轨承载（音画分离/导入），
    //      这里以「其他音频轨」为触发源，符合「音乐被旁白压低」的典型用法。
    std::vector<TrackId> triggerTracks;
    for (const auto& track : plan.tracks) {
        if (track.type != TrackType::Audio || track.muted || track.clips.empty()) {
            continue;
        }
        bool isTarget = false;
        for (const auto& target : targets) {
            if (target.trackId == track.trackId) {
                isTarget = true;
                break;
            }
        }
        if (!isTarget) {
            triggerTracks.push_back(track.trackId);
        }
    }
    if (triggerTracks.empty()) {
        // 无旁白源：无可压低对象（音乐不会被压低）
        return;
    }

    // 解码触发轨音频（跨触发轨合并为单条语音活动序列；多源时取并集效果）
    std::vector<AudioBlock> triggerBlocks;
    for (const auto& track : plan.tracks) {
        bool isTrigger = false;
        for (const auto& id : triggerTracks) {
            if (id == track.trackId) {
                isTrigger = true;
                break;
            }
        }
        if (!isTrigger) {
            continue;
        }
        for (const auto& renderClip : track.clips) {
            auto it = readers_.find(renderClip.asset.value);
            if (it == readers_.end()) {
                auto reader = readerFactory_.create();
                if (!reader->open(renderClip.assetPath).isOk()) {
                    continue;
                }
                it = readers_.emplace(renderClip.asset.value, std::move(reader)).first;
            }
            MediaReader* reader = it->second.get();
            if (!reader->isOpen()) {
                continue;
            }
            if (!reader->seek(renderClip.sourceIn).isOk()) {
                continue;
            }
            // 读取覆盖片段时长的音频块（按 1024 采样块流式读取）
            const double clipSeconds = renderClip.timelineEnd.toDouble() -
                                       renderClip.timelineStart.toDouble();
            const double sampleRate =
                reader->probe().sampleRate > 0 ? reader->probe().sampleRate : 48000.0;
            const std::int64_t maxBlocks = static_cast<std::int64_t>(
                clipSeconds * sampleRate / 1024.0 + 2);
            for (std::int64_t b = 0; b < maxBlocks; ++b) {
                AudioBlock block;
                if (!reader->readAudioBlock(block)) {
                    break;
                }
                // 时间对齐：源时间 → 时间轴时间（含 speed 简化处理）
                const Time offset = (block.start - renderClip.sourceIn) / renderClip.speed;
                block.start = renderClip.timelineStart + offset;
                triggerBlocks.push_back(std::move(block));
            }
        }
    }
    if (triggerBlocks.empty()) {
        return;  // 触发轨无可用音频：保持原音量
    }
    std::sort(triggerBlocks.begin(), triggerBlocks.end(),
              [](const AudioBlock& a, const AudioBlock& b) { return a.start < b.start; });

    // 为每个被压低轨生成计划（当前使用共享分析结果，参数按轨独立）
    for (const auto& target : targets) {
        AutoDuckConfig config;
        config.targetGain = duckTargetGain(target.settings);
        config.attackSeconds = target.settings.attackSeconds;
        config.releaseSeconds = target.settings.releaseSeconds;
        config.holdSeconds = target.settings.holdSeconds;
        config.voiceThreshold = target.settings.voiceThreshold();
        const AutoDuckResult analysis = analyzeAutoDuck(triggerBlocks, config);
        if (!analysis.valid()) {
            continue;
        }
        DuckPlan planEntry;
        planEntry.trackId = target.trackId;
        planEntry.regions = analysis.regions;
        duckPlans_.push_back(std::move(planEntry));
    }
}

void AudioMixer::mixInto(std::vector<float>& buffer, const Time& blockStart) {
    const std::int64_t blockFrames = static_cast<std::int64_t>(
        buffer.size() / static_cast<std::size_t>(format_.channels));
    const Time blockEnd = blockStart + Time{Rational::make(blockFrames, format_.sampleRate)};

    for (const auto& clip : clips_) {
        // 快速跳过：片段与块无交集
        if (clip.timelineEnd <= blockStart || clip.timelineStart >= blockEnd) {
            continue;
        }
        // 确保 reader 已打开
        auto it = readers_.find(clip.asset.value);
        if (it == readers_.end()) {
            auto reader = readerFactory_.create();
            if (!reader->open(clip.assetPath).isOk()) {
                continue;  // 打开失败：跳过该片段（静音）
            }
            it = readers_.emplace(clip.asset.value, std::move(reader)).first;
        }
        MediaReader* reader = it->second.get();

        // 计算重叠区间 [overlapStart, overlapEnd)
        const Time overlapStart =
            clip.timelineStart > blockStart ? clip.timelineStart : blockStart;
        const Time overlapEnd = clip.timelineEnd < blockEnd ? clip.timelineEnd : blockEnd;
        if (!(overlapStart < overlapEnd)) {
            continue;
        }

        // 输出块内的样本偏移与数量（有理数精度 → 四舍五入到采样帧）
        const Rational offsetFrames = (overlapStart - blockStart) *
                                      Rational::make(format_.sampleRate);
        std::int64_t outOffset = static_cast<std::int64_t>(
            offsetFrames.toDouble() + 0.5);
        if (outOffset < 0) outOffset = 0;
        if (outOffset >= blockFrames) continue;
        const Rational countFrames = (overlapEnd - overlapStart) *
                                     Rational::make(format_.sampleRate);
        std::int64_t outCount = static_cast<std::int64_t>(countFrames.toDouble() + 0.5);
        if (outCount <= 0) continue;
        if (outOffset + outCount > blockFrames) {
            outCount = blockFrames - outOffset;
        }

        // 源时间（含 speed 映射）
        const Time sourceTime =
            clip.sourceIn + Time{(overlapStart - clip.timelineStart) * clip.speed};
        // 读取源块：降噪片段走连续降噪流（专用读取器）；其余片段走共享读取器
        // 逐块 seek + 读取（原始路径）。
        AudioBlock sourceBlock;
        if (clip.denoise != DenoiseStrength::Off) {
            if (!serveDenoisedBlock(clip, sourceTime, outCount, sourceBlock) ||
                sourceBlock.samples.empty()) {
                continue;
            }
        } else {
            if (!reader->seek(sourceTime).isOk()) {
                continue;
            }
            if (!reader->readAudioBlock(sourceBlock) || sourceBlock.samples.empty()) {
                continue;
            }
        }
        const std::int64_t srcFrames = static_cast<std::int64_t>(sourceBlock.frameCount());
        if (srcFrames <= 0) continue;

        // 拷贝/混入：源块样本 
        // 采样率不一致时按比率取样（线性近似；精确重采样在平台层）
        for (std::int64_t i = 0; i < outCount; ++i) {
            std::int64_t srcIndex = i;
            if (sourceBlock.sampleRate != format_.sampleRate && sourceBlock.sampleRate > 0) {
                srcIndex = static_cast<std::int64_t>(
                    static_cast<double>(i) * sourceBlock.sampleRate / format_.sampleRate);
            }
            if (srcIndex < 0 || srcIndex >= srcFrames) {
                break;  // 源块耗尽
            }
            // 淡入淡出包络（本地时间）
            const Time frameTime = overlapStart + Time{Rational::make(i, format_.sampleRate)};
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
            // 自动闪避：被压低轨在该时刻应用闪避增益（真实联动旁白）
            if (clip.ducked) {
                for (const auto& duckPlan : duckPlans_) {
                    if (duckPlan.trackId == clip.trackId) {
                        envelope *= duckGainAt(duckPlan.regions, frameTime);
                        break;
                    }
                }
            }
            const float gain = static_cast<float>(clip.linearGain * clip.trackGain * envelope);
            // 声道映射（源声道不足时用通道 0 复制）
            for (int ch = 0; ch < format_.channels; ++ch) {
                const int srcCh = ch < sourceBlock.channels ? ch : 0;
                const float sample = sourceBlock.samples[
                    static_cast<std::size_t>(srcIndex) *
                        static_cast<std::size_t>(sourceBlock.channels) +
                    static_cast<std::size_t>(srcCh)];
                buffer[static_cast<std::size_t>(outOffset + i) *
                           static_cast<std::size_t>(format_.channels) +
                       static_cast<std::size_t>(ch)] += sample * gain;
            }
        }
    }
    // 硬限幅（防削顶保护）；先应用主输出增益（混音台「主输出」推子）
    for (auto& s : buffer) {
        s = static_cast<float>(s * masterGain_);
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
    }
}
bool AudioMixer::serveDenoisedBlock(const TrackedClip& clip, const Time& sourceTime,
                                    std::int64_t outCount, AudioBlock& out) {
    if (outCount <= 0) {
        return false;
    }
    // 专用读取器（连续顺序读取；与共享读取器互相独立）
    auto readerIt = denoiseReaders_.find(clip.clipId);
    if (readerIt == denoiseReaders_.end()) {
        auto reader = readerFactory_.create();
        if (!reader->open(clip.assetPath).isOk()) {
            return false;
        }
        readerIt = denoiseReaders_.emplace(clip.clipId, std::move(reader)).first;
    }
    MediaReader* reader = readerIt->second.get();
    const int srcRate = reader->probe().sampleRate > 0 ? reader->probe().sampleRate : 48000;
    const int srcChannels = reader->probe().channels > 0 ? reader->probe().channels : 1;

    DenoiseState& state = denoiseStates_[clip.clipId];
    // 强度变化 / 首次使用 / 源参数变化 → 重建流
    if (!state.valid || state.strength != clip.denoise || state.srcRate != srcRate ||
        state.srcChannels != srcChannels) {
        state = DenoiseState{};
        state.valid = true;
        state.strength = clip.denoise;
        state.srcRate = srcRate;
        state.srcChannels = srcChannels;
        state.denoiser = AudioDenoiser(clip.denoise, srcChannels);
    }

    // 请求区间（源帧号，源采样率；四舍五入到帧）
    const long long reqFrame =
        static_cast<long long>(sourceTime.toDouble() * srcRate + 0.5);

    // 重置判定：
    //   · 前跳超过阈值（> 2s 未处理区）：直接重建（并 seek 到目标）；
    //   · 回退超过保留历史：重建（罕见；小块回退在历史窗内直接恢复）。
    const long long producedFront = state.bufFirst +
                                    static_cast<long long>(state.buf.size() /
                                                           static_cast<std::size_t>(srcChannels));
    const bool forwardJump = !state.buf.empty() && reqFrame > producedFront +
                                                                   srcRate * kDenoiseForwardResetSeconds;
    const bool rewindFar = !state.buf.empty() && reqFrame <
                                                      state.bufFirst -
                                                          srcRate * kDenoiseHistoryRetainSeconds;
    if (!state.buf.empty() && (forwardJump || rewindFar)) {
        state = DenoiseState{};
        state.valid = true;
        state.strength = clip.denoise;
        state.srcRate = srcRate;
        state.srcChannels = srcChannels;
        state.denoiser = AudioDenoiser(clip.denoise, srcChannels);
    }

    // 首次初始化：锚定输出流起点为请求位置（流从请求处开始产出）
    if (state.buf.empty() && !state.flushed) {
        state.baseToken = reqFrame;
        state.fedCount = 0;
        state.bufFirst = reqFrame;
        if (!reader->seek(sourceTime).isOk()) {
            return false;
        }
    }

    const std::size_t channels = static_cast<std::size_t>(srcChannels);
    const std::size_t needFrames = static_cast<std::size_t>(outCount);
    const long long needEnd = reqFrame + static_cast<long long>(needFrames);

    // 目标：buf 覆盖 [reqFrame, needEnd)
    // 循环：喂入输入（按需）→ 取产出 → 检查覆盖
    int guard = 0;
    while (guard++ < 100000) {
        const long long haveFront = state.bufFirst;
        const long long haveEnd = haveFront + static_cast<long long>(state.buf.size() / channels);
        if (haveFront <= reqFrame && haveEnd >= needEnd) {
            break;  // 覆盖满足
        }

        // 需要更多数据：喂入（输入连续推进；源耗尽后零填充）
        const long long feedTarget =
            std::max(needEnd - state.baseToken, 0LL) + 4096;  // 目标喂入前沿（含余量）
        if (state.fedCount < feedTarget) {
            const long long wantFeed =
                std::min(feedTarget - state.fedCount, 16384LL);
            std::vector<float> chunk(static_cast<std::size_t>(wantFeed) * channels, 0.0f);
            long long got = 0;
            if (!state.sourceExhausted) {
                // 读取源（连续）：读取器位置由自身维护（首次已 seek 到 baseToken）
                while (got < wantFeed) {
                    AudioBlock block;
                    if (!reader->readAudioBlock(block) || block.frameCount() == 0) {
                        state.sourceExhausted = true;
                        break;
                    }
                    const long long frames = static_cast<long long>(block.frameCount());
                    const long long take = std::min(frames, wantFeed - got);
                    for (std::size_t i = 0; i < static_cast<std::size_t>(take) * channels; ++i) {
                        const int srcCh = static_cast<int>(i % channels);
                        const std::size_t srcIdx =
                            static_cast<std::size_t>(i / channels) * static_cast<std::size_t>(block.channels) +
                            static_cast<std::size_t>(srcCh < block.channels ? srcCh : 0);
                        // i 已是「帧 × 声道」连续下标：目标 = got*channels + i
                        chunk[static_cast<std::size_t>(got) * channels + i] =
                            srcIdx < block.samples.size() ? block.samples[srcIdx] : 0.0f;
                    }
                    got += take;
                    if (take < frames) {
                        break;  // 防御：块内剩余（当前读取器不会发生）
                    }
                }
            }
            const bool isFinal = state.sourceExhausted;
            std::vector<float> produced;
            if (got > 0 || isFinal) {
                state.denoiser.pushAndPull(chunk.data(), static_cast<int>(wantFeed), isFinal,
                                           produced);
            }
            state.fedCount += wantFeed;
            // 追加产出
            if (!produced.empty()) {
                const std::size_t base = state.buf.size();
                state.buf.resize(base + produced.size());
                std::copy(produced.begin(), produced.end(),
                          state.buf.begin() + static_cast<std::ptrdiff_t>(base));
            }
            if (isFinal) {
                state.flushed = true;
            }
            continue;
        }
        break;  // 无法继续（防御）
    }

    // 从 buf 切片交付 [reqFrame, min(needEnd, bufEnd))
    const long long bufEnd = state.bufFirst + static_cast<long long>(state.buf.size() / channels);
    const long long start = std::max(reqFrame, state.bufFirst);
    const long long end = std::min(needEnd, bufEnd);
    if (end <= start) {
        return false;
    }
    const std::size_t frames = static_cast<std::size_t>(end - start);
    out.sampleRate = srcRate;
    out.channels = srcChannels;
    out.start = Rational::make(reqFrame, srcRate);
    out.samples.resize(frames * channels);
    const std::size_t off = static_cast<std::size_t>(start - state.bufFirst) * channels;
    std::copy(state.buf.begin() + static_cast<std::ptrdiff_t>(off),
              state.buf.begin() + static_cast<std::ptrdiff_t>(off + frames * channels),
              out.samples.begin());

    // 修剪：保留 [reqFrame - 历史保留, ...)，避免 buf 无限增长
    const long long retainFrom =
        std::max(reqFrame - srcRate * kDenoiseHistoryRetainSeconds, state.bufFirst);
    if (retainFrom > state.bufFirst) {
        const std::size_t dropFrames = static_cast<std::size_t>(retainFrom - state.bufFirst);
        const std::size_t dropSamples = dropFrames * channels;
        if (dropSamples < state.buf.size()) {
            state.buf.erase(state.buf.begin(),
                            state.buf.begin() + static_cast<std::ptrdiff_t>(dropSamples));
            state.bufFirst = retainFrom;
        }
    }
    return true;
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
    // 跳转后降噪流与专用读取器位置失效：整体重置（下次 mixInto 重新锚定）
    denoiseStates_.clear();
    denoiseReaders_.clear();
    return Result::ok();
}

}  // namespace core
}  // namespace nairoclip
