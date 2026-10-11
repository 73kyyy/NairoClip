// =============================================================================
// 会员心跳校验测试（M2.26，文档第 8 部分「五、会员心跳校验具体流程」）
// =============================================================================
// 覆盖：
//   · 触发调度：启动/功能入口/周期到期判定；
//   · 分级处理：Ok / Warning（给纠正机会）/ Locked（多次升级）/ NetworkError（保持现状）；
//   · 时钟偏差：超限判定（防改时间绕过到期）；
//   · 锁定与解除：连续异常升级、clearLock/reset 恢复；
//   · 权益更新：正常时按服务端权威值更新；网络失败不降级。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/heartbeat.h"

#include <string>

using namespace nairoclip::core;

namespace {

HeartbeatResponse okResponse(std::int64_t serverTime = 100000) {
    HeartbeatResponse response;
    response.transportOk = true;
    response.serverTimeUnix = serverTime;
    response.tier = MembershipTier::Pro;
    response.expiresAtUnix = serverTime + 86400 * 30;
    response.activeDevices = 1;
    response.deviceLimit = 3;
    response.signatureValid = true;
    return response;
}

}  // namespace

// ---------------------------------------------------------------------------
// 名称与触发
// ---------------------------------------------------------------------------

NC_TEST(Heartbeat, TriggerNameRoundTrip) {
    NC_CHECK_EQ(std::string(heartbeatTriggerName(HeartbeatTrigger::AppStart)),
                std::string("app_start"));
    NC_CHECK_EQ(std::string(heartbeatTriggerName(HeartbeatTrigger::AiFeatureEntry)),
                std::string("ai_feature"));
    NC_CHECK_EQ(std::string(heartbeatTriggerName(HeartbeatTrigger::Periodic)),
                std::string("periodic"));
    for (const auto* name : {"app_start", "ai_feature", "periodic", "manual", "resume"}) {
        HeartbeatTrigger trigger;
        NC_CHECK(heartbeatTriggerFromString(name, trigger));
    }
    HeartbeatTrigger trigger;
    NC_CHECK(!heartbeatTriggerFromString("bogus", trigger));
    NC_CHECK(!heartbeatTriggerFromString(nullptr, trigger));
}

NC_TEST(Heartbeat, VerdictNames) {
    NC_CHECK_EQ(std::string(heartbeatVerdictName(HeartbeatVerdict::Ok)), std::string("ok"));
    NC_CHECK_EQ(std::string(heartbeatVerdictName(HeartbeatVerdict::Warning)),
                std::string("warning"));
    NC_CHECK_EQ(std::string(heartbeatVerdictName(HeartbeatVerdict::Locked)), std::string("locked"));
    NC_CHECK_EQ(std::string(heartbeatVerdictName(HeartbeatVerdict::NetworkError)),
                std::string("network_error"));
}

NC_TEST(Heartbeat, PeriodicDue) {
    HeartbeatState state;
    // 从未校验 → 立即到期
    NC_CHECK(state.periodicDue(1000));
    state.markChecked(1000);
    NC_CHECK_EQ(state.lastCheckedUnix(), std::int64_t(1000));
    // 未到 30 分钟 → 不触发
    NC_CHECK(!state.periodicDue(1000 + 1799));
    // 到期
    NC_CHECK(state.periodicDue(1000 + 1800));
    NC_CHECK(state.periodicDue(1000 + 2000));
}

// ---------------------------------------------------------------------------
// 分级处理
// ---------------------------------------------------------------------------

NC_TEST(Heartbeat, NormalResponseUpdatesEntitlement) {
    HeartbeatState state;
    Entitlement entitlement;  // 默认 Free
    std::string message;
    const HeartbeatVerdict verdict = state.handleResponse(
        okResponse(100000), HeartbeatConfig{}, entitlement, 100000, &message);
    NC_CHECK_EQ(verdict, HeartbeatVerdict::Ok);
    NC_CHECK_EQ(entitlement.tier, MembershipTier::Pro);
    NC_CHECK(entitlement.expiresAtUnix > 0);
    NC_CHECK(!message.empty());
    NC_CHECK(!state.locked());
    NC_CHECK_EQ(state.warningCount(), 0);
    NC_CHECK_EQ(state.lastCheckedUnix(), std::int64_t(100000));
}

NC_TEST(Heartbeat, NetworkErrorKeepsState) {
    HeartbeatState state;
    Entitlement entitlement;
    entitlement.tier = MembershipTier::Pro;  // 已有 Pro
    entitlement.expiresAtUnix = 200000;
    HeartbeatResponse response;
    response.transportOk = false;
    std::string message;
    const HeartbeatVerdict verdict =
        state.handleResponse(response, HeartbeatConfig{}, entitlement, 100000, &message);
    NC_CHECK_EQ(verdict, HeartbeatVerdict::NetworkError);
    // 权益不变（不降级）
    NC_CHECK_EQ(entitlement.tier, MembershipTier::Pro);
    NC_CHECK_EQ(entitlement.expiresAtUnix, std::int64_t(200000));
    NC_CHECK(!state.locked());
    NC_CHECK_EQ(state.warningCount(), 0);  // 网络失败不计异常
    NC_CHECK(!message.empty());
}

NC_TEST(Heartbeat, WarningGivesChanceToCorrect) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse response = okResponse(100000);
    response.activeDevices = 5;  // 超出上限 3
    std::string message;
    const HeartbeatVerdict verdict =
        state.handleResponse(response, HeartbeatConfig{}, entitlement, 100000, &message);
    NC_CHECK_EQ(verdict, HeartbeatVerdict::Warning);
    NC_CHECK(!state.locked());  // 首次异常不锁定
    NC_CHECK_EQ(state.warningCount(), 1);
    NC_CHECK(message.find("异常登录") != std::string::npos);
    NC_CHECK(message.find("第 1 次") != std::string::npos);
    // 权益仍按服务端下发更新（给纠正机会）
    NC_CHECK_EQ(entitlement.tier, MembershipTier::Pro);
}

NC_TEST(Heartbeat, WarningsEscalateToLock) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse response = okResponse(100000);
    response.activeDevices = 5;
    HeartbeatConfig config;  // 阈值 3
    std::string message;
    NC_CHECK_EQ(state.handleResponse(response, config, entitlement, 100000, &message),
                HeartbeatVerdict::Warning);
    NC_CHECK_EQ(state.handleResponse(response, config, entitlement, 100001, &message),
                HeartbeatVerdict::Warning);
    // 第 3 次 → 锁定
    NC_CHECK_EQ(state.handleResponse(response, config, entitlement, 100002, &message),
                HeartbeatVerdict::Locked);
    NC_CHECK(state.locked());
    NC_CHECK_EQ(state.warningCount(), 3);
    NC_CHECK(!state.lockReason().empty());
    NC_CHECK(state.lockReason().find("连续异常") != std::string::npos);
}

NC_TEST(Heartbeat, ServerLockedImmediately) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse response = okResponse();
    response.serverLocked = true;
    response.message = "检测到破解特征";
    std::string message;
    NC_CHECK_EQ(state.handleResponse(response, HeartbeatConfig{}, entitlement, 100000, &message),
                HeartbeatVerdict::Locked);
    NC_CHECK(state.locked());
    NC_CHECK_EQ(state.lockReason(), std::string("检测到破解特征"));
    NC_CHECK_EQ(state.warningCount(), 0);  // 服务端明确锁定不依赖本地计数
}

NC_TEST(Heartbeat, SignatureInvalidWarns) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse response = okResponse();
    response.signatureValid = false;
    std::string message;
    NC_CHECK_EQ(state.handleResponse(response, HeartbeatConfig{}, entitlement, 100000, &message),
                HeartbeatVerdict::Warning);
    NC_CHECK(message.find("签名") != std::string::npos);
}

NC_TEST(Heartbeat, RecoveringFromWarningResetsCount) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse bad = okResponse(100000);
    bad.activeDevices = 5;
    std::string message;
    // 两次异常后恢复正常
    state.handleResponse(bad, HeartbeatConfig{}, entitlement, 100000, &message);
    state.handleResponse(bad, HeartbeatConfig{}, entitlement, 100001, &message);
    NC_CHECK_EQ(state.warningCount(), 2);
    NC_CHECK_EQ(state.handleResponse(okResponse(100002), HeartbeatConfig{}, entitlement, 100002,
                                     &message),
                HeartbeatVerdict::Ok);
    NC_CHECK_EQ(state.warningCount(), 0);  // 恢复正常 → 清零
    NC_CHECK(!state.locked());
}

// ---------------------------------------------------------------------------
// 时钟偏差
// ---------------------------------------------------------------------------

NC_TEST(Heartbeat, ClockSkewDetection) {
    HeartbeatConfig config;  // 允许 300 秒
    // 在容差内
    NC_CHECK(!clockSkewExceeds(100000, 100000, config));
    NC_CHECK(!clockSkewExceeds(100250, 100000, config));
    NC_CHECK(!clockSkewExceeds(99750, 100000, config));
    // 超限（本地时间快 → 可能改时间绕过到期）
    NC_CHECK(clockSkewExceeds(100301, 100000, config));
    // 超限（本地时间慢）
    NC_CHECK(clockSkewExceeds(99699, 100000, config));
    // 0 容差（严格模式）：任何偏差都判定为异常
    HeartbeatConfig strict;
    strict.maxClockSkewSeconds = 0;
    NC_CHECK(strict.valid());  // 合法配置（严格模式）
    NC_CHECK(!clockSkewExceeds(100000, 100000, strict));  // 无偏差 → 正常
    NC_CHECK(clockSkewExceeds(100001, 100000, strict));
    // 更大容差 → 更宽松
    HeartbeatConfig loose;
    loose.maxClockSkewSeconds = 3600;
    NC_CHECK(!clockSkewExceeds(100000 + 3599, 100000, loose));
    NC_CHECK(clockSkewExceeds(100000 + 3601, 100000, loose));
}

NC_TEST(Heartbeat, ClockSkewWarnsThroughResponse) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse response = okResponse(100000);
    std::string message;
    // 本地时间比服务器快 1 小时（模拟用户改时间）
    NC_CHECK_EQ(state.handleResponse(response, HeartbeatConfig{}, entitlement, 100000 + 3600,
                                     &message),
                HeartbeatVerdict::Warning);
    NC_CHECK(message.find("时间") != std::string::npos);
}

// ---------------------------------------------------------------------------
// 锁定与恢复
// ---------------------------------------------------------------------------

NC_TEST(Heartbeat, ClearLockAndReset) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse response = okResponse();
    response.serverLocked = true;
    std::string message;
    state.handleResponse(response, HeartbeatConfig{}, entitlement, 1000, &message);
    NC_CHECK(state.locked());
    state.clearLock();
    NC_CHECK(!state.locked());
    NC_CHECK_EQ(state.warningCount(), 0);
    NC_CHECK(state.lockReason().empty());
    // reset 清理全部状态
    state.markChecked(5000);
    state.reset();
    NC_CHECK_EQ(state.lastCheckedUnix(), std::int64_t(0));
    NC_CHECK(!state.locked());
    NC_CHECK(state.periodicDue(6000));  // 从未校验 → 立即到期
}

NC_TEST(Heartbeat, InvalidConfigRejected) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatConfig bad;
    bad.warningThresholdForLock = 0;
    std::string message;
    NC_CHECK_EQ(state.handleResponse(okResponse(), bad, entitlement, 100000, &message),
                HeartbeatVerdict::NetworkError);
    NC_CHECK(!state.locked());
}

NC_TEST(Heartbeat, DeviceLimitDisabledWhenZero) {
    HeartbeatState state;
    Entitlement entitlement;
    HeartbeatResponse response = okResponse();
    response.activeDevices = 99;
    response.deviceLimit = 0;  // 未提供上限 → 不判定
    std::string message;
    NC_CHECK_EQ(state.handleResponse(response, HeartbeatConfig{}, entitlement, 100000, &message),
                HeartbeatVerdict::Ok);
}
