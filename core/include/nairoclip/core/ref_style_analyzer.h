#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 参考视频分析（AI 剪辑的「理解」层）。
///
/// ## 这一层到底在做什么
///
/// 它**不做语义理解**（不知道画面里是跑车还是猫），也不调用云端 ——
/// 与 chapter.h 的合规约束一致：全部在设备本地、只做几何与色彩统计。
///
/// 它能做的是把「参考视频看起来是什么节奏」量化成可复用的参数：
///   · 镜头切点在哪、每镜多长
///   · 切点上发生了什么（硬切 / 溶解 / 闪白 / 位移 / 缩放……）
///   · 画面抖动的振幅与频率（这两个量本来就存在于像素里）
///   · 色调画像（均值 / 饱和 / 对比）
///   · 运动强度随时间的曲线（用来判断该在哪加变速）
///
/// 这些量足够驱动「同类风格」的成片，但**不等于原片特效参数**。
/// 任何声称能从画面反推出「对方用了 ZoomPunch 0.37」的说法都是假的。

/// 转场类型。数值固定，C ABI 与前端共用，不要重排。
enum class RefTransitionKind {
    Unknown = 0,
    HardCut = 1,   ///< 硬切：单帧跳变
    Dissolve = 2,  ///< 溶解 / 叠化：多帧渐变混合
    Slide = 3,     ///< 滑动 / 推拉：差异重心持续朝一个方向移动
    WhipPan = 4,   ///< 甩镜：高位移 + 高频细节丢失（方向性模糊）
    Zoom = 5,      ///< 缩放：差异由中心向外扩散
    Rotate = 6,    ///< 旋转：差异呈环形分布
    Flash = 7,     ///< 闪白 / 闪黑：整帧亮度尖峰后回落
    Glitch = 8,    ///< 故障：成片的横向条带错位
    Wipe = 9,      ///< 擦除：差异集中在一条扫过的边界带上
};

/// 一次转场事件。
struct RefTransition {
    RefTransitionKind kind = RefTransitionKind::Unknown;
    double atSeconds = 0.0;
    double durationSeconds = 0.0;
    /// 0..1。低于 0.5 时 UI 应显示「疑似」而不是断言。
    double confidence = 0.0;
};

/// 一个镜头（相邻两个切点之间）。
struct RefShot {
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    /// 这段内的平均运动强度 0..1。
    double motion = 0.0;
    double brightness = 0.0;
    double saturation = 0.0;
    double meanR = 0.0;
    double meanG = 0.0;
    double meanB = 0.0;
    double duration() const { return endSeconds - startSeconds; }
};

/// 单帧采样结果。把「解码」与「分析」分开，分析部分可以脱离视频单测。
struct FrameSample {
    double timeSeconds = 0.0;
    /// 原图尺寸（差异量按对角线归一化，避免不同分辨率不可比）。
    int width = 0;
    int height = 0;
    double meanR = 0.0;      ///< 0..1
    double meanG = 0.0;
    double meanB = 0.0;
    double luma = 0.0;       ///< 0..1
    double saturation = 0.0; ///< 0..1
    double contrast = 0.0;   ///< 亮度标准差 0..1
    /// 与上一帧的平均绝对差（0..1）。第一帧填 0。
    double diff = 0.0;
    /// 差异的空间重心（0..1，左上为原点）。无差异时为 -1。
    double diffCentroidX = -1.0;
    double diffCentroidY = -1.0;
    /// 差异沿方向轴的集中度：0 = 铺满全屏，1 = 集中在一条窄带。
    double diffConcentration = 0.0;
    /// 相邻像素梯度能量（0..1）。方向性模糊会让它显著下降。
    double detailEnergy = 0.0;
};

/// 参考视频的完整画像。
struct RefStyleProfile {
    int shotCount = 0;
    std::vector<RefShot> shots;
    std::vector<RefTransition> transitions;

    // 全片色彩画像
    double avgR = 0.0;
    double avgG = 0.0;
    double avgB = 0.0;
    double avgSaturation = 0.0;
    double avgContrast = 0.0;

    // 抖动估计（来自画面位移的周期性抖动）
    double shakeAmplitude = 0.0;    ///< 占画面短边比例 0..0.2
    double shakeFrequencyHz = 0.0;  ///< 0..20
    double shakeConfidence = 0.0;   ///< 0..1

    // 节奏
    double motionIntensity = 0.0;   ///< 全片平均运动 0..1
    double cutsPerMinute = 0.0;     ///< 切点密度
    double totalSeconds = 0.0;

    /// 序列化为 JSON（C ABI 直接返回给前端）。
    std::string toJson() const;
};

/// 从采样序列推导画像。**纯函数**，不碰文件系统 —— 便于单测。
void analyzeFrameSamples(const std::vector<FrameSample>& samples, RefStyleProfile& out);

/// 采样真实视频文件（解码 + 降采样）。失败返回 false 并写 error。
bool sampleReferenceVideo(const std::string& path, double maxFps, int maxSamples,
                          std::vector<FrameSample>& out, std::string& error);

/// 一步到位：采样 + 分析。
bool analyzeReferenceVideo(const std::string& path, RefStyleProfile& out, std::string& error);

/// 转场类型名（中文，给 UI 直接显示）。
const char* refTransitionName(RefTransitionKind kind);

}  // namespace core
}  // namespace nairoclip
