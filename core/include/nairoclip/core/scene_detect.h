#pragma once

#include "nairoclip/core/reference_style.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 镜头边界检测（Scene / Shot Detection）
// =============================================================================
// 输入：逐帧的缩略图特征（RGB 均值 / 直方图差异，由平台层解码并降采样后传入）；
// 输出：ShotBoundary 列表（供 rhythm 分析消费）。
//
// 说明：本模块是纯算法层（可测试），解码与降采样由平台层负责。

/// 单帧特征（平台层从解码帧计算）。
struct FrameFeature {
    Time timestamp;             // 帧时间戳
    double avgR = 0.0;          // 均值 [0,1]
    double avgG = 0.0;
    double avgB = 0.0;
    double histogramDiff = 0.0; // 与上一帧的直方图差异 [0,1]（由平台层计算）
};

/// 检测配置。
struct SceneDetectConfig {
    /// 直方图差异阈值（超过视为镜头切换）[0,1]。
    double threshold = 0.35;
    /// 最短镜头时长（秒）——防止闪烁误报。
    double minShotDurationSeconds = 0.25;
    /// 是否合并相似相邻镜头（色差 < tolerance）。
    double colorTolerance = 0.02;
};

/// 从逐帧特征检测镜头边界。
/// 返回的 ShotBoundary：lastShot 截止素材末尾（由 endTime 参数给出）。
/// motionScore/brightness 由帧特征估算（运动=最大直方图差异，亮度=均值）。
std::vector<ShotBoundary> detectShots(const std::vector<FrameFeature>& features,
                                      const Time& endTime, const SceneDetectConfig& config = {});

}  // namespace core
}  // namespace nairoclip
