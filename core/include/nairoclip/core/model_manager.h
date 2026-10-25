#pragma once
// =============================================================================
// AI 本地模型管理（Local AI Model Management）—— 文档第 5 部分 + 第 8 部分
// =============================================================================
// 产品对应：
//   · 第 5 部分「二、AI模型管理」：模型列表（名称/占用/已安装/下载/删除）、
//     存储统计（可用/总计）、下载中断恢复（「继续下载」/「取消」）；
//   · 第 8 部分「二、模型分发架构」：Token 换签名 URL（带有效期）→ 下载 →
//     本地 AES 加密存储 → 推理时用服务器下发的密钥在内存解密；
//   · 第 8 部分 3.3：使用次数计数在服务器端，本模块只管理本机模型资产。
//
// 职责边界（core 无网络、无文件系统）：
//   · 本模块实现客户端逻辑：模型目录、下载状态机（含断点续传坐标）、
//     签名 URL 有效期校验、完整性校验（SHA-256）、AES 加密存储/内存解密、
//     占用统计、JSON 持久化（供平台层落盘）；
//   · 真实网络请求与文件读写由平台层完成，平台层按状态机回调本模块
//     （begin/progress/finish/fail），本模块保证状态迁移合法、断点坐标精确；
//   · 模型文件在磁盘上始终为密文；密钥只存在于内存（平台层不落盘）。

#include "nairoclip/core/crypto.h"
#include "nairoclip/core/entitlement.h"
#include "nairoclip/core/types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

// =============================================================================
// 模型目录（内置清单）
// =============================================================================

/// 模型能力分类（决定权益门控与 UI 分组）。
enum class ModelCapability {
    None = 0,
    MattingBasic = 1,          ///< 智能抠像（基础版）——免费
    MattingHighPrecision = 2,  ///< 智能抠像（高精度版）——Pro（文档 5.2）
    SpeechToText = 3,          ///< 语音转字幕（Whisper 中文优化）
    FrameInterpolation = 4,    ///< 光流补帧——Pro（文档 7.6）
    SuperResolution = 5,       ///< 超分辨率——Pro 无限次（免费 2x 每月 3 次）
    Tts = 6,                   ///< 智能配音（TTS；合规标识见 ai_label）
    StemSeparation = 7,        ///< 人声分离（标准/高精度）
    Translation = 8,           ///< 字幕翻译
};

/// 能力名称（"matting_basic" / ...；序列化与 UI 用）。
const char* modelCapabilityName(ModelCapability capability);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool modelCapabilityFromString(const char* text, ModelCapability& out);

/// 模型目录条目（内置清单的单一事实来源；与文档「AI模型管理」页一致）。
struct ModelInfo {
    /// 稳定标识（如 "matting_hq"；持久化与 C ABI 使用）。
    std::string id;
    /// 展示名（如「智能抠像（高精度版）」）。
    std::string name;
    /// 副标题/说明（供 UI 一行描述）。
    std::string description;
    /// 下载体积（字节；以服务器清单为准，此处为内置参考值）。
    std::int64_t sizeBytes = 0;
    /// sizeBytes 是否为**精确**校验值。
    /// - false（内置目录的默认值）：sizeBytes 只是展示用的参考体积，
    ///   完整性完全由 SHA-256 保证，体积只做合理性下限校验；
    /// - true（默认，保持既有语义）：逐字节精确校验；
    ///   服务端清单与外部构造的 ModelInfo 都走这条路径。
    bool sizeExact = true;
    /// 模型版本（以服务器清单为准；用于更新检测）。
    std::string version;
    /// 能力分类（权益门控依据）。
    ModelCapability capability = ModelCapability::None;
    /// 是否 Pro 专属（展示用；权威判定见 modelAccessAllowed）。
    bool requiresPro = false;

    bool valid() const { return !id.empty() && sizeBytes > 0; }
};

/// 内置模型目录（顺序即 UI 展示顺序）。
const std::vector<ModelInfo>& builtinModelCatalog();

/// 查找内置模型（未找到返回 nullptr）。
const ModelInfo* findBuiltinModel(const std::string& modelId);

/// 模型在该权益与时刻下是否可用（权威门控；与 Entitlement 单一事实来源一致）。
/// - MattingHighPrecision → Entitlement::highPrecisionMatting()；
/// - FrameInterpolation → Entitlement::frameInterpolation()；
/// - 其余能力本地放行（次数/时长配额由服务端计数裁决，见文档 8.3）。
bool modelAccessAllowed(const ModelInfo& info, const Entitlement& entitlement,
                        std::int64_t nowUnix);

// =============================================================================
// 安装状态
// =============================================================================

/// 模型安装状态（文档 5.2：未下载 / 下载中 / 已安装 / 损坏 + 更新）。
enum class ModelStatus {
    NotDownloaded = 0,    ///< 未下载（[⬇️下载]）
    Downloading = 1,      ///< 下载中（详情见 DownloadPhase）
    Installed = 2,        ///< 已安装（[✅已安装]）
    Corrupt = 3,          ///< 损坏（校验失败；UI 提示重新下载）
    UpdateAvailable = 4,  ///< 有更新（已安装版本 < 清单版本）
    Verifying = 5,        ///< 校验中（下载完成 → 摘要校验）
};

/// 状态名称（"not_downloaded" / "downloading" / "installed" / "corrupt" /
/// "update_available" / "verifying"）。
const char* modelStatusName(ModelStatus status);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool modelStatusFromString(const char* text, ModelStatus& out);

// =============================================================================
// 下载状态机（断点续传）
// =============================================================================

/// 下载阶段（状态机核心；平台层只允许沿合法边迁移）。
enum class DownloadPhase {
    Idle = 0,         ///< 未开始（可开始下载）
    Downloading = 1,  ///< 下载中（可暂停/取消）
    Paused = 2,       ///< 已暂停（断点保留，可恢复）
    Verifying = 3,    ///< 校验中（SHA-256 + 大小）
    Completed = 4,    ///< 完成（安装成功）
    Failed = 5,       ///< 失败（断点保留，可重试）
    Cancelled = 6,    ///< 已取消（文档「[继续下载] [取消]」）
};

/// 阶段名称（"idle" / "downloading" / "paused" / "verifying" / "completed" /
/// "failed" / "cancelled"）。
const char* downloadPhaseName(DownloadPhase phase);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool downloadPhaseFromString(const char* text, DownloadPhase& out);

/// 迁移是否合法（状态机不变量；非法迁移不得改变状态）。
bool isDownloadTransitionValid(DownloadPhase from, DownloadPhase to);

/// 单模型下载状态。
struct DownloadState {
    std::string modelId;
    DownloadPhase phase = DownloadPhase::Idle;
    /// 总字节数（0 = 未知，等待服务器 Content-Length）。
    std::int64_t totalBytes = 0;
    /// 已落盘字节数（= 断点续传坐标；密文长度）。
    std::int64_t downloadedBytes = 0;
    std::int64_t startedAtUnix = 0;
    std::int64_t updatedAtUnix = 0;
    /// 失败原因/提示（可直接展示）。
    std::string message;

    /// 是否处于活动过程（下载/校验）。
    bool isActive() const {
        return phase == DownloadPhase::Downloading || phase == DownloadPhase::Verifying;
    }
    /// 是否可恢复/重试（有断点且未完成）。
    bool canResume() const {
        return (phase == DownloadPhase::Paused || phase == DownloadPhase::Failed ||
                phase == DownloadPhase::Cancelled) &&
               downloadedBytes > 0 && downloadedBytes < totalBytes;
    }
    /// 进度（0..1；总量未知返回 0）。
    double progress() const;
    /// 剩余字节（总量未知返回 0）。
    std::int64_t remainingBytes() const;
};

/// 断点续传坐标（供平台层发起续传请求与定位解密/校验）。
struct DownloadResumePlan {
    bool valid = false;
    /// 续传起点（HTTP Range: bytes=offset-；= 已落盘密文长度）。
    std::int64_t offsetBytes = 0;
    /// AES-256-CTR 起始计数器（16 字节，大端递增）。
    std::uint8_t counter[kAesCtrCounterSize] = {};
    /// 起始块内需丢弃的密钥流字节数（offset % 16）。
    std::size_t intraBlockSkip = 0;
    std::string note;
};

/// 由模型 ID + 版本派生 AES-256-CTR 初始计数器。
/// 说明：模型文件密钥（32 字节）由服务器在下载时下发；计数器不需要额外传输，
/// 由「模型 ID + 版本」确定性派生，保证断点续传/重复安装时坐标一致。
void deriveModelCounter(const std::string& modelId, const std::string& version,
                        std::uint8_t outCounter[kAesCtrCounterSize]);

/// 计算断点续传坐标（模型 ID + 版本 + 已下载字节数；不依赖网络）。
DownloadResumePlan planDownloadResume(const std::string& modelId, const std::string& version,
                                      std::int64_t downloadedBytes);

/// 下载状态机管理器（平台层驱动；每个模型一条状态记录）。
class ModelDownloadManager {
public:
    const DownloadState* find(const std::string& modelId) const;
    DownloadState* find(const std::string& modelId);

    /// 开始下载（Idle/Cancelled/Failed/Completed → Downloading；totalBytes <= 0 拒绝）。
    Result begin(const std::string& modelId, std::int64_t totalBytes, std::int64_t nowUnix);
    /// 暂停（仅 Downloading → Paused；断点保留）。
    Result pause(const std::string& modelId, std::int64_t nowUnix);
    /// 恢复（Paused/Failed/Cancelled → Downloading）。
    Result resume(const std::string& modelId, std::int64_t nowUnix);
    /// 取消（Downloading/Paused/Failed/Verifying → Cancelled；断点保留供「继续下载」）。
    Result cancel(const std::string& modelId, std::int64_t nowUnix);
    /// 更新进度（仅 Downloading；单调不减，超出总量被钳制）。
    Result markProgress(const std::string& modelId, std::int64_t downloadedBytes,
                        std::int64_t nowUnix);
    /// 下载完成、进入校验（Downloading → Verifying）。
    Result finishDownload(const std::string& modelId, std::int64_t nowUnix);
    /// 校验通过、安装完成（Verifying → Completed）。
    Result complete(const std::string& modelId, std::int64_t nowUnix);
    /// 失败（Downloading/Verifying → Failed；记录原因）。
    Result fail(const std::string& modelId, const std::string& reason, std::int64_t nowUnix);
    /// 移除状态记录（重装/清理）。
    bool remove(const std::string& modelId);
    /// 活动任务数（Downloading/Verifying）。
    std::size_t activeCount() const;
    void clear();

private:
    DownloadState& ensure(const std::string& modelId);
    std::vector<DownloadState> states_;
};

// =============================================================================
// 设备性能分级与量化版本（三端部署规格第五章「模型分发与下载机制」）
// =============================================================================
// 产品对应（原文）：
//   · 「App 启动时探测设备规格：总内存(RAM) / CPU 核心数/架构 / 是否存在
//      NNAPI(Android)/CoreML(iOS) 硬件加速单元 / 轻量基准测试（用小模型跑一次
//      推理计时）」；
//   · 「分级结果（高/中/低三档）作为参数之一，随模型下载请求一起发送给服务端」；
//   · 「服务端根据功能 ID + 设备分级，返回对应量化精度版本的模型文件下载地址」；
//   · 「桌面端默认统一下发 FP32 原始精度版本，不做自动分级探测；如需性能优化
//      提供用户主动选择的『性能模式』开关（允许手动切换到 FP16）」；
//   · 「三端（Android/iOS/桌面）的功能集合与最终导出效果质量上限必须完全一致，
//      唯一允许存在差异的变量是模型文件的量化精度」。
//
// 职责边界：探测数据由平台层采集（core 不做系统调用），本模块负责：
//   · 分级判定（阈值化 + 基准耗时校准；确定性、可测）；
//   · 量化版本推荐（按平台与分级 → FP32/FP16/INT8）；
//   · 下载请求参数组装（功能 ID + 设备分级 + 量化版本）。

/// 量化精度（文档 3.2：FP32 100% / FP16 约 50% / INT8 约 25% 体积）。
enum class QuantizationLevel {
    Fp32 = 0,  ///< 原始精度（桌面端默认）
    Fp16 = 1,  ///< 半精度（移动端默认首选）
    Int8 = 2,  ///< 整数量化（低端移动设备兜底）
};

/// 量化等级名称（"fp32" / "fp16" / "int8"）。
const char* quantizationLevelName(QuantizationLevel level);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool quantizationLevelFromString(const char* text, QuantizationLevel& out);
/// 相对 FP32 的参考体积比例（1.0 / 0.5 / 0.25；文档 3.2 表）。
double quantizationSizeRatio(QuantizationLevel level);

/// 平台类别（影响分级策略：桌面不做自动分级）。
enum class DevicePlatform {
    Desktop = 0,  ///< Windows / macOS / Linux
    Mobile = 1,   ///< Android / iOS
};

/// 设备探测数据（平台层采集后传入；core 只做判定）。
struct DeviceProfile {
    DevicePlatform platform = DevicePlatform::Mobile;
    /// 总内存（MB；0 = 未知）。
    int totalRamMb = 0;
    /// CPU 核心数（0 = 未知）。
    int cpuCores = 0;
    /// 是否支持 NEON/AVX 等向量加速指令集。
    bool hasVectorExtensions = false;
    /// 是否存在 NNAPI(Android) / CoreML(iOS) 硬件加速单元。
    bool hasNpuAccelerator = false;
    /// 轻量基准测试耗时（毫秒；0 = 未测。用小模型跑一次推理计时）。
    double benchmarkMs = 0.0;
};

/// 设备性能分级（文档：高/中/低三档）。
enum class DeviceTier {
    High = 0,  ///< 高性能（桌面 / 旗舰移动）
    Mid = 1,   ///< 中性能
    Low = 2,   ///< 低性能
};

/// 分级名称（"high" / "mid" / "low"）。
const char* deviceTierName(DeviceTier tier);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool deviceTierFromString(const char* text, DeviceTier& out);

/// 设备分级判定（确定性规则；文档 5.1 探测项）：
/// - 桌面：恒为 High（文档：桌面不做自动分级，默认 FP32）；
/// - 移动：
///     High = RAM >= 6144MB 且（向量指令集或 NPU）且基准 <= 800ms（未测时不否决）；
///     Mid  = RAM >= 4096MB；
///     Low  = 其余（含数据缺失的保守情形）。
/// 说明：基准未测（0）时仅按 RAM/加速单元判定（避免"未测即降级"误伤）。
DeviceTier classifyDeviceTier(const DeviceProfile& profile);

/// 量化版本推荐（文档三端对等原则）：
/// - 桌面：恒 FP32（文档：桌面默认下发 FP32；用户主动「性能模式」才切 FP16）；
/// - 移动 High：FP16；移动 Mid：FP16；移动 Low：INT8（文档 3.1 规格表）；
/// - 注意：量化只影响体积与速度，**不影响功能集合与输出质量上限**（强制原则）。
QuantizationLevel recommendedQuantization(const DeviceProfile& profile, DeviceTier tier);

/// 模型下载请求参数（平台层组装后随请求发送）。
struct ModelDownloadRequestParams {
    std::string modelId;                ///< 功能/模型 ID（服务端据此定位文件）
    DeviceTier tier = DeviceTier::High; ///< 设备分级（高/中/低）
    QuantizationLevel quantization = QuantizationLevel::Fp32;  ///< 量化版本
    /// 用户主动选择的性能模式覆盖（桌面端）：非空时覆盖推荐量化（文档 5.2）。
    bool userPerformanceMode = false;

    /// 请求摘要（供日志/诊断；不含敏感信息）。
    std::string summary() const;
};

/// 组装模型下载请求参数（单一入口；平台层调用后随 HTTP 请求发送）。
/// - profile：设备探测数据；modelId：目标模型。
/// - 说明：加密/校验机制不因量化精度而有差异（文档 5.3 分发安全性要求）——
///   本结构只影响 URL 选择，不改变 AES-256-CTR + SHA-256 流程。
ModelDownloadRequestParams buildModelDownloadRequest(const std::string& modelId,
                                                     const DeviceProfile& profile);

// =============================================================================
// 签名 URL（Token 换签名 URL；文档 8.2）
// =============================================================================

/// 请求签名 URL 的结果（服务器下发；平台层填充）。
struct SignedUrl {
    std::string url;                 ///< 下载地址（http/https）
    std::int64_t expiresAtUnix = 0;  ///< 过期时间（Unix 秒；0 = 无有效期信息）
    /// 换取该 URL 的 Token（仅审计用途；日志输出须脱敏，禁止落盘具体值）。
    std::string token;

    /// 校验（fail-closed）：URL 非空且为 http(s)，有效期存在且未过期。
    /// - nowUnix：当前时间（服务端时间优先，见文档 8.5 时间偏差校验）；
    /// - outReason：出参（可空）非法原因（可直接展示）。
    bool validAt(std::int64_t nowUnix, std::string* outReason = nullptr) const;
};

/// 剩余有效时间（秒；已过期或无有效期返回 0）。
std::int64_t signedUrlRemainingSeconds(const SignedUrl& url, std::int64_t nowUnix);

// =============================================================================
// 模型记录（已安装/损坏；供设置页与统计）
// =============================================================================

/// 单模型安装记录（平台层完成落盘后写入；core 不触碰文件系统）。
struct ModelRecord {
    std::string modelId;
    std::string version;
    std::string path;              ///< 加密模型文件路径
    std::int64_t sizeBytes = 0;    ///< 密文文件大小
    std::string sha256;            ///< 密文摘要（安装校验通过值；十六进制小写）
    std::int64_t installedAtUnix = 0;
    ModelStatus status = ModelStatus::Installed;  ///< Installed / Corrupt
    std::string note;                             ///< 损坏说明等（可直接展示）

    bool valid() const { return !modelId.empty() && sizeBytes >= 0; }
};

/// 模型记录表（设备级资产；平台层持久化，core 提供 JSON 序列化）。
struct ModelStore {
    std::vector<ModelRecord> records;

    const ModelRecord* find(const std::string& modelId) const;
    ModelRecord* find(const std::string& modelId);
    /// 写入/覆盖（同 ID 替换）。
    void upsert(ModelRecord record);
    /// 移除；返回是否移除成功（供平台层据此删除文件）。
    bool remove(const std::string& modelId);
    void clear();

    /// 已安装记录数。
    int installedCount() const;
    /// 损坏记录数。
    int corruptCount() const;
    /// 已安装模型占用总量（字节；不含损坏项）。
    std::int64_t totalSizeBytes() const;
    /// 查询某模型状态（无记录 → NotDownloaded）。
    ModelStatus statusOf(const std::string& modelId) const;
    /// 查询某模型记录（无记录返回 nullptr）。
    const ModelRecord* recordOf(const std::string& modelId) const;
};

// =============================================================================
// 完整性校验与加密存储
// =============================================================================

/// 模型载荷校验结果（安装前调用；载荷为密文）。
struct ModelVerifyResult {
    bool ok = false;             ///< 是否通过（摘要 + 大小均符合）
    bool digestMatch = false;    ///< SHA-256 是否匹配
    bool sizeMatch = true;       ///< 大小是否符合（期望为 0 时不检查）
    std::int64_t sizeBytes = 0;  ///< 实际密文长度
    std::string message;         ///< 可直接展示的结论/原因
};

/// 校验下载的密文载荷（大小 + SHA-256；expectedSha256Hex 为服务器清单值）。
/// - info.sizeBytes > 0 时校验大小；expectedSha256Hex 为空串 → 摘要检查失败（fail-closed）。
ModelVerifyResult verifyModelPayload(const ModelInfo& info,
                                     const std::vector<std::uint8_t>& ciphertext,
                                     const std::string& expectedSha256Hex);

/// 加密模型载荷（分发工具链/测试路径：明文 → 密文）。
/// - 计数器由「模型 ID + 版本」派生；密钥由服务器分发（调用方保证）。
std::vector<std::uint8_t> encryptModelPayload(const std::uint8_t key[kAes256KeySize],
                                              const std::string& modelId,
                                              const std::string& version,
                                              const std::vector<std::uint8_t>& plaintext);

/// 解密模型载荷（推理前内存解密）。
std::vector<std::uint8_t> decryptModelPayload(const std::uint8_t key[kAes256KeySize],
                                              const std::string& modelId,
                                              const std::string& version,
                                              const std::vector<std::uint8_t>& ciphertext);

/// 从偏移解密（offset = 密文起始字节；用于续传片段解密，不重算前缀）。
std::vector<std::uint8_t> decryptModelPayloadAt(const std::uint8_t key[kAes256KeySize],
                                                const std::string& modelId,
                                                const std::string& version,
                                                std::uint64_t offset,
                                                const std::vector<std::uint8_t>& ciphertext);

// =============================================================================
// 存储统计（文档 5.2「存储：可用45GB/总计128GB」）
// =============================================================================

/// 模型存储统计。
struct ModelStorageStats {
    std::int64_t deviceTotalBytes = 0;  ///< 设备总容量（平台层提供）
    std::int64_t deviceFreeBytes = 0;   ///< 设备可用空间（平台层提供）
    std::int64_t modelBytes = 0;        ///< 模型占用（已安装记录合计）
    int installedCount = 0;
    int corruptCount = 0;

    /// 是否可容纳（含安全系数；文档 3.3 采用 ×1.2 保守系数）。
    bool hasRoomFor(std::int64_t requiredBytes, double safetyFactor = 1.2) const;
    /// 展示文案（「可用 45 GB / 总计 128 GB」）。
    std::string usageText() const;
    /// 模型占用文案（「模型占用 445 MB」）。
    std::string modelText() const;
};

/// 计算存储统计（设备容量由平台层探测传入；不触碰文件系统）。
ModelStorageStats computeModelStorage(const ModelStore& store, std::int64_t deviceTotalBytes,
                                      std::int64_t deviceFreeBytes);

/// 字节数 → 友好文本（"445 MB" / "1.2 GB" / "512 KB" / "0 B"）。
std::string formatBytesText(std::int64_t bytes);

// =============================================================================
// 持久化（设备级；平台层落盘，core 只做 JSON 互转）
// =============================================================================

/// 序列化模型记录表为 JSON 文本。
std::string saveModelStoreToJson(const ModelStore& store);

/// 解析 JSON 文本（严格校验；非法记录跳过并计数）。
/// - error：出参（可空）错误/降级说明。
bool loadModelStoreFromJson(const std::string& text, ModelStore& out, std::string* error);

}  // namespace core
}  // namespace nairoclip
