#include "nairoclip/core/render_engine.h"

#include <algorithm>

namespace nairoclip {
namespace core {

const char* renderPathName(RenderPath path) {
    switch (path) {
        case RenderPath::Preview: return "preview";
        case RenderPath::Export: return "export";
    }
    return "export";
}

RenderQualityCheck validateRenderQuality(const RenderQuality& quality) {
    RenderQualityCheck check;
    if (!quality.valid()) {
        check.allowed = false;
        check.reason =
            "渲染质量参数非法（导出路径必须 scale >= 1、sampleBudget >= 1、不允许丢帧）";
        return check;
    }
    if (quality.path == RenderPath::Export) {
        check.allowed = true;
        check.reason = "导出质量：完整分辨率与采样（质量上限三端一致）";
        return check;
    }
    check.allowed = true;
    check.reason = "预览质量：允许降分辨率/简化采样（仅影响编辑流畅度）";
    return check;
}

double effectiveKernelRadius(const RenderQuality& quality, double radius) {
    if (radius <= 0.0) {
        return 0.0;
    }
    if (quality.path == RenderPath::Export || quality.sampleBudget >= 1.0) {
        return radius;
    }
    const double scaled = radius * quality.sampleBudget;
    // 预览下限 1px：避免简化后效果完全消失（用户仍能看到效果方向）
    return scaled < 1.0 ? 1.0 : scaled;
}

int effectiveSampleCount(const RenderQuality& quality, int fullSamples) {
    if (fullSamples <= 0) {
        return 0;
    }
    if (quality.path == RenderPath::Export || quality.sampleBudget >= 1.0) {
        return fullSamples;
    }
    const int scaled = static_cast<int>(static_cast<double>(fullSamples) *
                                        quality.sampleBudget + 0.5);
    // 预览下限 4：低于 4 采样时旋转模糊等会明显断裂
    return scaled < 4 ? 4 : scaled;
}

}  // namespace core
}  // namespace nairoclip
