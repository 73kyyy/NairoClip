#pragma once

#include "nairoclip/core/watermark.h"

#include <cstdint>

namespace nairoclip {
namespace core {

// =============================================================================
// 会员权益（Membership Entitlement）—— 本地能力边界 + 导出强制门控
// =============================================================================
// 产品对应（文档第 6 部分「会员订阅体系」与第 7 部分「会员权益矩阵」）：
//   · 能力矩阵在本模块集中定义（单一事实来源），供 UI 置灰与导出门控使用；
//   · 使用次数计数（每月 5 次 AI 成片等）以服务端为准（文档第 8 部分），
//     本模块只描述「本机当前生效的能力边界」，不做本地计数。
//
// 安全设计（与文档第 8 部分对齐）：
//   · tier 由平台层在服务端心跳校验后更新（客户端没有可篡改的数字）；
//   · 导出门控使用 proActiveAt（到期即降级），纵深防御——
//     即使服务端未及时下发，过期会员导出时也会强制 1080p + 水印；
//   · 本模块不联网、不存储凭据。

/// 会员层级。
enum class MembershipTier {
    Free = 0,
    Pro = 1,
};

// ---- 免费版能力上限（文档 6.3.1 / 7.6 权益矩阵）----
/// 免费版最大轨道数（主轨 + 3 条 = 共 4 条）。
constexpr int kFreeMaxTracks = 4;
/// 免费版最大导出队列任务数（Pro = 5）。
constexpr int kFreeMaxExportQueue = 1;
/// 免费版导出短边上限（1080p；Pro 无限制 = 0）。
constexpr int kFreeMaxExportShortSide = 1080;
/// 免费版语音转字幕每月分钟数（Pro 无限 = 0）。
constexpr int kFreeSubtitleMinutesPerMonth = 30;
/// 免费版 AI 成片每月次数（Pro 无限 = 0）。
constexpr int kFreeAiGenerationsPerMonth = 5;

/// 会员权益快照（账号级状态；不持久化到工程文件）。
struct Entitlement {
    /// 当前层级（由平台层在服务端校验后更新）。
    MembershipTier tier = MembershipTier::Free;
    /// 永久买断（文档 3.2：不因新分级降级）。
    bool lifetime = false;
    /// 到期时间（Unix 秒；0 = 无到期信息）。
    std::int64_t expiresAtUnix = 0;

    /// 在指定时刻权益是否生效（导出/高价值功能的纵深防御判断）。
    /// - tier != Pro → false；
    /// - 永久买断 → true（不受时间影响）；
    /// - 有到期时间 → now < 到期时间；无到期信息 → true（以服务端为准）。
    bool proActiveAt(std::int64_t nowUnix) const;

    // ---- 能力查询（基于 tier 字段；0 = 无限/不适用）----
    /// 最大轨道数（Free 4 / Pro 0=无限）。
    int maxTracks() const;
    /// 最大导出队列任务数（Free 1 / Pro 5）。
    int maxExportQueue() const;
    /// 导出短边上限（Free 1080 / Pro 0=无限）。
    int maxExportShortSide() const;
    /// 导出是否需要水印（Free true / Pro false）。
    bool watermarkRequired() const;
    /// 语音转字幕每月分钟数（Free 30 / Pro 0=无限）。
    int subtitleMinutesPerMonth() const;
    /// AI 成片每月次数（Free 5 / Pro 0=无限）。
    int aiGeneratePerMonth() const;
    /// 光流补帧是否可用（Free false / Pro true）。
    bool frameInterpolation() const;
    /// 高精度抠像是否可用（Free false / Pro true）。
    bool highPrecisionMatting() const;

    /// 当前轨道数下是否允许继续新增（UI 置灰依据；文档 3.3 付费墙触发条件）。
    bool canAddTrack(int currentTrackCount) const;
};

/// 按短边上限等比缩小尺寸（不放大）。
/// - maxShortSide <= 0 或无需缩小时返回 false（outWidth/outHeight = 原值）；
/// - 缩小时保持宽高比（四舍五入取整，最小 1px）。
bool clampSizeToShortSide(int width, int height, int maxShortSide, int& outWidth, int& outHeight);

/// 导出执行策略（由权益派生；平台层构造后放入 ExportRunConfig）。
/// 默认 = 不限制、无水印（避免对未接入会员体系的调用方产生行为变化）。
struct ExportPolicy {
    /// 输出短边上限（0 = 不限制；免费版 1080）。
    int maxExportShortSide = 0;
    /// 水印（enabled = 是否逐帧附加）。
    WatermarkSpec watermark;
};

/// 由权益与当前时刻构造导出策略（文档 3.10：到期后强制降级 1080p + 水印）。
/// - Pro 生效 → 不限制、无水印；
/// - 其余（免费/过期/信息不足）→ 1080p 上限 + 水印（fail-closed）。
ExportPolicy makeExportPolicy(const Entitlement& entitlement, std::int64_t nowUnix);

}  // namespace core
}  // namespace nairoclip
