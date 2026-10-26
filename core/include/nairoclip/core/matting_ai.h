#pragma once
// =============================================================================
// 智能抠像 AI（Matting AI）—— 文档 Part 4-A 1.6「智能抠像」真实推理链路
// =============================================================================
// 产品对应（原文 1.6）：
//   「抠像模式 [Tab]：人像/自定义/绿幕
//     边缘羽化 [滑块] 0~100  默认 35
//     抠像精度 [下拉] 快速/标准/高精度  默认 标准
//     背景替换方式 [Radio]：透明/模糊/自定义图片或颜色  默认 透明」
//
// 锁定模型：《NairoClip 所有模型协议和其它要求》§1.2 要求
//   MediaPipe Selfie Segmentation（Apache-2.0）。
//   输入 [1,3,256,256]（BGR，[-1,1]），输出 [1,1,256,256]（前景概率）。
//   ⚠️ 当前仓库未附带可用 ONNX 权重：Qualcomm AI Hub 导出版实测
//   对真实人像输出恒 0（sigmoid 后 0.5000），判定不可用；官方仅提供
//   .tflite，需 TensorFlow 转换。在拿到可用权重前，本模块保持
//   fail-closed（未加载即明确失败，不伪造 Alpha）。
//
// 原始实现（MODNet）说明保留如下，供换模型时参考预处理差异：
//   · 预处理：RGBA8 → 归一化 CHW（[-1,1]；MODNet 标准）；
//   · 推理：MODNet → 单通道 Alpha（前景概率）；
//   · 后处理：Alpha 归一化 → 边缘羽化（按羽化度做高斯平滑）→ 输出灰度蒙版；
//   · 精度档位（快速/标准/高精度）→ 输入分辨率（256/512/768）。
//
// 与既有模块的关系：
//   · chroma_key：绿幕抠像（规则算法；无需模型）——「绿幕」模式；
//   · matting_ai（本模块）：「人像」模式的模型推理；
//   · person_effects：瘦身/发光轮廓消费本模块产出的 Alpha。
//
// 可用性：需 ONNX Runtime + MODNet 模型；不可用时明确失败（不伪造 Alpha）。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nairoclip/core/ai_engine.h"
// 抠像精度档位（文档 1.6「抠像精度」下拉）复用 chroma_key.h 的 MattingQuality
// （Basic/HighPrecision；同一产品语义的单一事实来源）。
#include "nairoclip/core/chroma_key.h"

namespace nairoclip {
namespace core {

/// 档位对应的推理输入边长（Basic=512 / HighPrecision=768）。
int mattingAiQualityInputSize(MattingQuality quality);

/// 人像抠像推理器（会话复用；单实例单线程）。
class MattingEstimator {
public:
    MattingEstimator();
    ~MattingEstimator();
    MattingEstimator(const MattingEstimator&) = delete;
    MattingEstimator& operator=(const MattingEstimator&) = delete;

    /// 加载模型（MODNet .onnx 路径）。
    bool load(const std::string& modelPath, std::string* outputFailure = nullptr);
    bool loaded() const { return loaded_; }
    void unload();

    /// 推理单帧人像 Alpha 蒙版。
    /// - pixels：RGBA8 交错；width/height 源尺寸；
    /// - quality：精度档位（决定推理分辨率）；
    /// - feather：边缘羽化 0..100（文档 1.6 默认 35；0 = 不羽化）；
    /// - outAlpha：出参灰度 Alpha（尺寸 = width × height；0=背景 255=人像）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：未加载 / 尺寸非法 / 推理失败。
    bool estimate(const std::uint8_t* pixels, int width, int height, MattingQuality quality,
                  double feather, std::vector<std::uint8_t>& outAlpha,
                  std::string* outputFailure = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool loaded_ = false;
};

/// 模块说明（供 UI/日志）。
const char* mattingAiPurpose();

}  // namespace core
}  // namespace nairoclip
