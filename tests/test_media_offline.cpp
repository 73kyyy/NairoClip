// =============================================================================
// 媒体离线检测与重新链接测试（M2.24，文档第 3 部分「四、媒体离线检测与重新链接」）
// =============================================================================
// 覆盖：
//   · 扫描：存在性 / 摘要校验（不符 → fail-closed）/ 无探测器降级；
//   · 标记写回：offline 状态同步、在线清除；
//   · 自动扫描：文件名匹配、摘要优先排序、目录不存在容忍；
//   · 重链：单素材（摘要校验、路径占用拒绝）/ 批量（部分失败不阻断、事务）；
//   · 撤销/重做：重链完整回退（含摘要与离线标记）；
//   · 摘要写入与序列化往返。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/crypto.h"
#include "nairoclip/core/editor.h"
#include "nairoclip/core/media_offline.h"
#include "nairoclip/core/project.h"
#include "nairoclip/core/serialization.h"

#include <map>
#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

/// 内存探测器：完全在内存中模拟文件系统（零文件系统依赖，可测试）。
class MemoryProbe : public OfflineProbe {
public:
    /// 虚拟文件：路径 → 内容。
    std::map<std::string, std::string> files;
    /// 虚拟目录：目录 → 文件名列表。
    std::map<std::string, std::vector<std::string>> directories;
    /// 模拟摘要计算失败（fileDigest 返回空）。
    bool failDigest = false;

    bool fileExists(const std::string& path) override {
        return files.find(path) != files.end();
    }
    std::string fileDigest(const std::string& path) override {
        if (failDigest) {
            return std::string();
        }
        const auto it = files.find(path);
        if (it == files.end()) {
            return std::string();
        }
        return sha256Hex(it->second);
    }
    std::vector<std::string> listDirectory(const std::string& dir) override {
        const auto it = directories.find(dir);
        return it == directories.end() ? std::vector<std::string>() : it->second;
    }
};

/// 建工程：一个素材（路径 /old/a.mp4，摘要 = 内容摘要）。
struct OfflineFixture {
    Project project;
    TimelineEditor* editor = nullptr;
    AssetId assetId;

    explicit OfflineFixture(const std::string& path = "/old/a.mp4",
                            const std::string& content = "hello",
                            bool withDigest = true) {
        editor = new TimelineEditor(project);
        MediaAsset asset;
        asset.id = AssetId{project.ids.next()};
        asset.name = "素材A";
        asset.path = path;
        asset.type = MediaType::Video;
        asset.duration = Rational::make(10);
        if (withDigest) {
            asset.contentSha256 = sha256Hex(content);
        }
        assetId = editor->importAsset(std::move(asset));
    }
    ~OfflineFixture() { delete editor; }

    const MediaAsset* asset() const { return project.findAsset(assetId); }
};

}  // namespace

// ---------------------------------------------------------------------------
// 状态名称
// ---------------------------------------------------------------------------

NC_TEST(MediaOffline, StatusNameRoundTrip) {
    NC_CHECK_EQ(std::string(offlineStatusName(OfflineStatus::Online)), std::string("online"));
    NC_CHECK_EQ(std::string(offlineStatusName(OfflineStatus::Missing)), std::string("missing"));
    NC_CHECK_EQ(std::string(offlineStatusName(OfflineStatus::DigestMismatch)),
                std::string("digest_mismatch"));
    NC_CHECK_EQ(std::string(offlineStatusName(OfflineStatus::Unverified)),
                std::string("unverified"));
    for (const auto* name : {"online", "missing", "digest_mismatch", "unverified"}) {
        OfflineStatus status;
        NC_CHECK(offlineStatusFromString(name, status));
    }
    OfflineStatus status;
    NC_CHECK(!offlineStatusFromString("bogus", status));
    NC_CHECK(!offlineStatusFromString(nullptr, status));
}

// ---------------------------------------------------------------------------
// 扫描
// ---------------------------------------------------------------------------

NC_TEST(MediaOffline, ScanNoProbeReturnsUnverified) {
    OfflineFixture fx;
    const OfflineScanReport report = scanOfflineAssets(fx.project, nullptr);
    NC_CHECK_EQ(report.checks.size(), std::size_t(1));
    NC_CHECK_EQ(report.checks[0].status, OfflineStatus::Unverified);
    NC_CHECK_EQ(report.offlineCount(), std::size_t(0));  // 未验证不视为离线
    NC_CHECK(!report.notes.empty());
}

NC_TEST(MediaOffline, ScanMissingFile) {
    OfflineFixture fx;
    MemoryProbe probe;  // 空文件系统 → 文件不存在
    const OfflineScanReport report = scanOfflineAssets(fx.project, &probe);
    NC_CHECK_EQ(report.checks.size(), std::size_t(1));
    NC_CHECK_EQ(report.checks[0].status, OfflineStatus::Missing);
    NC_CHECK(report.checks[0].isOffline());
    NC_CHECK_EQ(report.offlineCount(), std::size_t(1));
    NC_CHECK_EQ(report.summary(), std::string("检测到 1 个素材离线"));
}

NC_TEST(MediaOffline, ScanOnlineWithDigestMatch) {
    OfflineFixture fx;
    MemoryProbe probe;
    probe.files["/old/a.mp4"] = "hello";
    const OfflineScanReport report = scanOfflineAssets(fx.project, &probe);
    NC_CHECK_EQ(report.checks[0].status, OfflineStatus::Online);
    NC_CHECK(report.checks[0].digestChecked);
    NC_CHECK_EQ(report.onlineCount(), std::size_t(1));
    NC_CHECK(report.summary().empty());
}

NC_TEST(MediaOffline, ScanDigestMismatchFailsClosed) {
    OfflineFixture fx;  // 期望摘要 = sha256("hello")
    MemoryProbe probe;
    probe.files["/old/a.mp4"] = "tampered";  // 内容被改
    const OfflineScanReport report = scanOfflineAssets(fx.project, &probe);
    NC_CHECK_EQ(report.checks[0].status, OfflineStatus::DigestMismatch);
    NC_CHECK(report.checks[0].isOffline());
    NC_CHECK(report.checks[0].message.find("不一致") != std::string::npos);
}

NC_TEST(MediaOffline, ScanDigestUnreadableIsMismatch) {
    OfflineFixture fx;
    MemoryProbe probe;
    probe.files["/old/a.mp4"] = "hello";
    probe.failDigest = true;  // 摘要无法计算 → fail-closed
    const OfflineScanReport report = scanOfflineAssets(fx.project, &probe);
    NC_CHECK_EQ(report.checks[0].status, OfflineStatus::DigestMismatch);
}

NC_TEST(MediaOffline, ScanWithoutDigestSkipsDigestCheck) {
    OfflineFixture fx("/old/a.mp4", "hello", /*withDigest=*/false);
    MemoryProbe probe;
    probe.files["/old/a.mp4"] = "完全不同的内容";  // 无期望摘要 → 不算问题
    const OfflineScanReport report = scanOfflineAssets(fx.project, &probe);
    NC_CHECK_EQ(report.checks[0].status, OfflineStatus::Online);
    NC_CHECK(!report.checks[0].digestChecked);
}

NC_TEST(MediaOffline, ScanEmptyPathIsMissing) {
    OfflineFixture fx("", "hello");
    MemoryProbe probe;
    const OfflineScanReport report = scanOfflineAssets(fx.project, &probe);
    NC_CHECK_EQ(report.checks[0].status, OfflineStatus::Missing);
}

NC_TEST(MediaOffline, ScanMultipleAssetsMixed) {
    Project project;
    TimelineEditor editor(project);
    MediaAsset a;
    a.id = AssetId{project.ids.next()};
    a.name = "A";
    a.path = "/ok/a.mp4";
    a.type = MediaType::Video;
    editor.importAsset(std::move(a));
    MediaAsset b;
    b.id = AssetId{project.ids.next()};
    b.name = "B";
    b.path = "/gone/b.mp4";
    b.type = MediaType::Video;
    editor.importAsset(std::move(b));
    MemoryProbe probe;
    probe.files["/ok/a.mp4"] = "x";
    const OfflineScanReport report = scanOfflineAssets(project, &probe);
    NC_CHECK_EQ(report.checks.size(), std::size_t(2));
    NC_CHECK_EQ(report.offlineCount(), std::size_t(1));
    const auto offline = report.offlineChecks();
    NC_CHECK_EQ(offline.size(), std::size_t(1));
    NC_CHECK_EQ(offline[0]->name, std::string("B"));
}

// ---------------------------------------------------------------------------
// 标记写回
// ---------------------------------------------------------------------------

NC_TEST(MediaOffline, ApplyFlagsSyncsOfflineState) {
    OfflineFixture fx;
    MemoryProbe probe;  // 文件缺失
    const std::size_t updated = applyOfflineFlags(fx.project, scanOfflineAssets(fx.project, &probe));
    NC_CHECK_EQ(updated, std::size_t(1));
    NC_CHECK(fx.asset()->offline);
    // 再次应用（无变化）→ 0
    NC_CHECK_EQ(applyOfflineFlags(fx.project, scanOfflineAssets(fx.project, &probe)),
                std::size_t(0));
    // 文件到位后清除标记
    probe.files["/old/a.mp4"] = "hello";
    NC_CHECK_EQ(applyOfflineFlags(fx.project, scanOfflineAssets(fx.project, &probe)),
                std::size_t(1));
    NC_CHECK(!fx.asset()->offline);
}

// ---------------------------------------------------------------------------
// 自动扫描查找
// ---------------------------------------------------------------------------

NC_TEST(MediaOffline, FindCandidatesByFileName) {
    OfflineFixture fx;
    MemoryProbe probe;
    probe.files["/search/a.mp4"] = "hello";
    probe.directories["/search"] = {"a.mp4", "other.mp4"};
    const std::vector<MediaAsset>& assets = fx.project.assets;
    const auto candidates = findRelinkCandidates(assets, {"/search"}, &probe);
    NC_CHECK_EQ(candidates.size(), std::size_t(1));
    NC_CHECK_EQ(candidates[0].candidatePath, std::string("/search/a.mp4"));
    NC_CHECK(candidates[0].digestMatches);  // 摘要一致
    NC_CHECK(candidates[0].hasExpectedDigest);
}

NC_TEST(MediaOffline, FindCandidatesDigestMismatchSortedLast) {
    OfflineFixture fx;  // 期望摘要 = sha256("hello")
    MemoryProbe probe;
    probe.files["/d1/a.mp4"] = "wrong";     // 摘要不符
    probe.files["/d2/a.mp4"] = "hello";     // 摘要一致
    probe.directories["/d1"] = {"a.mp4"};
    probe.directories["/d2"] = {"a.mp4"};
    const auto candidates = findRelinkCandidates(fx.project.assets, {"/d1", "/d2"}, &probe);
    NC_CHECK_EQ(candidates.size(), std::size_t(2));
    // 摘要一致者优先
    NC_CHECK(candidates[0].digestMatches);
    NC_CHECK_EQ(candidates[0].candidatePath, std::string("/d2/a.mp4"));
    NC_CHECK(!candidates[1].digestMatches);
}

NC_TEST(MediaOffline, FindCandidatesCaseInsensitiveFileName) {
    OfflineFixture fx;
    MemoryProbe probe;
    probe.files["/s/A.MP4"] = "hello";
    probe.directories["/s"] = {"A.MP4"};
    const auto candidates = findRelinkCandidates(fx.project.assets, {"/s"}, &probe);
    NC_CHECK_EQ(candidates.size(), std::size_t(1));  // a.mp4 大小写不敏感匹配
}

NC_TEST(MediaOffline, FindCandidatesNoProbeOrNoDirs) {
    OfflineFixture fx;
    MemoryProbe probe;
    NC_CHECK(findRelinkCandidates(fx.project.assets, {"/s"}, nullptr).empty());
    NC_CHECK(findRelinkCandidates(fx.project.assets, {}, &probe).empty());
    // 目录不存在（listDirectory 返回空）→ 无候选
    NC_CHECK(findRelinkCandidates(fx.project.assets, {"/nonexistent"}, &probe).empty());
}

// ---------------------------------------------------------------------------
// 重新链接
// ---------------------------------------------------------------------------

NC_TEST(MediaOffline, RelinkSingleWithDigestVerify) {
    OfflineFixture fx;
    MemoryProbe probe;
    probe.files["/new/a.mp4"] = "hello";
    // 摘要一致 → 成功
    const RelinkResult r = relinkAsset(*fx.editor, fx.project, fx.assetId, "/new/a.mp4", &probe,
                                       fx.asset()->contentSha256);
    NC_CHECK(r.result.isOk());
    NC_CHECK_EQ(r.relinked, std::size_t(1));
    NC_CHECK_EQ(fx.asset()->path, std::string("/new/a.mp4"));
    NC_CHECK(!fx.asset()->offline);
}

NC_TEST(MediaOffline, RelinkRejectsDigestMismatch) {
    OfflineFixture fx;
    fx.project.findAsset(fx.assetId)->offline = true;
    MemoryProbe probe;
    probe.files["/new/a.mp4"] = "恶意内容";  // 与期望不符
    const RelinkResult r = relinkAsset(*fx.editor, fx.project, fx.assetId, "/new/a.mp4", &probe,
                                       fx.asset()->contentSha256);
    NC_CHECK(!r.result.isOk());
    NC_CHECK_EQ(r.relinked, std::size_t(0));
    NC_CHECK_EQ(fx.asset()->path, std::string("/old/a.mp4"));  // 未改变
    NC_CHECK(fx.asset()->offline);                             // 标记保留
}

NC_TEST(MediaOffline, RelinkRejectsMissingFile) {
    OfflineFixture fx;
    MemoryProbe probe;  // 新路径不存在
    const RelinkResult r = relinkAsset(*fx.editor, fx.project, fx.assetId, "/new/a.mp4", &probe);
    NC_CHECK(!r.result.isOk());
    NC_CHECK(r.result.message.find("不存在") != std::string::npos);
}

NC_TEST(MediaOffline, RelinkRejectsOccupiedPath) {
    OfflineFixture fx;
    // 第二个素材占用目标路径
    MediaAsset other;
    other.id = AssetId{fx.project.ids.next()};
    other.name = "其他素材";
    other.path = "/new/a.mp4";
    other.type = MediaType::Video;
    fx.editor->importAsset(std::move(other));
    MemoryProbe probe;
    probe.files["/new/a.mp4"] = "hello";
    const RelinkResult r = relinkAsset(*fx.editor, fx.project, fx.assetId, "/new/a.mp4", &probe);
    NC_CHECK(!r.result.isOk());
    NC_CHECK(r.result.message.find("已被") != std::string::npos);
}

NC_TEST(MediaOffline, RelinkWithoutProbeFailsClosedWhenDigestExpected) {
    OfflineFixture fx;
    // 要求摘要校验但无探测器 → fail-closed
    const RelinkResult r = relinkAsset(*fx.editor, fx.project, fx.assetId, "/new/a.mp4", nullptr,
                                       fx.asset()->contentSha256);
    NC_CHECK(!r.result.isOk());
}

NC_TEST(MediaOffline, RelinkUndoRedoRoundTrip) {
    OfflineFixture fx;
    fx.project.findAsset(fx.assetId)->offline = true;
    MemoryProbe probe;
    probe.files["/new/a.mp4"] = "hello";
    const std::string oldDigest = fx.asset()->contentSha256;
    NC_CHECK(relinkAsset(*fx.editor, fx.project, fx.assetId, "/new/a.mp4", &probe).result.isOk());
    NC_CHECK_EQ(fx.asset()->path, std::string("/new/a.mp4"));
    // 撤销 → 完整还原（路径 + 离线标记 + 摘要）
    NC_CHECK(fx.editor->undo().isOk());
    NC_CHECK_EQ(fx.asset()->path, std::string("/old/a.mp4"));
    NC_CHECK(fx.asset()->offline);
    NC_CHECK_EQ(fx.asset()->contentSha256, oldDigest);
    // 重做 → 再次生效
    NC_CHECK(fx.editor->redo().isOk());
    NC_CHECK_EQ(fx.asset()->path, std::string("/new/a.mp4"));
    NC_CHECK(!fx.asset()->offline);
    // 历史面板显示事务名
    const auto names = fx.editor->undoHistoryNames();
    NC_CHECK(!names.empty());
    NC_CHECK_EQ(names.back(), std::string("重新链接素材"));
}

NC_TEST(MediaOffline, RelinkMissingAssetFails) {
    OfflineFixture fx;
    MemoryProbe probe;
    const RelinkResult r = relinkAsset(*fx.editor, fx.project, AssetId{9999}, "/x", &probe);
    NC_CHECK(!r.result.isOk());
}

// ---------------------------------------------------------------------------
// 批量重链
// ---------------------------------------------------------------------------

namespace {

/// 双素材夹具（用于批量测试）。
struct BatchFixture {
    Project project;
    TimelineEditor* editor = nullptr;
    AssetId a, b;

    BatchFixture() {
        editor = new TimelineEditor(project);
        MediaAsset first;
        first.id = AssetId{project.ids.next()};
        first.name = "A";
        first.path = "/old/a.mp4";
        first.type = MediaType::Video;
        first.contentSha256 = sha256Hex("contentA");
        a = editor->importAsset(std::move(first));
        MediaAsset second;
        second.id = AssetId{project.ids.next()};
        second.name = "B";
        second.path = "/old/b.mp4";
        second.type = MediaType::Video;
        second.contentSha256 = sha256Hex("contentB");
        b = editor->importAsset(std::move(second));
    }
    ~BatchFixture() { delete editor; }
};

}  // namespace

NC_TEST(MediaOffline, RelinkBatchAllSucceed) {
    BatchFixture fx;
    MemoryProbe probe;
    probe.files["/new/a.mp4"] = "contentA";
    probe.files["/new/b.mp4"] = "contentB";
    const std::vector<std::pair<AssetId, std::string>> mappings = {
        {fx.a, "/new/a.mp4"}, {fx.b, "/new/b.mp4"}};
    const RelinkResult r = relinkAssets(*fx.editor, fx.project, mappings, &probe);
    NC_CHECK(r.result.isOk());
    NC_CHECK_EQ(r.relinked, std::size_t(2));
    NC_CHECK_EQ(fx.project.findAsset(fx.a)->path, std::string("/new/a.mp4"));
    NC_CHECK_EQ(fx.project.findAsset(fx.b)->path, std::string("/new/b.mp4"));
    // 一次撤销整体回退
    NC_CHECK(fx.editor->undo().isOk());
    NC_CHECK_EQ(fx.project.findAsset(fx.a)->path, std::string("/old/a.mp4"));
    NC_CHECK_EQ(fx.project.findAsset(fx.b)->path, std::string("/old/b.mp4"));
}

NC_TEST(MediaOffline, RelinkBatchPartialFailure) {
    BatchFixture fx;
    MemoryProbe probe;
    probe.files["/new/a.mp4"] = "contentA";  // A 成功
    probe.files["/new/b.mp4"] = "corrupted"; // B 摘要不符
    const std::vector<std::pair<AssetId, std::string>> mappings = {
        {fx.a, "/new/a.mp4"}, {fx.b, "/new/b.mp4"}};
    const RelinkResult r = relinkAssets(*fx.editor, fx.project, mappings, &probe);
    NC_CHECK(r.result.isOk());  // 部分成功视为整体成功（逐项回报）
    NC_CHECK_EQ(r.relinked, std::size_t(1));
    NC_CHECK_EQ(fx.project.findAsset(fx.a)->path, std::string("/new/a.mp4"));
    NC_CHECK_EQ(fx.project.findAsset(fx.b)->path, std::string("/old/b.mp4"));  // 未变
    NC_CHECK(!r.digestWarnings.empty());  // 失败原因有记录
}

NC_TEST(MediaOffline, RelinkBatchAllFail) {
    BatchFixture fx;
    MemoryProbe probe;  // 文件都不存在
    const std::vector<std::pair<AssetId, std::string>> mappings = {
        {fx.a, "/new/a.mp4"}, {fx.b, "/new/b.mp4"}};
    const RelinkResult r = relinkAssets(*fx.editor, fx.project, mappings, &probe);
    NC_CHECK(!r.result.isOk());
    NC_CHECK_EQ(r.relinked, std::size_t(0));
}

NC_TEST(MediaOffline, RelinkBatchEmptyRejected) {
    BatchFixture fx;
    MemoryProbe probe;
    NC_CHECK(!relinkAssets(*fx.editor, fx.project, {}, &probe).result.isOk());
}

// ---------------------------------------------------------------------------
// 摘要写入与序列化
// ---------------------------------------------------------------------------

NC_TEST(MediaOffline, SetDigestValidates) {
    OfflineFixture fx;
    // 合法 64 位 hex
    const std::string digest = sha256Hex("x");
    NC_CHECK(setAssetDigest(fx.project, fx.assetId, digest).isOk());
    NC_CHECK_EQ(fx.asset()->contentSha256, digest);
    // 空串 = 清除
    NC_CHECK(setAssetDigest(fx.project, fx.assetId, "").isOk());
    NC_CHECK(fx.asset()->contentSha256.empty());
    // 非法（长度/字符）
    NC_CHECK(!setAssetDigest(fx.project, fx.assetId, "abc").isOk());
    NC_CHECK(!setAssetDigest(fx.project, fx.assetId, std::string(64, 'z')).isOk());
    // 素材不存在
    NC_CHECK(!setAssetDigest(fx.project, AssetId{9999}, digest).isOk());
}

NC_TEST(MediaOffline, DigestSerializationRoundTrip) {
    OfflineFixture fx;
    const std::string digest = sha256Hex("payload");
    setAssetDigest(fx.project, fx.assetId, digest);
    fx.project.findAsset(fx.assetId)->offline = true;
    const std::string text = saveProjectToNclip(fx.project);
    // 摘要已写入 JSON（pretty 格式：键值间有空格）
    NC_CHECK(text.find(digest) != std::string::npos);
    NC_CHECK(text.find("\"offline\": true") != std::string::npos);
    // 载入往返
    Project loaded;
    const NclipLoadResult load = loadProjectFromNclip(text, loaded);
    NC_CHECK(load.result.isOk());
    NC_CHECK_EQ(loaded.assets.size(), std::size_t(1));
    NC_CHECK_EQ(loaded.assets[0].contentSha256, digest);
    NC_CHECK(loaded.assets[0].offline);
}

NC_TEST(MediaOffline, MalformedDigestDowngradesOnLoad) {
    OfflineFixture fx("/old/a.mp4", "hello", /*withDigest=*/false);  // 无摘要 → 无已存 sha256 键
    std::string text = saveProjectToNclip(fx.project);
    // 注入非法摘要（长度错误）→ 载入时降级为「未记录」，不阻止打开
    const std::size_t pos = text.find("\"duration\"");
    NC_CHECK(pos != std::string::npos);
    text.insert(pos, "\"sha256\":\"zzzz\",");
    Project loaded;
    const NclipLoadResult load = loadProjectFromNclip(text, loaded);
    NC_CHECK(load.result.isOk());  // 容错：非法摘要不阻止工程打开
    NC_CHECK(loaded.assets[0].contentSha256.empty());  // 降级为未记录
}

NC_TEST(MediaOffline, OfflineBadgeText) {
    NC_CHECK_EQ(std::string(offlineBadgeText()), std::string("素材离线"));
}
