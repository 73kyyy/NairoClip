#include "nairoclip/core/ai_engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#ifdef NAIROCLIP_HAS_ONNX_RUNTIME
#include <onnxruntime_cxx_api.h>
#endif

namespace nairoclip {
namespace core {

// =============================================================================
// 可用性
// =============================================================================

bool aiEngineAvailable() {
#ifdef NAIROCLIP_HAS_ONNX_RUNTIME
    return true;
#else
    return false;  // 未链接 ONNX Runtime：明确不可用（不伪造推理）
#endif
}

AiEngineInfo aiEngineInfo() {
    AiEngineInfo info;
#ifdef NAIROCLIP_HAS_ONNX_RUNTIME
    info.available = true;
    info.version = Ort::GetVersionString();
    info.cpuBackend = true;
#if defined(__ANDROID__)
    info.nnapiBackend = true;  // 运行时仍需 aiBackendAvailable 探测（文档 1.3）
#endif
#if defined(__APPLE__)
    info.coremlBackend = true;
#endif
    info.note = "ONNX Runtime " + info.version + "（MIT）；CPU 后端恒可用";
#else
    info.available = false;
    info.note = "此构建未链接 ONNX Runtime（可选依赖；见 docs/COMPLIANCE.md）——"
                "AI 推理功能不可用，其余功能不受影响";
#endif
    return info;
}

// =============================================================================
// 后端
// =============================================================================

const char* aiBackendName(AiBackend backend) {
    switch (backend) {
        case AiBackend::Cpu: return "cpu";
        case AiBackend::Nnapi: return "nnapi";
        case AiBackend::CoreMl: return "coreml";
        case AiBackend::Cuda: return "cuda";
        case AiBackend::DirectMl: return "directml";
        case AiBackend::Xnnpack: return "xnnpack";
    }
    return "cpu";
}

bool aiBackendFromString(const char* text, AiBackend& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    if (s == "cpu") { out = AiBackend::Cpu; return true; }
    if (s == "nnapi") { out = AiBackend::Nnapi; return true; }
    if (s == "coreml") { out = AiBackend::CoreMl; return true; }
    if (s == "cuda") { out = AiBackend::Cuda; return true; }
    if (s == "directml") { out = AiBackend::DirectMl; return true; }
    if (s == "xnnpack") { out = AiBackend::Xnnpack; return true; }
    return false;
}

bool aiBackendAvailable(AiBackend backend) {
    if (!aiEngineAvailable()) {
        return false;
    }
    switch (backend) {
        case AiBackend::Cpu:
            return true;  // CPU 恒可用（文档强制要求）
        case AiBackend::Nnapi:
        case AiBackend::CoreMl:
            // 平台层需在真实设备上探测（NNAPI/CoreML EP 是否可用）；
            // core 侧保守返回 false（调用方降级 CPU；文档 1.3「必须做运行时检测」）。
            return false;
        case AiBackend::Cuda:
        case AiBackend::Xnnpack:
        case AiBackend::DirectMl:
            // 桌面 GPU EP 需构建期启用（--use_cuda 等）；保守返回 false。
            return false;
    }
    return false;
}

// =============================================================================
// 张量
// =============================================================================

std::size_t AiTensor::elementCount() const {
    if (shape.empty()) {
        return 0;
    }
    std::size_t count = 1;
    for (const std::int64_t dim : shape) {
        if (dim <= 0) {
            return 0;  // 动态维度视为无效（调用方需在推理前固定）
        }
        count *= static_cast<std::size_t>(dim);
    }
    return count;
}

bool AiTensor::valid() const {
    // 说明：形状含 0 长度维度是**合法**的（如 Whisper 首步的
    // past_key_values 为 [1,6,0,64]）。因此不能因 count == 0 直接判无效；
    // 只有「形状为空」或「负维度」才非法。
    if (shape.empty()) {
        return false;
    }
    std::size_t count = 1;
    for (const std::int64_t dim : shape) {
        if (dim < 0) {
            return false;
        }
        count *= static_cast<std::size_t>(dim);
    }
    if (type == AiTensorType::Float32) {
        return dataFloat.size() == count;
    }
    if (type == AiTensorType::Int64) {
        return dataInt64.size() == count;
    }
    return dataBytes.size() == count;
}

std::size_t aiTensorByteSize(const AiTensor& tensor) {
    const std::size_t count = tensor.elementCount();
    if (tensor.type == AiTensorType::Float32) {
        return count * sizeof(float);
    }
    if (tensor.type == AiTensorType::Int64) {
        return count * sizeof(std::int64_t);
    }
    return count;  // Int8 / Uint8
}

}  // namespace core
}  // namespace nairoclip

// =============================================================================
// 会话实现（有 ORT 时用真实现；无 ORT 时全部失败——不伪造推理）
// =============================================================================

namespace nairoclip {
namespace core {

#ifdef NAIROCLIP_HAS_ONNX_RUNTIME

namespace {

/// 张量类型映射：ORT 元素类型 → core 张量类型（不支持的默认 Float32）。
AiTensorType mapOrtType(ONNXTensorElementDataType type) {
    switch (type) {
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:
            return AiTensorType::Int8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:
            return AiTensorType::Uint8;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
            return AiTensorType::Int64;
        case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:
            return AiTensorType::Bool;
        default:
            return AiTensorType::Float32;
    }
}

/// 后端枚举 → ORT EP 名称（CPU 为空 = 不追加 EP；文档 1.3）。
/// 说明：ORT 1.22 无统一 AppendEP 函数，需按 EP 调用具体 API；
/// 未链接对应 EP 库时调用会失败（捕获并降级 CPU——文档 1.3「自动降级」）。
const char* ortProviderName(AiBackend backend) {
    switch (backend) {
        case AiBackend::Nnapi: return "NNAPI";
        case AiBackend::CoreMl: return "CoreML";
        case AiBackend::Cuda: return "CUDA";
        case AiBackend::DirectMl: return "DML";
        case AiBackend::Xnnpack: return "XNNPACK";
        case AiBackend::Cpu: return nullptr;
    }
    return nullptr;
}

/// 尝试追加 EP（按后端调用具体 API；失败静默降级 CPU）。
void tryAppendExecutionProvider(Ort::SessionOptions& options, AiBackend backend) {
#if defined(NAIROCLIP_ORT_HAS_NNAPI) || defined(NAIROCLIP_ORT_HAS_COREML) || \
    defined(NAIROCLIP_ORT_HAS_CUDA) || defined(NAIROCLIP_ORT_HAS_XNNPACK)
    try {
        switch (backend) {
#if defined(NAIROCLIP_ORT_HAS_XNNPACK)
            case AiBackend::Xnnpack:
                Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_XNNPACK(
                    static_cast<OrtSessionOptions*>(options), nullptr));
                break;
#endif
#if defined(NAIROCLIP_ORT_HAS_CUDA)
            case AiBackend::Cuda:
                OrtSessionOptionsAppendExecutionProvider_CUDA(
                    static_cast<OrtSessionOptions*>(options), 0);
                break;
#endif
            default:
                break;  // 其余 EP 在 core 构建中未启用（平台层按需启用）
        }
    } catch (...) {
        // EP 追加失败：静默降级 CPU（文档 1.3）
    }
#else
    (void)options;
    (void)backend;
    // core 默认构建不带 EP（仅 CPU；平台层/AAR 构建按需启用）
#endif
}

}  // namespace

struct AiSession::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "nairoclip"};
    std::unique_ptr<Ort::Session> session;
    Ort::AllocatorWithDefaultOptions allocator;
    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::vector<TensorInfo> inputInfos;
    std::vector<TensorInfo> outputInfos;
};

#else  // 无 ONNX Runtime

struct AiSession::Impl {
    std::vector<TensorInfo> inputInfos;
    std::vector<TensorInfo> outputInfos;
};

#endif

AiSession::AiSession() : impl_(std::make_unique<Impl>()) {}
AiSession::~AiSession() = default;

bool AiSession::loaded() const {
#ifdef NAIROCLIP_HAS_ONNX_RUNTIME
    return impl_->session != nullptr;
#else
    return false;
#endif
}

void AiSession::unload() {
#ifdef NAIROCLIP_HAS_ONNX_RUNTIME
    impl_->session.reset();
    impl_->inputNames.clear();
    impl_->outputNames.clear();
    impl_->inputInfos.clear();
    impl_->outputInfos.clear();
#endif
}

const std::vector<AiSession::TensorInfo>& AiSession::inputs() const {
    return impl_->inputInfos;
}

const std::vector<AiSession::TensorInfo>& AiSession::outputs() const {
    return impl_->outputInfos;
}

bool AiSession::load(const std::string& modelPath, AiBackend backend,
                     std::string* outputFailure) {
#ifdef NAIROCLIP_HAS_ONNX_RUNTIME
    unload();
    if (modelPath.empty()) {
        if (outputFailure != nullptr) *outputFailure = "模型路径为空";
        return false;
    }
    {
        // 文件存在性检查（fail-closed：路径错误立即返回明确原因）
        std::ifstream check(modelPath, std::ios::binary);
        if (!check.good()) {
            if (outputFailure != nullptr) *outputFailure = "模型文件不存在：" + modelPath;
            return false;
        }
    }
    try {
        Ort::SessionOptions options;
        // 资源占用：默认 2 线程（文档 1.3 未规定；移动端与低内存设备友好）。
        // 图优化：基础级（ORT_ENABLE_ALL 在大模型加载时需要额外内存，
        // 低内存设备上实测 std::bad_alloc——见 M2.31 端到端验证记录）。
        options.SetIntraOpNumThreads(2);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_BASIC);
        // 后端选择（文档 1.3）：核心构建默认 CPU；平台层可定义
        // NAIROCLIP_ORT_HAS_* 宏启用对应 EP（app 侧 ORT 包自带 EP）。
        const char* provider = ortProviderName(backend);
        if (provider != nullptr) {
            tryAppendExecutionProvider(options, backend);
        }
        impl_->session = std::make_unique<Ort::Session>(impl_->env, modelPath.c_str(),
                                                        options);
        // 输入元数据
        // 注意（真实缺陷修复，M2.31 实测）：不能链式调用
        //   session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo()
        // ——TypeInfo 临时对象在此表达式内析构，后续 GetShape() 读取已释放内存
        // （实测 `cannot create std::vector larger than max_size()`。
        // 修复：把 TypeInfo 存为具名局部变量，延长生命周期到信息读取完成）。
        const std::size_t inputCount = impl_->session->GetInputCount();
        impl_->inputNames.clear();
        impl_->inputInfos.clear();
        for (std::size_t i = 0; i < inputCount; ++i) {
            auto name = impl_->session->GetInputNameAllocated(i, impl_->allocator);
            impl_->inputNames.push_back(name.get());
            Ort::TypeInfo typeInfo = impl_->session->GetInputTypeInfo(i);  // 具名
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            TensorInfo tensor;
            tensor.name = name.get();
            tensor.shape = info.GetShape();
            tensor.type = mapOrtType(info.GetElementType());
            impl_->inputInfos.push_back(std::move(tensor));
        }
        // 输出元数据（同一修复：TypeInfo 具名保存）
        const std::size_t outputCount = impl_->session->GetOutputCount();
        impl_->outputNames.clear();
        impl_->outputInfos.clear();
        for (std::size_t i = 0; i < outputCount; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, impl_->allocator);
            impl_->outputNames.push_back(name.get());
            Ort::TypeInfo typeInfo = impl_->session->GetOutputTypeInfo(i);  // 具名
            const auto info = typeInfo.GetTensorTypeAndShapeInfo();
            TensorInfo tensor;
            tensor.name = name.get();
            tensor.shape = info.GetShape();
            tensor.type = mapOrtType(info.GetElementType());
            impl_->outputInfos.push_back(std::move(tensor));
        }
        return true;
    } catch (const std::exception& e) {
        impl_->session.reset();
        if (outputFailure != nullptr) {
            *outputFailure = std::string("模型加载失败：") + e.what();
        }
        return false;
    }
#else
    (void)modelPath;
    (void)backend;
    if (outputFailure != nullptr) {
        *outputFailure = "此构建未链接 ONNX Runtime——AI 推理不可用（可选依赖）";
    }
    return false;
#endif
}

bool AiSession::run(const std::vector<AiTensor>& inputs, std::vector<AiTensor>& outputs,
                    std::string* outputFailure) {
#ifdef NAIROCLIP_HAS_ONNX_RUNTIME
    if (impl_->session == nullptr) {
        if (outputFailure != nullptr) *outputFailure = "会话未加载模型";
        return false;
    }
    if (inputs.size() != impl_->inputNames.size()) {
        if (outputFailure != nullptr) {
            *outputFailure = "输入数量不匹配（模型需要 " +
                             std::to_string(impl_->inputNames.size()) + "，收到 " +
                             std::to_string(inputs.size()) + "）";
        }
        return false;
    }
    try {
        const Ort::MemoryInfo memInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        std::vector<Ort::Value> inputValues;
        inputValues.reserve(inputs.size());
        for (std::size_t i = 0; i < inputs.size(); ++i) {
            const AiTensor& tensor = inputs[i];
            if (!tensor.valid()) {
                if (outputFailure != nullptr) *outputFailure = "输入张量无效（形状/数据不匹配）";
                return false;
            }
            // 形状校验：与模型元数据比较（动态维度 -1 跳过）
            const auto& expected = impl_->inputInfos[i].shape;
            if (expected.size() == tensor.shape.size()) {
                for (std::size_t d = 0; d < expected.size(); ++d) {
                    if (expected[d] > 0 && expected[d] != tensor.shape[d]) {
                        if (outputFailure != nullptr) {
                            *outputFailure = "输入形状不匹配（维度 " + std::to_string(d) + "）";
                        }
                        return false;
                    }
                }
            }
            if (tensor.type == AiTensorType::Float32) {
                inputValues.push_back(Ort::Value::CreateTensor<float>(
                    memInfo, const_cast<float*>(tensor.dataFloat.data()),
                    tensor.dataFloat.size(), tensor.shape.data(), tensor.shape.size()));
            } else if (tensor.type == AiTensorType::Uint8) {
                inputValues.push_back(Ort::Value::CreateTensor<std::uint8_t>(
                    memInfo, const_cast<std::uint8_t*>(tensor.dataBytes.data()),
                    tensor.dataBytes.size(), tensor.shape.data(), tensor.shape.size()));
            } else if (tensor.type == AiTensorType::Int64) {
                inputValues.push_back(Ort::Value::CreateTensor<std::int64_t>(
                    memInfo, const_cast<std::int64_t*>(tensor.dataInt64.data()),
                    tensor.dataInt64.size(), tensor.shape.data(), tensor.shape.size()));
            } else if (tensor.type == AiTensorType::Bool) {
                // ORT 的 bool 张量在 C++ API 中用 bool*（1 字节/元素）；
                // core 以 dataBytes 承载，此处按 bool 语义直接传入。
                inputValues.push_back(Ort::Value::CreateTensor<bool>(
                    memInfo, reinterpret_cast<bool*>(
                                 const_cast<std::uint8_t*>(tensor.dataBytes.data())),
                    tensor.dataBytes.size(), tensor.shape.data(), tensor.shape.size()));
            } else {
                inputValues.push_back(Ort::Value::CreateTensor<std::int8_t>(
                    memInfo, reinterpret_cast<std::int8_t*>(
                                 const_cast<std::uint8_t*>(tensor.dataBytes.data())),
                    tensor.dataBytes.size(), tensor.shape.data(), tensor.shape.size()));
            }
        }
        std::vector<const char*> inputNamePtrs;
        for (const auto& name : impl_->inputNames) {
            inputNamePtrs.push_back(name.c_str());
        }
        std::vector<const char*> outputNamePtrs;
        for (const auto& name : impl_->outputNames) {
            outputNamePtrs.push_back(name.c_str());
        }
        auto results = impl_->session->Run(Ort::RunOptions{nullptr}, inputNamePtrs.data(),
                                           inputValues.data(), inputValues.size(),
                                           outputNamePtrs.data(), outputNamePtrs.size());
        outputs.clear();
        for (auto& value : results) {
            const auto info = value.GetTensorTypeAndShapeInfo();
            AiTensor tensor;
            tensor.shape = info.GetShape();
            tensor.type = mapOrtType(info.GetElementType());
            // 注意：输出形状可能含 0 长度维度（如 Whisper 首步的
            // present.*.decoder.key 为 [1,6,0,64] —— 合法的空 cache）。
            // 空张量是**有效输出**，不能当作错误（此前误判为「形状无效」，
            // 导致 Whisper 自回归完全无法运行）。
            std::size_t count = 1;
            bool shapeValid = !tensor.shape.empty();
            for (const std::int64_t dim : tensor.shape) {
                if (dim < 0) {
                    shapeValid = false;  // 负维度非法
                    break;
                }
                count *= static_cast<std::size_t>(dim);  // 0 维度 → count = 0（合法）
            }
            if (!shapeValid) {
                if (outputFailure != nullptr) *outputFailure = "输出张量形状无效";
                return false;
            }
            if (tensor.type == AiTensorType::Float32) {
                const float* data = value.GetTensorData<float>();
                tensor.dataFloat.assign(data, data + count);
            } else if (tensor.type == AiTensorType::Int64) {
                const std::int64_t* data = value.GetTensorData<std::int64_t>();
                tensor.dataInt64.assign(data, data + count);
            } else if (tensor.type == AiTensorType::Int8) {
                const std::int8_t* data = value.GetTensorData<std::int8_t>();
                tensor.dataBytes.assign(reinterpret_cast<const std::uint8_t*>(data),
                                        reinterpret_cast<const std::uint8_t*>(data) + count);
            } else {
                const std::uint8_t* data = value.GetTensorData<std::uint8_t>();
                tensor.dataBytes.assign(data, data + count);
            }
            outputs.push_back(std::move(tensor));
        }
        return true;
    } catch (const std::exception& e) {
        if (outputFailure != nullptr) {
            *outputFailure = std::string("推理失败：") + e.what();
        }
        return false;
    }
#else
    (void)inputs;
    (void)outputs;
    if (outputFailure != nullptr) {
        *outputFailure = "此构建未链接 ONNX Runtime——AI 推理不可用（可选依赖）";
    }
    return false;
#endif
}

// =============================================================================
// 图像预处理 / 后处理（纯像素运算；与 ORT 无关，恒可用）
// =============================================================================

bool aiPreprocessImage(const std::uint8_t* pixels, int srcWidth, int srcHeight,
                       const AiImagePreprocess& config, AiTensor& out) {
    if (pixels == nullptr || srcWidth <= 0 || srcHeight <= 0 || !config.valid()) {
        return false;
    }
    // 双线性缩放 + HWC(RGB) → CHW 重排 + 归一化
    out = AiTensor{};
    out.type = config.type;
    out.shape = {1, 3, config.height, config.width};
    const std::size_t count =
        static_cast<std::size_t>(3) * config.height * config.width;
    out.dataFloat.assign(count, 0.0f);
    for (int y = 0; y < config.height; ++y) {
        for (int x = 0; x < config.width; ++x) {
            // 目标像素中心 → 源坐标（双线性）
            const double sx =
                (static_cast<double>(x) + 0.5) * srcWidth / config.width - 0.5;
            const double sy =
                (static_cast<double>(y) + 0.5) * srcHeight / config.height - 0.5;
            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            const double fx = sx - x0;
            const double fy = sy - y0;
            for (int c = 0; c < 3; ++c) {
                double value = 0.0;
                // 2x2 邻域双线性（边缘钳制）
                for (int dy = 0; dy <= 1; ++dy) {
                    for (int dx = 0; dx <= 1; ++dx) {
                        const int px = std::min(srcWidth - 1, std::max(0, x0 + dx));
                        const int py = std::min(srcHeight - 1, std::max(0, y0 + dy));
                        const double weight =
                            (dx == 0 ? 1.0 - fx : fx) * (dy == 0 ? 1.0 - fy : fy);
                        const std::size_t idx =
                            (static_cast<std::size_t>(py) * srcWidth + px) * 4 +
                            static_cast<std::size_t>(c);
                        value += weight * static_cast<double>(pixels[idx]) / 255.0;
                    }
                }
                // 归一化（mean/stddev；未配置则 [0,1] 原样）
                if (!config.mean.empty()) {
                    value = (value - config.mean[static_cast<std::size_t>(c)]) /
                            config.stddev[static_cast<std::size_t>(c)];
                }
                // CHW 索引：c * H * W + y * W + x
                out.dataFloat[static_cast<std::size_t>(c) * config.height * config.width +
                              static_cast<std::size_t>(y) * config.width + x] =
                    static_cast<float>(value);
            }
        }
    }
    if (config.type != AiTensorType::Float32) {
        // 量化模型输入：float → 字节（[0,1] 反量化映射；mean/stddev 配置时按值域钳制）
        out.dataBytes.assign(count, 0);
        for (std::size_t i = 0; i < count; ++i) {
            const double v = std::min(1.0, std::max(0.0,
                                                    static_cast<double>(out.dataFloat[i])));
            out.dataBytes[i] = static_cast<std::uint8_t>(v * 255.0 + 0.5);
        }
        out.dataFloat.clear();
    }
    return true;
}

bool aiPostprocessMask(const AiTensor& mask, int outWidth, int outHeight,
                       std::vector<std::uint8_t>& rgba) {
    // 期望形状 [1, 1, H, W]（宽松：任意 4 维且后两维为正）
    if (mask.type != AiTensorType::Float32 || mask.shape.size() != 4) {
        return false;
    }
    const int maskH = static_cast<int>(mask.shape[2]);
    const int maskW = static_cast<int>(mask.shape[3]);
    const std::size_t maskCount =
        static_cast<std::size_t>(mask.shape[1]) * maskH * maskW;
    if (maskW <= 0 || maskH <= 0 || mask.dataFloat.size() < maskCount ||
        outWidth <= 0 || outHeight <= 0) {
        return false;
    }
    rgba.assign(static_cast<std::size_t>(outWidth) * outHeight * 4, 0);
    // 双线性放大（单通道）→ alpha（RGB 置白，供上层着色）
    for (int y = 0; y < outHeight; ++y) {
        for (int x = 0; x < outWidth; ++x) {
            const double sx =
                (static_cast<double>(x) + 0.5) * maskW / outWidth - 0.5;
            const double sy =
                (static_cast<double>(y) + 0.5) * maskH / outHeight - 0.5;
            const int x0 = static_cast<int>(std::floor(sx));
            const int y0 = static_cast<int>(std::floor(sy));
            const double fx = sx - x0;
            const double fy = sy - y0;
            double value = 0.0;
            for (int dy = 0; dy <= 1; ++dy) {
                for (int dx = 0; dx <= 1; ++dx) {
                    const int px = std::min(maskW - 1, std::max(0, x0 + dx));
                    const int py = std::min(maskH - 1, std::max(0, y0 + dy));
                    const double weight = (dx == 0 ? 1.0 - fx : fx) * (dy == 0 ? 1.0 - fy : fy);
                    value += weight * mask.dataFloat[static_cast<std::size_t>(py) * maskW + px];
                }
            }
            const double coverage = std::min(1.0, std::max(0.0, value));
            const std::size_t idx =
                (static_cast<std::size_t>(y) * outWidth + x) * 4;
            rgba[idx] = 255;
            rgba[idx + 1] = 255;
            rgba[idx + 2] = 255;
            rgba[idx + 3] = static_cast<std::uint8_t>(coverage * 255.0 + 0.5);
        }
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
