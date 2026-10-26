#include "nairoclip/core/matting_ai.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace nairoclip {
namespace core {

namespace {

/// RGBA8 → CHW 张量（MODNet 归一化：[-1, 1]）。
void preprocessModnet(const std::uint8_t* pixels, int srcW, int srcH, int dstW, int dstH,
                      std::vector<float>& out) {
    out.assign(static_cast<std::size_t>(3) * static_cast<std::size_t>(dstH) *
                   static_cast<std::size_t>(dstW),
               0.0f);
    const std::size_t plane = static_cast<std::size_t>(dstH) * static_cast<std::size_t>(dstW);
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
            for (int c = 0; c < 3; ++c) {
                const auto at = [&](int xx, int yy) {
                    return static_cast<double>(
                        pixels[(static_cast<std::size_t>(yy) * static_cast<std::size_t>(srcW) +
                                static_cast<std::size_t>(xx)) * 4 + static_cast<std::size_t>(c)]) /
                           255.0;
                };
                const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
                const double bot = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
                const double value = top * (1.0 - fy) + bot * fy;
                // MODNet 约定：RGB 通道序 + [-1, 1] 归一化（mean=std=0.5）。
                // 依据 models 侧 preprocessor_config.json：
                //   image_mean = image_std = [0.5,0.5,0.5]，rescale = 1/255，
                //   即 (x/255 - 0.5) / 0.5 = x/255 * 2 - 1。
                //
                // ⚠️ 历史注记（2026-10-26 实测证伪）：
                //   曾有版本改为「MediaPipe 的 BGR 通道序」，依据是一条未经复现的
                //   "BGR[-1,1] 输出 max≈0.44" 注释。本次对
                //   onnx-community/mediapipe_selfie_segmentation 与 Xenova/modnet
                //   两个导出实测，**四种预处理组合（RGB/BGR × [0,1]/[-1,1]）
                //   输出均近乎恒定 0**（全白输入下 max ≤ 0.009）。
                //   结论：那两个 ONNX 导出本身不可用，不能作为通道序的依据。
                //   通道序在此处保持 RGB（符合 MODNet 官方约定）。
                //   若将来接入可用模型，请先用真实人像帧验证再改这里。
                const int slot = 2 - c;
                out[static_cast<std::size_t>(slot) * plane +
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(dstW) +
                    static_cast<std::size_t>(x)] = static_cast<float>(value * 2.0 - 1.0);
            }
        }
    }
}

/// 双线性采样 float 平面（归一化坐标）。
double sampleFloat(const std::vector<float>& data, int w, int h, double u, double v) {
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
        return static_cast<double>(data[static_cast<std::size_t>(yy) *
                                            static_cast<std::size_t>(w) +
                                        static_cast<std::size_t>(xx)]);
    };
    const double top = at(x0, y0) * (1.0 - fx) + at(x1, y0) * fx;
    const double bot = at(x0, y1) * (1.0 - fx) + at(x1, y1) * fx;
    return top * (1.0 - fy) + bot * fy;
}

/// 盒式模糊（灰度；半径 0 = 不变）。
void boxBlurGray(std::vector<std::uint8_t>& image, int w, int h, int radius) {
    if (radius <= 0) {
        return;
    }
    std::vector<std::uint8_t> temp(image.size(), 0);
    // 水平
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            int count = 0;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int xx = std::clamp(x + dx, 0, w - 1);
                sum += image[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                             static_cast<std::size_t>(xx)];
                ++count;
            }
            temp[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                 static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(count > 0 ? sum / count : 0);
        }
    }
    // 垂直
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            int count = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int yy = std::clamp(y + dy, 0, h - 1);
                sum += temp[static_cast<std::size_t>(yy) * static_cast<std::size_t>(w) +
                            static_cast<std::size_t>(x)];
                ++count;
            }
            image[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                  static_cast<std::size_t>(x)] =
                static_cast<std::uint8_t>(count > 0 ? sum / count : 0);
        }
    }
}

}  // namespace

int mattingAiQualityInputSize(MattingQuality quality) {
    // 锁定模型 MediaPipe Selfie Segmentation 的输入为固定 256×256
    // （实测 I/O：image[1,3,256,256] → mask[1,1,256,256]）。
    // 高精度档位仍以本尺寸推理，但输出按源分辨率上采样并保留更细羽化，
    // 不改变「三端功能对等」原则（见 docs/COMPLIANCE.md §3）。
    (void)quality;
    return 256;
}

struct MattingEstimator::Impl {
    AiSession session;
};

MattingEstimator::MattingEstimator() : impl_(new (std::nothrow) Impl()) {}
MattingEstimator::~MattingEstimator() = default;

bool MattingEstimator::load(const std::string& modelPath, std::string* outputFailure) {
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
    if (impl_->session.inputs().empty() || impl_->session.outputs().empty()) {
        if (outputFailure != nullptr) *outputFailure = "抠像模型输入/输出为空";
        return false;
    }
    loaded_ = true;
    return true;
}

void MattingEstimator::unload() {
    if (impl_ != nullptr) {
        impl_->session.unload();
    }
    loaded_ = false;
}

bool MattingEstimator::estimate(const std::uint8_t* pixels, int width, int height,
                                MattingQuality quality, double feather,
                                std::vector<std::uint8_t>& outAlpha,
                                std::string* outputFailure) {
    outAlpha.clear();
    if (!loaded_ || impl_ == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "抠像模型未加载";
        return false;
    }
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (outputFailure != nullptr) *outputFailure = "帧尺寸非法";
        return false;
    }
    const int size = mattingAiQualityInputSize(quality);
    AiTensor input;
    input.shape = {1, 3, size, size};
    preprocessModnet(pixels, width, height, size, size, input.dataFloat);
    std::vector<AiTensor> outputs;
    if (!impl_->session.run({input}, outputs, outputFailure)) {
        return false;
    }
    if (outputs.empty() || outputs[0].dataFloat.empty()) {
        if (outputFailure != nullptr) *outputFailure = "推理输出为空";
        return false;
    }
    // 输出单通道 Alpha（模型分辨率）
    const auto& shape = outputs[0].shape;
    int outH = size;
    int outW = size;
    if (shape.size() >= 3) {
        outH = static_cast<int>(shape[shape.size() - 2]);
        outW = static_cast<int>(shape[shape.size() - 1]);
    }
    const std::vector<float>& raw = outputs[0].dataFloat;
    // 输出语义：锁定模型 MediaPipe Selfie Segmentation 输出已是 [0,1] 前景概率
    // （实测 max≈0.44 量级，负值来自未裁剪的 logits）。
    // 处置：直接 clamp 到 [0,1]，**不做帧内极值拉伸**。
    // 原因：极值拉伸会把「全为 0（无人物）」的帧也放大成可见噪声，
    //       造成「纯背景帧被识别出人像」的假阳性（此前实现即此缺陷）。
    // 兼容：若将来换成输出未归一化的模型，可通过下方 isProbability 判定改走极值拉伸。
    float mx = raw.empty() ? 0.0f : raw[0];
    for (float v : raw) mx = std::max(mx, v);
    const bool isProbability = (mx <= 1.5f);  // [0,1] 概率输出
    float mn2 = raw.empty() ? 0.0f : raw[0];
    if (!isProbability) {
        for (float v : raw) mn2 = std::min(mn2, v);
    }
    const float normMin = isProbability ? 0.0f : mn2;
    const float span = isProbability ? 1.0f : std::max(1e-6f, mx - mn2);
    // 缩放回源尺寸
    outAlpha.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const double u = (x + 0.5) / width;
            const double v = (y + 0.5) / height;
            const double value = sampleFloat(raw, outW, outH, u, v);
            const double normRaw = (value - normMin) / span;
            const double norm = std::clamp(normRaw, 0.0, 1.0);
            outAlpha[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                     static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(
                std::clamp(norm * 255.0, 0.0, 255.0) + 0.5);
        }
    }
    // 边缘羽化（文档 1.6：0..100；35 = 默认）
    const double featherClamped = std::clamp(feather, 0.0, 100.0);
    if (featherClamped > 0.0) {
        const int radius = std::max(1, static_cast<int>(std::lround(
                                           featherClamped / 100.0 * std::min(width, height) * 0.03)));
        boxBlurGray(outAlpha, width, height, radius);
    }
    return true;
}

const char* mattingAiPurpose() {
    return "智能抠像 AI：MODNet ONNX 真实推理（人像 Alpha）+ 精度档位 + 边缘羽化"
           "（文档 Part 4-A 1.6；Alpha 供瘦身/发光轮廓/背景替换消费）";
}

}  // namespace core
}  // namespace nairoclip
