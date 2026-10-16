#include "nairoclip/core/interpolation_ai.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

/// RGBA8 → CHW float（[0,1]；RIFE 标准输入）。
void preprocessRife(const std::uint8_t* pixels, int width, int height,
                    std::vector<float>& out) {
    out.assign(static_cast<std::size_t>(3) * static_cast<std::size_t>(height) *
                   static_cast<std::size_t>(width),
               0.0f);
    const std::size_t plane = static_cast<std::size_t>(height) * static_cast<std::size_t>(width);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* px =
                pixels + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                          static_cast<std::size_t>(x)) * 4;
            const std::size_t idx = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            for (int c = 0; c < 3; ++c) {
                out[static_cast<std::size_t>(c) * plane + idx] =
                    static_cast<float>(px[c]) / 255.0f;
            }
        }
    }
}

/// CHW float → RGBA8。
void postprocessRife(const std::vector<float>& in, int width, int height,
                     std::vector<std::uint8_t>& outRgba) {
    outRgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 255);
    const std::size_t plane = static_cast<std::size_t>(height) * static_cast<std::size_t>(width);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) *
                                        static_cast<std::size_t>(width) +
                                    static_cast<std::size_t>(x);
            std::uint8_t* px = outRgba.data() + idx * 4;
            for (int c = 0; c < 3; ++c) {
                const std::size_t src = static_cast<std::size_t>(c) * plane + idx;
                const float value = src < in.size() ? in[src] : 0.0f;
                px[c] = static_cast<std::uint8_t>(
                    std::clamp(value * 255.0f, 0.0f, 255.0f) + 0.5f);
            }
            px[3] = 255;
        }
    }
}

/// 双线性缩放 CHW float（高精度模式内部放大推理）。
void scaleCHW(const std::vector<float>& in, int srcW, int srcH, int dstW, int dstH,
              std::vector<float>& out) {
    out.assign(static_cast<std::size_t>(3) * static_cast<std::size_t>(dstW) *
                   static_cast<std::size_t>(dstH),
               0.0f);
    const std::size_t srcPlane = static_cast<std::size_t>(srcW) * static_cast<std::size_t>(srcH);
    const std::size_t dstPlane = static_cast<std::size_t>(dstW) * static_cast<std::size_t>(dstH);
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < dstH; ++y) {
            for (int x = 0; x < dstW; ++x) {
                const double u = (x + 0.5) * srcW / dstW - 0.5;
                const double v = (y + 0.5) * srcH / dstH - 0.5;
                const int x0 = std::clamp(static_cast<int>(std::floor(u)), 0, srcW - 1);
                const int y0 = std::clamp(static_cast<int>(std::floor(v)), 0, srcH - 1);
                const int x1 = std::min(srcW - 1, x0 + 1);
                const int y1 = std::min(srcH - 1, y0 + 1);
                const double fx = std::clamp(u - std::floor(u), 0.0, 1.0);
                const double fy = std::clamp(v - std::floor(v), 0.0, 1.0);
                const auto at = [&](int xx, int yy) {
                    return static_cast<double>(
                        in[static_cast<std::size_t>(c) * srcPlane +
                           static_cast<std::size_t>(yy) * static_cast<std::size_t>(srcW) +
                           static_cast<std::size_t>(xx)]);
                };
                const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
                const double bot = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
                out[static_cast<std::size_t>(c) * dstPlane +
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) +
                    static_cast<std::size_t>(x)] = static_cast<float>(top * (1.0 - fy) + bot * fy);
            }
        }
    }
}

}  // namespace

const char* interpolationTargetName(InterpolationTarget target) {
    switch (target) {
        case InterpolationTarget::Fps60: return "60";
        case InterpolationTarget::Fps120: return "120";
        case InterpolationTarget::Fps240: return "240";
    }
    return "60";
}

bool interpolationTargetFromInt(int value, InterpolationTarget& out) {
    switch (value) {
        case 60: out = InterpolationTarget::Fps60; return true;
        case 120: out = InterpolationTarget::Fps120; return true;
        case 240: out = InterpolationTarget::Fps240; return true;
        default: return false;
    }
}

int interpolationInsertCount(int sourceFps, InterpolationTarget target) {
    if (sourceFps <= 0) {
        return 0;
    }
    const int targetFps = static_cast<int>(target);
    if (targetFps <= sourceFps) {
        return 0;  // 无需插值
    }
    // 每对相邻帧插入 count 帧后：源帧率 × (count + 1) ≈ 目标帧率
    const int count = (targetFps / sourceFps) - 1;
    return std::max(0, count);
}

std::vector<double> interpolationTimesteps(int insertCount, double smoothness) {
    std::vector<double> steps;
    if (insertCount <= 0) {
        return steps;
    }
    smoothness = std::clamp(smoothness, 0.0, 100.0);
    for (int i = 1; i <= insertCount; ++i) {
        double t = static_cast<double>(i) / (insertCount + 1);  // 均匀分布（0..1 开）
        // 平滑度调整：>50 → 中间密集（非线性映射）；<50 → 更加均匀/两端密集
        if (smoothness > 50.0 && insertCount > 1) {
            const double strength = (smoothness - 50.0) / 50.0;  // 0..1
            // 以 0.5 为中心的 S 曲线（中间密集）
            const double centered = (t - 0.5) * 2.0;             // -1..1
            const double warped = std::sin(centered * 3.141592653589793 / 2.0);
            t = 0.5 + warped * 0.5 * (1.0 - strength * 0.3);
        }
        steps.push_back(std::clamp(t, 0.0, 1.0));
    }
    return steps;
}

struct InterpolationEngine::Impl {
    AiSession session;
};

InterpolationEngine::InterpolationEngine() : impl_(new (std::nothrow) Impl()) {}
InterpolationEngine::~InterpolationEngine() = default;

bool InterpolationEngine::load(const std::string& modelPath, std::string* outputFailure) {
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
    // RIFE 需要 3 个输入（img0/img1/timestep）
    if (impl_->session.inputs().size() < 3) {
        if (outputFailure != nullptr) *outputFailure = "RIFE 模型输入不足 3 个（非补帧模型）";
        return false;
    }
    loaded_ = true;
    return true;
}

void InterpolationEngine::unload() {
    if (impl_ != nullptr) {
        impl_->session.unload();
    }
    loaded_ = false;
}

bool InterpolationEngine::interpolate(const std::uint8_t* frame0, const std::uint8_t* frame1,
                                      int width, int height, double timestep,
                                      InterpolationMode mode, std::vector<std::uint8_t>& outRgba,
                                      std::string* outputFailure) {
    outRgba.clear();
    if (!loaded_ || impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "补帧模型未加载";
        return false;
    }
    if (frame0 == nullptr || frame1 == nullptr || width <= 0 || height <= 0) {
        if (outputFailure != nullptr) *outputFailure = "帧尺寸非法";
        return false;
    }
    timestep = std::clamp(timestep, 0.0, 1.0);
    // 高精度模式：内部 2x 放大推理再降采样（文档「高精度(较慢)」）
    const int inferW = mode == InterpolationMode::HighPrecision ? width * 2 : width;
    const int inferH = mode == InterpolationMode::HighPrecision ? height * 2 : height;
    std::vector<float> in0;
    std::vector<float> in1;
    preprocessRife(frame0, width, height, in0);
    preprocessRife(frame1, width, height, in1);
    if (inferW != width || inferH != height) {
        std::vector<float> scaled0;
        std::vector<float> scaled1;
        scaleCHW(in0, width, height, inferW, inferH, scaled0);
        scaleCHW(in1, width, height, inferW, inferH, scaled1);
        in0 = std::move(scaled0);
        in1 = std::move(scaled1);
    }
    AiTensor img0;
    AiTensor img1;
    AiTensor ts;
    img0.shape = {1, 3, inferH, inferW};
    img0.dataFloat = std::move(in0);
    img1.shape = {1, 3, inferH, inferW};
    img1.dataFloat = std::move(in1);
    ts.shape = {1};
    ts.dataFloat = {static_cast<float>(timestep)};
    std::vector<AiTensor> outputs;
    if (!impl_->session.run({img0, img1, ts}, outputs, outputFailure)) {
        return false;
    }
    if (outputs.empty() || outputs[0].dataFloat.empty()) {
        if (outputFailure != nullptr) *outputFailure = "推理输出为空";
        return false;
    }
    // 输出可能为推理分辨率（高精度模式）→ 降采样回源尺寸
    const auto& shape = outputs[0].shape;
    int outH = inferH;
    int outW = inferW;
    if (shape.size() >= 4) {
        outH = static_cast<int>(shape[2]);
        outW = static_cast<int>(shape[3]);
    } else if (shape.size() == 3) {
        outH = static_cast<int>(shape[1]);
        outW = static_cast<int>(shape[2]);
    }
    if (outW != width || outH != height) {
        std::vector<float> scaled;
        scaleCHW(outputs[0].dataFloat, outW, outH, width, height, scaled);
        postprocessRife(scaled, width, height, outRgba);
    } else {
        postprocessRife(outputs[0].dataFloat, width, height, outRgba);
    }
    return true;
}

const char* interpolationAiPurpose() {
    return "光流补帧 AI：RIFE ONNX 真实推理（两帧 + timestep → 中间帧；"
           "文档 Part 4-A 1.7；目标帧率 60/120/240 + 运动平滑度）";
}

}  // namespace core
}  // namespace nairoclip
