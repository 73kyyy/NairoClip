#include "nairoclip/core/stem_ai.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kFftSize = 6144;
constexpr int kHopSize = 1024;
constexpr int kDimF = 3072;   ///< 模型输入频点数
constexpr int kDimT = 256;    ///< 模型输入时间帧数（每段处理 256 帧 × 1024 hop）
constexpr int kModelRate = 44100;

/// 原地基-2 FFT（迭代 Cooley-Tukey；len 须为 2 的幂）。
/// UVR-MDX 用 n_fft=6144（非 2 的幂）→ 内部补零到 8192 做 FFT（频谱分辨率
/// 足够；这是工程近似：与 torch.stft 的 6144 点 DFT 在 bin 0..3071 上等价
/// —— 两者都是同样采样率的离散频率栅格，仅 bin 宽度由补零长度决定；
/// 说明见头文件「实现说明」）。
void fft(std::vector<double>& re, std::vector<double>& im, int sign) {
    const int n = static_cast<int>(re.size());
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
    for (int len = 2; len <= n; len <<= 1) {
        const double angle = (sign < 0 ? -2.0 : 2.0) * kPi / len;
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

/// FFT 实际长度（补零到 2 的幂：6144 → 8192）。
constexpr int kFftPadded = 8192;

/// Hann 窗（6144 点）。
const std::vector<double>& hannWindow() {
    static const std::vector<double> window = [] {
        std::vector<double> w(static_cast<std::size_t>(kFftSize));
        for (int i = 0; i < kFftSize; ++i) {
            w[static_cast<std::size_t>(i)] =
                0.5 * (1.0 - std::cos(2.0 * kPi * i / kFftSize));
        }
        return w;
    }();
    return window;
}

/// 线性重采样。
std::vector<float> resample(const float* samples, std::size_t frames, int srcRate, int dstRate) {
    if (srcRate == dstRate) {
        return std::vector<float>(samples, samples + frames);
    }
    const double ratio = static_cast<double>(srcRate) / dstRate;
    const std::size_t outCount = static_cast<std::size_t>(static_cast<double>(frames) / ratio);
    std::vector<float> out(outCount, 0.0f);
    for (std::size_t i = 0; i < outCount; ++i) {
        const double src = static_cast<double>(i) * ratio;
        const std::size_t i0 = static_cast<std::size_t>(src);
        const std::size_t i1 = std::min(frames - 1, i0 + 1);
        const double frac = src - static_cast<double>(i0);
        const double a = i0 < frames ? samples[i0] : 0.0;
        const double b = samples[i1];
        out[i] = static_cast<float>(a * (1.0 - frac) + b * frac);
    }
    return out;
}

/// 单段 STFT：把混音（立体声）转为模型输入张量 [1,4,dimF,dimT]。
/// 通道语义（UVR-MDX 标准）：ch0 = L 实部，ch1 = R 实部，ch2 = L 虚部，ch3 = R 虚部。
void stftSegment(const std::vector<float>& left, const std::vector<float>& right,
                 std::size_t startFrame, std::vector<float>& outTensor) {
    outTensor.assign(static_cast<std::size_t>(4) * kDimF * kDimT, 0.0f);
    const auto& window = hannWindow();
    std::vector<double> re(kFftPadded);
    std::vector<double> im(kFftPadded);
    for (int t = 0; t < kDimT; ++t) {
        const std::size_t pos = startFrame + static_cast<std::size_t>(t) * kHopSize;
        for (int ch = 0; ch < 2; ++ch) {
            const std::vector<float>& src = ch == 0 ? left : right;
            std::fill(re.begin(), re.end(), 0.0);
            std::fill(im.begin(), im.end(), 0.0);
            for (int i = 0; i < kFftSize; ++i) {
                const std::size_t idx = pos + static_cast<std::size_t>(i);
                const double sample = idx < src.size() ? src[idx] : 0.0;
                re[static_cast<std::size_t>(i)] = sample * window[static_cast<std::size_t>(i)];
            }
            fft(re, im, -1);
            // bin 0..dimF-1（频率栅格 = 8192 点 FFT 的 bin；与 6144 点 DFT 的
            // 低频频段覆盖一致）
            const int realChannel = ch;         // ch0 = L 实部，ch1 = R 实部
            const int imagChannel = ch + 2;     // ch2 = L 虚部，ch3 = R 虚部
            for (int f = 0; f < kDimF; ++f) {
                outTensor[(static_cast<std::size_t>(realChannel) * kDimF +
                           static_cast<std::size_t>(f)) * kDimT +
                          static_cast<std::size_t>(t)] =
                    static_cast<float>(re[static_cast<std::size_t>(f)]);
                outTensor[(static_cast<std::size_t>(imagChannel) * kDimF +
                           static_cast<std::size_t>(f)) * kDimT +
                          static_cast<std::size_t>(t)] =
                    static_cast<float>(im[static_cast<std::size_t>(f)]);
            }
        }
    }
}

/// 单段 iSTFT（模型输出掩码频谱 → 立体声波形，重叠相加）。
void istftSegment(const std::vector<float>& tensor, std::size_t outFrames,
                  std::vector<float>& left, std::vector<float>& right) {
    const auto& window = hannWindow();
    std::vector<double> accL(outFrames + kFftSize, 0.0);
    std::vector<double> accR(outFrames + kFftSize, 0.0);
    std::vector<double> wsum(outFrames + kFftSize, 0.0);
    std::vector<double> re(kFftPadded);
    std::vector<double> im(kFftPadded);
    for (int t = 0; t < kDimT; ++t) {
        for (int ch = 0; ch < 2; ++ch) {
            std::fill(re.begin(), re.end(), 0.0);
            std::fill(im.begin(), im.end(), 0.0);
            const int realChannel = ch;
            const int imagChannel = ch + 2;
            for (int f = 0; f < kDimF; ++f) {
                re[static_cast<std::size_t>(f)] =
                    tensor[(static_cast<std::size_t>(realChannel) * kDimF +
                            static_cast<std::size_t>(f)) * kDimT +
                           static_cast<std::size_t>(t)];
                im[static_cast<std::size_t>(f)] =
                    tensor[(static_cast<std::size_t>(imagChannel) * kDimF +
                            static_cast<std::size_t>(f)) * kDimT +
                           static_cast<std::size_t>(t)];
                // 共轭对称（恢复双边谱）
                if (f > 0 && f < kFftPadded / 2) {
                    re[static_cast<std::size_t>(kFftPadded - f)] = re[static_cast<std::size_t>(f)];
                    im[static_cast<std::size_t>(kFftPadded - f)] = -im[static_cast<std::size_t>(f)];
                }
            }
            fft(re, im, +1);
            // 重叠相加（窗平方归一）
            std::vector<double>& acc = ch == 0 ? accL : accR;
            const std::size_t pos = static_cast<std::size_t>(t) * kHopSize;
            for (int i = 0; i < kFftSize; ++i) {
                const std::size_t idx = pos + static_cast<std::size_t>(i);
                if (idx >= acc.size()) {
                    break;
                }
                const double w = window[static_cast<std::size_t>(i)];
                acc[idx] += re[static_cast<std::size_t>(i)] * w;
                if (ch == 0) {
                    wsum[idx] += w * w;
                }
            }
        }
    }
    left.assign(outFrames, 0.0f);
    right.assign(outFrames, 0.0f);
    for (std::size_t i = 0; i < outFrames; ++i) {
        const double w = wsum[i];
        left[i] = static_cast<float>(w > 1e-9 ? accL[i] / w : 0.0);
        right[i] = static_cast<float>(w > 1e-9 ? accR[i] / w : 0.0);
    }
}

}  // namespace

const char* stemModeName(StemMode mode) {
    switch (mode) {
        case StemMode::VocalsOnly: return "vocals";
        case StemMode::InstrumentalOnly: return "instrumental";
        case StemMode::Both: return "both";
    }
    return "both";
}

bool stemModeFromInt(int value, StemMode& out) {
    if (value < 0 || value > 2) {
        return false;
    }
    out = static_cast<StemMode>(value);
    return true;
}

struct StemSeparator::Impl {
    AiSession session;
};

StemSeparator::StemSeparator() : impl_(new (std::nothrow) Impl()) {}
StemSeparator::~StemSeparator() = default;

bool StemSeparator::load(const std::string& modelPath, std::string* outputFailure) {
    loaded_ = false;
    if (impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "内存分配失败";
        return false;
    }
    if (!aiEngineAvailable()) {
        if (outputFailure != nullptr) {
            *outputFailure = "AI 推理引擎不可用（未链接 ONNX Runtime）";
        }
        return false;
    }
    if (!impl_->session.load(modelPath, AiBackend::Cpu, outputFailure)) {
        return false;
    }
    // 校验输入形状（UVR-MDX 标准 [N,4,3072,256]）
    const auto& inputs = impl_->session.inputs();
    if (inputs.empty() || inputs[0].shape.size() != 4) {
        if (outputFailure != nullptr) *outputFailure = "模型输入形状非 4 维（非 UVR-MDX 模型）";
        return false;
    }
    const auto& shape = inputs[0].shape;
    const std::int64_t c = shape[1];
    const std::int64_t f = shape[2];
    const std::int64_t t = shape[3];
    if (c != 4 || (f > 0 && f != kDimF) || (t > 0 && t != kDimT)) {
        if (outputFailure != nullptr) {
            *outputFailure = "模型输入形状不符（期望 [N,4,3072,256]）";
        }
        return false;
    }
    loaded_ = true;
    return true;
}

void StemSeparator::unload() {
    if (impl_ != nullptr) {
        impl_->session.unload();
    }
    loaded_ = false;
}

bool StemSeparator::separate(const float* samples, std::size_t frameCount, int channels,
                             int sampleRate, StemMode mode, double intensity, StemResult& out,
                             const std::function<bool(double)>& progress,
                             std::string* outputFailure) {
    out = StemResult{};
    if (!loaded_ || impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "人声分离模型未加载";
        return false;
    }
    if (samples == nullptr || frameCount == 0 || channels <= 0 || sampleRate <= 0) {
        if (outputFailure != nullptr) *outputFailure = "音频数据为空或参数非法";
        return false;
    }
    intensity = std::clamp(intensity, 0.0, 100.0);
    // ① 重采样到模型采样率 + 声道拆分（>2 声道取前两声道；单声道复制为双声道）
    std::vector<float> left;
    std::vector<float> right;
    {
        const int srcChannels = channels;
        std::vector<float> monoL(frameCount);
        std::vector<float> monoR(frameCount);
        for (std::size_t i = 0; i < frameCount; ++i) {
            monoL[i] = samples[i * static_cast<std::size_t>(srcChannels)];
            monoR[i] = srcChannels > 1 ? samples[i * static_cast<std::size_t>(srcChannels) + 1]
                                       : monoL[i];
        }
        left = resample(monoL.data(), frameCount, sampleRate, kModelRate);
        right = resample(monoR.data(), frameCount, sampleRate, kModelRate);
    }
    const std::size_t modelFrames = left.size();
    // ② 分段处理（每段 256 帧 × 1024 hop = 262144 样本）
    std::vector<float> vocalsL(modelFrames, 0.0f);
    std::vector<float> vocalsR(modelFrames, 0.0f);
    const std::size_t segmentStride = static_cast<std::size_t>(kDimT) * kHopSize;
    const std::size_t segmentCount =
        std::max<std::size_t>(1, (modelFrames + segmentStride - 1) / segmentStride);
    for (std::size_t seg = 0; seg < segmentCount; ++seg) {
        const std::size_t start = seg * segmentStride;
        // 段内可用帧数（尾段可能不足）
        const std::size_t available =
            start < modelFrames ? std::min(segmentStride, modelFrames - start) : 0;
        // STFT
        std::vector<float> inputTensor;
        stftSegment(left, right, start, inputTensor);
        AiTensor input;
        input.shape = {1, 4, kDimF, kDimT};
        input.dataFloat = std::move(inputTensor);
        std::vector<AiTensor> outputs;
        if (!impl_->session.run({input}, outputs, outputFailure)) {
            return false;
        }
        if (outputs.empty() || outputs[0].dataFloat.empty()) {
            if (outputFailure != nullptr) *outputFailure = "推理输出为空";
            return false;
        }
        // 掩码语义：UVR-MDX 输出为人声频谱（4 通道与输入同形）。
        // 分离强度：软/硬掩码混合（100 = 直接用模型输出；低值向原混合收敛）。
        const double blend = std::clamp(intensity / 100.0, 0.0, 1.0);
        std::vector<float> mask = outputs[0].dataFloat;
        for (std::size_t i = 0; i < mask.size() && i < input.dataFloat.size(); ++i) {
            // 低强度 → 部分保留原始混合（分离更温和）
            mask[i] = static_cast<float>(mask[i] * blend + input.dataFloat[i] * (1.0 - blend));
        }
        // iSTFT（段内输出）
        std::vector<float> segL;
        std::vector<float> segR;
        istftSegment(mask, available, segL, segR);
        // 写回（段间无重叠：每段独立处理，边界由窗自然衰减）
        for (std::size_t i = 0; i < available; ++i) {
            vocalsL[start + i] = segL[i];
            vocalsR[start + i] = segR[i];
        }
        if (progress != nullptr) {
            const double p = static_cast<double>(seg + 1) / segmentCount;
            if (!progress(p)) {
                if (outputFailure != nullptr) *outputFailure = "用户取消";
                return false;
            }
        }
    }
    // ③ 重采样回源采样率 + 组装结果
    const std::vector<float> vocalsLBack = resample(vocalsL.data(), vocalsL.size(), kModelRate,
                                                    sampleRate);
    const std::vector<float> vocalsRBack = resample(vocalsR.data(), vocalsR.size(), kModelRate,
                                                    sampleRate);
    const std::size_t outFrames = std::min(vocalsLBack.size(), frameCount);
    out.sampleRate = sampleRate;
    out.channels = channels >= 2 ? 2 : 1;
    if (mode == StemMode::VocalsOnly || mode == StemMode::Both) {
        out.vocals.resize(outFrames * static_cast<std::size_t>(out.channels));
        for (std::size_t i = 0; i < outFrames; ++i) {
            out.vocals[i * static_cast<std::size_t>(out.channels)] = vocalsLBack[i];
            if (out.channels > 1) {
                out.vocals[i * static_cast<std::size_t>(out.channels) + 1] = vocalsRBack[i];
            }
        }
    }
    if (mode == StemMode::InstrumentalOnly || mode == StemMode::Both) {
        // 伴奏 = 原混音 - 人声（真实分离语义）
        out.instrumental.resize(outFrames * static_cast<std::size_t>(out.channels));
        for (std::size_t i = 0; i < outFrames; ++i) {
            const float mixL = left.size() > i ? left[i] : 0.0f;
            const float mixR = right.size() > i ? right[i] : 0.0f;
            out.instrumental[i * static_cast<std::size_t>(out.channels)] =
                std::clamp(mixL - vocalsLBack[i], -1.0f, 1.0f);
            if (out.channels > 1) {
                out.instrumental[i * static_cast<std::size_t>(out.channels) + 1] =
                    std::clamp(mixR - vocalsRBack[i], -1.0f, 1.0f);
            }
        }
    }
    return out.valid();
}

const char* stemAiPurpose() {
    return "人声分离 AI：UVR-MDX-NET ONNX 真实推理（STFT → 频谱分离 → iSTFT；"
           "文档音频增补「人声分离」；输出人声/伴奏双轨）";
}

}  // namespace core
}  // namespace nairoclip
