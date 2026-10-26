#include "nairoclip/core/super_res_ai.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <vector>

#include "nairoclip/core/ai_engine.h"

namespace nairoclip {
namespace core {

namespace {

constexpr int kDefaultTile = 256;
constexpr int kDefaultHaloDiv = 8;

inline float bilinearSample(const std::uint8_t* src, int w, int h, int c, float fx, float fy) {
    const float x = std::min(std::max(fx, 0.0f), static_cast<float>(w - 1));
    const float y = std::min(std::max(fy, 0.0f), static_cast<float>(h - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float dx = x - static_cast<float>(x0);
    const float dy = y - static_cast<float>(y0);
    const auto at = [&](int xx, int yy) -> float {
        return static_cast<float>(src[(static_cast<std::size_t>(yy) * w + xx) * 4 + c]);
    };
    const float top = at(x0, y0) * (1.0f - dx) + at(x1, y0) * dx;
    const float bottom = at(x0, y1) * (1.0f - dx) + at(x1, y1) * dx;
    return top * (1.0f - dy) + bottom * dy;
}

inline std::uint8_t clampByte(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 255.0f) return 255;
    return static_cast<std::uint8_t>(v + 0.5f);
}

void preprocessTile(const std::uint8_t* pixels, int width, int x0, int y0, int tileW,
                    int tileH, std::vector<float>& out) {
    out.assign(static_cast<std::size_t>(3) * tileW * tileH, 0.0f);
    const std::size_t plane = static_cast<std::size_t>(tileW) * tileH;
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            const std::size_t srcIdx = (static_cast<std::size_t>(y0 + y) * width + (x0 + x)) * 4;
            const std::size_t dstIdx = static_cast<std::size_t>(y) * tileW + x;
            for (int c = 0; c < 3; ++c) {
                out[c * plane + dstIdx] = static_cast<float>(pixels[srcIdx + c]) / 255.0f;
            }
        }
    }
}

}  // namespace

void upscaleBilinear(const std::uint8_t* pixels, int width, int height, int scale,
                     std::vector<std::uint8_t>& outPixels) {
    if (pixels == nullptr || width <= 0 || height <= 0 || scale < 1) {
        outPixels.clear();
        return;
    }
    const int outW = width * scale;
    const int outH = height * scale;
    outPixels.assign(static_cast<std::size_t>(outW) * outH * 4, 0);

    for (int y = 0; y < outH; ++y) {
        const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(scale) - 0.5f;
        for (int x = 0; x < outW; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(scale) - 0.5f;
            const std::size_t dst = (static_cast<std::size_t>(y) * outW + x) * 4;
            for (int c = 0; c < 3; ++c) {
                outPixels[dst + c] = clampByte(bilinearSample(pixels, width, height, c, fx, fy));
            }
            const int sx = std::min(std::max(static_cast<int>(fx + 0.5f), 0), width - 1);
            const int sy = std::min(std::max(static_cast<int>(fy + 0.5f), 0), height - 1);
            outPixels[dst + 3] = pixels[(static_cast<std::size_t>(sy) * width + sx) * 4 + 3];
        }
    }
}

int superResSuggestedTileSize(const std::vector<std::int64_t>& modelInputShape) {
    if (modelInputShape.size() >= 4) {
        const std::int64_t h = modelInputShape[modelInputShape.size() - 2];
        if (h > 0 && h <= 2048) return static_cast<int>(h);
    }
    return kDefaultTile;
}

struct SuperResEstimator::Impl {
    AiSession session;
    int modelTile = kDefaultTile;
};

SuperResEstimator::SuperResEstimator() : impl_(new Impl()) {}
SuperResEstimator::~SuperResEstimator() = default;

bool SuperResEstimator::load(const std::string& modelPath, std::string* outputFailure) {
    loaded_ = false;
    if (!impl_->session.load(modelPath, AiBackend::Cpu, outputFailure)) {
        return false;
    }
    const auto& ins = impl_->session.inputs();
    if (ins.empty()) {
        if (outputFailure != nullptr) *outputFailure = "模型无输入张量";
        impl_->session.unload();
        return false;
    }
    impl_->modelTile = superResSuggestedTileSize(ins[0].shape);
    loaded_ = true;
    return true;
}

void SuperResEstimator::unload() {
    impl_->session.unload();
    loaded_ = false;
}

bool SuperResEstimator::upscale(const std::uint8_t* pixels, int width, int height,
                                SuperResScale scale, const SuperResTiling& tiling,
                                std::vector<std::uint8_t>& outPixels, SuperResStats* outStats,
                                std::string* outputFailure) {
    outPixels.clear();
    const int factor = static_cast<int>(scale);
    if (pixels == nullptr || width <= 0 || height <= 0 || (factor != 2 && factor != 4)) {
        if (outputFailure != nullptr) *outputFailure = "输入非法：尺寸或倍数不合规";
        return false;
    }
    if (!loaded_) {
        if (outputFailure != nullptr) *outputFailure = "模型未加载";
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const int outW = width * factor;
    const int outH = height * factor;
    outPixels.assign(static_cast<std::size_t>(outW) * outH * 4, 0);

    int tile = tiling.tileSize > 0 ? tiling.tileSize : impl_->modelTile;
    tile = std::max(32, std::min(tile, std::max(width, height)));
    int halo = tiling.halo > 0 ? tiling.halo : tile / kDefaultHaloDiv;
    halo = std::max(0, std::min(halo, tile / 2));

    SuperResStats stats;
    stats.inputWidth = width;
    stats.inputHeight = height;
    stats.outputWidth = outW;
    stats.outputHeight = outH;

    const int step = std::max(1, tile - 2 * halo);
    const int tilesX = (width + step - 1) / step;
    const int tilesY = (height + step - 1) / step;

    std::vector<float> inputBuf;
    std::vector<float> weightMap(static_cast<std::size_t>(outW) * outH, 0.0f);
    std::vector<float> accum(static_cast<std::size_t>(outW) * outH * 3, 0.0f);

    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            ++stats.tilesTotal;
            const int coreX = tx * step;
            const int coreY = ty * step;
            const int coreW = std::min(step, width - coreX);
            const int coreH = std::min(step, height - coreY);
            if (coreW <= 0 || coreH <= 0) continue;

            const int readX = std::max(0, coreX - halo);
            const int readY = std::max(0, coreY - halo);
            const int readR = std::min(width, coreX + coreW + halo);
            const int readB = std::min(height, coreY + coreH + halo);
            const int readW = readR - readX;
            const int readH = readB - readY;

            preprocessTile(pixels, width, readX, readY, readW, readH, inputBuf);

            AiTensor input;
            input.shape = {1, 3, readH, readW};
            input.dataFloat = inputBuf;

            std::vector<AiTensor> outputs;
            bool ok = impl_->session.run({input}, outputs, nullptr) && !outputs.empty() &&
                      !outputs[0].dataFloat.empty();

            int outTileW = 0;
            int outTileH = 0;
            const float* src = nullptr;
            if (ok) {
                const auto& shape = outputs[0].shape;
                outTileH = readH * factor;
                outTileW = readW * factor;
                if (shape.size() >= 2) {
                    const std::int64_t hh = shape[shape.size() - 2];
                    const std::int64_t ww = shape[shape.size() - 1];
                    if (hh > 0 && ww > 0) {
                        outTileH = static_cast<int>(hh);
                        outTileW = static_cast<int>(ww);
                    }
                }
                const std::size_t need = static_cast<std::size_t>(3) * outTileW * outTileH;
                if (outputs[0].dataFloat.size() < need) ok = false;
                if (ok) src = outputs[0].dataFloat.data();
            }

            std::vector<std::uint8_t> fallback;
            if (!ok) {
                ++stats.tilesFallback;
                upscaleBilinear(pixels, width, height, factor, fallback);
                if (fallback.empty()) {
                    if (outputFailure != nullptr) *outputFailure = "双线性回退失败";
                    return false;
                }
            }

            const std::size_t plane = static_cast<std::size_t>(outTileW) * outTileH;
            for (int y = 0; y < coreH; ++y) {
                for (int x = 0; x < coreW; ++x) {
                    const int bx = (coreX + x) - readX;
                    const int by = (coreY + y) - readY;
                    float wgt = 1.0f;
                    if (tiling.blendSeams && halo > 0) {
                        const float dxL = static_cast<float>(bx);
                        const float dxR = static_cast<float>(readW - 1 - bx);
                        const float dyT = static_cast<float>(by);
                        const float dyB = static_cast<float>(readH - 1 - by);
                        const float d = std::min(std::min(dxL, dxR), std::min(dyT, dyB));
                        wgt = std::min(1.0f, (d + 1.0f) / static_cast<float>(halo + 1));
                        if (wgt < 0.001f) wgt = 0.001f;
                    }
                    for (int sy = 0; sy < factor; ++sy) {
                        for (int sx = 0; sx < factor; ++sx) {
                            const int ox = coreX * factor + x * factor + sx;
                            const int oy = coreY * factor + y * factor + sy;
                            if (ox >= outW || oy >= outH) continue;
                            const std::size_t dst = static_cast<std::size_t>(oy) * outW + ox;
                            const int ix = bx * factor + sx;
                            const int iy = by * factor + sy;
                            for (int c = 0; c < 3; ++c) {
                                float v;
                                if (src != nullptr && ix < outTileW && iy < outTileH) {
                                    v = src[c * plane + static_cast<std::size_t>(iy) * outTileW + ix];
                                    v *= 255.0f;
                                } else {
                                    const std::size_t fb =
                                        (static_cast<std::size_t>(oy) * outW + ox) * 4 + c;
                                    v = static_cast<float>(fallback[fb]);
                                }
                                accum[(dst * 3) + c] += v * wgt;
                            }
                            weightMap[dst] += wgt;
                        }
                    }
                }
            }
        }
    }

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const std::size_t idx = static_cast<std::size_t>(y) * outW + x;
            const float w = weightMap[idx];
            const std::size_t dst = idx * 4;
            if (w <= 0.0f) {
                outPixels[dst + 3] = 255;
                continue;
            }
            for (int c = 0; c < 3; ++c) {
                outPixels[dst + c] = clampByte(accum[idx * 3 + c] / w);
            }
            outPixels[dst + 3] = 255;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats.elapsedMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    if (outStats != nullptr) *outStats = stats;
    return true;
}

}  // namespace core
}  // namespace nairoclip
