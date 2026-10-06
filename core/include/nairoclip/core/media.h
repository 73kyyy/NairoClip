#pragma once

#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <string>

namespace nairoclip {
namespace core {

enum class MediaType {
    Video,
    Audio,
    Image,
    Unknown,
};

/// 媒体素材（素材池中的一项）。
struct MediaAsset {
    AssetId id;
    std::string path;      // 本地文件路径
    std::string name;      // 显示名称
    MediaType type = MediaType::Unknown;
    Time duration;         // 媒体时长；图片为 0（可无限拉长）
    FrameRate frameRate;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
    int channels = 0;
    bool offline = false;  // 素材离线（文件缺失）标记

    bool isImage() const { return type == MediaType::Image; }
};

}  // namespace core
}  // namespace nairoclip