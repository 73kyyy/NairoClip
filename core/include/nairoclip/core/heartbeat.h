#pragma once
// =============================================================================
// 会员心跳校验（Membership Heartbeat）—— 文档第 8 部分「五、会员心跳校验具体流程」
// =============================================================================
// 产品对应（原文）：
//   触发时机：① App 启动时 ② 用户点击任意 AI 功能入口时 ③ 每 30 分钟自动触发
//   校验内容：1. 会员状态是否在有效期内
//             2. 本地设备时间与服务器时间是否存在异常偏差（防改时间绕过到期）
//             3. 同一账号活跃设备数是否超出套餐上限
//             4. App 签名是否与官方发布版本一致（加固 SDK / App Attest）
//   结果处理：✅ 正常 → 放行
//             ⚠️ 异常（如多设备超限）→ 弹窗提示，给纠正机会（不直接封号）
//             🚫 确认违规 → 服务器下发功能锁定指令，拒绝本次及后续 AI 请求
//   设计原则：不要一刀切封号；第一次异常是「提示 + 纠正机会」，
//             只有多次、明确的违规特征才升级为功能锁定。
//
// 本模块定位（客户端状态机，零网络依赖）：
//   · core 不发起网络请求（那是平台层职责）；本模块提供：
//     · 触发调度：何时应发起心跳（启动/功能入口/30 分钟周期）；
//     · 响应处理：把服务端校验结果映射为本地权益状态与提示（分级处理）；
//     · 异常计数与升级：多次异常后才进入功能锁定（防误伤）；
//     · 时间偏差判定：服务端时间与本地时间的偏差检测（防改时间）。
//   · 平台层职责：真实 HTTP 请求 + 签名校验（加固 SDK / App Attest）+ 展示提示。

#include "nairoclip/core/entitlement.h"

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {

/// 心跳触发原因（文档「触发时机」）。
enum class HeartbeatTrigger {
    AppStart = 0,     ///< ① App 启动
    AiFeatureEntry = 1,  ///< ② 用户点击 AI 功能入口
    Periodic = 2,     ///< ③ 30 分钟周期
    Manual = 3,       ///< 用户主动刷新（设置页「检查会员状态」）
    ResumeFromBackground = 4,  ///< 从后台恢复（平台层可选触发）
};

/// 触发原因名称（"app_start"/"ai_feature"/"periodic"/"manual"/"resume"）。
const char* heartbeatTriggerName(HeartbeatTrigger trigger);
/// 从字符串解析（大小写不敏感；非法返回 false）。
bool heartbeatTriggerFromString(const char* text, HeartbeatTrigger& out);

/// 服务端心跳响应（平台层填充；客户端不构造）。
struct HeartbeatResponse {
    /// HTTP 层是否成功（网络错误 = false；此时不应改变权益状态）。
    bool transportOk = false;
    /// 服务端时间（Unix 秒；用于时间偏差判定）。
    std::int64_t serverTimeUnix = 0;
    /// 会员状态（服务端权威值；tier/expires/lifetime 三项）。
    MembershipTier tier = MembershipTier::Free;
    bool lifetime = false;
    std::int64_t expiresAtUnix = 0;
    /// 活跃设备数（文档校验内容 3）。
    int activeDevices = 0;
    /// 套餐允许的设备数上限（0 = 未提供）。
    int deviceLimit = 0;
    /// App 签名是否一致（文档校验内容 4；由加固 SDK / App Attest 判定）。
    bool signatureValid = true;
    /// 服务端是否明确下发「功能锁定」指令（文档 🚫）。
    bool serverLocked = false;
    /// 服务端附带的说明文案（可直接展示；可空）。
    std::string message;
};

/// 心跳判定结果（客户端分级处理）。
enum class HeartbeatVerdict {
    Ok = 0,          ///< ✅ 正常 → 放行
    Warning = 1,     ///< ⚠️ 异常 → 提示 + 给纠正机会（不封号）
    Locked = 2,      ///< 🚫 确认违规 → 功能锁定
    NetworkError = 3,  ///< 网络失败 → 保持现状（不降级权益）
};

/// 判定结果名称（"ok"/"warning"/"locked"/"network_error"）。
const char* heartbeatVerdictName(HeartbeatVerdict verdict);

/// 心跳处理配置。
struct HeartbeatConfig {
    /// 异常计数达到该阈值后升级为功能锁定（文档「多次、明确」）。
    int warningThresholdForLock = 3;
    /// 本地时间与服务器时间的允许偏差（秒；超过视为异常，文档校验内容 2）。
    std::int64_t maxClockSkewSeconds = 300;

    bool valid() const { return warningThresholdForLock >= 1 && maxClockSkewSeconds >= 0; }
};

/// 心跳客户端状态机（平台层持有；单实例与其编辑器句柄对应）。
class HeartbeatState {
public:
    /// 记录一次心跳的时间（触发周期判定）。
    void markChecked(std::int64_t nowUnix);
    /// 上次心跳时间（0 = 从未）。
    std::int64_t lastCheckedUnix() const { return lastCheckedUnix_; }
    /// 周期触发是否到期（文档「每 30 分钟」；intervalSeconds 由平台层传入）。
    bool periodicDue(std::int64_t nowUnix, std::int64_t intervalSeconds = 1800) const;

    /// 处理服务端响应：更新权益、累计异常、判定分级。
    /// - entitlement：入出参（按响应更新 tier/expires/lifetime）；
    /// - outMessage：出参（可空）展示文案；
    /// - 返回判定结果。transportOk=false 时返回 NetworkError 且不改权益。
    HeartbeatVerdict handleResponse(const HeartbeatResponse& response,
                                    const HeartbeatConfig& config, Entitlement& entitlement,
                                    std::int64_t localNowUnix, std::string* outMessage);

    /// 当前是否被功能锁定（平台层在 AI 入口处检查）。
    bool locked() const { return locked_; }
    /// 连续异常计数（用于 UI 诊断）。
    int warningCount() const { return warningCount_; }
    /// 解除锁定（如用户完成账号验证后由平台层调用）。
    void clearLock();
    /// 重置（切换账号/退出登录）。
    void reset();

    /// 锁定时给用户看的原因（可空）。
    const std::string& lockReason() const { return lockReason_; }

private:
    std::int64_t lastCheckedUnix_ = 0;
    int warningCount_ = 0;
    bool locked_ = false;
    std::string lockReason_;
};

/// 判定时钟偏差是否超限（本地时间 - 服务端时间；绝对值）。
/// - 返回 true = 偏差异常（应提示用户校准时间，防止改时间绕过到期）。
bool clockSkewExceeds(std::int64_t localNowUnix, std::int64_t serverTimeUnix,
                      const HeartbeatConfig& config = {});

}  // namespace core
}  // namespace nairoclip
