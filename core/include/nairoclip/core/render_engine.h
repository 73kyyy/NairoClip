#pragma once
// =============================================================================
// 渲染引擎抽象层（Render Engine Abstraction）
//   —— 文档第二段「四、技术选型总纲：跨平台图形渲染架构」+ 4.4「实时预览 vs
//      导出渲染质量分级」
// =============================================================================
// 产品对应（原文）：
//   · 「core 层定义统一渲染管线接口，底层分别用 GLSL/MSL/HLSL 具体实现」
//     （自建跨平台渲染抽象层）；
//   · 「优先调研采用 bgfx（BSD-2-Clause，支持 OpenGL ES/Metal/Vulkan/DirectX
//      多后端），评估成本过高再走自建抽象层路线」；
//   · 4.4「编辑时实时预览优先保证流畅度（30fps 以上），降低渲染分辨率，
//      复杂特效用简化近似；最终导出不计算耗时成本，完整分辨率 + 完整算法精度」；
//   · 4.4「架构层面区分预览质量与导出质量两套渲染路径，复用同一套 Shader 代码
//      但传入不同分辨率/采样参数」。
//
// 本模块定位（架构分层，与全项目「core 定义接口、平台层注入」惯例一致）：
//   · 定义渲染管线接口：质量分级（RenderQuality）+ 后端契约（IRenderBackend）；
//   · CPU 参考后端语义在既有逐帧管线内落地（export_executor/preview 即实例），
//     所有新效果（spatial/screen/blend）均为纯函数式 CPU 实现 = 正确性基准；
//   · GPU 后端（bgfx/自建）由平台层按同一接口注入（先接口后实现，避免返工）；
//   · RenderQuality 为预览/导出质量分级的单一事实来源（文档 4.4 强制要求）。
//
// 三端对等原则（文档「三端模型体积策略」第二节）：
//   · 质量分级只允许影响「编辑预览」；导出质量上限三端完全一致；
//   · 预览降分辨率/降采样不构成功能缩水（文档第四章边界澄清）。

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {

// =============================================================================
// 渲染质量分级（文档 4.4 强制要求）
// =============================================================================

/// 渲染路径（预览 vs 导出）。
enum class RenderPath {
    Preview = 0,  ///< 编辑时实时预览：优先流畅度（允许降分辨率/简化近似）
    Export = 1,   ///< 最终导出：完整分辨率 + 完整算法精度（不可缩水）
};

/// 渲染质量参数（文档 4.4「两套渲染路径」在接口层的体现）。
struct RenderQuality {
    /// 渲染路径。
    RenderPath path = RenderPath::Export;
    /// 渲染分辨率缩放（相对输出分辨率；仅预览路径允许 < 1，导出恒为 1）。
    double scale = 1.0;
    /// 采样预算乘数（空间效果核半径/高采样效果迭代数的上限系数）。
    /// 1.0 = 完整精度（导出）；< 1 = 简化近似（预览）。
    double sampleBudget = 1.0;
    /// 是否允许丢弃中间帧（预览路径的丢帧保流畅；导出路径必须 false）。
    bool allowFrameDrop = false;

    /// 是否合法：导出路径不允许降分辨率/降采样/丢帧（fail-closed 校验入口）。
    bool valid() const {
        if (scale <= 0.0 || sampleBudget <= 0.0) return false;
        if (path == RenderPath::Export) {
            return scale >= 1.0 && sampleBudget >= 1.0 && !allowFrameDrop;
        }
        return true;
    }

    /// 导出质量（完整精度；单一事实来源）。
    static RenderQuality exportQuality() {
        RenderQuality q;
        q.path = RenderPath::Export;
        q.scale = 1.0;
        q.sampleBudget = 1.0;
        q.allowFrameDrop = false;
        return q;
    }

    /// 预览质量（可降分辨率；默认 0.5 缩放 / 0.5 采样预算）。
    static RenderQuality previewQuality(double scaleValue = 0.5,
                                        double sampleBudgetValue = 0.5) {
        RenderQuality q;
        q.path = RenderPath::Preview;
        q.scale = scaleValue > 0.0 && scaleValue <= 1.0 ? scaleValue : 0.5;
        q.sampleBudget = sampleBudgetValue > 0.0 && sampleBudgetValue <= 1.0
                             ? sampleBudgetValue
                             : 0.5;
        q.allowFrameDrop = true;
        return q;
    }
};

/// 质量分级名称（"preview" / "export"；供日志/诊断）。
const char* renderPathName(RenderPath path);

// =============================================================================
// 渲染后端抽象（core 定义契约；CPU 后端落地，GPU 后端由平台层注入）
// =============================================================================

/// 后端能力描述（平台层注入时填充；CPU 参考后端为全 false）。
struct RenderBackendInfo {
    /// 后端名称（"cpu" / "bgfx-gl" / "metal" / "vulkan" / "d3d11" 等）。
    std::string name = "cpu";
    /// 是否硬件加速。
    bool hardwareAccelerated = false;
    /// 是否支持在 GPU 上执行空间效果（模糊/锐化/混合等）。
    bool supportsGpuEffects = false;
    /// 是否支持 compute shader（粒子/旋转模糊等高采样效果）。
    bool supportsCompute = false;

    bool valid() const { return !name.empty(); }
};

/// 渲染后端接口（GPU 后端实现此接口；CPU 参考路径由既有逐帧管线承担）。
///
/// 契约（与全项目工厂注入惯例一致）：
///   · 单实例单线程使用（平台层串行化访问）；
///   · 所有方法返回 false 表示不可用；调用方须有 CPU 兜底路径；
///   · 后端不得改变效果语义（参数含义与 CPU 参考实现完全一致）。
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    /// 后端能力。
    virtual RenderBackendInfo info() const = 0;

    /// 准备一帧的处理上下文（尺寸/质量分级）。
    /// 返回 false 表示该后端无法处理此配置（调用方回退 CPU）。
    virtual bool beginFrame(int width, int height, const RenderQuality& quality) = 0;

    /// 结束一帧（提交 GPU 命令/清理临时资源）。
    virtual void endFrame() = 0;
};

/// 质量分级门控结果（供平台层在切换后端/配置时校验；fail-closed）。
struct RenderQualityCheck {
    bool allowed = false;
    std::string reason;
};

/// 校验质量分级是否可用于指定路径：
///   · 导出路径要求完整精度（scale = 1 / sampleBudget = 1 / 不丢帧）；
///   · 预览路径允许优化（文档 4.4：不违反功能对等）；
///   · 传入非法值（如导出 + scale 0.5）→ 拒绝并给出原因（fail-closed）。
RenderQualityCheck validateRenderQuality(const RenderQuality& quality);

// =============================================================================
// 质量分级对特效参数的影响（文档 4.4：简化近似 vs 完整精度）
// =============================================================================

/// 按质量分级调整「空间效果」的采样参数（预览简化；导出完整）。
/// - radius：完整精度核半径（像素）；
/// - 返回：导出 = 原值；预览 = 按 sampleBudget 缩放（下限 1px，效果不消失）。
double effectiveKernelRadius(const RenderQuality& quality, double radius);

/// 按质量分级调整「高采样效果」的迭代/采样次数（旋转模糊/粒子的采样数）。
/// - fullSamples：完整精度采样数（如旋转模糊 24 次）；
/// - 返回：导出 = 原值；预览 = 按 sampleBudget 缩放（下限 4，避免明显断裂）。
int effectiveSampleCount(const RenderQuality& quality, int fullSamples);

}  // namespace core
}  // namespace nairoclip
