#include "nairoclip/core/media_offline.h"

#include "nairoclip/core/commands.h"
#include "nairoclip/core/crypto.h"

#include <algorithm>
#include <cctype>

namespace nairoclip {
namespace core {

namespace {

/// 摘要合法性：空串或 64 位十六进制（大小写不敏感）。
bool digestValid(const std::string& digest) {
    if (digest.empty()) {
        return true;
    }
    if (digest.size() != 64) {
        return false;
    }
    for (const char c : digest) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

std::string toLowerAscii(std::string s) {
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}

/// 提取路径中的文件名（'/' 与 '\\' 均为分隔符；Windows 路径兼容）。
std::string fileNameOf(const std::string& path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/// 拼接目录与文件名（保留目录末尾是否已有分隔符的语义）。
std::string joinPath(const std::string& dir, const std::string& fileName) {
    if (dir.empty()) {
        return fileName;
    }
    const char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + fileName;
    }
    return dir + "/" + fileName;
}

}  // namespace

// =============================================================================
// 状态
// =============================================================================

const char* offlineStatusName(OfflineStatus status) {
    switch (status) {
        case OfflineStatus::Online: return "online";
        case OfflineStatus::Missing: return "missing";
        case OfflineStatus::DigestMismatch: return "digest_mismatch";
        case OfflineStatus::Unverified: return "unverified";
    }
    return "unverified";
}

bool offlineStatusFromString(const char* text, OfflineStatus& out) {
    if (text == nullptr) {
        return false;
    }
    const std::string s = toLowerAscii(text);
    if (s == "online") { out = OfflineStatus::Online; return true; }
    if (s == "missing") { out = OfflineStatus::Missing; return true; }
    if (s == "digest_mismatch" || s == "mismatch") { out = OfflineStatus::DigestMismatch; return true; }
    if (s == "unverified") { out = OfflineStatus::Unverified; return true; }
    return false;
}

const char* offlineBadgeText() { return "素材离线"; }

// =============================================================================
// 扫描
// =============================================================================

std::size_t OfflineScanReport::offlineCount() const {
    std::size_t count = 0;
    for (const auto& check : checks) {
        if (check.isOffline()) {
            ++count;
        }
    }
    return count;
}

std::size_t OfflineScanReport::onlineCount() const {
    std::size_t count = 0;
    for (const auto& check : checks) {
        if (check.status == OfflineStatus::Online) {
            ++count;
        }
    }
    return count;
}

std::vector<const AssetOfflineCheck*> OfflineScanReport::offlineChecks() const {
    std::vector<const AssetOfflineCheck*> out;
    for (const auto& check : checks) {
        if (check.isOffline()) {
            out.push_back(&check);
        }
    }
    return out;
}

std::string OfflineScanReport::summary() const {
    const std::size_t count = offlineCount();
    if (count == 0) {
        return std::string();
    }
    return "检测到 " + std::to_string(count) + " 个素材离线";
}

OfflineScanReport scanOfflineAssets(const Project& project, OfflineProbe* probe) {
    OfflineScanReport report;
    if (probe == nullptr) {
        report.notes.push_back("未提供离线探测器，无法检测素材状态（不阻塞项目打开）");
        for (const auto& asset : project.assets) {
            AssetOfflineCheck check;
            check.asset = asset.id;
            check.name = asset.name;
            check.path = asset.path;
            check.status = OfflineStatus::Unverified;
            check.message = "未检测（无探测器）";
            report.checks.push_back(std::move(check));
        }
        return report;
    }
    for (const auto& asset : project.assets) {
        AssetOfflineCheck check;
        check.asset = asset.id;
        check.name = asset.name;
        check.path = asset.path;
        // 1) 存在性
        if (asset.path.empty() || !probe->fileExists(asset.path)) {
            check.status = OfflineStatus::Missing;
            check.message = "文件不存在";
            report.checks.push_back(std::move(check));
            continue;
        }
        // 2) 摘要（仅当素材记录了期望摘要）
        if (!asset.contentSha256.empty()) {
            check.digestChecked = true;
            const std::string actual = probe->fileDigest(asset.path);
            if (actual.empty() ||
                toLowerAscii(actual) != toLowerAscii(asset.contentSha256)) {
                check.status = OfflineStatus::DigestMismatch;
                check.message = "文件内容与记录不一致（校验失败）";
                report.checks.push_back(std::move(check));
                continue;
            }
        }
        check.status = OfflineStatus::Online;
        check.message = check.digestChecked ? "在线（校验通过）" : "在线";
        report.checks.push_back(std::move(check));
    }
    return report;
}

std::size_t applyOfflineFlags(Project& project, const OfflineScanReport& report) {
    std::size_t updated = 0;
    for (const auto& check : report.checks) {
        MediaAsset* asset = project.findAsset(check.asset);
        if (asset == nullptr) {
            continue;
        }
        const bool offline = check.isOffline();
        if (asset->offline != offline) {
            asset->offline = offline;
            ++updated;
        }
    }
    return updated;
}

// =============================================================================
// 自动扫描查找
// =============================================================================

std::vector<RelinkCandidate> findRelinkCandidates(const std::vector<MediaAsset>& assets,
                                                  const std::vector<std::string>& searchDirs,
                                                  OfflineProbe* probe) {
    std::vector<RelinkCandidate> out;
    if (probe == nullptr || searchDirs.empty()) {
        return out;
    }
    // 预取目录列表（每个目录只列一次）
    std::vector<std::vector<std::string>> dirListings;
    dirListings.reserve(searchDirs.size());
    for (const auto& dir : searchDirs) {
        dirListings.push_back(probe->listDirectory(dir));
    }
    for (const auto& asset : assets) {
        const std::string fileName = fileNameOf(asset.path);
        if (fileName.empty()) {
            continue;
        }
        const std::string lowerName = toLowerAscii(fileName);
        std::vector<RelinkCandidate> matches;
        for (std::size_t d = 0; d < searchDirs.size(); ++d) {
            for (const auto& entry : dirListings[d]) {
                if (toLowerAscii(entry) != lowerName) {
                    continue;
                }
                RelinkCandidate candidate;
                candidate.asset = asset.id;
                candidate.fileName = fileName;
                candidate.candidatePath = joinPath(searchDirs[d], entry);
                candidate.hasExpectedDigest = !asset.contentSha256.empty();
                if (candidate.hasExpectedDigest) {
                    const std::string actual = probe->fileDigest(candidate.candidatePath);
                    candidate.digestMatches = !actual.empty() &&
                                              toLowerAscii(actual) ==
                                                  toLowerAscii(asset.contentSha256);
                }
                matches.push_back(std::move(candidate));
            }
        }
        // 摘要一致者优先（稳定：再按路径字典序）
        std::sort(matches.begin(), matches.end(),
                  [](const RelinkCandidate& a, const RelinkCandidate& b) {
                      if (a.digestMatches != b.digestMatches) {
                          return a.digestMatches;
                      }
                      return a.candidatePath < b.candidatePath;
                  });
        for (auto& match : matches) {
            out.push_back(std::move(match));
        }
    }
    return out;
}

// =============================================================================
// 重新链接
// =============================================================================

namespace {

/// 单条重链共用逻辑（校验 + 命令执行），供单素材/批量复用。
/// 返回错误信息（空 = 成功）。
std::string relinkOne(TimelineEditor& editor, Project& project, AssetId assetId,
                      const std::string& newPath, OfflineProbe* probe,
                      const std::string& expectedDigest, std::string& outDigestWarning) {
    MediaAsset* asset = project.findAsset(assetId);
    if (asset == nullptr) {
        return "素材不存在";
    }
    if (newPath.empty()) {
        return "新路径不能为空";
    }
    // 路径占用检查（避免两个素材指向同一文件造成混乱）
    for (const auto& other : project.assets) {
        if (other.id != assetId && other.path == newPath) {
            return "该文件已被素材「" + other.name + "」引用";
        }
    }
    std::string digest;
    if (probe != nullptr) {
        if (!probe->fileExists(newPath)) {
            return "文件不存在：" + newPath;
        }
        if (!expectedDigest.empty()) {
            const std::string actual = probe->fileDigest(newPath);
            if (actual.empty() || toLowerAscii(actual) != toLowerAscii(expectedDigest)) {
                return "文件内容与记录不一致（校验失败），已拒绝重链";
            }
            digest = expectedDigest;
        }
    } else if (!expectedDigest.empty()) {
        // 无探测器时无法校验摘要：fail-closed
        return "未提供探测器，无法校验文件内容";
    }
    const Result r = editor.relinkAsset(assetId, newPath, digest);
    if (!r.isOk()) {
        return r.message;
    }
    (void)outDigestWarning;
    return std::string();
}

}  // namespace

RelinkResult relinkAsset(TimelineEditor& editor, Project& project, AssetId assetId,
                         const std::string& newPath, OfflineProbe* probe,
                         const std::string& expectedDigest) {
    RelinkResult out;
    std::string digestWarning;
    const std::string error =
        relinkOne(editor, project, assetId, newPath, probe, expectedDigest, digestWarning);
    if (!error.empty()) {
        out.result = Result::fail(ErrorCode::InvalidArgument, error);
        return out;
    }
    out.relinked = 1;
    out.result = Result::ok();
    return out;
}

RelinkResult relinkAssets(TimelineEditor& editor, Project& project,
                          const std::vector<std::pair<AssetId, std::string>>& mappings,
                          OfflineProbe* probe) {
    RelinkResult out;
    if (mappings.empty()) {
        out.result = Result::fail(ErrorCode::InvalidArgument, "重链列表为空");
        return out;
    }
    // 事务外先做全量校验（失败项不改变工程；成功项在事务内落盘）
    struct Pending {
        AssetId asset;
        std::string path;
        std::string digest;
    };
    std::vector<Pending> pending;
    std::vector<std::string> errors;
    for (const auto& entry : mappings) {
        MediaAsset* asset = project.findAsset(entry.first);
        if (asset == nullptr) {
            errors.push_back("素材不存在");
            continue;
        }
        if (entry.second.empty()) {
            errors.push_back("「" + asset->name + "」新路径为空");
            continue;
        }
        bool occupied = false;
        for (const auto& other : project.assets) {
            if (other.id != entry.first && other.path == entry.second) {
                errors.push_back("「" + asset->name + "」目标文件已被其他素材引用");
                occupied = true;
                break;
            }
        }
        if (occupied) {
            continue;
        }
        if (probe == nullptr) {
            errors.push_back("「" + asset->name + "」未提供探测器，无法校验文件");
            continue;
        }
        if (!probe->fileExists(entry.second)) {
            errors.push_back("「" + asset->name + "」文件不存在");
            continue;
        }
        Pending p;
        p.asset = entry.first;
        p.path = entry.second;
        if (!asset->contentSha256.empty()) {
            const std::string actual = probe->fileDigest(entry.second);
            if (actual.empty() ||
                toLowerAscii(actual) != toLowerAscii(asset->contentSha256)) {
                errors.push_back("「" + asset->name + "」文件内容与记录不一致，已跳过");
                continue;
            }
            p.digest = asset->contentSha256;
        }
        pending.push_back(std::move(p));
    }
    if (pending.empty()) {
        out.result = Result::fail(ErrorCode::InvalidArgument,
                                  errors.empty() ? "没有可重链的素材" : errors.front());
        for (auto& e : errors) {
            out.digestWarnings.push_back(e);
        }
        return out;
    }
    editor.beginTransaction("重新链接素材");
    for (const auto& p : pending) {
        const Result r = editor.relinkAsset(p.asset, p.path, p.digest);
        if (r.isOk()) {
            ++out.relinked;
        } else {
            errors.push_back(r.message);
        }
    }
    editor.endTransaction();
    for (auto& e : errors) {
        out.digestWarnings.push_back(e);
    }
    if (out.relinked == 0) {
        out.result = Result::fail(ErrorCode::Internal, "批量重链未成功任何一项");
        return out;
    }
    out.result = Result::ok();
    return out;
}

// =============================================================================
// 摘要写入
// =============================================================================

Result setAssetDigest(Project& project, AssetId assetId, const std::string& sha256Hex) {
    if (!digestValid(sha256Hex)) {
        return Result::fail(ErrorCode::InvalidArgument, "摘要必须为 64 位十六进制字符串");
    }
    MediaAsset* asset = project.findAsset(assetId);
    if (asset == nullptr) {
        return Result::fail(ErrorCode::NotFound, "素材不存在");
    }
    asset->contentSha256 = sha256Hex;
    return Result::ok();
}

}  // namespace core
}  // namespace nairoclip
