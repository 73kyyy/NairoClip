#pragma once

#include "nairoclip/core/media.h"
#include "nairoclip/core/time.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 代理剪辑（Proxy Editing）—— 文档第 3 部分「三、代理剪辑」
// =============================================================================
// 产品对应：
//   · 导入检测到高分辨率素材（4K）→ 提示生成代理（1/2 或 1/4 分辨率）；
//   · 轨道片段角标 [P] 代理模式 / [原] 原始画质；
//   · 时间轴顶部常驻「🎞️当前预览:[代理画质▾] 导出时自动使用原始画质」；
//   · 设置页：代理文件（自动/手动/从不）、存储位置、占用统计与清理。
//
// 本模块定位（零文件系统依赖，全部逻辑可测试）：
//   · 代理登记表（工程级，随 .nclip 序列化）：记录每个素材的代理文件与状态；
//   · 导入检查：判断是否需要建议/自动生成代理，并给出估算（耗时/体积）；
//   · 转码驱动：generateProxyFrames 通过 MediaReader/MediaWriter 接口逐帧
//     缩放并编码（真实像素处理；解码器/编码器由平台层注入）；
//   · 预览源选择：预览用代理、导出用原片（selectPreviewSource 只服务预览路径）。
//
// 说明：
//   · 代理文件为「纯视频」（音频解码开销低，预览音频仍读原素材）；
//   · 代理文件本体的创建/删除由平台层完成（core 不触碰文件系统）；
//   · 「导出时自动使用原始画质」由导出管线保证：ExportPlan 编译自 asset->path
//     （原片），与代理登记表无关（见 test_proxy 的对应用例）。

class MediaReader;
class MediaWriter;

/// 代理质量（缩放比例）。
enum class ProxyQuality {
    Half = 0,     ///< 1/2 分辨率（默认，文档「代理质量: 1/2分辨率」）
    Quarter = 1,  ///< 1/4 分辨率（低配设备/超长素材）
};

/// 代理生成策略（文档设置页「代理文件: 自动生成(推荐)/手动/从不」）。
enum class ProxyMode {
    Auto = 0,    ///< 自动生成（高分辨率素材导入后直接生成）
    Manual = 1,  ///< 手动（导入时提示，用户确认后生成）
    Never = 2,   ///< 从不生成
};

/// 代理文件状态（供角标 [P] 与设置页展示）。
enum class ProxyStatus {
    None = 0,        ///< 未生成
    Generating = 1,  ///< 生成中（运行时状态；不持久化）
    Ready = 2,       ///< 可用（预览将使用代理）
    Failed = 3,      ///< 生成失败（note 记录原因）
};

/// 单个素材的代理记录。
struct ProxyRecord {
    AssetId asset;
    /// 代理文件路径（平台层生成后写入）。
    std::string path;
    /// 代理实际分辨率（生成完成后由平台层探测写入）。
    int width = 0;
    int height = 0;
    ProxyQuality quality = ProxyQuality::Half;
    ProxyStatus status = ProxyStatus::None;
    /// 代理文件大小（字节；平台层探测；0 = 未知/未生成）。
    std::int64_t sizeBytes = 0;
    /// 备注（失败原因等，可直接展示）。
    std::string note;

    /// 是否可用于预览（就绪 + 路径非空）。
    bool usable() const {
        return status == ProxyStatus::Ready && !path.empty();
    }
};

/// 代理登记表（工程级；随工程序列化，路径失效时由平台层重置状态）。
struct ProxyRegistry {
    std::vector<ProxyRecord> records;

    const ProxyRecord* find(AssetId assetId) const;
    ProxyRecord* find(AssetId assetId);
    /// 写入/覆盖记录（同素材 ID 替换）。
    void upsert(ProxyRecord record);
    /// 移除记录；返回是否移除成功（供平台层据此删除对应文件）。
    bool remove(AssetId assetId);
    void clear();
    /// 已就绪代理的占用总量（字节）。
    std::int64_t totalSizeBytes() const;
    /// 已就绪代理数量。
    std::size_t readyCount() const;
    /// 生成中数量。
    std::size_t generatingCount() const;
};

/// 代理设置（文档设置页；平台层持久化，core 只消费）。
struct ProxySettings {
    ProxyMode mode = ProxyMode::Auto;
    ProxyQuality quality = ProxyQuality::Half;
    /// 高分辨率判定阈值（像素总数；默认 2560×1440 ≈ 3.7MP，覆盖 4K 素材）。
    std::int64_t highResPixelThreshold = 2560LL * 1440LL;

    bool valid() const { return highResPixelThreshold > 0; }
};

/// 代理目标尺寸（按质量比例；只缩不放，最小 1）。
void proxyTargetSize(int srcWidth, int srcHeight, ProxyQuality quality, int& outWidth,
                     int& outHeight);

/// 是否为高分辨率素材（视频类型 + 像素总数 ≥ 阈值）。
bool isHighResolutionAsset(const MediaAsset& asset, const ProxySettings& settings = {});

/// 导入检查结果（文档「导入检测到高分辨率素材(4K)：是否生成代理文件以提升流畅度？」）。
struct ProxyImportCheck {
    /// 是否建议生成（手动策略下弹窗、自动策略下直接生成）。
    bool suggestProxy = false;
    /// 自动策略下是否直接生成（无需用户确认）。
    bool autoGenerate = false;
    int proxyWidth = 0;
    int proxyHeight = 0;
    /// 预计生成耗时（秒；设备相关的粗略估算，UI 可覆盖）。
    double estimatedSeconds = 0.0;
    /// 预计文件大小（字节；估算）。
    std::int64_t estimatedBytes = 0;
    /// 说明（可直接展示，如「4K 素材，代理可提升剪辑流畅度」）。
    std::string reason;

    bool valid() const { return !reason.empty(); }
};

/// 导入检查：由素材信息 + 设置推断是否需要代理（不触碰文件系统）。
/// registry 非空时，已有可用代理直接返回「无需生成」。
ProxyImportCheck checkProxyOnImport(const MediaAsset& asset, const ProxySettings& settings = {},
                                    const ProxyRegistry* registry = nullptr);

/// 预览源选择结果（文档「当前预览:[代理画质▾] 导出时自动使用原始画质」）。
struct SourceSelection {
    /// 实际使用的文件路径。
    std::string path;
    /// 是否使用代理。
    bool usingProxy = false;
    int width = 0;
    int height = 0;

    bool valid() const { return !path.empty(); }
};

/// 选择「预览」应使用的源：代理可用时返回代理，否则原素材。
/// ★ 仅用于预览路径；导出路径必须使用原片（compileExportPlan 已按此实现）。
SourceSelection selectPreviewSource(const MediaAsset& asset, const ProxyRegistry& registry);

/// 代理转码结果。
struct ProxyGenerateResult {
    Result result;
    std::int64_t framesWritten = 0;
    int proxyWidth = 0;
    int proxyHeight = 0;
    bool cancelled = false;
};

/// 逐帧驱动代理转码（真实像素：读取 → 双线性缩放 → 写编码器）。
/// - source：已打开的源素材读取器（视频）；
/// - writer：已按代理尺寸打开的写入器（includeAudio 建议 false，纯视频代理）；
/// - targetWidth/targetHeight：代理目标尺寸（由 proxyTargetSize 得到）；
/// - maxFrames：最大写入帧数（0 = 不限制；取消/测试保护用）；
/// - progress：进度回调（0..1，基于源时间戳；返回 false 取消）。
/// 失败/取消时不负责收尾（调用方按需 writer.abort()），成功返回 Ok。
ProxyGenerateResult generateProxyFrames(MediaReader& source, MediaWriter& writer,
                                        int targetWidth, int targetHeight,
                                        std::int64_t maxFrames = 0,
                                        const std::function<bool(double)>& progress = nullptr);

/// 质量名称（"half" / "quarter"；序列化与 UI 用）。
const char* proxyQualityName(ProxyQuality quality);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool proxyQualityFromString(const char* text, ProxyQuality& out);

/// 状态名称（"none" / "generating" / "ready" / "failed"）。
const char* proxyStatusName(ProxyStatus status);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool proxyStatusFromString(const char* text, ProxyStatus& out);

}  // namespace core
}  // namespace nairoclip
