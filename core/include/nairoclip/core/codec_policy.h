#pragma once
// =============================================================================
// 编解码器合规策略（Codec Compliance Policy）
//   —— 《NairoClip 所有模型协议和其它要求》第七章「FFmpeg 编解码器协议与三端
//      规格要求」+ 第九章强制校验规则
// =============================================================================
// 产品对应（原文）：
//   · 「H.264软件编码 libx264：GPL-2.0-or-later，禁止静态链接商用」；
//   · 「H.264软件编码兜底 OpenH264：Simplified BSD + Cisco 承担专利费覆盖」；
//   · 「H.265/HEVC 编码 libx265：全线禁止使用」；
//   · 「AV1/VP9/Opus：BSD/royalty-free，推荐」；
//   · 「FFmpeg 编译参数必须锁定（禁用 --enable-gpl / --enable-nonfree），
//      核心库动态链接，经代码评审确认协议维持 LGPL 后方可合入主干」；
//   · 「全平台禁止使用 libx264/libx265」。
//
// 本模块职责：
//   · 编码器「许可分级」：把 FFmpeg 编码器名映射到许可类别（硬件 / 宽松软件 /
//     GPL 传染 / 未知）；
//   · 合规选择：按「硬件优先 → 宽松软件兜底 → （仅开发构建）GPL」的顺序，
//     从可用编码器列表中选出合规实现；找不到任何合规实现时 fail-closed
//     （返回空名 = 拒绝编码），不静默降级到 GPL；
//   · 开发构建开关：沙箱/CI 环境只有 libx264 可用，允许开发构建显式打开
//     「开发例外」（默认由 CMake 选项 NAIROCLIP_DEVELOPMENT_GPL_OK 决定；
//     发布构建必须关闭）。
//
// 与全项目架构一致：本模块为纯逻辑（零 FFmpeg 依赖），可在任意环境单测；
// FFmpegMediaWriter 在打开编码器时消费本策略。
//
// 校验清单（文档第九章）对应关系：
//   规则 1 → FFMPEG_COMPLIANCE_NOTES（docs/COMPLIANCE.md 记录编译参数与动态链接）
//   规则 2 → selectVideoEncoder（硬件优先 + OpenH264 兜底 + 禁用 libx264/x265）
//   规则 3 → 本模块白名单覆盖 AV1/VP9/Opus
//   规则 4/5 → 平台层职责（UI 选项对等 + 软解兜底），core 保证能力不因平台缩水
//   规则 6 → docs/THIRD_PARTY_NOTICES.md（随依赖自动更新）

#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 构建期开发开关（CMake 选项 NAIROCLIP_DEVELOPMENT_GPL_OK；默认 ON）。
/// 发布构建（-DNAIROCLIP_DEVELOPMENT_GPL_OK=OFF）下，EncoderPolicy 的默认值
/// 为「拒绝 GPL」，即使调用方未显式设置也合规。
#ifdef NAIROCLIP_DEVELOPMENT_GPL_OK
constexpr bool kBuildAllowsGplForDevelopment = true;
#else
constexpr bool kBuildAllowsGplForDevelopment = false;
#endif

/// 编码器许可分级（合规判定的单一事实来源）。
enum class EncoderLicenseClass {
    Hardware = 0,      ///< 系统硬件编码器（MediaCodec/VideoToolbox/MF/VAAPI/NVENC/QSV/V4L2）
    Permissive = 1,    ///< 宽松软件实现（OpenH264/libaom/libvpx/libopus/FFmpeg 内置 AAC）
    GplInfectious = 2, ///< GPL 传染（libx264/libx265 等）——禁止商用发布
    Unknown = 3,       ///< 未在清单中（保守视为不可用）
};

/// 许可分级名称（"hardware" / "permissive" / "gpl" / "unknown"；供日志/UI）。
const char* encoderLicenseClassName(EncoderLicenseClass cls);

/// 编码器可用性描述（平台/后端探测结果）。
struct EncoderCandidate {
    /// FFmpeg 编码器名（如 "h264_videotoolbox" / "openh264" / "libx264"）。
    std::string name;
    /// 编解码格式（"h264" / "av1" / "vp9" / "aac" / "opus"）。
    std::string codec;
    /// 是否硬件实现（平台探测；如 avcodec 报告的 hw_configs 非空）。
    bool hardware = false;
};

/// 按名称判定许可分级（含硬件编码器识别；未知名称 → Unknown）。
EncoderLicenseClass classifyEncoderLicense(const std::string& encoderName);

/// 编码策略（默认值 = 合规首选）。
struct EncoderPolicy {
    /// 开发例外：允许选择 GPL 编码器（仅开发构建可用；发布构建置 false）。
    /// 说明：默认跟随构建期开关（kBuildAllowsGplForDevelopment）——沙箱/CI
    /// 只有 libx264 时测试可跑；发布构建默认合规（fail-closed）。
    bool allowGplForDevelopment = kBuildAllowsGplForDevelopment;
    /// 是否优先硬件编码器（文档：三端统一优先级最高）。恒为 true 时才尝试硬件；
    /// 提供开关仅供平台层在特定设备上强制软件路径（如硬件编码器有已知缺陷）。
    bool preferHardware = true;
};

/// 视频编码器选择结果。
struct EncoderSelection {
    /// 选中的编码器名（空 = 无合规实现，调用方必须拒绝编码）。
    std::string name;
    /// 选中项的许可分级（供日志/诊断）。
    EncoderLicenseClass license = EncoderLicenseClass::Unknown;
    /// 是否由「开发例外」放行（GPL 编码器；须在日志中显著记录）。
    bool usedDevelopmentException = false;
    /// 说明（可直接展示/记录）。
    std::string note;

    bool valid() const { return !name.empty(); }
};

/// 选择视频编码器（合规优先序）：
///   1. 硬件实现（文档：H.264 硬件编码三端统一优先级最高）；
///   2. 宽松软件实现（OpenH264 / libaom / libsvpx 等）；
///   3. GPL 实现 —— 仅当 policy.allowGplForDevelopment（开发构建/开发模式）；
///   4. 都没有 → 返回 invalid（fail-closed，绝不静默用 GPL）。
/// - codec：目标格式（"h264" 默认；"av1"/"vp9" 为 royalty-free 选项）；
/// - available：后端探测到的可用编码器列表。
/// 说明：同优先级内按「平台常见度」固定顺序（不依赖列表输入顺序，保证确定性）。
EncoderSelection selectVideoEncoder(const std::string& codec,
                                    const std::vector<EncoderCandidate>& available,
                                    const EncoderPolicy& policy = {});

/// 视频编码器「有序候选序列」（同一合规优先级内展开；供调用方逐个尝试打开）。
/// 用途：硬件编码器可能因驱动缺失而打开失败（如无 GPU 的 CI/沙箱上
/// h264_nvenc 报 "Cannot load libcuda.so.1"），此时按序回退到下一个合规候选；
/// 序列中的每一项都是"若可用即可接受"的实现，且遵守同一 fail-closed 规则
/// （GPL 只在开发例外下出现）。
std::vector<EncoderSelection> rankVideoEncoderCandidates(
    const std::string& codec, const std::vector<EncoderCandidate>& available,
    const EncoderPolicy& policy = {});

/// 选择音频编码器（合规优先序：硬件 → 宽松软件；AAC 内置实现视为宽松）。
/// - codec："aac"（默认）或 "opus"。
EncoderSelection selectAudioEncoder(const std::string& codec,
                                    const std::vector<EncoderCandidate>& available,
                                    const EncoderPolicy& policy = {});

/// 该编码器名是否允许在本策略下使用（供平台层在自建管线中复用判定）。
bool encoderAllowed(const std::string& encoderName, const EncoderPolicy& policy = {});

/// 合规说明文案（供「关于-开源软件许可」与文档引用）。
const char* codecComplianceStatement();

}  // namespace core
}  // namespace nairoclip
