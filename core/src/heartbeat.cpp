#include "nairoclip/core/heartbeat.h"

#include <cctype>
#include <cstdlib>

namespace nairoclip {
namespace core {

const char* heartbeatTriggerName(HeartbeatTrigger trigger) {
    switch (trigger) {
        case HeartbeatTrigger::AppStart: return "app_start";
        case HeartbeatTrigger::AiFeatureEntry: return "ai_feature";
        case HeartbeatTrigger::Periodic: return "periodic";
        case HeartbeatTrigger::Manual: return "manual";
        case HeartbeatTrigger::ResumeFromBackground: return "resume";
    }
    return "manual";
}

bool heartbeatTriggerFromString(const char* text, HeartbeatTrigger& out) {
    if (text == nullptr) {
        return false;
    }
    std::string s(text);
    for (auto& c : s) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (s == "app_start" || s == "start") { out = HeartbeatTrigger::AppStart; return true; }
    if (s == "ai_feature" || s == "ai") { out = HeartbeatTrigger::AiFeatureEntry; return true; }
    if (s == "periodic" || s == "timer") { out = HeartbeatTrigger::Periodic; return true; }
    if (s == "manual" || s == "refresh") { out = HeartbeatTrigger::Manual; return true; }
    if (s == "resume" || s == "background") {
        out = HeartbeatTrigger::ResumeFromBackground;
        return true;
    }
    return false;
}

const char* heartbeatVerdictName(HeartbeatVerdict verdict) {
    switch (verdict) {
        case HeartbeatVerdict::Ok: return "ok";
        case HeartbeatVerdict::Warning: return "warning";
        case HeartbeatVerdict::Locked: return "locked";
        case HeartbeatVerdict::NetworkError: return "network_error";
    }
    return "ok";
}

bool clockSkewExceeds(std::int64_t localNowUnix, std::int64_t serverTimeUnix,
                      const HeartbeatConfig& config) {
    if (!config.valid()) {
        return false;
    }
    std::int64_t diff = localNowUnix - serverTimeUnix;
    if (diff < 0) {
        diff = -diff;
    }
    return diff > config.maxClockSkewSeconds;
}

void HeartbeatState::markChecked(std::int64_t nowUnix) { lastCheckedUnix_ = nowUnix; }

bool HeartbeatState::periodicDue(std::int64_t nowUnix, std::int64_t intervalSeconds) const {
    if (intervalSeconds <= 0) {
        return false;
    }
    if (lastCheckedUnix_ == 0) {
        return true;  // 从未校验 → 立即需要
    }
    return nowUnix - lastCheckedUnix_ >= intervalSeconds;
}

HeartbeatVerdict HeartbeatState::handleResponse(const HeartbeatResponse& response,
                                                const HeartbeatConfig& config,
                                                Entitlement& entitlement,
                                                std::int64_t localNowUnix,
                                                std::string* outMessage) {
    if (!config.valid()) {
        if (outMessage != nullptr) *outMessage = "心跳配置非法";
        return HeartbeatVerdict::NetworkError;
    }
    // 网络失败：保持现状（不降级权益；不累计异常）
    if (!response.transportOk) {
        if (outMessage != nullptr) {
            *outMessage = response.message.empty() ? "网络连接失败，会员状态保持不变"
                                                   : response.message;
        }
        return HeartbeatVerdict::NetworkError;
    }
    // 服务端明确下发功能锁定
    if (response.serverLocked) {
        locked_ = true;
        lockReason_ = response.message.empty()
                          ? "服务端检测到违规使用，AI 功能已锁定（可联系客服复核）"
                          : response.message;
        if (outMessage != nullptr) *outMessage = lockReason_;
        return HeartbeatVerdict::Locked;
    }
    // 允许偏差 / 签名 / 设备数 检查（文档校验内容 2/3/4）
    bool abnormal = false;
    std::string reason;
    if (!response.signatureValid) {
        abnormal = true;
        reason = "App 签名校验未通过，请使用官方渠道下载的版本";
    } else if (response.deviceLimit > 0 && response.activeDevices > response.deviceLimit) {
        abnormal = true;
        reason = "检测到异常登录，请在其他设备退出后重试";
    } else if (clockSkewExceeds(localNowUnix, response.serverTimeUnix, config)) {
        abnormal = true;
        reason = "设备时间与服务器时间偏差过大，请校准系统时间";
    }
    if (abnormal) {
        ++warningCount_;
        if (warningCount_ >= config.warningThresholdForLock) {
            // 多次异常 → 升级为功能锁定（文档「多次、明确的违规特征」）
            locked_ = true;
            lockReason_ = reason + "（连续异常 " + std::to_string(warningCount_) + " 次）";
            if (outMessage != nullptr) *outMessage = lockReason_;
            return HeartbeatVerdict::Locked;
        }
        if (outMessage != nullptr) {
            *outMessage = reason + "（第 " + std::to_string(warningCount_) + " 次提示）";
        }
        // 异常但未锁定：权益仍按服务端下发的状态更新（给纠正机会）
        entitlement.tier = response.tier;
        entitlement.lifetime = response.lifetime;
        entitlement.expiresAtUnix = response.expiresAtUnix;
        return HeartbeatVerdict::Warning;
    }
    // ✅ 正常：更新权益（服务端权威值）+ 清零异常计数
    warningCount_ = 0;
    entitlement.tier = response.tier;
    entitlement.lifetime = response.lifetime;
    entitlement.expiresAtUnix = response.expiresAtUnix;
    lastCheckedUnix_ = localNowUnix;
    if (outMessage != nullptr) {
        *outMessage = response.message.empty() ? "会员状态已同步" : response.message;
    }
    return HeartbeatVerdict::Ok;
}

void HeartbeatState::clearLock() {
    locked_ = false;
    lockReason_.clear();
    warningCount_ = 0;
}

void HeartbeatState::reset() {
    lastCheckedUnix_ = 0;
    warningCount_ = 0;
    locked_ = false;
    lockReason_.clear();
}

}  // namespace core
}  // namespace nairoclip
