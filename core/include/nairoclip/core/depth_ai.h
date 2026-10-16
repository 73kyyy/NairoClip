#pragma once
// =============================================================================
// 深度图 AI（Depth AI）—— 文档 Part 4-C 13.2「深度图」真实推理链路
// =============================================================================
// 产品对应（原文 13.2）：
//   「[ ▶ 生成深度图 ] ▓▓▓▓▓▓▓▓▓░░░░ 分析中 72%
//     生成后显示灰度深度图缩略预览
//     应用效果Tab：[景深虚化] [雾化] [色彩分层] [视差3D]」
//
// 本模块实现「真实推理」：
//   · 模型：Depth-Anything-V2-Small（ONNX；IR v9；ONNX Runtime 推理）；
//   · 预处理：RGBA8 → 归一化 CHW 张量（ImageNet 均值/方差，518×518）；
//   · 后处理：单通道相对深度 → 归一化 0..1（近=1 / 远=0）→ 灰度图；
//   · 4 种应用效果（真实像素运算，由深度图数据驱动）：
//     景深虚化（对焦距离 + 光圈 + 虚化强度）、雾化（浓度/颜色/起始距离）、
//     色彩分层（近/中/远景三色调 + 过渡柔和度）、视差3D（强度/轨迹/速度）。
//
// 可用性（与全项目一致）：
//   · 需 ONNX Runtime（aiEngineAvailable）；不可用时明确失败（不伪造深度）。
//   · 模型文件由平台层经 model_manager 分发（路径由调用方提供）。
//
// 性能：模型首帧推理含预热（~1-3s CPU）；后续单帧推理 100-300ms 量级
//       （518×518 输入；实测沙箱 aarch64 数据见测试）。
//       平台层应在工作线程执行（AI 任务状态机 nc_ai_task_* 驱动进度）。

#include <cstdint>
#include <string>
#include <vector>

#include "nairoclip/core/ai_engine.h"
#include "nairoclip/core/background_fill.h"  // DepthEffectMode

namespace nairoclip {
namespace core {

// =============================================================================
// 深度图推理器（会话复用；单实例单线程）
// =============================================================================

/// 深度图推理器（封装 Depth-Anything ONNX 会话 + 预处理/后处理）。
class DepthEstimator {
public:
    DepthEstimator();
    ~DepthEstimator();
    DepthEstimator(const DepthEstimator&) = delete;
    DepthEstimator& operator=(const DepthEstimator&) = delete;

    /// 加载模型（.onnx 路径）。
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：引擎不可用 / 文件缺失 / 模型解析失败（fail-closed）。
    bool load(const std::string& modelPath, std::string* outputFailure = nullptr);
    /// 是否已加载。
    bool loaded() const { return loaded_; }
    /// 释放会话。
    void unload();

    /// 推理单帧深度图。
    /// - pixels：RGBA8 交错；width/height 源尺寸；
    /// - outDepth：出参灰度深度图（尺寸 = width × height；0=最近 255=最远；
    ///   已按帧内极值归一化，保证对比度可用）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：未加载 / 尺寸非法 / 推理失败。
    bool estimate(const std::uint8_t* pixels, int width, int height,
                  std::vector<std::uint8_t>& outDepth, std::string* outputFailure = nullptr);

    /// 推理输入边长（模型固定 518；供平台层预估耗时）。
    static constexpr int inputSize() { return 518; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

// =============================================================================
// 4 种深度应用效果（真实像素运算；深度图数据驱动）
// =============================================================================

/// 景深虚化参数（文档 13.2）。
struct BokehParams {
    /// 对焦距离（0..1；0 = 最近，1 = 最远；预览区点选赋值）。
    double focusDistance = 0.5;
    /// 光圈（1.0..22.0；越小虚化越强）。
    double aperture = 2.8;
    /// 虚化强度（0..100）。
    double intensity = 70.0;
};

/// 雾化参数（文档 13.2）。
struct FogParams {
    /// 雾浓度（0..100）。
    double intensity = 55.0;
    /// 雾颜色（0xRRGGBBAA）。
    std::uint32_t colorRgba = 0xFFFFFFF0;
    /// 起始距离（0..1；深度超过该值的区域开始起雾）。
    double startDistance = 0.3;
};

/// 色彩分层参数（文档 13.2）。
struct ColorBandsParams {
    /// 近景/中景/远景色调（0xRRGGBBAA）。
    std::uint32_t nearRgba = 0xFFD08CFF;   ///< 暖黄
    std::uint32_t midRgba = 0xE8E8E8FF;    ///< 自然
    std::uint32_t farRgba = 0x8CB4FFFF;    ///< 冷蓝
    /// 过渡柔和度（0..100）。
    double smoothness = 50.0;
};

/// 视差3D参数（文档 13.2）。
struct ParallaxParams {
    /// 视差强度（0..100）。
    double intensity = 40.0;
    /// 运动轨迹：0=静止 / 1=摇摆 / 2=环绕。
    int trajectory = 0;
    /// 运动速度：0=慢 / 1=中 / 2=快。
    int speed = 1;
};

/// 景深虚化（真实高斯模糊按深度加权；焦点处清晰，远处/近处模糊）。
bool applyBokehEffect(std::uint8_t* pixels, int width, int height,
                      const std::vector<std::uint8_t>& depth, const BokehParams& params);

/// 雾化（按深度混合雾色；startDistance 之后逐渐全雾）。
bool applyFogEffect(std::uint8_t* pixels, int width, int height,
                    const std::vector<std::uint8_t>& depth, const FogParams& params);

/// 色彩分层（近/中/远三色调按深度映射；柔和度控制过渡带）。
bool applyColorBandsEffect(std::uint8_t* pixels, int width, int height,
                           const std::vector<std::uint8_t>& depth,
                           const ColorBandsParams& params);

/// 视差3D（按深度图位移采样；t = 时间秒（轨迹动画相位））。
bool applyParallaxEffect(std::uint8_t* pixels, int width, int height,
                         const std::vector<std::uint8_t>& depth, const ParallaxParams& params,
                         double t);

/// 模块说明（供 UI/日志）。
const char* depthAiPurpose();

}  // namespace core
}  // namespace nairoclip
