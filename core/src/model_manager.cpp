#include "nairoclip/core/model_manager.h"

#include "nairoclip/core/json.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace nairoclip {
namespace core {

namespace {

bool equalsIgnoreCase(const char* a, const char* b) {
    if (a == nullptr || b == nullptr) {
        return false;
    }
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

/// 内置容量单位换算（1024 进制）。
constexpr std::int64_t kKB = 1024;
constexpr std::int64_t kMB = 1024 * kKB;
constexpr std::int64_t kGB = 1024 * kMB;

/// 定点格式化（避免依赖 locale）。
std::string formatFixed(double value, int digits) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", digits, value);
    return std::string(buffer);
}

/// 容量文本（整数单位 + 一位小数，与导出预估同风格）。
std::string bytesToText(std::int64_t bytes) {
    if (bytes < 0) {
        return "0 B";
    }
    if (bytes >= kGB) {
        return formatFixed(static_cast<double>(bytes) / static_cast<double>(kGB), 1) + " GB";
    }
    if (bytes >= kMB) {
        return formatFixed(static_cast<double>(bytes) / static_cast<double>(kMB), 0) + " MB";
    }
    if (bytes >= kKB) {
        return formatFixed(static_cast<double>(bytes) / static_cast<double>(kKB), 0) + " KB";
    }
    return std::to_string(bytes) + " B";
}

/// 版本比较（"1.2.3" 形式；返回 -1/0/1）。
/// 非法/空串按 0 处理（保守：不误判为「有更新」）。
int compareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& text, std::vector<int>& parts) {
        std::string current;
        for (char c : text) {
            if (c >= '0' && c <= '9') {
                current.push_back(c);
            } else {
                if (!current.empty()) {
                    parts.push_back(std::atoi(current.c_str()));
                    current.clear();
                }
            }
        }
        if (!current.empty()) {
            parts.push_back(std::atoi(current.c_str()));
        }
    };
    std::vector<int> pa;
    std::vector<int> pb;
    parse(a, pa);
    parse(b, pb);
    const std::size_t count = std::max(pa.size(), pb.size());
    for (std::size_t i = 0; i < count; ++i) {
        const int va = i < pa.size() ? pa[i] : 0;
        const int vb = i < pb.size() ? pb[i] : 0;
        if (va != vb) {
            return va < vb ? -1 : 1;
        }
    }
    return 0;
}

}  // namespace

// =============================================================================
// 模型目录
// =============================================================================

namespace {

std::vector<ModelInfo> makeCatalog() {
    std::vector<ModelInfo> catalog;

    auto add = [&catalog](const char* id, const char* name, const char* description,
                          std::int64_t sizeBytes, const char* version,
                          ModelCapability capability, bool requiresPro) {
        ModelInfo info;
        info.id = id;
        info.name = name;
        info.description = description;
        info.sizeBytes = sizeBytes;
        info.version = version;
        info.capability = capability;
        info.requiresPro = requiresPro;
        catalog.push_back(std::move(info));
    };

    // 体积参考文档第 5 部分「二、AI模型管理」的展示值。
    add("matting_basic", "智能抠像（基础版）", "人像抠像，精度约 85%（免费版可用）", 60 * kMB,
        "1.0.0", ModelCapability::MattingBasic, false);
    add("matting_hq", "智能抠像（高精度版）", "人像/物品/色度抠像，精度约 98%（Pro）", 120 * kMB,
        "1.1.0", ModelCapability::MattingHighPrecision, true);
    add("whisper_zh", "Whisper 语音转字幕（中文优化）", "本地语音识别，字幕自动打轴", 240 * kMB,
        "2.0.0", ModelCapability::SpeechToText, false);
    add("flow_interp", "光流补帧算法模型", "光流估计与中间帧合成（60/120fps，Pro）", 85 * kMB,
        "1.0.0", ModelCapability::FrameInterpolation, true);
    add("super_res_2x4x", "超分辨率模型（2x/4x）", "画质提升与放大（免费 2x 每月 3 次）", 150 * kMB,
        "1.2.0", ModelCapability::SuperResolution, false);
    add("tts_zh", "智能配音（中文音色）", "文字转语音；生成音频带 AI 合成标识", 95 * kMB, "1.0.0",
        ModelCapability::Tts, false);
    add("stem_sep", "人声分离模型", "人声/伴奏分离（标准 / 高精度）", 180 * kMB, "1.0.0",
        ModelCapability::StemSeparation, false);
    add("translate_zh_en", "字幕翻译模型", "中英互译（免费每月 2 次，Pro 无限）", 110 * kMB, "1.0.0",
        ModelCapability::Translation, false);
    return catalog;
}

}  // namespace

const std::vector<ModelInfo>& builtinModelCatalog() {
    static const std::vector<ModelInfo> catalog = makeCatalog();
    return catalog;
}

const ModelInfo* findBuiltinModel(const std::string& modelId) {
    for (const auto& info : builtinModelCatalog()) {
        if (info.id == modelId) {
            return &info;
        }
    }
    return nullptr;
}

const char* modelCapabilityName(ModelCapability capability) {
    switch (capability) {
        case ModelCapability::None: return "none";
        case ModelCapability::MattingBasic: return "matting_basic";
        case ModelCapability::MattingHighPrecision: return "matting_hq";
        case ModelCapability::SpeechToText: return "speech_to_text";
        case ModelCapability::FrameInterpolation: return "frame_interpolation";
        case ModelCapability::SuperResolution: return "super_resolution";
        case ModelCapability::Tts: return "tts";
        case ModelCapability::StemSeparation: return "stem_separation";
        case ModelCapability::Translation: return "translation";
    }
    return "none";
}

bool modelCapabilityFromString(const char* text, ModelCapability& out) {
    const ModelCapability all[] = {
        ModelCapability::None,         ModelCapability::MattingBasic,
        ModelCapability::MattingHighPrecision, ModelCapability::SpeechToText,
        ModelCapability::FrameInterpolation,   ModelCapability::SuperResolution,
        ModelCapability::Tts,                  ModelCapability::StemSeparation,
        ModelCapability::Translation};
    for (ModelCapability capability : all) {
        if (equalsIgnoreCase(text, modelCapabilityName(capability))) {
            out = capability;
            return true;
        }
    }
    return false;
}

bool modelAccessAllowed(const ModelInfo& info, const Entitlement& entitlement,
                        std::int64_t nowUnix) {
    switch (info.capability) {
        case ModelCapability::MattingHighPrecision:
            return entitlement.proActiveAt(nowUnix) && entitlement.highPrecisionMatting();
        case ModelCapability::FrameInterpolation:
            return entitlement.proActiveAt(nowUnix) && entitlement.frameInterpolation();
        default:
            // 其余能力本地放行（配额由服务端计数裁决，见文档 8.3）。
            return true;
    }
}

// =============================================================================
// 状态名称
// =============================================================================

const char* modelStatusName(ModelStatus status) {
    switch (status) {
        case ModelStatus::NotDownloaded: return "not_downloaded";
        case ModelStatus::Downloading: return "downloading";
        case ModelStatus::Installed: return "installed";
        case ModelStatus::Corrupt: return "corrupt";
        case ModelStatus::UpdateAvailable: return "update_available";
        case ModelStatus::Verifying: return "verifying";
    }
    return "not_downloaded";
}

bool modelStatusFromString(const char* text, ModelStatus& out) {
    const ModelStatus all[] = {
        ModelStatus::NotDownloaded, ModelStatus::Downloading, ModelStatus::Installed,
        ModelStatus::Corrupt,       ModelStatus::UpdateAvailable, ModelStatus::Verifying};
    for (ModelStatus status : all) {
        if (equalsIgnoreCase(text, modelStatusName(status))) {
            out = status;
            return true;
        }
    }
    return false;
}

const char* downloadPhaseName(DownloadPhase phase) {
    switch (phase) {
        case DownloadPhase::Idle: return "idle";
        case DownloadPhase::Downloading: return "downloading";
        case DownloadPhase::Paused: return "paused";
        case DownloadPhase::Verifying: return "verifying";
        case DownloadPhase::Completed: return "completed";
        case DownloadPhase::Failed: return "failed";
        case DownloadPhase::Cancelled: return "cancelled";
    }
    return "idle";
}

bool downloadPhaseFromString(const char* text, DownloadPhase& out) {
    const DownloadPhase all[] = {
        DownloadPhase::Idle,     DownloadPhase::Downloading, DownloadPhase::Paused,
        DownloadPhase::Verifying, DownloadPhase::Completed,  DownloadPhase::Failed,
        DownloadPhase::Cancelled};
    for (DownloadPhase phase : all) {
        if (equalsIgnoreCase(text, downloadPhaseName(phase))) {
            out = phase;
            return true;
        }
    }
    return false;
}

bool isDownloadTransitionValid(DownloadPhase from, DownloadPhase to) {
    // 自迁移始终允许（幂等回调）。
    if (from == to) {
        return true;
    }
    switch (from) {
        case DownloadPhase::Idle:
            // 未开始 → 开始/取消（下载中断弹窗的「取消」）。
            return to == DownloadPhase::Downloading || to == DownloadPhase::Cancelled;
        case DownloadPhase::Downloading:
            // 下载中 → 暂停/校验/失败/取消。
            return to == DownloadPhase::Paused || to == DownloadPhase::Verifying ||
                   to == DownloadPhase::Failed || to == DownloadPhase::Cancelled;
        case DownloadPhase::Paused:
            // 暂停 → 恢复/取消（断点保留）。
            return to == DownloadPhase::Downloading || to == DownloadPhase::Cancelled ||
                   to == DownloadPhase::Failed;
        case DownloadPhase::Verifying:
            // 校验 → 安装完成/失败/取消。
            return to == DownloadPhase::Completed || to == DownloadPhase::Failed ||
                   to == DownloadPhase::Cancelled;
        case DownloadPhase::Completed:
            // 完成 → 重新下载（更新）/失败（安装后校验失效）。
            return to == DownloadPhase::Downloading || to == DownloadPhase::Failed;
        case DownloadPhase::Failed:
            // 失败 → 重试/取消。
            return to == DownloadPhase::Downloading || to == DownloadPhase::Cancelled;
        case DownloadPhase::Cancelled:
            // 取消 → 「继续下载」/重新开始。
            return to == DownloadPhase::Downloading || to == DownloadPhase::Paused;
    }
    return false;
}

double DownloadState::progress() const {
    if (totalBytes <= 0) {
        return 0.0;
    }
    const double ratio = static_cast<double>(downloadedBytes) / static_cast<double>(totalBytes);
    return std::max(0.0, std::min(1.0, ratio));
}

std::int64_t DownloadState::remainingBytes() const {
    return std::max<std::int64_t>(0, totalBytes - downloadedBytes);
}

// =============================================================================
// 断点续传坐标
// =============================================================================

void deriveModelCounter(const std::string& modelId, const std::string& version,
                        std::uint8_t outCounter[kAesCtrCounterSize]) {
    if (outCounter == nullptr) {
        return;
    }
    std::string seed = "nairoclip-model-ctr-v1|";
    seed += modelId;
    seed += "|";
    seed += version;
    std::uint8_t digest[kSha256DigestSize];
    sha256(seed.data(), seed.size(), digest);
    // 取前 16 字节作为初始计数器（确定性派生，客户端/服务器可独立复算）。
    for (std::size_t i = 0; i < kAesCtrCounterSize; ++i) {
        outCounter[i] = digest[i];
    }
}

DownloadResumePlan planDownloadResume(const std::string& modelId, const std::string& version,
                                      std::int64_t downloadedBytes) {
    DownloadResumePlan plan;
    if (modelId.empty() || version.empty() || downloadedBytes < 0) {
        plan.note = "模型标识/版本为空或偏移非法";
        return plan;
    }
    std::uint8_t counter[kAesCtrCounterSize];
    deriveModelCounter(modelId, version, counter);

    const auto offset = static_cast<std::uint64_t>(downloadedBytes);
    plan.offsetBytes = downloadedBytes;
    plan.intraBlockSkip = aesCtrIntraBlockOffset(offset);
    aesCtrCounterAtOffset(counter, offset, plan.counter);
    plan.valid = true;
    plan.note = "从 " + std::to_string(downloadedBytes) + " 字节处续传";
    return plan;
}

// =============================================================================
// 下载状态机
// =============================================================================

DownloadState& ModelDownloadManager::ensure(const std::string& modelId) {
    for (auto& state : states_) {
        if (state.modelId == modelId) {
            return state;
        }
    }
    DownloadState state;
    state.modelId = modelId;
    states_.push_back(std::move(state));
    return states_.back();
}

const DownloadState* ModelDownloadManager::find(const std::string& modelId) const {
    for (const auto& state : states_) {
        if (state.modelId == modelId) {
            return &state;
        }
    }
    return nullptr;
}

DownloadState* ModelDownloadManager::find(const std::string& modelId) {
    for (auto& state : states_) {
        if (state.modelId == modelId) {
            return &state;
        }
    }
    return nullptr;
}

namespace {

/// 尝试迁移；非法迁移不改动状态并返回错误。
Result tryTransit(DownloadState& state, DownloadPhase to, const char* action) {
    if (!isDownloadTransitionValid(state.phase, to)) {
        return Result::fail(ErrorCode::InvalidArgument,
                            std::string("状态不允许") + action + "（当前：" +
                                downloadPhaseName(state.phase) + "）");
    }
    state.phase = to;
    return Result::ok();
}

}  // namespace

Result ModelDownloadManager::begin(const std::string& modelId, std::int64_t totalBytes,
                                   std::int64_t nowUnix) {
    if (modelId.empty()) {
        return Result::fail(ErrorCode::InvalidArgument, "模型标识为空");
    }
    if (totalBytes <= 0) {
        return Result::fail(ErrorCode::InvalidArgument, "模型大小非法（需服务器返回 Content-Length）");
    }
    DownloadState& state = ensure(modelId);
    const Result transit = tryTransit(state, DownloadPhase::Downloading, "开始下载");
    if (!transit.isOk()) {
        return transit;
    }
    state.totalBytes = totalBytes;
    state.downloadedBytes = 0;  // 全量重新下载
    state.startedAtUnix = nowUnix;
    state.updatedAtUnix = nowUnix;
    state.message.clear();
    return Result::ok();
}

Result ModelDownloadManager::pause(const std::string& modelId, std::int64_t nowUnix) {
    DownloadState* state = find(modelId);
    if (state == nullptr) {
        return Result::fail(ErrorCode::NotFound, "没有该模型的下载任务");
    }
    const Result transit = tryTransit(*state, DownloadPhase::Paused, "暂停");
    if (!transit.isOk()) {
        return transit;
    }
    state->updatedAtUnix = nowUnix;
    state->message = "已暂停（已下载 " + formatBytesText(state->downloadedBytes) + "）";
    return Result::ok();
}

Result ModelDownloadManager::resume(const std::string& modelId, std::int64_t nowUnix) {
    DownloadState* state = find(modelId);
    if (state == nullptr) {
        return Result::fail(ErrorCode::NotFound, "没有该模型的下载任务");
    }
    const Result transit = tryTransit(*state, DownloadPhase::Downloading, "继续下载");
    if (!transit.isOk()) {
        return transit;
    }
    state->updatedAtUnix = nowUnix;
    // 断点保留（downloadedBytes 不变），平台层用 planDownloadResume 取续传坐标。
    state->message = "继续下载（从 " + formatBytesText(state->downloadedBytes) + " 处续传）";
    return Result::ok();
}

Result ModelDownloadManager::cancel(const std::string& modelId, std::int64_t nowUnix) {
    DownloadState* state = find(modelId);
    if (state == nullptr) {
        return Result::fail(ErrorCode::NotFound, "没有该模型的下载任务");
    }
    const Result transit = tryTransit(*state, DownloadPhase::Cancelled, "取消");
    if (!transit.isOk()) {
        return transit;
    }
    state->updatedAtUnix = nowUnix;
    state->message = "已取消";
    return Result::ok();
}

Result ModelDownloadManager::markProgress(const std::string& modelId, std::int64_t downloadedBytes,
                                          std::int64_t nowUnix) {
    DownloadState* state = find(modelId);
    if (state == nullptr) {
        return Result::fail(ErrorCode::NotFound, "没有该模型的下载任务");
    }
    if (state->phase != DownloadPhase::Downloading) {
        return Result::fail(ErrorCode::InvalidArgument, "当前不在下载中");
    }
    if (downloadedBytes < 0) {
        return Result::fail(ErrorCode::InvalidArgument, "进度字节数非法");
    }
    if (downloadedBytes < state->downloadedBytes) {
        return Result::fail(ErrorCode::InvalidArgument, "进度不得回退（断点已落盘）");
    }
    state->downloadedBytes = std::min(downloadedBytes, state->totalBytes);
    state->updatedAtUnix = nowUnix;
    return Result::ok();
}

Result ModelDownloadManager::finishDownload(const std::string& modelId, std::int64_t nowUnix) {
    DownloadState* state = find(modelId);
    if (state == nullptr) {
        return Result::fail(ErrorCode::NotFound, "没有该模型的下载任务");
    }
    const Result transit = tryTransit(*state, DownloadPhase::Verifying, "进入校验");
    if (!transit.isOk()) {
        return transit;
    }
    state->updatedAtUnix = nowUnix;
    state->message = "正在校验完整性";
    return Result::ok();
}

Result ModelDownloadManager::complete(const std::string& modelId, std::int64_t nowUnix) {
    DownloadState* state = find(modelId);
    if (state == nullptr) {
        return Result::fail(ErrorCode::NotFound, "没有该模型的下载任务");
    }
    const Result transit = tryTransit(*state, DownloadPhase::Completed, "完成安装");
    if (!transit.isOk()) {
        return transit;
    }
    state->updatedAtUnix = nowUnix;
    state->downloadedBytes = state->totalBytes;
    state->message = "已安装";
    return Result::ok();
}

Result ModelDownloadManager::fail(const std::string& modelId, const std::string& reason,
                                  std::int64_t nowUnix) {
    DownloadState* state = find(modelId);
    if (state == nullptr) {
        return Result::fail(ErrorCode::NotFound, "没有该模型的下载任务");
    }
    const Result transit = tryTransit(*state, DownloadPhase::Failed, "标记失败");
    if (!transit.isOk()) {
        return transit;
    }
    state->updatedAtUnix = nowUnix;
    state->message = reason;
    return Result::ok();
}

bool ModelDownloadManager::remove(const std::string& modelId) {
    for (auto it = states_.begin(); it != states_.end(); ++it) {
        if (it->modelId == modelId) {
            states_.erase(it);
            return true;
        }
    }
    return false;
}

std::size_t ModelDownloadManager::activeCount() const {
    std::size_t count = 0;
    for (const auto& state : states_) {
        if (state.isActive()) {
            ++count;
        }
    }
    return count;
}

void ModelDownloadManager::clear() { states_.clear(); }

// =============================================================================
// 签名 URL
// =============================================================================

bool SignedUrl::validAt(std::int64_t nowUnix, std::string* outReason) const {
    auto reject = [outReason](const char* reason) {
        if (outReason != nullptr) {
            *outReason = reason;
        }
        return false;
    };

    if (url.empty()) {
        return reject("下载地址为空");
    }
    if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
        return reject("下载地址协议不合法（仅允许 http/https）");
    }
    if (expiresAtUnix <= 0) {
        return reject("缺少有效期信息（拒绝无期限地址）");
    }
    if (nowUnix >= expiresAtUnix) {
        return reject("签名地址已过期，请重新获取");
    }
    return true;
}

std::int64_t signedUrlRemainingSeconds(const SignedUrl& url, std::int64_t nowUnix) {
    if (url.expiresAtUnix <= 0) {
        return 0;
    }
    if (nowUnix >= url.expiresAtUnix) {
        return 0;
    }
    return url.expiresAtUnix - nowUnix;
}

// =============================================================================
// 模型记录表
// =============================================================================

const ModelRecord* ModelStore::find(const std::string& modelId) const {
    for (const auto& record : records) {
        if (record.modelId == modelId) {
            return &record;
        }
    }
    return nullptr;
}

ModelRecord* ModelStore::find(const std::string& modelId) {
    for (auto& record : records) {
        if (record.modelId == modelId) {
            return &record;
        }
    }
    return nullptr;
}

void ModelStore::upsert(ModelRecord record) {
    for (auto& existing : records) {
        if (existing.modelId == record.modelId) {
            existing = std::move(record);
            return;
        }
    }
    records.push_back(std::move(record));
}

bool ModelStore::remove(const std::string& modelId) {
    for (auto it = records.begin(); it != records.end(); ++it) {
        if (it->modelId == modelId) {
            records.erase(it);
            return true;
        }
    }
    return false;
}

void ModelStore::clear() { records.clear(); }

int ModelStore::installedCount() const {
    int count = 0;
    for (const auto& record : records) {
        if (record.status == ModelStatus::Installed) {
            ++count;
        }
    }
    return count;
}

int ModelStore::corruptCount() const {
    int count = 0;
    for (const auto& record : records) {
        if (record.status == ModelStatus::Corrupt) {
            ++count;
        }
    }
    return count;
}

std::int64_t ModelStore::totalSizeBytes() const {
    std::int64_t total = 0;
    for (const auto& record : records) {
        if (record.status == ModelStatus::Installed) {
            total += record.sizeBytes;
        }
    }
    return total;
}

ModelStatus ModelStore::statusOf(const std::string& modelId) const {
    const ModelRecord* record = find(modelId);
    if (record == nullptr) {
        return ModelStatus::NotDownloaded;
    }
    // 已安装但清单版本更新 → UpdateAvailable（UI 显示「可更新」）。
    if (record->status == ModelStatus::Installed) {
        const ModelInfo* info = findBuiltinModel(modelId);
        if (info != nullptr && !info->version.empty() && !record->version.empty()) {
            if (compareVersions(record->version, info->version) < 0) {
                return ModelStatus::UpdateAvailable;
            }
        }
    }
    return record->status;
}

const ModelRecord* ModelStore::recordOf(const std::string& modelId) const { return find(modelId); }

// =============================================================================
// 完整性校验与加密存储
// =============================================================================

ModelVerifyResult verifyModelPayload(const ModelInfo& info,
                                     const std::vector<std::uint8_t>& ciphertext,
                                     const std::string& expectedSha256Hex) {
    ModelVerifyResult result;
    result.sizeBytes = static_cast<std::int64_t>(ciphertext.size());

    if (info.sizeBytes > 0) {
        result.sizeMatch = result.sizeBytes == info.sizeBytes;
    }

    std::uint8_t digest[kSha256DigestSize];
    sha256(ciphertext.data(), ciphertext.size(), digest);
    const std::string actual = sha256Hex(digest);
    std::uint8_t expected[kSha256DigestSize];
    // 期望摘要缺失或非法 → fail-closed（不得视为通过）。
    if (!sha256FromHex(expectedSha256Hex.c_str(), expected)) {
        result.ok = false;
        result.digestMatch = false;
        result.message = "缺少有效的完整性校验值，已拒绝安装";
        return result;
    }
    result.digestMatch = constantTimeEquals(digest, expected, kSha256DigestSize);
    result.ok = result.digestMatch && result.sizeMatch;

    if (result.ok) {
        result.message = "完整性校验通过";
    } else if (!result.sizeMatch) {
        result.message = "文件大小不符（期望 " + formatBytesText(info.sizeBytes) + "，实际 " +
                         formatBytesText(result.sizeBytes) + "）";
    } else {
        result.message = "摘要校验失败，文件可能已损坏，请重新下载";
    }
    return result;
}

std::vector<std::uint8_t> encryptModelPayload(const std::uint8_t key[kAes256KeySize],
                                              const std::string& modelId,
                                              const std::string& version,
                                              const std::vector<std::uint8_t>& plaintext) {
    if (key == nullptr || modelId.empty() || version.empty()) {
        return {};
    }
    std::uint8_t counter[kAesCtrCounterSize];
    deriveModelCounter(modelId, version, counter);
    return aes256CtrCrypt(key, counter, plaintext);
}

std::vector<std::uint8_t> decryptModelPayload(const std::uint8_t key[kAes256KeySize],
                                              const std::string& modelId,
                                              const std::string& version,
                                              const std::vector<std::uint8_t>& ciphertext) {
    return encryptModelPayload(key, modelId, version, ciphertext);  // CTR 对称
}

std::vector<std::uint8_t> decryptModelPayloadAt(const std::uint8_t key[kAes256KeySize],
                                                const std::string& modelId,
                                                const std::string& version,
                                                std::uint64_t offset,
                                                const std::vector<std::uint8_t>& ciphertext) {
    if (key == nullptr || modelId.empty() || version.empty()) {
        return {};
    }
    std::uint8_t counter[kAesCtrCounterSize];
    deriveModelCounter(modelId, version, counter);
    return aes256CtrCryptAtOffset(key, counter, offset, ciphertext);
}

// =============================================================================
// 存储统计
// =============================================================================

bool ModelStorageStats::hasRoomFor(std::int64_t requiredBytes, double safetyFactor) const {
    if (requiredBytes <= 0) {
        return true;
    }
    const double factor = safetyFactor > 0.0 ? safetyFactor : 1.0;
    const double needed = static_cast<double>(requiredBytes) * factor;
    return static_cast<double>(deviceFreeBytes) >= needed;
}

std::string ModelStorageStats::usageText() const {
    return "可用 " + formatBytesText(deviceFreeBytes) + " / 总计 " +
           formatBytesText(deviceTotalBytes);
}

std::string ModelStorageStats::modelText() const {
    return "模型占用 " + formatBytesText(modelBytes);
}

ModelStorageStats computeModelStorage(const ModelStore& store, std::int64_t deviceTotalBytes,
                                      std::int64_t deviceFreeBytes) {
    ModelStorageStats stats;
    stats.deviceTotalBytes = std::max<std::int64_t>(0, deviceTotalBytes);
    stats.deviceFreeBytes = std::max<std::int64_t>(0, deviceFreeBytes);
    stats.modelBytes = store.totalSizeBytes();
    stats.installedCount = store.installedCount();
    stats.corruptCount = store.corruptCount();
    return stats;
}

std::string formatBytesText(std::int64_t bytes) { return bytesToText(bytes); }

// =============================================================================
// 持久化
// =============================================================================

std::string saveModelStoreToJson(const ModelStore& store) {
    JsonValue root = JsonValue::object();
    root.set("schema", JsonValue::integer(1));
    JsonValue records = JsonValue::array();
    for (const auto& record : store.records) {
        JsonValue item = JsonValue::object();
        item.set("id", JsonValue::string(record.modelId));
        if (!record.version.empty()) {
            item.set("version", JsonValue::string(record.version));
        }
        if (!record.path.empty()) {
            item.set("path", JsonValue::string(record.path));
        }
        item.set("size", JsonValue::integer(record.sizeBytes));
        if (!record.sha256.empty()) {
            item.set("sha256", JsonValue::string(record.sha256));
        }
        if (record.installedAtUnix > 0) {
            item.set("installedAt", JsonValue::integer(record.installedAtUnix));
        }
        item.set("status", JsonValue::string(modelStatusName(record.status)));
        if (!record.note.empty()) {
            item.set("note", JsonValue::string(record.note));
        }
        records.push(std::move(item));
    }
    root.set("records", std::move(records));
    return toJsonString(root, true);
}

bool loadModelStoreFromJson(const std::string& text, ModelStore& out, std::string* error) {
    out.clear();
    JsonValue root;
    JsonParseError parseError;
    if (!parseJson(text, root, parseError)) {
        if (error != nullptr) {
            *error = "JSON 解析失败：" + parseError.message;
        }
        return false;
    }
    if (!root.isObject()) {
        if (error != nullptr) {
            *error = "模型记录表根节点必须是对象";
        }
        return false;
    }
    const JsonValue& records = root.get("records");
    if (!records.isArray()) {
        // 空表是合法状态（首次使用）。
        return true;
    }
    int skipped = 0;
    for (std::size_t i = 0; i < records.size(); ++i) {
        const JsonValue& item = records.at(i);
        if (!item.isObject()) {
            ++skipped;
            continue;
        }
        ModelRecord record;
        record.modelId = item.get("id").asString();
        record.version = item.get("version").asString();
        record.path = item.get("path").asString();
        record.sizeBytes = item.get("size").asInteger();
        record.sha256 = item.get("sha256").asString();
        record.installedAtUnix = item.get("installedAt").asInteger();
        ModelStatus status = ModelStatus::Installed;
        const std::string statusText = item.get("status").asString();
        if (!modelStatusFromString(statusText.c_str(), status)) {
            status = ModelStatus::Installed;
        }
        // 运行态不算持久态：下载中/校验中恢复为「损坏/未下载」由平台层重判定。
        if (status == ModelStatus::Downloading || status == ModelStatus::Verifying) {
            status = ModelStatus::Corrupt;
        }
        record.status = status;
        record.note = item.get("note").asString();

        if (record.modelId.empty() || record.sizeBytes < 0) {
            ++skipped;  // 非法记录跳过（不阻断其余记录）
            continue;
        }
        out.upsert(std::move(record));
    }
    if (error != nullptr) {
        *error = skipped > 0 ? ("已跳过 " + std::to_string(skipped) + " 条非法记录") : "";
    }
    return true;
}

}  // namespace core
}  // namespace nairoclip
