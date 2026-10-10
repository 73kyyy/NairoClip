#pragma once
// =============================================================================
// 媒体离线检测与重新链接（Offline Detection & Relink）
//   —— 文档第 3 部分「四、媒体离线检测与重新链接」
// =============================================================================
// 产品对应（原文）：
//   「⚠️ 检测到3个素材离线
//      · 片段A.mp4   [重新链接]
//      · 音乐B.mp3   [重新链接]
//      · 贴纸C.png   [重新链接]
//     [自动扫描本地查找] [全部手动选择]」
//
//   「触发条件：项目打开时扫描素材路径，文件不存在或MD5校验失败。处理：
//    显示斜纹灰色占位块+⚠️图标，不阻塞项目打开，允许继续编辑其他部分。」
//
// 本模块定位（零文件系统依赖，全部逻辑可测试）：
//   · 探测通过 OfflineProbe 抽象注入（平台层用系统 API/FFmpeg 实现）；
//   · 扫描：逐素材检查「路径存在性 + 摘要校验」（摘要用标准 SHA-256，
//     与 crypto 模块统一；原文 MD5 为表述，SHA-256 更强且零额外依赖）；
//   · 标记：扫描结果写回 asset.offline（UI 据此显示斜纹占位块+⚠️，不阻塞打开）；
//   · 重新链接：单素材（手动选择）/ 批量（全部手动选择）；进入撤销栈；
//   · 自动扫描：按原文件名在候选目录中查找（[自动扫描本地查找]）。
//
// 职责边界：
//   · core 不触碰文件系统（探测由平台层注入）；
//   · 重新链接后素材元数据（分辨率/时长）的重新探测由平台层用
//     MediaReaderFactory 完成，core 只负责路径/摘要/标记的工程状态。

#include "nairoclip/core/editor.h"
#include "nairoclip/core/media.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 素材在线状态。
enum class OfflineStatus {
    Online = 0,          ///< 文件存在（有期望摘要时校验通过）
    Missing = 1,         ///< 文件不存在（文档「文件不存在」）
    DigestMismatch = 2,  ///< 文件存在但摘要不符（文档「MD5校验失败」；fail-closed）
    Unverified = 3,      ///< 未提供探测器，无法检测
};

/// 状态名称（"online"/"missing"/"digest_mismatch"/"unverified"）。
const char* offlineStatusName(OfflineStatus status);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool offlineStatusFromString(const char* text, OfflineStatus& out);

/// 离线探测器：由平台层实现（core 不触碰文件系统）。
/// 线程约束：单实例不要求线程安全；调用方串行使用。
class OfflineProbe {
public:
    virtual ~OfflineProbe() = default;
    /// 文件是否存在且可读。
    virtual bool fileExists(const std::string& path) = 0;
    /// 计算文件内容摘要（SHA-256 十六进制小写）。
    /// 返回空串表示无法计算（不存在/不可读）；此时若存在期望摘要按不符处理。
    virtual std::string fileDigest(const std::string& path) = 0;
    /// 列出目录下的文件名（仅文件名，不含路径；顺序不要求）。
    /// 用于「自动扫描本地查找」；目录不存在时返回空列表。
    virtual std::vector<std::string> listDirectory(const std::string& dir) = 0;
};

/// 单个素材的检测结果。
struct AssetOfflineCheck {
    AssetId asset;
    std::string name;   ///< 展示名
    std::string path;   ///< 当前记录的路径
    OfflineStatus status = OfflineStatus::Unverified;
    /// 是否有期望摘要（无 = 只做了存在性检查）。
    bool digestChecked = false;
    /// 结论文案（可直接展示，如「文件不存在」）。
    std::string message;

    /// 是否离线（需要 UI 显示占位块+⚠️）。
    bool isOffline() const {
        return status == OfflineStatus::Missing || status == OfflineStatus::DigestMismatch;
    }
};

/// 扫描报告（文档「检测到 N 个素材离线」）。
struct OfflineScanReport {
    std::vector<AssetOfflineCheck> checks;
    /// 备注（如「未提供探测器」）。
    std::vector<std::string> notes;

    /// 离线素材数（Missing + DigestMismatch）。
    std::size_t offlineCount() const;
    /// 在线素材数。
    std::size_t onlineCount() const;
    /// 取离线项（供弹窗列表）。
    std::vector<const AssetOfflineCheck*> offlineChecks() const;
    /// 弹窗标题文案（如「检测到 3 个素材离线」；无离线返回空串）。
    std::string summary() const;
};

/// 扫描工程全部素材的在线状态（只读，不修改工程）。
/// - probe 为 nullptr 时全部返回 Unverified（note 说明），便于平台层未接入时降级；
/// - 校验顺序：存在性 → 摘要（仅当素材记录了 contentSha256）。
OfflineScanReport scanOfflineAssets(const Project& project, OfflineProbe* probe);

/// 把扫描结果写回工程（设置 asset.offline；在线时清除标记）。
/// - 返回被更新（标记发生变化）的素材数；
/// - 属于状态同步而非用户编辑：不进入撤销栈（与代理登记表同一约定）。
std::size_t applyOfflineFlags(Project& project, const OfflineScanReport& report);

// ---------------------------------------------------------------------------
// 重新链接
// ---------------------------------------------------------------------------

/// 重链候选（自动扫描查找结果）。
struct RelinkCandidate {
    AssetId asset;
    std::string fileName;        ///< 原文件名（匹配依据）
    std::string candidatePath;   ///< 找到的候选路径（目录 + 文件名）
    bool digestMatches = false;  ///< 候选文件摘要与期望一致（有期望摘要时才有意义）
    bool hasExpectedDigest = false;
};

/// 自动扫描：在候选目录中按原文件名查找离线素材的新位置。
/// - assets：待查找的素材（通常来自报告的离线项）；
/// - searchDirs：候选目录列表（平台层提供，如上次位置的同级目录/用户选择的文件夹）；
/// - 匹配规则：文件名大小写不敏感完全匹配；摘要一致时优先（排序在前）；
/// - probe 为 nullptr 返回空列表。
std::vector<RelinkCandidate> findRelinkCandidates(const std::vector<MediaAsset>& assets,
                                                  const std::vector<std::string>& searchDirs,
                                                  OfflineProbe* probe);

/// 重新链接结果。
struct RelinkResult {
    Result result;
    /// 成功重链的素材数（批量时为计数）。
    std::size_t relinked = 0;
    /// 摘要与被替换前提不一致的素材（已重链但提示风险；fail-closed 由平台层决定是否采纳）。
    std::vector<std::string> digestWarnings;
};

/// 重新链接单个素材（手动选择路径）。
/// - 前置条件：素材存在、newPath 非空、newPath 未被其他素材占用；
/// - 语义：写入新路径、清除离线标记；可选校验摘要（expectedDigest 非空时比对，
///   不符则拒绝并返回错误——避免用户选错文件；传空则跳过摘要校验）；
/// - 进入撤销栈（事务名「重新链接素材」）。
RelinkResult relinkAsset(TimelineEditor& editor, Project& project, AssetId assetId,
                         const std::string& newPath, OfflineProbe* probe,
                         const std::string& expectedDigest = "");

/// 批量重新链接（文档「全部手动选择」/自动扫描结果一键应用）。
/// - mappings：(素材, 新路径) 列表；
/// - 单条失败不阻断其余（逐项回报；整体包在一个撤销事务中，可一次撤销）。
RelinkResult relinkAssets(TimelineEditor& editor, Project& project,
                          const std::vector<std::pair<AssetId, std::string>>& mappings,
                          OfflineProbe* probe);

/// 设置素材内容摘要（平台层在导入/重链后计算写入；直接修改，不进撤销栈）。
/// - 校验：素材存在、digest 为 64 位十六进制（大小写不敏感）或空（清除）；
/// - 空串 = 清除摘要（后续扫描只做存在性检查）。
Result setAssetDigest(Project& project, AssetId assetId, const std::string& sha256Hex);

/// 离线标记的展示文案（供 UI 占位块角标；如「素材离线」）。
const char* offlineBadgeText();

}  // namespace core
}  // namespace nairoclip
