// =============================================================================
// AI 推理引擎（ONNX Runtime 可选集成）测试（M2.31）
//   —— 文档第一段「一、技术选型总纲：统一推理引擎 ONNX Runtime」+「1.2 通用架构」
// =============================================================================
// 覆盖：
//   · 可用性查询（有/无 ORT 两种构建都要通过——无 ORT 时明确不可用，不伪造）；
//   · 后端枚举/名称/解析 + 可用性探测（CPU 恒可用；EP 保守返回 false）；
//   · 张量（elementCount/valid/byteSize）；
//   · 图像预处理（HWC→CHW 重排、双线性缩放、归一化、量化类型）；
//   · 蒙版后处理（双线性放大 + 覆盖率映射）；
//   · 会话（加载/推理/元数据/失败路径）——有 ORT 且模型存在时做真实推理。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/ai_engine.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 微型测试模型路径（M2.31 构建期生成：MatMul + Add，188 字节，IR v9）。
constexpr const char* kTinyModelPath = "/workspace/deps/tiny_test.onnx";

bool tinyModelExists() {
    std::ifstream check(kTinyModelPath, std::ios::binary);
    return check.good();
}

}  // namespace

// ---------------------------------------------------------------------------
// 可用性与后端
// ---------------------------------------------------------------------------

NC_TEST(AiEngine, AvailabilityAndInfo) {
    const AiEngineInfo info = aiEngineInfo();
    // 可用性与 info.available 必须一致（不伪造）
    NC_CHECK_EQ(info.available, aiEngineAvailable());
    NC_CHECK(!info.note.empty());
    if (info.available) {
        NC_CHECK(!info.version.empty());  // 有 ORT 时版本非空
        NC_CHECK(info.cpuBackend);        // CPU 后端恒可用（文档强制）
    } else {
        NC_CHECK(info.version.empty());   // 无 ORT 时明确不可用
    }
}

NC_TEST(AiEngine, BackendNamesAndParsing) {
    NC_CHECK_EQ(std::string(aiBackendName(AiBackend::Cpu)), std::string("cpu"));
    NC_CHECK_EQ(std::string(aiBackendName(AiBackend::Nnapi)), std::string("nnapi"));
    NC_CHECK_EQ(std::string(aiBackendName(AiBackend::CoreMl)), std::string("coreml"));
    NC_CHECK_EQ(std::string(aiBackendName(AiBackend::Cuda)), std::string("cuda"));
    NC_CHECK_EQ(std::string(aiBackendName(AiBackend::DirectMl)), std::string("directml"));
    NC_CHECK_EQ(std::string(aiBackendName(AiBackend::Xnnpack)), std::string("xnnpack"));
    AiBackend backend;
    NC_CHECK(aiBackendFromString("cpu", backend));
    NC_CHECK(backend == AiBackend::Cpu);
    NC_CHECK(aiBackendFromString("NNAPI", backend));  // 大小写不敏感
    NC_CHECK(backend == AiBackend::Nnapi);
    NC_CHECK(aiBackendFromString("coreml", backend));
    NC_CHECK(backend == AiBackend::CoreMl);
    NC_CHECK(!aiBackendFromString("bogus", backend));
    NC_CHECK(!aiBackendFromString(nullptr, backend));
}

NC_TEST(AiEngine, BackendAvailabilityFailClosed) {
    // CPU 的可用性跟随引擎（有 ORT 才可用）
    NC_CHECK_EQ(aiBackendAvailable(AiBackend::Cpu), aiEngineAvailable());
    // EP 后端保守返回 false（平台层在真实设备上探测；文档 1.3）
    NC_CHECK(!aiBackendAvailable(AiBackend::Nnapi));
    NC_CHECK(!aiBackendAvailable(AiBackend::CoreMl));
    NC_CHECK(!aiBackendAvailable(AiBackend::Cuda));
    NC_CHECK(!aiBackendAvailable(AiBackend::DirectMl));
    NC_CHECK(!aiBackendAvailable(AiBackend::Xnnpack));
}

// ---------------------------------------------------------------------------
// 张量
// ---------------------------------------------------------------------------

NC_TEST(AiEngine, TensorElementCountAndValidity) {
    AiTensor tensor;
    tensor.type = AiTensorType::Float32;
    tensor.shape = {1, 3, 224, 224};
    NC_CHECK_EQ(tensor.elementCount(), std::size_t(1 * 3 * 224 * 224));
    NC_CHECK(!tensor.valid());  // 数据未填充 → 无效
    tensor.dataFloat.assign(tensor.elementCount(), 0.0f);
    NC_CHECK(tensor.valid());
    NC_CHECK_EQ(aiTensorByteSize(tensor), tensor.elementCount() * sizeof(float));
    // 量化类型
    AiTensor quant;
    quant.type = AiTensorType::Uint8;
    quant.shape = {1, 1, 4, 4};
    quant.dataBytes.assign(16, 128);
    NC_CHECK(quant.valid());
    NC_CHECK_EQ(aiTensorByteSize(quant), std::size_t(16));
    // 动态维度（<=0）→ 无效
    AiTensor dynamic;
    dynamic.shape = {1, 3, -1, -1};
    NC_CHECK_EQ(dynamic.elementCount(), std::size_t(0));
    NC_CHECK(!dynamic.valid());
    // 空 shape
    AiTensor empty;
    NC_CHECK_EQ(empty.elementCount(), std::size_t(0));
    NC_CHECK(!empty.valid());
}

// ---------------------------------------------------------------------------
// 图像预处理（纯像素运算；无 ORT 也恒可用）
// ---------------------------------------------------------------------------

NC_TEST(AiEngine, PreprocessHwcToChwAndNormalization) {
    const int w = 4, h = 4;
    // 纯红图（R=255, G=0, B=0）
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 255);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = 255;
        rgba[i + 1] = 0;
        rgba[i + 2] = 0;
    }
    AiImagePreprocess config;
    config.width = 8;
    config.height = 8;
    AiTensor out;
    NC_CHECK(aiPreprocessImage(rgba.data(), w, h, config, out));
    NC_CHECK_EQ(out.shape.size(), std::size_t(4));
    NC_CHECK_EQ(out.shape[1], std::int64_t(3));
    NC_CHECK_EQ(out.shape[2], std::int64_t(8));
    NC_CHECK_EQ(out.shape[3], std::int64_t(8));
    NC_CHECK_EQ(out.elementCount(), std::size_t(3 * 8 * 8));
    // CHW 布局：R 通道在前（值 1.0），G/B 通道为 0
    NC_CHECK(std::fabs(out.dataFloat[0] - 1.0f) < 1e-4);            // R[0]
    NC_CHECK(std::fabs(out.dataFloat[8 * 8]) < 1e-4);               // G[0]
    NC_CHECK(std::fabs(out.dataFloat[2 * 8 * 8]) < 1e-4);           // B[0]
    // 归一化：mean=0.5/std=0.5 → (1.0-0.5)/0.5 = 1.0
    config.mean = {0.5f, 0.5f, 0.5f};
    config.stddev = {0.5f, 0.5f, 0.5f};
    AiTensor normalized;
    NC_CHECK(aiPreprocessImage(rgba.data(), w, h, config, normalized));
    NC_CHECK(std::fabs(normalized.dataFloat[0] - 1.0f) < 1e-4);
    NC_CHECK(std::fabs(normalized.dataFloat[8 * 8] + 1.0f) < 1e-4);  // G: (0-0.5)/0.5 = -1
}

NC_TEST(AiEngine, PreprocessRejectsInvalid) {
    AiImagePreprocess config;
    config.width = 8;
    config.height = 8;
    std::vector<std::uint8_t> rgba(16, 0);
    AiTensor out;
    NC_CHECK(!aiPreprocessImage(nullptr, 2, 2, config, out));
    NC_CHECK(!aiPreprocessImage(rgba.data(), 0, 2, config, out));
    NC_CHECK(!aiPreprocessImage(rgba.data(), 2, -1, config, out));
    AiImagePreprocess bad;
    bad.width = 0;  // 非法
    NC_CHECK(!bad.valid());
    NC_CHECK(!aiPreprocessImage(rgba.data(), 2, 2, bad, out));
    // 归一化向量长度非法
    AiImagePreprocess badMean = config;
    badMean.mean = {0.5f, 0.5f};  // 需 3
    NC_CHECK(!badMean.valid());
    NC_CHECK(!aiPreprocessImage(rgba.data(), 2, 2, badMean, out));
}

NC_TEST(AiEngine, PreprocessQuantizedOutput) {
    // 量化模型输入（Uint8）：输出为字节缓冲
    const int w = 2, h = 2;
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 0);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = 255;  // 红
        rgba[i + 3] = 255;
    }
    AiImagePreprocess config;
    config.width = 4;
    config.height = 4;
    config.type = AiTensorType::Uint8;
    AiTensor out;
    NC_CHECK(aiPreprocessImage(rgba.data(), w, h, config, out));
    NC_CHECK(out.type == AiTensorType::Uint8);
    NC_CHECK(out.dataFloat.empty());  // 量化输出用 dataBytes
    NC_CHECK_EQ(out.dataBytes.size(), std::size_t(3 * 4 * 4));
    NC_CHECK_EQ(out.dataBytes[0], std::uint8_t(255));  // R → 255
}

// ---------------------------------------------------------------------------
// 蒙版后处理
// ---------------------------------------------------------------------------

NC_TEST(AiEngine, PostprocessMaskUpsamplesAndMaps) {
    AiTensor mask;
    mask.type = AiTensorType::Float32;
    mask.shape = {1, 1, 2, 2};
    mask.dataFloat = {0.0f, 0.5f, 1.0f, 0.25f};
    std::vector<std::uint8_t> rgba;
    NC_CHECK(aiPostprocessMask(mask, 4, 4, rgba));
    NC_CHECK_EQ(rgba.size(), std::size_t(4 * 4 * 4));
    // 角落采样应接近原值（双线性）：左上角 alpha ≈ 0
    NC_CHECK(rgba[3] < 40);
    // RGB 置白（供上层着色）
    NC_CHECK_EQ(rgba[0], std::uint8_t(255));
    NC_CHECK_EQ(rgba[1], std::uint8_t(255));
    NC_CHECK_EQ(rgba[2], std::uint8_t(255));
    // alpha 是覆盖率 × 255
    // 源蒙版（行主序）：(0,0)=0.0 (0,1)=0.5 (1,0)=1.0 (1,1)=0.25
    // 右下角 (3,3) 的双线性采样被钳制到源 (1,1)=0.25 → alpha ≈ 64
    const std::size_t bottomRight = (static_cast<std::size_t>(3) * 4 + 3) * 4 + 3;
    NC_CHECK(rgba[bottomRight] > 50 && rgba[bottomRight] < 80);
    // 左下角 (0,3) 邻域为源 (1,0)=1.0 → alpha ≈ 255
    const std::size_t bottomLeft = (static_cast<std::size_t>(3) * 4 + 0) * 4 + 3;
    NC_CHECK(rgba[bottomLeft] > 200);
}

NC_TEST(AiEngine, PostprocessRejectsInvalid) {
    AiTensor mask;
    mask.type = AiTensorType::Float32;
    mask.shape = {1, 1, 2, 2};
    mask.dataFloat = {0.0f, 0.5f, 1.0f, 0.25f};
    std::vector<std::uint8_t> rgba;
    // 非法尺寸
    NC_CHECK(!aiPostprocessMask(mask, 0, 4, rgba));
    NC_CHECK(!aiPostprocessMask(mask, 4, -1, rgba));
    // 形状不合法（非 4 维）
    AiTensor badShape = mask;
    badShape.shape = {2, 2};
    NC_CHECK(!aiPostprocessMask(badShape, 4, 4, rgba));
    // 类型不对
    AiTensor badType = mask;
    badType.type = AiTensorType::Uint8;
    NC_CHECK(!aiPostprocessMask(badType, 4, 4, rgba));
    // 数据不足
    AiTensor shortData = mask;
    shortData.dataFloat = {0.5f};
    NC_CHECK(!aiPostprocessMask(shortData, 4, 4, rgba));
}

// ---------------------------------------------------------------------------
// 会话（加载 / 推理 / 失败路径）
// ---------------------------------------------------------------------------

NC_TEST(AiEngine, SessionFailsClosedWithoutModel) {
    AiSession session;
    std::string failure;
    // 空路径
    NC_CHECK(!session.load("", AiBackend::Cpu, &failure));
    NC_CHECK(!failure.empty());
    // 不存在的文件
    failure.clear();
    NC_CHECK(!session.load("/nonexistent/model.onnx", AiBackend::Cpu, &failure));
    NC_CHECK(!failure.empty());
    NC_CHECK(!session.loaded());
    // 未加载时推理失败（不伪造）
    AiTensor input;
    input.type = AiTensorType::Float32;
    input.shape = {1, 2};
    input.dataFloat = {1.0f, 2.0f};
    std::vector<AiTensor> outputs;
    failure.clear();
    NC_CHECK(!session.run({input}, outputs, &failure));
    NC_CHECK(!failure.empty());
    NC_CHECK(outputs.empty());
    // 元数据为空
    NC_CHECK(session.inputs().empty());
    NC_CHECK(session.outputs().empty());
}

NC_TEST(AiEngine, SessionRealInferenceOnTinyModel) {
    if (!aiEngineAvailable()) {
        return;  // 无 ORT：跳过（无 ORT 构建的正确行为见 SessionFailsClosedWithoutModel）
    }
    if (!tinyModelExists()) {
        return;  // 模型未生成：跳过
    }
    // 微型模型：Y = X·W + B，W=[[1,0.5],[0.5,1]]，B=[0.1,-0.1]
    // 输入 [1, 2] → mm=[2.0, 2.5] → +B=[2.1, 2.4]
    AiSession session;
    std::string failure;
    NC_CHECK(session.load(kTinyModelPath, AiBackend::Cpu, &failure));
    NC_CHECK(session.loaded());
    NC_CHECK_EQ(session.inputs().size(), std::size_t(1));
    NC_CHECK_EQ(session.outputs().size(), std::size_t(1));
    NC_CHECK_EQ(session.inputs()[0].name, std::string("input"));
    NC_CHECK_EQ(session.inputs()[0].shape.size(), std::size_t(2));
    NC_CHECK_EQ(session.inputs()[0].shape[0], std::int64_t(1));
    NC_CHECK_EQ(session.inputs()[0].shape[1], std::int64_t(2));
    AiTensor input;
    input.type = AiTensorType::Float32;
    input.shape = {1, 2};
    input.dataFloat = {1.0f, 2.0f};
    std::vector<AiTensor> outputs;
    NC_CHECK(session.run({input}, outputs, &failure));
    NC_CHECK_EQ(outputs.size(), std::size_t(1));
    NC_CHECK(outputs[0].dataFloat.size() >= 2);
    // 真实推理数值（与手算精确一致）
    NC_CHECK(std::fabs(outputs[0].dataFloat[0] - 2.1f) < 1e-4);
    NC_CHECK(std::fabs(outputs[0].dataFloat[1] - 2.4f) < 1e-4);
    // 输入数量不匹配 → 失败
    std::vector<AiTensor> outs2;
    failure.clear();
    NC_CHECK(!session.run({input, input}, outs2, &failure));
    NC_CHECK(!failure.empty());
    // 输入形状不匹配 → 失败
    AiTensor wrongShape;
    wrongShape.type = AiTensorType::Float32;
    wrongShape.shape = {1, 3};
    wrongShape.dataFloat = {1.0f, 2.0f, 3.0f};
    failure.clear();
    NC_CHECK(!session.run({wrongShape}, outs2, &failure));
    // 卸载后推理失败
    session.unload();
    NC_CHECK(!session.loaded());
    failure.clear();
    NC_CHECK(!session.run({input}, outs2, &failure));
}
