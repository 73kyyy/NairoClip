#include "nairoclip/core/audio_denoise.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;
/// 分析窗长（与 AudioDenoiser::analysisWindow() 一致）。
constexpr int kN = 1024;
/// hop（与 AudioDenoiser::hopSize() 一致；50% 重叠）。
constexpr int kH = 512;

/// 噪声估计平滑系数（逐频点；可在构建时用 -D 覆盖以便校准扫描，默认值见下）：
///   · 谱幅度低于估计（mag < noise）→ 快速跟随下降（噪声变安静时迅速收敛）；
///   · 谱幅度高于估计（mag > noise）→ 缓慢上浮（避免把语音当作噪声吞掉）。
/// 两者都按「每 hop」的固定比率（与采样率无关：hop 固定 512 样本）。
/// 校准结论（见 tests/test_audio_denoise.cpp 的保留/抑制断言）：
///   · 快速跟随 0.30：噪声变安静时 ~5 个 hop（≈50ms）内收敛，实测良好；
///   · 慢速上升 0.002：语音突发（~200ms = 19 hop）内估计仅上浮约 4%，
///     语音频点不被误减；更快的上升率（0.02）会把突发语音减掉 10dB 以上。
#ifndef NAIROCLIP_DENOISE_FAST_FOLLOW
#define NAIROCLIP_DENOISE_FAST_FOLLOW 0.30
#endif
#ifndef NAIROCLIP_DENOISE_SLOW_RISE
#define NAIROCLIP_DENOISE_SLOW_RISE 0.002
#endif
constexpr double kNoiseFastFollow = NAIROCLIP_DENOISE_FAST_FOLLOW;
constexpr double kNoiseSlowRise = NAIROCLIP_DENOISE_SLOW_RISE;

/// 噪声估计偏差补偿：
/// 「快速跟随 + 慢速上升」的单边平滑会使稳态噪声估计系统性低于真实均值
/// （Rayleigh 幅度谱；对 fast=0.30 / slow=0.002 实测收敛到均值 ×0.262），
/// 导致谱减过量不足。该系数把估计恢复到无偏（1/0.262 ≈ 3.81），由纯噪声
/// 仿真校准并写入单元测试（Denoise.BiasCompensation）防止回归。
#ifndef NAIROCLIP_DENOISE_BIAS_COMP
#define NAIROCLIP_DENOISE_BIAS_COMP 3.81
#endif
constexpr double kNoiseBiasCompensation = NAIROCLIP_DENOISE_BIAS_COMP;

/// 周期型 Hann 窗（N 点；静态只构造一次）。
const std::vector<double>& hannWindow() {
    static const std::vector<double> window = [] {
        std::vector<double> w(static_cast<std::size_t>(kN));
        for (int i = 0; i < kN; ++i) {
            w[static_cast<std::size_t>(i)] =
                0.5 * (1.0 - std::cos(2.0 * kPi * i / kN));
        }
        return w;
    }();
    return window;
}

/// 原地基-2 FFT（迭代 Cooley-Tukey；长度必须为 2 的幂）。
/// sign = -1 正变换，+1 逆变换（逆变换含 1/n 归一化）。
void fft(std::vector<double>& re, std::vector<double>& im, int sign) {
    const int n = static_cast<int>(re.size());
    // 位反转置换
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) {
            j ^= bit;
        }
        j ^= bit;
        if (i < j) {
            std::swap(re[static_cast<std::size_t>(i)], re[static_cast<std::size_t>(j)]);
            std::swap(im[static_cast<std::size_t>(i)], im[static_cast<std::size_t>(j)]);
        }
    }
    // 蝶形运算
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = 2.0 * kPi / len * (sign < 0 ? -1.0 : 1.0);
        const double wRe = std::cos(angle);
        const double wIm = std::sin(angle);
        for (int i = 0; i < n; i += len) {
            double curRe = 1.0;
            double curIm = 0.0;
            for (int k = 0; k < len / 2; ++k) {
                const int a = i + k;
                const int b = i + k + len / 2;
                const double evenRe = re[static_cast<std::size_t>(a)];
                const double evenIm = im[static_cast<std::size_t>(a)];
                const double oddRe = re[static_cast<std::size_t>(b)] * curRe -
                                     im[static_cast<std::size_t>(b)] * curIm;
                const double oddIm = re[static_cast<std::size_t>(b)] * curIm +
                                     im[static_cast<std::size_t>(b)] * curRe;
                re[static_cast<std::size_t>(a)] = evenRe + oddRe;
                im[static_cast<std::size_t>(a)] = evenIm + oddIm;
                re[static_cast<std::size_t>(b)] = evenRe - oddRe;
                im[static_cast<std::size_t>(b)] = evenIm - oddIm;
                const double nextRe = curRe * wRe - curIm * wIm;
                curIm = curRe * wIm + curIm * wRe;
                curRe = nextRe;
            }
        }
    }
    if (sign > 0) {
        const double inv = 1.0 / n;
        for (int i = 0; i < n; ++i) {
            re[static_cast<std::size_t>(i)] *= inv;
            im[static_cast<std::size_t>(i)] *= inv;
        }
    }
}

}  // namespace

const char* denoiseStrengthName(DenoiseStrength strength) {
    switch (strength) {
        case DenoiseStrength::Off: return "off";
        case DenoiseStrength::Weak: return "weak";
        case DenoiseStrength::Medium: return "medium";
        case DenoiseStrength::Strong: return "strong";
    }
    return "off";
}

bool denoiseStrengthFromString(const char* text, DenoiseStrength& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (s == "off" || s == "关闭" || s == "关") { out = DenoiseStrength::Off; return true; }
    if (s == "weak" || s == "弱") { out = DenoiseStrength::Weak; return true; }
    if (s == "medium" || s == "中" || s == "中等") { out = DenoiseStrength::Medium; return true; }
    if (s == "strong" || s == "强") { out = DenoiseStrength::Strong; return true; }
    return false;
}

bool denoiseStrengthFromInt(int value, DenoiseStrength& out) {
    switch (value) {
        case 0: out = DenoiseStrength::Off; return true;
        case 1: out = DenoiseStrength::Weak; return true;
        case 2: out = DenoiseStrength::Medium; return true;
        case 3: out = DenoiseStrength::Strong; return true;
        default: return false;
    }
}

DenoiseTuning denoiseTuningFor(DenoiseStrength strength) {
    switch (strength) {
        case DenoiseStrength::Off:
            // 数学等价直通（α=1、β=1：减去 1 倍估计后仍不小于原幅度）
            return DenoiseTuning{1.0, 1.0};
        case DenoiseStrength::Weak:
            return DenoiseTuning{1.3, 0.12};
        case DenoiseStrength::Medium:
            return DenoiseTuning{1.8, 0.06};
        case DenoiseStrength::Strong:
            return DenoiseTuning{2.4, 0.02};
    }
    return DenoiseTuning{1.8, 0.06};
}

AudioDenoiser::AudioDenoiser(DenoiseStrength strength, int channels)
    : strength_(strength), channels_(channels) {
    if (channels_ < 1 || channels_ > 16) {
        // 非法通道数：退化为直通（防御；调用方应避免）
        active_ = false;
        channels_ = channels_ < 1 ? 1 : channels_;
        return;
    }
    active_ = strength_ != DenoiseStrength::Off;
    tuning_ = denoiseTuningFor(strength_);
    states_.resize(static_cast<std::size_t>(channels_));
    outChannels_.resize(static_cast<std::size_t>(channels_));
    if (active_) {
        for (auto& state : states_) {
            state.noiseMag.assign(static_cast<std::size_t>(kN / 2 + 1), 0.0);
            state.fftRe.assign(static_cast<std::size_t>(kN), 0.0);
            state.fftIm.assign(static_cast<std::size_t>(kN), 0.0);
            state.acc.assign(static_cast<std::size_t>(2 * kH), 0.0);
            state.wsum.assign(static_cast<std::size_t>(2 * kH), 0.0);
        }
    }
}

int AudioDenoiser::feedFramesFor(int desiredOutputFrames) const {
    if (desiredOutputFrames <= 0) {
        return 0;
    }
    if (!active_ || !firstPull_) {
        return desiredOutputFrames;
    }
    return desiredOutputFrames + kH;  // 首块建立半窗前视
}

void AudioDenoiser::reset() {
    for (auto& state : states_) {
        state.inFifo.clear();
        state.processedFrames = 0;
        state.inFifoStart = 0;
        state.anchor = 0;
        state.noiseInitialized = false;
        std::fill(state.noiseMag.begin(), state.noiseMag.end(), 0.0);
        std::fill(state.acc.begin(), state.acc.end(), 0.0);
        std::fill(state.wsum.begin(), state.wsum.end(), 0.0);
    }
    for (auto& queue : outChannels_) {
        queue.clear();
    }
    fedFrames_ = 0;
    emittedFrames_ = 0;
    deliveredFrames_ = 0;
    firstPull_ = true;
    finalReceived_ = false;
}

void AudioDenoiser::appendInput(const float* input, int frames) {
    if (frames <= 0) {
        return;
    }
    for (int ch = 0; ch < channels_; ++ch) {
        auto& fifo = states_[static_cast<std::size_t>(ch)].inFifo;
        const std::size_t base = fifo.size();
        fifo.resize(base + static_cast<std::size_t>(frames));
        for (int i = 0; i < frames; ++i) {
            fifo[base + static_cast<std::size_t>(i)] =
                input != nullptr
                    ? static_cast<double>(input[
                          static_cast<std::size_t>(i) * static_cast<std::size_t>(channels_) +
                          static_cast<std::size_t>(ch)])
                    : 0.0;
        }
    }
    fedFrames_ += frames;
}

void AudioDenoiser::processFrame(ChannelState& state) {
    const long long p = state.processedFrames;
    const long long frameStart = p * kH - kH;  // 帧左边界（绝对样本位置）
    const std::vector<double>& window = hannWindow();

    // 取窗（inFifo 之外补零 = 边界处理；isFinal 时构成右零填充）
    for (int i = 0; i < kN; ++i) {
        const long long abs = frameStart + i;
        const long long rel = abs - state.inFifoStart;
        double v = 0.0;
        if (rel >= 0 && rel < static_cast<long long>(state.inFifo.size())) {
            v = state.inFifo[static_cast<std::size_t>(rel)];
        }
        state.fftRe[static_cast<std::size_t>(i)] = v * window[static_cast<std::size_t>(i)];
        state.fftIm[static_cast<std::size_t>(i)] = 0.0;
    }

    fft(state.fftRe, state.fftIm, -1);

    // 幅度谱（单边 N/2+1）→ 噪声估计更新 → 谱减（保留相位）
    const int bins = kN / 2 + 1;
    for (int k = 0; k < bins; ++k) {
        const std::size_t idx = static_cast<std::size_t>(k);
        const double re = state.fftRe[idx];
        const double im = state.fftIm[idx];
        const double mag = std::sqrt(re * re + im * im);
        double& noise = state.noiseMag[idx];
        if (!state.noiseInitialized) {
            noise = mag;
        } else if (mag < noise) {
            noise += (mag - noise) * kNoiseFastFollow;
        } else {
            noise += (mag - noise) * kNoiseSlowRise;
        }
        // 谱减：|X'| = max(|X| - α·(N·comp), β·|X|)
        // comp：单边平滑导致的估计偏低补偿（见 kNoiseBiasCompensation）
        double outMag = mag - tuning_.overSubtraction * noise * kNoiseBiasCompensation;
        const double floorMag = tuning_.spectralFloor * mag;
        if (outMag < floorMag) {
            outMag = floorMag;
        }
        const double scale = mag > 1e-12 ? outMag / mag : 0.0;
        state.fftRe[idx] *= scale;
        state.fftIm[idx] *= scale;
    }
    state.noiseInitialized = true;

    // 共轭对称（恢复双边谱；k = N/2 自共轭，保持不变）
    for (int k = 1; k < kN / 2; ++k) {
        state.fftRe[static_cast<std::size_t>(kN - k)] =
            state.fftRe[static_cast<std::size_t>(k)];
        state.fftIm[static_cast<std::size_t>(kN - k)] =
            -state.fftIm[static_cast<std::size_t>(k)];
    }

    fft(state.fftRe, state.fftIm, +1);

    // OLA 累加（acc/wsum 覆盖 [anchor, anchor+2H)）
    for (int i = 0; i < kN; ++i) {
        const long long abs = frameStart + i;
        const long long rel = abs - state.anchor;
        if (rel < 0 || rel >= 2 * kH) {
            continue;
        }
        const double w = window[static_cast<std::size_t>(i)];
        const std::size_t idx = static_cast<std::size_t>(rel);
        state.acc[idx] += state.fftRe[static_cast<std::size_t>(i)] * w;
        state.wsum[idx] += w * w;
    }
    state.processedFrames = p + 1;
}

void AudioDenoiser::emitHop() {
    for (int ch = 0; ch < channels_; ++ch) {
        auto& state = states_[static_cast<std::size_t>(ch)];
        auto& queue = outChannels_[static_cast<std::size_t>(ch)];
        for (int i = 0; i < kH; ++i) {
            const std::size_t idx = static_cast<std::size_t>(i);
            const double w = state.wsum[idx];
            queue.push_back(w > 1e-9 ? state.acc[idx] / w : 0.0);
        }
        // 左移一个 hop（已发射部分不再需要）
        std::move(state.acc.begin() + kH, state.acc.end(), state.acc.begin());
        std::fill(state.acc.begin() + kH, state.acc.end(), 0.0);
        std::move(state.wsum.begin() + kH, state.wsum.end(), state.wsum.begin());
        std::fill(state.wsum.begin() + kH, state.wsum.end(), 0.0);
    }
    for (auto& state : states_) {
        state.anchor += kH;
    }
    emittedFrames_ += kH;
}

void AudioDenoiser::emitTailPartial(long long tail) {
    if (tail <= 0) {
        return;
    }
    for (int ch = 0; ch < channels_; ++ch) {
        auto& state = states_[static_cast<std::size_t>(ch)];
        auto& queue = outChannels_[static_cast<std::size_t>(ch)];
        for (long long i = 0; i < tail; ++i) {
            const std::size_t idx = static_cast<std::size_t>(i);
            const double w = state.wsum[idx];
            queue.push_back(w > 1e-9 ? state.acc[idx] / w : 0.0);
        }
    }
    emittedFrames_ += tail;
}

void AudioDenoiser::processFrames(bool isFinal) {
    while (true) {
        // 各通道同步；以通道 0 的帧号为准
        const long long p = states_[0].processedFrames;
        if (isFinal) {
            // 终块：帧左边界 (p-1)H < 已喂入量（右部零填充完成尾部 hop）
            if (!((p - 1) * kH < fedFrames_)) {
                break;
            }
        } else {
            // 非终块：窗右边界 (p+1)H <= 已喂入量（半窗前视就绪）
            if (!(p * kH + kH <= fedFrames_)) {
                break;
            }
        }
        for (auto& state : states_) {
            processFrame(state);
        }
        // 帧 p 完成后可发射 hop p-1（终块要求该 hop 完全位于 [0, 已喂入量)）
        if (p >= 1) {
            const long long hopEnd = p * kH;
            const bool canEmit = isFinal ? (hopEnd <= fedFrames_) : true;
            if (canEmit) {
                emitHop();
            }
        }
        // 修剪输入缓冲：未来帧的窗口自 pH 起（丢弃更早的数据，保证内存有界）
        const long long keepFrom = p * kH;
        for (auto& state : states_) {
            const long long drop = keepFrom - state.inFifoStart;
            if (drop > 0) {
                const long long n = std::min<long long>(
                    drop, static_cast<long long>(state.inFifo.size()));
                state.inFifo.erase(state.inFifo.begin(),
                                   state.inFifo.begin() + static_cast<std::ptrdiff_t>(n));
                state.inFifoStart += n;
            }
        }
    }
    if (isFinal) {
        // 尾部残余（不足 1 个 hop）：全部产出，保证「输出总样本数 == 已喂入总样本数」
        const long long tail = fedFrames_ - emittedFrames_;
        if (tail > 0) {
            emitTailPartial(tail);
        }
    }
}

std::size_t AudioDenoiser::drainOutput(int desiredFrames, std::vector<float>& output) {
    if (desiredFrames <= 0) {
        return 0;
    }
    std::size_t available = static_cast<std::size_t>(desiredFrames);
    for (const auto& queue : outChannels_) {
        available = std::min(available, queue.size());
    }
    if (available == 0) {
        return 0;
    }
    const std::size_t base = output.size();
    output.resize(base + available * static_cast<std::size_t>(channels_));
    for (int ch = 0; ch < channels_; ++ch) {
        auto& queue = outChannels_[static_cast<std::size_t>(ch)];
        for (std::size_t i = 0; i < available; ++i) {
            output[base + i * static_cast<std::size_t>(channels_) +
                   static_cast<std::size_t>(ch)] =
                static_cast<float>(queue[i]);
        }
        queue.erase(queue.begin(), queue.begin() + static_cast<std::ptrdiff_t>(available));
    }
    deliveredFrames_ += static_cast<long long>(available);
    return available;
}

std::size_t AudioDenoiser::pushAndPull(const float* input, int inputFrames, bool isFinal,
                                       std::vector<float>& output) {
    if (channels_ < 1) {
        return 0;
    }
    if (!active_) {
        // 直通（不消耗前视；输出 = 输入）
        if (input == nullptr || inputFrames <= 0) {
            return 0;
        }
        const std::size_t frames = static_cast<std::size_t>(inputFrames);
        const std::size_t total = frames * static_cast<std::size_t>(channels_);
        const std::size_t base = output.size();
        output.resize(base + total);
        for (std::size_t i = 0; i < total; ++i) {
            output[base + i] = input[i];
        }
        deliveredFrames_ += static_cast<long long>(frames);
        return frames;
    }
    if (finalReceived_) {
        return 0;  // 幂等：结束标记后不再产出
    }
    if (inputFrames < 0) {
        inputFrames = 0;
    }

    appendInput(input, inputFrames);
    processFrames(isFinal);

    std::size_t wanted = 0;
    if (isFinal) {
        // 终块：把剩余样本全部产出（含前视欠账），使总样本守恒
        const long long remaining = fedFrames_ - deliveredFrames_;
        if (remaining > 0) {
            wanted = static_cast<std::size_t>(remaining);
        }
    } else {
        // 稳态：期望产出 = 本次喂入量；首块扣掉半窗前视
        const long long want = static_cast<long long>(inputFrames) - (firstPull_ ? kH : 0);
        if (want > 0) {
            wanted = static_cast<std::size_t>(want);
        }
    }

    const std::size_t produced = drainOutput(static_cast<int>(
                                                   std::min<std::size_t>(
                                                       wanted,
                                                       static_cast<std::size_t>(0x7FFFFFFF))),
                                               output);
    if (isFinal) {
        finalReceived_ = true;
    }
    if (inputFrames > 0) {
        firstPull_ = false;
    }
    return produced;
}

}  // namespace core
}  // namespace nairoclip
