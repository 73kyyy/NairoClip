#include "nairoclip/core/depth_ai.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace nairoclip {
namespace core {

namespace {

/// 双线性缩放 RGBA8 → RGB float（0..1）并归一化（Depth-Anything 预处理）。
/// 输出布局：[1, 3, H, W]（CHW；ImageNet 均值/方差）。
void preprocessToCHW(const std::uint8_t* pixels, int srcW, int srcH, int dstW, int dstH,
                     std::vector<float>& out) {
    static constexpr float kMean[3] = {0.485f, 0.456f, 0.406f};
    static constexpr float kStd[3] = {0.229f, 0.224f, 0.225f};
    out.assign(static_cast<std::size_t>(3) * static_cast<std::size_t>(dstH) *
                   static_cast<std::size_t>(dstW),
               0.0f);
    const std::size_t plane = static_cast<std::size_t>(dstH) * static_cast<std::size_t>(dstW);
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            // 目标 → 源（双线性）
            const double u = (x + 0.5) * srcW / dstW - 0.5;
            const double v = (y + 0.5) * srcH / dstH - 0.5;
            const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, srcW - 1);
            const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, srcH - 1);
            const int x1 = std::min(srcW - 1, x0 + 1);
            const int y1 = std::min(srcH - 1, y0 + 1);
            const double fx = std::clamp(u - std::floor(u), 0.0, 1.0);
            const double fy = std::clamp(v - std::floor(v), 0.0, 1.0);
            for (int c = 0; c < 3; ++c) {
                const auto at = [&](int xx, int yy) {
                    return static_cast<double>(
                        pixels[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(srcW) +
                                static_cast<std::size_t>(xx)) * 4 + static_cast<std::size_t>(c)]) /
                           255.0;
                };
                const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
                const double bot = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
                double value = top * (1.0 - fy) + bot * fy;
                value = (value - kMean[c]) / kStd[c];
                out[static_cast<std::size_t>(c) * plane +
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) +
                    static_cast<std::size_t>(x)] = static_cast<float>(value);
            }
        }
    }
}

/// 后处理：单通道深度 → 归一化灰度（近=255 近白 / 远=0 黑。
/// Depth-Anything 输出为「相对逆深度」（值越大越近），此处按该语义映射）。
void postprocessDepth(const std::vector<float>& raw, int w, int h,
                      std::vector<std::uint8_t>& outRgba) {
    outRgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 255);
    if (raw.empty()) {
        return;
    }
    float mn = raw[0];
    float mx = raw[0];
    for (float v : raw) {
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }
    const float span = (mx - mn) > 1e-6f ? (mx - mn) : 1.0f;
    const std::size_t count = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < count && i < raw.size(); ++i) {
        const float norm = (raw[i] - mn) / span;  // 0=最远 1=最近
        const std::uint8_t gray =
            static_cast<std::uint8_t>(std::clamp(norm * 255.0f, 0.0f, 255.0f) + 0.5f);
        std::uint8_t* px = outRgba.data() + i * 4;
        px[0] = gray;
        px[1] = gray;
        px[2] = gray;
        px[3] = 255;
    }
}

/// 双线性采样灰度深度（归一化坐标）。
double sampleDepth(const std::vector<std::uint8_t>& depth, int w, int h, double u, double v) {
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double x = u * (w - 1);
    const double y = v * (h - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(w - 1, x0 + 1);
    const int y1 = std::min(h - 1, y0 + 1);
    const double fx = x - x0;
    const double fy = y - y0;
    const auto at = [&](int xx, int yy) {
        return static_cast<double>(depth[static_cast<std::size_t>(yy) *
                                              static_cast<std::size_t>(w) +
                                          static_cast<std::size_t>(xx)]) /
               255.0;
    };
    const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
    const double bot = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
    return top * (1.0 - fy) + bot * fy;
}

/// 双线性采样 RGBA（归一化坐标；边缘钳制）。
void sampleRgba(const std::uint8_t* pixels, int w, int h, double u, double v,
                std::uint8_t* outRgba) {
    u = std::clamp(u, 0.0, 1.0);
    v = std::clamp(v, 0.0, 1.0);
    const double x = u * (w - 1);
    const double y = v * (h - 1);
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(w - 1, x0 + 1);
    const int y1 = std::min(h - 1, y0 + 1);
    const double fx = x - x0;
    const double fy = y - y0;
    for (int c = 0; c < 4; ++c) {
        const auto at = [&](int xx, int yy) {
            return static_cast<double>(
                pixels[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(w) +
                        static_cast<std::size_t>(xx)) * 4 + static_cast<std::size_t>(c)]);
        };
        const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
        const double bot = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
        outRgba[c] = static_cast<std::uint8_t>(std::clamp(top * (1.0 - fy) + bot * fy,
                                                          0.0, 255.0) + 0.5);
    }
}

}  // namespace

// =============================================================================
// 推理器
// =============================================================================

struct DepthEstimator::Impl {
    AiSession session;
    std::string inputName;
    std::string outputName;
};

DepthEstimator::DepthEstimator() : impl_(new (std::nothrow) Impl()) {}
DepthEstimator::~DepthEstimator() = default;

bool DepthEstimator::load(const std::string& modelPath, std::string* outputFailure) {
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
    const auto& inputs = impl_->session.inputs();
    const auto& outputs = impl_->session.outputs();
    if (inputs.empty() || outputs.empty()) {
        if (outputFailure != nullptr) *outputFailure = "深度模型输入/输出为空";
        return false;
    }
    impl_->inputName = inputs[0].name;
    impl_->outputName = outputs[0].name;
    loaded_ = true;
    return true;
}

void DepthEstimator::unload() {
    if (impl_ != nullptr) {
        impl_->session.unload();
    }
    loaded_ = false;
}

bool DepthEstimator::estimate(const std::uint8_t* pixels, int width, int height,
                              std::vector<std::uint8_t>& outDepth,
                              std::string* outputFailure) {
    outDepth.clear();
    if (!loaded_ || impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "深度模型未加载";
        return false;
    }
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (outputFailure != nullptr) *outputFailure = "帧尺寸非法";
        return false;
    }
    const int size = inputSize();
    // 预处理 → CHW 张量
    AiTensor input;
    input.shape = {1, 3, size, size};
    preprocessToCHW(pixels, width, height, size, size, input.dataFloat);
    std::vector<AiTensor> outputs;
    if (!impl_->session.run({input}, outputs, outputFailure)) {
        return false;
    }
    if (outputs.empty() || outputs[0].dataFloat.empty()) {
        if (outputFailure != nullptr) *outputFailure = "推理输出为空";
        return false;
    }
    // 后处理：模型输出尺寸（通常 = 输入尺寸）→ 缩放到源帧尺寸
    const auto& outShape = outputs[0].shape;
    int outH = size;
    int outW = size;
    if (outShape.size() >= 3) {
        outH = static_cast<int>(outShape[outShape.size() - 2]);
        outW = static_cast<int>(outShape[outShape.size() - 1]);
    }
    const std::vector<float>& raw = outputs[0].dataFloat;
    // 先把模型输出归一化到灰度（模型分辨率）
    std::vector<std::uint8_t> modelGray;
    postprocessDepth(raw, outW, outH, modelGray);
    // 再缩放到源帧尺寸（双线性）
    outDepth.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double u = (x + 0.5) / width;
            const double v = (y + 0.5) / height;
            const double value = sampleDepth(modelGray, outW, outH, u, v);
            outDepth[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(std::clamp(value * 255.0, 0.0, 255.0) + 0.5);
        }
    }
    return true;
}

// =============================================================================
// 4 种应用效果
// =============================================================================

bool applyBokehEffect(std::uint8_t* pixels, int width, int height,
                      const std::vector<std::uint8_t>& depth, const BokehParams& params) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (depth.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }
    const double intensity = std::clamp(params.intensity, 0.0, 100.0) / 100.0;
    if (intensity <= 0.0) {
        return true;
    }
    // 光圈：1.0..22.0 → 半径系数（光圈越小虚化越强）
    const double apertureFactor = std::clamp((22.0 - std::clamp(params.aperture, 1.0, 22.0)) /
                                                 21.0,
                                             0.0, 1.0);
    // 最大模糊半径：随强度与光圈增长（基准 = 画面短边的 8%；
    // 修复——初版系数 0.04 且再乘 weight 后在小尺寸下取整为 0，虚化无效）。
    const int radius = std::max(1, static_cast<int>(std::lround(
                                       (1.0 + apertureFactor * 2.0) * intensity *
                                       std::min(width, height) * 0.08)));
    // 焦点深度（0..1 映射为灰度 0..255）
    const double focus = std::clamp(params.focusDistance, 0.0, 1.0) * 255.0;
    std::vector<std::uint8_t> source(pixels, pixels + static_cast<std::size_t>(width) *
                                                        static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            const double depthValue = depth[idx];
            // 与焦点的深度差 → 模糊权重（0..1）
            const double distance = std::min(1.0, std::fabs(depthValue - focus) / 128.0);
            const double weight = distance * intensity;
            if (weight <= 0.02) {
                continue;  // 焦点附近：保持清晰
            }
            // 盒式模糊（半径按权重缩放；权重过小时至少 1 像素，保证真实模糊）
            const int r = std::max(1, static_cast<int>(std::lround(radius * weight)));
            int sum[3] = {0, 0, 0};
            int count = 0;
            for (int dy = -r; dy <= r; ++dy) {
                const int yy = std::clamp(y + dy, 0, height - 1);
                for (int dx = -r; dx <= r; ++dx) {
                    const int xx = std::clamp(x + dx, 0, width - 1);
                    const std::uint8_t* px =
                        source.data() + (static_cast<std::size_t>(yy) *
                                             static_cast<std::size_t>(width) +
                                         static_cast<std::size_t>(xx)) *
                                            4;
                    sum[0] += px[0];
                    sum[1] += px[1];
                    sum[2] += px[2];
                    ++count;
                }
            }
            if (count == 0) {
                continue;
            }
            std::uint8_t* dst = pixels + idx * 4;
            for (int c = 0; c < 3; ++c) {
                const double blurred = static_cast<double>(sum[c]) / count;
                dst[c] = static_cast<std::uint8_t>(
                    std::clamp(dst[c] * (1.0 - weight) + blurred * weight, 0.0, 255.0) + 0.5);
            }
        }
    }
    return true;
}

bool applyFogEffect(std::uint8_t* pixels, int width, int height,
                    const std::vector<std::uint8_t>& depth, const FogParams& params) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (depth.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }
    const double intensity = std::clamp(params.intensity, 0.0, 100.0) / 100.0;
    if (intensity <= 0.0) {
        return true;
    }
    const std::uint8_t fogR = static_cast<std::uint8_t>((params.colorRgba >> 24) & 0xFF);
    const std::uint8_t fogG = static_cast<std::uint8_t>((params.colorRgba >> 16) & 0xFF);
    const std::uint8_t fogB = static_cast<std::uint8_t>((params.colorRgba >> 8) & 0xFF);
    const double start = std::clamp(params.startDistance, 0.0, 1.0);
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < count; ++i) {
        // 深度灰度：0=最近 255=最远（postprocessDepth 语义：近=255 白）
        // 说明：postprocessDepth 输出「近=255」；远 = 低值。
        //       雾化应对「远」起雾 → 以 (1 - depth/255) 为远端权重。
        const double farWeight = 1.0 - static_cast<double>(depth[i]) / 255.0;
        if (farWeight <= start) {
            continue;
        }
        const double fogAmount =
            std::clamp((farWeight - start) / std::max(1e-6, 1.0 - start), 0.0, 1.0) * intensity;
        if (fogAmount <= 0.0) {
            continue;
        }
        std::uint8_t* px = pixels + i * 4;
        px[0] = static_cast<std::uint8_t>(
            std::clamp(px[0] * (1.0 - fogAmount) + fogR * fogAmount, 0.0, 255.0) + 0.5);
        px[1] = static_cast<std::uint8_t>(
            std::clamp(px[1] * (1.0 - fogAmount) + fogG * fogAmount, 0.0, 255.0) + 0.5);
        px[2] = static_cast<std::uint8_t>(
            std::clamp(px[2] * (1.0 - fogAmount) + fogB * fogAmount, 0.0, 255.0) + 0.5);
    }
    return true;
}

bool applyColorBandsEffect(std::uint8_t* pixels, int width, int height,
                           const std::vector<std::uint8_t>& depth,
                           const ColorBandsParams& params) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (depth.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }
    // 拆色（近/中/远；文档 13.2 默认暖黄/自然/冷蓝）
    const auto channel = [](std::uint32_t rgba, int index) {
        return static_cast<std::uint8_t>((rgba >> (24 - index * 8)) & 0xFF);
    };
    std::uint8_t near[3] = {channel(params.nearRgba, 0), channel(params.nearRgba, 1),
                            channel(params.nearRgba, 2)};
    std::uint8_t mid[3] = {channel(params.midRgba, 0), channel(params.midRgba, 1),
                           channel(params.midRgba, 2)};
    std::uint8_t far[3] = {channel(params.farRgba, 0), channel(params.farRgba, 1),
                           channel(params.farRgba, 2)};
    // 过渡柔和度：0 = 硬边界（三档阈值）；100 = 平滑渐变
    const double smoothness = std::clamp(params.smoothness, 0.0, 100.0) / 100.0;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    for (std::size_t i = 0; i < count; ++i) {
        const double d = static_cast<double>(depth[i]) / 255.0;  // 1=近 0=远
        double wNear = 0.0;
        double wMid = 0.0;
        double wFar = 0.0;
        if (smoothness < 1e-6) {
            // 硬边界（阈值 0.66 / 0.33）
            wNear = d > 0.66 ? 1.0 : 0.0;
            wMid = (d <= 0.66 && d > 0.33) ? 1.0 : 0.0;
            wFar = d <= 0.33 ? 1.0 : 0.0;
        } else {
            // 平滑：以 0.66/0.33 为中心的过渡带（带宽随柔和度）
            const double band = 0.05 + smoothness * 0.35;
            const double t1 = 0.66;
            const double t2 = 0.33;
            const auto smoothstep = [](double edge0, double edge1, double x) {
                const double t = std::clamp((x - edge0) / std::max(1e-6, edge1 - edge0), 0.0, 1.0);
                return t * t * (3.0 - 2.0 * t);
            };
            wNear = smoothstep(t1 - band, t1 + band, d);
            wFar = 1.0 - smoothstep(t2 - band, t2 + band, d);
            wMid = std::max(0.0, 1.0 - wNear - wFar);
        }
        std::uint8_t* px = pixels + i * 4;
        for (int c = 0; c < 3; ++c) {
            const double mixed = wNear * near[c] + wMid * mid[c] + wFar * far[c];
            // 与原图混合（保留纹理：50% 原图 + 50% 色调叠加）
            px[c] = static_cast<std::uint8_t>(
                std::clamp(px[c] * 0.5 + mixed * 0.5, 0.0, 255.0) + 0.5);
        }
    }
    return true;
}

bool applyParallaxEffect(std::uint8_t* pixels, int width, int height,
                         const std::vector<std::uint8_t>& depth, const ParallaxParams& params,
                         double t) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    if (depth.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) {
        return false;
    }
    const double intensity = std::clamp(params.intensity, 0.0, 100.0) / 100.0;
    if (intensity <= 0.0) {
        return true;
    }
    // 轨迹：0 静止 / 1 摇摆（正弦）/ 2 环绕（圆周）
    double offsetX = 0.0;
    double offsetY = 0.0;
    const double speedFactor = params.speed == 0 ? 0.5 : (params.speed == 1 ? 1.0 : 2.0);
    const double phase = t * speedFactor;
    switch (params.trajectory) {
        case 1:  // 摇摆
            offsetX = std::sin(phase * 2.0) * intensity * 0.04;
            break;
        case 2:  // 环绕
            offsetX = std::cos(phase * 2.0) * intensity * 0.03;
            offsetY = std::sin(phase * 2.0) * intensity * 0.03;
            break;
        default:  // 静止：仅按深度做静态偏移（立体重感）
            offsetX = intensity * 0.015;
            break;
    }
    std::vector<std::uint8_t> source(pixels, pixels + static_cast<std::size_t>(width) *
                                                        static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            // 近处（高深度）位移大、远处（低深度）位移小（视差语义）
            const double parallax = static_cast<double>(depth[idx]) / 255.0;
            const double u = (x + offsetX * parallax * width) / std::max(1, width - 1);
            const double v = (y + offsetY * parallax * height) / std::max(1, height - 1);
            sampleRgba(source.data(), width, height, u, v, pixels + idx * 4);
        }
    }
    return true;
}

const char* depthAiPurpose() {
    return "深度图 AI：Depth-Anything-V2-Small ONNX 真实推理 + 4 种应用效果"
           "（景深虚化/雾化/色彩分层/视差3D；文档 Part 4-C 13.2）";
}

}  // namespace core
}  // namespace nairoclip
