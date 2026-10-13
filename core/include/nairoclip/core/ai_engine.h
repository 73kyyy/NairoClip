#pragma once
// =============================================================================
// AI 推理引擎抽象（AI Engine Abstraction）
//   —— 文档第一段「一、技术选型总纲：统一推理引擎 ONNX Runtime」+「1.2 通用架构」
// =============================================================================
// 产品对应（原文 1.2 通用架构）：
//   「core 层新增模块 ai_engine.h / ai_engine.cpp：
//     ├─ ORT Session 生命周期管理
//     ├─ 模型加载（从 model_manager 下载的本地文件路径读取 .onnx 文件）
//     ├─ 输入预处理通用接口（图像：resize/normalize/HWC→CHW）
//     ├─ 推理执行（同步/异步两种模式）
//     ├─ 输出后处理通用接口
//     └─ 硬件加速后端选择：
//         ├─ Android: NNAPI / XNNPACK
//         ├─ iOS: CoreML EP
//         └─ 桌面: CUDA/DirectML/CPU」
//   关键工程原则：「每新增一个 AI 功能不需要重新搭建推理框架，只需新增
//    预处理+后处理薄层代码。模型文件通过模型管理客户端（AES-256-CTR 加密下载）
//    分发，不打包进 App 安装包」。
//
// 本模块定位（与全项目「可选集成 + 优雅降级」惯例一致）：
//   · 与 FFmpeg 同一模式的可选集成：构建时探测 ONNX Runtime，有则启用
//     （NAIROCLIP_HAS_ONNX_RUNTIME=1），无则 aiEngineAvailable() 返回 false；
//   · 可用性查询 + 明确失败（不伪造推理结果——无 ORT 时任何推理返回失败）；
//   · 会话生命周期（加载/释放/元数据查询）、张量接口、图像预处理通用接口；
//   · 后端选择（NNAPI/CoreML/CUDA/DirectML/CPU；不可用的后端安全降级 CPU）；
//   · 平台层职责：真实模型下载（model_manager 已实现）、密钥管理与解密、
//     设备分级 → 量化版本选择（model_manager 已实现）。
//
// 合规约束（文档第一段 1.1/三）：ONNX Runtime 为 MIT 协议（无传染性）；
//   锁定模型清单见 docs/COMPLIANCE.md（均为 MIT/BSD/Apache-2.0）。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 可用性（编译期/运行期）
// =============================================================================

/// 推理引擎是否可用（编译时链接了 ONNX Runtime）。
bool aiEngineAvailable();

/// 引擎描述（版本/后端能力；供 UI「关于」页与诊断）。
struct AiEngineInfo {
    bool available = false;      ///< 是否可用
    std::string version;         ///< ONNX Runtime 版本（如 "1.22.0"；不可用为空）
    bool cpuBackend = false;     ///< CPU 后端可用（有 ORT 时恒 true）
    bool nnapiBackend = false;   ///< Android NNAPI EP 可用
    bool coremlBackend = false;  ///< iOS CoreML EP 可用
    /// 说明（不可用原因；可直接展示）。
    std::string note;
};

/// 查询引擎信息（确定性；无副作用）。
AiEngineInfo aiEngineInfo();

// =============================================================================
// 推理后端（文档 1.3「跨平台硬件加速注意事项」）
// =============================================================================

/// 推理执行后端（平台差异只影响速度，不影响功能与输出质量——文档「三端对等」）。
enum class AiBackend {
    Cpu = 0,      ///< CPU（恒可用；文档「纯 CPU 模式下也能正常运行」强制要求）
    Nnapi = 1,    ///< Android NNAPI（不支持的机型自动降级）
    CoreMl = 2,   ///< iOS CoreML
    Cuda = 3,     ///< 桌面 NVIDIA
    DirectMl = 4, ///< Windows DirectML
    Xnnpack = 5,  ///< XNNPACK（移动端 CPU 优化）
};

/// 后端名称（"cpu" / "nnapi" / "coreml" / "cuda" / "directml" / "xnnpack"）。
const char* aiBackendName(AiBackend backend);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool aiBackendFromString(const char* text, AiBackend& out);

/// 后端可用性探测（运行时；不匹配的后端返回 false，调用方降级 CPU）。
bool aiBackendAvailable(AiBackend backend);

// =============================================================================
// 张量（推理输入/输出）
// =============================================================================

/// 张量元素类型。
enum class AiTensorType {
    Float32 = 0,
    Int8 = 1,
    Uint8 = 2,
};

/// 张量（连续内存；shape 为维度列表）。
struct AiTensor {
    AiTensorType type = AiTensorType::Float32;
    std::vector<std::int64_t> shape;
    /// 数据缓冲（Float32 → dataFloat；Int8/Uint8 → dataBytes）。
    std::vector<float> dataFloat;
    std::vector<std::uint8_t> dataBytes;

    /// 元素总数（shape 乘积；空 shape 返回 0）。
    std::size_t elementCount() const;
    /// 是否有效（shape 非空 + 元素数与缓冲一致）。
    bool valid() const;
};

/// 张量数据大小（字节）。
std::size_t aiTensorByteSize(const AiTensor& tensor);

// =============================================================================
// 会话（Session 生命周期管理）
// =============================================================================

/// 推理会话（加载模型 → 推理 → 释放）。
/// - 线程约束：单实例单线程使用（平台层串行化访问）；
/// - 引擎不可用（无 ORT）时所有方法返回失败（不伪造结果）。
class AiSession {
public:
    AiSession();
    ~AiSession();
    AiSession(const AiSession&) = delete;
    AiSession& operator=(const AiSession&) = delete;

    /// 加载模型（.onnx 文件路径）。
    /// - backend：执行后端偏好（不可用时自动降级 CPU；文档 1.3）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：引擎不可用 / 文件不存在 / 模型解析失败（fail-closed）。
    bool load(const std::string& modelPath, AiBackend backend = AiBackend::Cpu,
              std::string* outputFailure = nullptr);

    /// 是否已加载（可推理）。
    bool loaded() const;

    /// 释放会话。
    void unload();

    /// 模型元数据：输入/输出名称与形状（-1 = 动态维度）。
    struct TensorInfo {
        std::string name;
        std::vector<std::int64_t> shape;
        AiTensorType type = AiTensorType::Float32;
    };
    const std::vector<TensorInfo>& inputs() const;
    const std::vector<TensorInfo>& outputs() const;

    /// 同步推理（文档 1.2「同步/异步两种模式」的同步路径）。
    /// - inputs：按模型输入顺序（名称由 inputs() 提供）；
    /// - outputs：出参（成功时填充；顺序与 outputs() 一致）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：未加载 / 输入数量或形状不匹配 / 推理失败。
    bool run(const std::vector<AiTensor>& inputs, std::vector<AiTensor>& outputs,
             std::string* outputFailure = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// =============================================================================
// 图像预处理通用接口（文档 1.2「图像：resize/normalize/HWC→CHW」）
// =============================================================================

/// 图像 → 张量预处理配置（各 AI 功能复用）。
struct AiImagePreprocess {
    /// 目标尺寸（模型输入宽高）。
    int width = 0;
    int height = 0;
    /// 归一化均值/标准差（逐通道；空 = 不归一化，直接 [0,1] 映射）。
    std::vector<float> mean;   // 如 ImageNet: {0.485, 0.456, 0.406}
    std::vector<float> stddev; // 如 ImageNet: {0.229, 0.224, 0.225}
    /// 输出张量类型（Float32 默认；Int8/Uint8 为量化模型输入）。
    AiTensorType type = AiTensorType::Float32;

    bool valid() const {
        return width > 0 && height > 0 &&
               (mean.empty() || mean.size() == 3) &&
               (stddev.empty() || stddev.size() == 3) &&
               (mean.empty() || stddev.empty() || mean.size() == stddev.size());
    }
};

/// RGBA8 图像 → CHW 张量（双线性缩放；HWC → CHW 重排；归一化）。
/// - pixels：交错 RGBA8；srcWidth/srcHeight 源尺寸；
/// - config：目标尺寸与归一化配置；
/// - out：出参张量（shape = [1, 3, height, width]）；
/// - 返回 false：参数非法（不修改 out）。
bool aiPreprocessImage(const std::uint8_t* pixels, int srcWidth, int srcHeight,
                       const AiImagePreprocess& config, AiTensor& out);

/// 单通道 Alpha 蒙版张量 → RGBA8 图像后处理（双线性放大 + 覆盖率映射）。
/// - mask：形状 [1, 1, H, W] 的 Float32 张量（值域按模型约定，通常 0..1）；
/// - outWidth/outHeight：目标图像尺寸；
/// - rgba：出参（尺寸 = outWidth × outHeight × 4；alpha = 覆盖率 × 255，
///   RGB 置白——供上层按需着色）；
/// - 返回 false：参数非法。
bool aiPostprocessMask(const AiTensor& mask, int outWidth, int outHeight,
                       std::vector<std::uint8_t>& rgba);

}  // namespace core
}  // namespace nairoclip
