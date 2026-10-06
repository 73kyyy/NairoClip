#pragma once

#include "nairoclip/core/media_io.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 缩略图生成器（Timeline Thumbnails）
// =============================================================================
// 用途：时间轴胶片条 / 素材库预览 / 导出封面。
// 策略：目标缩略图数 N → 均匀采样时间点 → seek + 解码 + 缩放。
// 注意：解码与缩放通过 MediaReader 完成（平台层可提供硬件加速实现）。

/// 单张缩略图。
struct Thumbnail {
    Time sourceTime;              // 对应源时间
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  // 交错 RGBA8
};

/// 缩略图生成配置。
struct ThumbnailConfig {
    int count = 8;           // 目标数量
    int maxWidth = 160;      // 输出最大宽度（保持宽高比）
    int maxHeight = 90;      // 输出最大高度
    /// 缩略图时间点是否避开首尾（true 时从 duration/N/2 开始等距采样）。
    bool centered = true;
};

/// 为单个素材生成缩略图列表。
/// - reader：已配置的解码器（函数内打开/关闭，调用后处于关闭状态）；
/// - assetPath：素材路径；
/// - duration：素材时长（由调用方提供，避免重复探测）。
///
/// 返回：缩略图列表（可能少于 count，若解码失败则跳过该点）。
std::vector<Thumbnail> generateThumbnails(MediaReader& reader, const std::string& assetPath,
                                          const Time& duration, const ThumbnailConfig& config = {});

/// 最近邻降采样 RGBA8（用于缩略图缩放；保持宽高比）。
/// 输出尺寸按 scale 比例（0..1）。若无缩放必要返回 false（调用方保留原图）。
bool downscaleRgba(const std::uint8_t* src, int srcWidth, int srcHeight, int* dstWidth,
                   int* dstHeight, std::vector<std::uint8_t>& dst);

}  // namespace core
}  // namespace nairoclip
