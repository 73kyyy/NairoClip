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
    /// 是否为 HDR 素材（PQ / HLG / BT.2020）。
    ///
    /// 由平台层在导入/探测时写入。当前渲染链只做 SDR 的 BT.601/709 转换，
    /// HDR 素材会发灰 —— 这个标记用于 UI 提示用户原因，
    /// 而不是静默偏色让人以为「应用把画质弄坏了」。
    bool isHdr = false;

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
    /// 内容摘要（SHA-256 十六进制小写；空 = 未记录，不做校验）。
    /// 用途：文档第 3 部分「四、媒体离线检测」——项目打开时校验「文件不存在
    /// 或摘要不符」（原文为 MD5；本项目统一用标准 SHA-256，见 crypto 模块）。
    /// 由平台层在导入/重新链接后用 nc_asset_set_digest 写入。
    std::string contentSha256;

    bool isImage() const { return type == MediaType::Image; }
};

}  // namespace core
}  // namespace nairoclip