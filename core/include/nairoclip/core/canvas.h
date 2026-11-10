#pragma once
// ============================================================================
// 画布模型（B1-2）
// ----------------------------------------------------------------------------
// 权威数据只有一个：Sequence::width / Sequence::height。
// 宽高比**不单独存储**，一律由宽高用有理数精确计算得出——
// 若另存一份 aspectRatio，改宽高时忘记同步就会出现两个互相冲突的权威源
// （B1 任务书 §5.1 明令禁止）。
//
// 预设表只提供「建议尺寸」，不是限制：用户可以自定义任意合法宽高，
// 同一比例也允许多种分辨率（1:1 可以是 1080 也可以是 2160）。
// ============================================================================

#include "nairoclip/core/rational.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 画布尺寸上下限。
///
/// 为什么下限是 16：编码器对极窄尺寸支持不一，16 是 H.264 宏块粒度(16x16)
/// 的最小合理单位。
/// 为什么上限是 16384：与既有 `nc_set_project_canvas` 的历史约束保持一致，
/// 且主流硬编码器/纹理实现普遍在 16384 附近封顶。**这不是编造的统一上限**，
/// 而是沿用仓库既有策略；真实设备能力另由 GPU 能力查询决定（见下）。
inline constexpr int kCanvasMinDimension = 16;
inline constexpr int kCanvasMaxDimension = 16384;
/// 像素总量上限（宽×高）。8K(7680×4320) ≈ 33.2M；这里放到 8192×8192 = 67M，
/// 给方形大画布留余量，同时挡住 16384×16384 = 268M 这种必然爆内存的组合。
inline constexpr std::int64_t kCanvasMaxPixels = 8192LL * 8192LL;

/// 画布比例预设（B1 §5.2）。
struct CanvasPreset {
    const char* id;      ///< 稳定 ID，如 "16:9"
    const char* label;   ///< 展示名，如 "横屏 16:9"
    int width;           ///< 建议宽度（示例尺寸，不是限制）
    int height;          ///< 建议高度
};

/// 内置预设表（1:1 / 4:5 / 3:4 / 9:16 / 2:3 / 16:9 / 4:3 / 3:2 / 21:9 / 2.39:1）。
const std::vector<CanvasPreset>& canvasPresets();

/// 按 ID 查预设；不存在返回 nullptr。
const CanvasPreset* findCanvasPreset(const std::string& id);

/// 画布校验结果。区分「比例合法」「宽高合法」「像素总量合法」三类
/// （B1 §5.7 要求分开，不能混成一个"非法"）。
struct CanvasValidation {
    bool ok = false;
    std::string reason;       ///< 用户可读原因（ok=true 时为空）
    std::string errorCode;    ///< 稳定码，如 NC-TIMELINE-INVALID_CANVAS_SIZE
};

/// 校验宽高是否构成合法画布。
/// - 非正数、越界、像素总量超限、乘法溢出均拒绝；
/// - 全部用 int64 计算，避免 16384*16384 溢出 int32。
CanvasValidation validateCanvasSize(int width, int height);

/// 宽高比（有理数，精确）。height 为 0 时返回 0/1。
Rational canvasAspectRatio(int width, int height);

/// 判断两个尺寸是否为同一比例（有理数精确比较，**不用浮点**）。
bool sameAspectRatio(int w1, int h1, int w2, int h2);

/// 比例锁定下的尺寸推算（B1 §5.4）。
///
/// 取整策略：保持**给定的那一维**不变，另一维四舍五入到最近整数，
/// 并钳制到合法范围。取整后比例可能不再严格相等（如 1920×(1080*2/3)=720 精确，
/// 但 1000×(1000*9/16)=563 有舍入），调用方需按 `exact` 字段决定是否提示用户。
struct AspectSolve {
    int width = 0;
    int height = 0;
    bool exact = false;   ///< 取整后比例是否与目标比例严格相等
};

/// 给定宽度，按目标比例推算高度。
AspectSolve solveHeightForWidth(int width, Rational ratio);
/// 给定高度，按目标比例推算宽度。
AspectSolve solveWidthForHeight(int height, Rational ratio);

/// 横竖屏切换（B1 §5.5）：交换宽高。返回 false 表示交换后非法。
bool swapCanvasOrientation(int* width, int* height);

// ---------------------------------------------------------------------------
// 素材适配策略（B1 §5.6）
// ---------------------------------------------------------------------------

/// 画布尺寸变化时的素材适配策略。
enum class CanvasAdaptMode {
    /// 保持原有布局：不动任何片段变换参数（超出新画布的内容暂时不可见，
    /// 但**不删除、不裁剪源素材**）。
    Preserve = 0,
    /// 适配画布：把每个片段按保持宽高比缩放至完整落入新画布。
    Fit,
    /// 填充画布：按保持宽高比缩放至覆盖新画布（允许溢出边界）。
    Fill,
};

const char* canvasAdaptModeName(CanvasAdaptMode mode);
bool canvasAdaptModeFromName(const std::string& name, CanvasAdaptMode* out);

/// 计算把 (srcW,srcH) 按 mode 适配到 (dstW,dstH) 所需的**缩放倍数**。
/// 返回值用于写入 `ClipTransform::scale`（1.0 = 铺满既有行为）。
/// Preserve 恒返回 1.0（不改动）。
double canvasAdaptScale(CanvasAdaptMode mode, int srcW, int srcH, int dstW, int dstH);

// ---------------------------------------------------------------------------
// RGBA 背景色（B1-3）
// ---------------------------------------------------------------------------

/// 背景色，**straight alpha**（非预乘）。
///
/// 为什么显式区分：预乘与直通在混合公式上差一个乘法，
/// 混用会让半透明背景偏暗（预乘当直通）或过亮（直通当预乘）。
/// 本项目的渲染管线（frame_fit / xform / preview）都按 straight alpha
/// 写像素，因此这里保持 straight，并在合成处按 straight 公式混合。
struct CanvasBackground {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    /// 打包为 0xRRGGBBAA（与 Sequence::backgroundRgba 一致）。
    std::uint32_t toRgba32() const;
    static CanvasBackground fromRgba32(std::uint32_t rgba);
};

/// 校验打包后的 RGBA（B1 §6.6）。8-bit 通道天然在 0..255，
/// 这里主要挡住「调用方把 0..1 浮点乘 255 后的越界值」与非法输入。
CanvasValidation validateBackgroundRgba(std::uint32_t rgba);

/// straight-alpha 源色叠加到不透明背景上的结果（用于「不支持 Alpha 的导出格式」）。
/// 公式：out = src*a + bg*(1-a)
CanvasBackground flattenOnto(CanvasBackground src, CanvasBackground bg);

}  // namespace core
}  // namespace nairoclip
