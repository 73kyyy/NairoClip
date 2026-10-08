#pragma once

#include "nairoclip/core/media_io.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 智能重构图（Intelligent Reframe）—— 横转竖 / 竖转横 / 跟踪灵敏度
// =============================================================================
// 产品对应（文档第 2 部分 AI实验室 / 第 7 部分 AI工具）：
//   「智能重构图 → 横转竖/竖转横/跟踪灵敏度」。
//
// 场景：源素材比例与目标画布不一致（如 16:9 横拍素材要发 9:16 竖屏），
//       静态裁切（frame_fit 的 Crop/Fill）会固定切掉画面两侧；
//       智能重构图逐帧估计「主体位置」并让裁剪窗口跟随主体移动，
//       从而在转换画幅时始终把主体保持在画面内（横转竖/竖转横）。
//
// 算法（全部本地、零第三方依赖、可解释）：
//   1. 显著性估计（computeFrameSaliency）：
//      - 亮度梯度（边缘/细节密度，主体通常有更多细节）；
//      - 色彩对比（与全图均值的偏离）；
//      - 中心/上部先验（人像构图先验，权重可配）；
//      - 可选运动能量（与前一帧差分，由调用方提供前一帧）。
//   2. 主体位置：显著性图的重心（加权平均）；低能量帧判为「无主体」
//      → 沿用前一位置（保持稳定，避免无主体时窗口乱飘）。
//   3. 轨迹规划（planReframe）：
//      - 时间平滑（滑动平均，窗口由「跟踪灵敏度」映射）；
//      - 速度限制（限制每秒平移量，防抖/防甩镜）；
//      - 边界钳制（窗口必须完整落在源画面内）；
//      - 输出关键帧序列（供序列化与逐帧查询）。
//   4. 渲染（reframeRenderFrameAt / cropWindowAt）：按轨迹窗口裁剪缩放。
//
// 与 frame_fit 的关系：frame_fit 是静态适配（整片固定策略），
// 智能重构图是其动态扩展（逐帧窗口），两者可在同一片段上叠加使用。

/// 重构图跟踪配置。
struct ReframeConfig {
    /// 跟踪灵敏度 0..1：越高越积极跟随主体（响应快，可能更抖）；
    /// 越低越平滑（响应慢，更稳）。映射到平滑窗口长度。
    double trackingSensitivity = 0.5;
    /// 平滑窗口时长边界（秒）——实际值由灵敏度插值：
    ///   windowSeconds = lerp(maxSmoothing, minSmoothing, sensitivity)
    double minSmoothingSeconds = 0.12;
    double maxSmoothingSeconds = 0.9;
    /// 最大平移速度（画布宽度比例/秒）：限制窗口移动速度，防甩镜。
    double maxPanSpeedPerSecond = 0.5;
    /// 中心先验权重 0..1（主体通常靠近画面中心）。
    double centerPriorWeight = 0.25;
    /// 上部先验权重 0..1（人像头部/上半身在画面上部）。
    double upperPriorWeight = 0.15;
    /// 显著性低能量阈值：低于此值视为「无主体」，沿用前一位置。
    double minEnergy = 0.15;

    bool valid() const {
        return trackingSensitivity >= 0.0 && trackingSensitivity <= 1.0 &&
               maxPanSpeedPerSecond >= 0.0 && centerPriorWeight >= 0.0 &&
               centerPriorWeight <= 1.0 && upperPriorWeight >= 0.0 &&
               upperPriorWeight <= 1.0;
    }
    /// 实际平滑窗口时长（由灵敏度插值）。
    double smoothingSeconds() const {
        const double s = trackingSensitivity < 0.0 ? 0.0
                         : (trackingSensitivity > 1.0 ? 1.0 : trackingSensitivity);
        return maxSmoothingSeconds + (minSmoothingSeconds - maxSmoothingSeconds) * s;
    }
};

/// 单帧显著性分析结果。
struct FrameSaliency {
    Time timestamp;
    /// 主体重心（归一化 0..1；无主体时无意义）。
    double centerX = 0.5;
    double centerY = 0.5;
    /// 显著性总能量（0..1；低能量 = 无明确主体）。
    double energy = 0.0;
    /// 是否有明确主体（energy >= 阈值）。
    bool hasSubject = false;
};

/// 计算一帧的显著性（从 RGBA8 像素）。
/// - src：交错 RGBA8；stride：每行字节数（0 = width*4）；
/// - prevSrc/prevStride：前一帧（可空；提供时启用运动能量项）；
/// - 返回 false：参数非法（空指针/非正尺寸）。
bool computeFrameSaliency(const std::uint8_t* src, int width, int height, int stride,
                          const std::uint8_t* prevSrc, int prevStride,
                          const ReframeConfig& config, const Time& timestamp,
                          FrameSaliency& out);

/// 重构图关键帧（裁剪窗口中心）。
struct ReframeKeyframe {
    Time time;
    /// 窗口中心（归一化 0..1，相对源画面）。
    double centerX = 0.5;
    double centerY = 0.5;
};

/// 重构图轨迹（可序列化、可逐帧查询）。
struct ReframeTrack {
    std::vector<ReframeKeyframe> keyframes;  // 按 time 升序
    int srcWidth = 0;
    int srcHeight = 0;
    /// 跟踪灵敏度（记录生成参数，供 UI 回显与再生成）。
    double trackingSensitivity = 0.5;
    std::string note;  // 诊断（如「全程未检测到主体，退化为居中裁剪」）

    bool valid() const { return !keyframes.empty() && srcWidth > 0 && srcHeight > 0; }
};

/// 规划重构图轨迹（平滑 + 速度限制 + 边界钳制）。
/// - saliency：按时间升序的单帧显著性序列（computeFrameSaliency 产出）；
/// - srcWidth/srcHeight：源画布尺寸；
/// - dstWidth/dstHeight：目标画布尺寸（决定裁剪窗口大小与中心活动范围）；
/// - 返回轨迹（keyframes 数量 = saliency 数量；低能量帧沿用前一位置）。
ReframeTrack planReframe(const std::vector<FrameSaliency>& saliency, int srcWidth, int srcHeight,
                         int dstWidth, int dstHeight, const ReframeConfig& config);

/// 查询时刻 t 的裁剪窗口中心（关键帧线性插值；t 超出范围时取端点）。
/// 返回 false 表示轨迹无效（空），out 保留默认值。
bool reframeAt(const ReframeTrack& track, const Time& t, ReframeKeyframe& out);

/// 裁剪窗口（源画面内的像素矩形）。
struct CropWindow {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// 计算某时刻的裁剪窗口像素矩形（已钳制在源画面内）。
/// 窗口尺寸：按目标画布比例取源画面内最大矩形。
/// 返回 false：参数非法（轨迹无效/目标尺寸非正）。
bool cropWindowAt(const ReframeTrack& track, const Time& t, int dstWidth, int dstHeight,
                  CropWindow& out);

/// 按轨迹裁剪并缩放到目标尺寸（双线性）。
/// - dst 必须已分配 dstWidth × dstHeight × 4 字节；
/// - 参数非法返回 false 且不触碰 dst。
bool reframeRenderFrameAt(const ReframeTrack& track, const Time& t, const std::uint8_t* src,
                          int srcWidth, int srcHeight, int srcStride, std::uint8_t* dst,
                          int dstWidth, int dstHeight);

/// 重构方向（供 UI 文案：横转竖 / 竖转横 / 同方向）。
enum class ReframeDirection {
    LandscapeToPortrait = 0,  // 横转竖
    PortraitToLandscape = 1,  // 竖转横
    SameOrientation = 2,      // 同方向（比例接近，无需重构）
};

/// 判定重构方向（按源/目标的横竖关系；比例差异 < 5% 视为同方向）。
ReframeDirection reframeDirection(int srcWidth, int srcHeight, int dstWidth, int dstHeight);

/// 方向名称（"横转竖" / "竖转横" / "同方向"）。
const char* reframeDirectionName(ReframeDirection direction);

}  // namespace core
}  // namespace nairoclip
