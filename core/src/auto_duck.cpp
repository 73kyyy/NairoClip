#include "nairoclip/core/auto_duck.h"

#include <algorithm>
#include <cmath>

namespace nairoclip {
namespace core {

namespace {

/// 计算帧 RMS（多声道取平均能量）。
double frameRms(const std::vector<float>& mono, std::int64_t start, std::int64_t count) {
    if (count <= 0 || start < 0 || start >= static_cast<std::int64_t>(mono.size())) {
        return 0.0;
    }
    const std::int64_t end =
        std::min(start + count, static_cast<std::int64_t>(mono.size()));
    if (end <= start) {
        return 0.0;
    }
    double sum = 0.0;
    for (std::int64_t i = start; i < end; ++i) {
        const double v = static_cast<double>(mono[static_cast<std::size_t>(i)]);
        sum += v * v;
    }
    return std::sqrt(sum / static_cast<double>(end - start));
}

/// 时间（秒）→ Time（微秒精度）。
Time secondsToTime(double seconds, const Time& origin) {
    return origin +
           Rational::make(static_cast<std::int64_t>(seconds * 1000000.0 + 0.5), 1000000);
}

}  // namespace

double duckTargetGain(const AutoDuckSettings& settings) {
    // dB → 线性：gain = 10^(-dB/20)；24dB 及以下即 0.063 量级。
    const double db = settings.duckAmountDb;
    if (db <= 0.0) {
        return 1.0;  // 0dB = 不压低
    }
    return std::pow(10.0, -db / 20.0);
}

AutoDuckResult analyzeAutoDuck(const std::vector<AudioBlock>& blocks, const AutoDuckConfig& config) {
    AutoDuckResult result;
    if (blocks.empty()) {
        result.note = "触发轨无音频输入";
        return result;
    }
    const int sampleRate = blocks.front().sampleRate > 0 ? blocks.front().sampleRate : 48000;
    result.analyzedDuration = Rational::make(0);
    result.frameSeconds = static_cast<double>(std::max(16, config.hopSize)) /
                          static_cast<double>(sampleRate);

    // ---- 1) 下混为单声道（跨块连续）----
    std::vector<float> mono;
    for (const auto& block : blocks) {
        const std::size_t frames = block.frameCount();
        const int blockChannels = std::max(1, block.channels);
        mono.reserve(mono.size() + frames);
        for (std::size_t f = 0; f < frames; ++f) {
            double sum = 0.0;
            for (int c = 0; c < blockChannels; ++c) {
                const std::size_t idx = f * static_cast<std::size_t>(blockChannels) +
                                        static_cast<std::size_t>(c);
                if (idx < block.samples.size()) {
                    sum += static_cast<double>(block.samples[idx]);
                }
            }
            mono.push_back(static_cast<float>(sum / static_cast<double>(blockChannels)));
        }
    }
    const std::int64_t totalFrames = static_cast<std::int64_t>(mono.size());
    if (totalFrames < static_cast<std::int64_t>(config.frameSize)) {
        result.note = "触发轨音频过短";
        return result;
    }
    result.analyzedDuration = Rational::make(totalFrames, sampleRate);

    // ---- 2) 逐帧 RMS → VAD（阈值比较）----
    const int frameSize = std::max(64, config.frameSize);
    const int hopSize = std::max(16, std::min(config.hopSize, frameSize));
    const double threshold = config.voiceThreshold;
    const std::int64_t frameCount = (totalFrames - frameSize) / hopSize + 1;
    std::vector<bool> active(static_cast<std::size_t>(frameCount), false);
    result.voiceRms.resize(static_cast<std::size_t>(frameCount), 0.0);
    std::size_t activeFrames = 0;
    for (std::int64_t f = 0; f < frameCount; ++f) {
        const double rms = frameRms(mono, f * hopSize, frameSize);
        result.voiceRms[static_cast<std::size_t>(f)] = rms;
        if (rms >= threshold) {
            active[static_cast<std::size_t>(f)] = true;
            ++activeFrames;
        }
    }
    if (activeFrames == 0) {
        result.note = "触发轨未检测到有效语音（可提高灵敏度）";
        result.voiceSeconds = 0.0;
        return result;
    }

    // ---- 3) 区间合并（hold 保留短间隙）----
    const std::int64_t holdFrames =
        static_cast<std::int64_t>(config.holdSeconds / std::max(1e-9, result.frameSeconds) + 0.5);
    std::vector<std::pair<std::int64_t, std::int64_t>> spans;  // [startFrame, endFrame]
    std::int64_t spanStart = -1;
    std::int64_t lastActive = -1;
    for (std::int64_t f = 0; f < frameCount; ++f) {
        if (!active[static_cast<std::size_t>(f)]) {
            continue;
        }
        if (spanStart < 0) {
            spanStart = f;
        } else if (f - lastActive > holdFrames + 1) {
            // 间隙超过 hold：结束上一区间，开启新区间
            spans.emplace_back(spanStart, lastActive);
            spanStart = f;
        }
        lastActive = f;
    }
    if (spanStart >= 0) {
        spans.emplace_back(spanStart, lastActive);
    }

    // ---- 4) 加 attack/release 过渡 → DuckRegion ----
    const Time origin = blocks.front().start;
    const double frameSeconds = result.frameSeconds;
    double voiceSeconds = 0.0;
    for (const auto& span : spans) {
        const double rawStart = static_cast<double>(span.first) * frameSeconds;
        const double rawEnd = static_cast<double>(span.second + 1) * frameSeconds;
        voiceSeconds += rawEnd - rawStart;
        DuckRegion region;
        double startSeconds = rawStart;
        double endSeconds = rawEnd;
        if (config.smoothTransitions) {
            // attack 提前量：语音开始前先压低（避免人声首字被音乐盖住）
            startSeconds = std::max(0.0, rawStart - config.attackSeconds);
            // release 延后量：语音结束后延迟恢复（避免抽吸感）
            endSeconds = rawEnd + config.releaseSeconds;
        }
        if (endSeconds <= startSeconds) {
            continue;
        }
        region.start = secondsToTime(startSeconds, origin);
        region.end = secondsToTime(endSeconds, origin);
        region.gain = std::min(1.0, std::max(0.0, config.targetGain));
        result.regions.push_back(region);
    }
    result.voiceSeconds = voiceSeconds;

    // 区间可能因 attack 提前量重叠：合并重叠区间（保持单调不重叠，便于查询）
    std::vector<DuckRegion> merged;
    for (const auto& region : result.regions) {
        if (!merged.empty() && region.start <= merged.back().end) {
            merged.back().end = std::max(merged.back().end, region.end);
            merged.back().gain = std::min(merged.back().gain, region.gain);
        } else {
            merged.push_back(region);
        }
    }
    result.regions = std::move(merged);
    if (result.regions.empty()) {
        result.note = "未生成有效压低区间";
    }
    return result;
}

double duckGainAt(const std::vector<DuckRegion>& regions, const Time& t) {
    // 区间已按时间升序且不重叠：线性扫描（区间数通常很小）。
    for (const auto& region : regions) {
        if (t < region.start) {
            break;  // 升序：后续区间更晚
        }
        if (t < region.end) {
            return region.gain;
        }
    }
    return 1.0;
}

}  // namespace core
}  // namespace nairoclip
