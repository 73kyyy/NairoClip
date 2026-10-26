#pragma once
// =============================================================================
// 超分辨率 AI（Super Resolution AI）—— 文档 Part 4-A「AI工具箱 → 超分辨率」
// =============================================================================
// 产品对应（原文）：
//   「超分辨率 [按钮组]：2x / 4x  默认 2x
//     处理范围 [Radio]：整段素材 / 选时间段  默认 整段素材」
//
// 本模块实现真实推理链路（模型 models/super_res_2x4x/model.onnx）：
//   · 预处理：RGBA8 → 归一化 CHW（[0,1]；超分模型标准）→ 按模型输入尺寸分块；
//   · 推理：逐块前向（重叠分块避免接缝）；
//   · 后处理：输出通道 clamp 到 [0,255] → 按倍数双线性缩放兜底 → 拼回 RGBA8。
//
// 分块策略（关键工程约束）：
//   · 超分模型输入尺寸固定（通常 256×256 或动态）；整帧 4K 直接送入会爆显存/失败；
//   · 采用重叠分块（tile + halo），块间按线性羽化混合，消除可见接缝；
//   · 块内推理失败时该块回退双线性放大（保证「不整帧失败」），并汇总警告。
//
// 与既有模块的关系：
//   · frame_fit：纯几何缩放（无细节增强）——本模块的兜底路径就是它；
//   · interpolation_ai：时间域补帧；本模块是空间域放大，二者互补；
//   · proxy：代理剪辑是「降分辨率加速」，本模块是「升分辨率提质」，方向相反。
//
// 可用性：需 ONNX Runtime + 模型；不可用时明确失败（不伪造放大结果）。
// 三端对等：导出路径必须完整分辨率；预览可仅处理可视区域（scale 由调用方裁剪）。

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 放大倍数（文档「2x / 4x」）。
enum class SuperResScale {
    X2 = 2,  ///< 2 倍
    X4 = 4,  ///< 4 倍
};

/// 模型名称（models/super_res_2x4x 下的权重文件名）。
constexpr const char* kSuperResModelFile = "model.onnx";

/// 分块参数（重叠 halo；0 = 自动按模型输入尺寸推导）。
struct SuperResTiling {
    int tileSize = 0;  ///< 块边长（像素）；0 = 自动
    int halo = 0;      ///< 重叠边宽（像素）；0 = 自动（tileSize / 8）
    bool blendSeams = true;  ///< 块间羽化混合（关闭会有可见接缝，仅调试用）
};

/// 统计（供 UI 展示与诊断）。
struct SuperResStats {
    int inputWidth = 0;
    int inputHeight = 0;
    int outputWidth = 0;
    int outputHeight = 0;
    int tilesTotal = 0;    ///< 分块总数
    int tilesFallback = 0; ///< 回退双线性的块数（0 = 全部走模型）
    double elapsedMs = 0.0;
};

/// 超分辨率推理器（会话复用；单实例单线程）。
class SuperResEstimator {
public:
    SuperResEstimator();
    ~SuperResEstimator();
    SuperResEstimator(const SuperResEstimator&) = delete;
    SuperResEstimator& operator=(const SuperResEstimator&) = delete;

    /// 加载模型（.onnx 路径）。
    /// 返回 false：引擎不可用 / 文件不存在 / 解析失败（fail-closed）。
    bool load(const std::string& modelPath, std::string* outputFailure = nullptr);
    bool loaded() const { return loaded_; }
    void unload();

    /// 放大单帧。
    /// - pixels：RGBA8 交错，尺寸 width × height；
    /// - scale：2x 或 4x；
    /// - tiling：分块参数（默认自动）；
    /// - outPixels：出参 RGBA8（尺寸 = width*scale × height*scale）；
    /// - outStats：出参统计（可空）；
    /// - outputFailure：失败说明（可空）。
    /// 返回 false：未加载 / 尺寸非法 / 全部块失败。
    bool upscale(const std::uint8_t* pixels, int width, int height, SuperResScale scale,
                 const SuperResTiling& tiling, std::vector<std::uint8_t>& outPixels,
                 SuperResStats* outStats = nullptr, std::string* outputFailure = nullptr);

private:
    bool loaded_ = false;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// 无需模型的纯几何放大（双线性）——兜底路径与「模型不可用」时的诚实降级。
/// 与 frame_fit 的缩放器语义一致（此处独立实现以免调用方为了兜底引入整条管线）。
void upscaleBilinear(const std::uint8_t* pixels, int width, int height, int scale,
                     std::vector<std::uint8_t>& outPixels);

/// 从模型输入形状推导建议分块尺寸（供 UI 预估内存/耗时；失败返回 256）。
int superResSuggestedTileSize(const std::vector<std::int64_t>& modelInputShape);

}  // namespace core
}  // namespace nairoclip
