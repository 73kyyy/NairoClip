// =============================================================================
// 编解码器合规策略测试（M2.29，文档第七章「FFmpeg 编解码器协议与三端规格要求」
// + 第九章强制校验规则）
// =============================================================================
// 覆盖：
//   · 许可分级：硬件编码器识别、宽松白名单（OpenH264/AV1/VP9/Opus）、
//     GPL 传染识别（libx264/libx265/libxvid/libfdk_aac）、未知保守；
//   · 视频选择：硬件优先、宽松优先于 GPL、HEVC 全线禁用、fail-closed；
//   · 开发例外：仅开发构建允许 GPL，且标记 usedDevelopmentException；
//   · 候选序列：硬件 → 宽松 → GPL（开发例外）的展开顺序与确定性；
//   · 音频选择：内置 AAC/Opus 宽松；libfdk_aac（不可分发）拒绝；
//   · encoderAllowed 代理判定。
// =============================================================================
#include "test_framework.h"

#include "nairoclip/core/codec_policy.h"

#include <string>
#include <vector>

using namespace nairoclip::core;

namespace {

EncoderCandidate hw(const char* name, const char* codec) {
    return EncoderCandidate{name, codec, true};
}
EncoderCandidate sw(const char* name, const char* codec) {
    return EncoderCandidate{name, codec, false};
}

}  // namespace

// ---------------------------------------------------------------------------
// 许可分级
// ---------------------------------------------------------------------------

NC_TEST(CodecPolicy, LicenseClassification) {
    // GPL 传染（文档第七章禁用清单）
    NC_CHECK(classifyEncoderLicense("libx264") == EncoderLicenseClass::GplInfectious);
    NC_CHECK(classifyEncoderLicense("libx264rgb") == EncoderLicenseClass::GplInfectious);
    NC_CHECK(classifyEncoderLicense("libx265") == EncoderLicenseClass::GplInfectious);
    NC_CHECK(classifyEncoderLicense("libxvid") == EncoderLicenseClass::GplInfectious);
    NC_CHECK(classifyEncoderLicense("libfdk_aac") == EncoderLicenseClass::GplInfectious);
    // 硬件（三端系统编码器）
    NC_CHECK(classifyEncoderLicense("h264_videotoolbox") == EncoderLicenseClass::Hardware);
    NC_CHECK(classifyEncoderLicense("h264_mediacodec") == EncoderLicenseClass::Hardware);
    NC_CHECK(classifyEncoderLicense("h264_nvenc") == EncoderLicenseClass::Hardware);
    NC_CHECK(classifyEncoderLicense("hevc_qsv") == EncoderLicenseClass::Hardware);
    NC_CHECK(classifyEncoderLicense("h264_vaapi") == EncoderLicenseClass::Hardware);
    NC_CHECK(classifyEncoderLicense("h264_mf") == EncoderLicenseClass::Hardware);
    /* 软件 H.264（2026-11-05 变更）：不再是 Permissive，改为独立分级。
       它许可宽松但**AVC 专利要自付**，与 AV1/VP9 的 royalty-free 性质不同。 */
    NC_CHECK(classifyEncoderLicense("openh264") == EncoderLicenseClass::SoftwareH264);
    NC_CHECK(classifyEncoderLicense("h264") == EncoderLicenseClass::SoftwareH264);
    NC_CHECK(classifyEncoderLicense("libopenh264") == EncoderLicenseClass::SoftwareH264);
    // 宽松软件（**仅非 H.264**）
    NC_CHECK(classifyEncoderLicense("libsvtav1") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("libaom-av1") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("libvpx-vp9") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("libopus") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("aac") == EncoderLicenseClass::Permissive);
    /* "x264_custom" 含 264 且不是硬件 → 归为 SoftwareH264（保守禁用）。
       这比 Unknown 更准确：Unknown 是「不认识」，SoftwareH264 是
       「认识但禁止使用」，后者能给出更有用的提示。 */
    NC_CHECK(classifyEncoderLicense("x264_custom") == EncoderLicenseClass::SoftwareH264);
    NC_CHECK(classifyEncoderLicense("mystery_codec") == EncoderLicenseClass::Unknown);
    NC_CHECK(classifyEncoderLicense("") == EncoderLicenseClass::Unknown);
    // 名称
    NC_CHECK_EQ(std::string(encoderLicenseClassName(EncoderLicenseClass::Hardware)),
                std::string("hardware"));
    NC_CHECK_EQ(std::string(encoderLicenseClassName(EncoderLicenseClass::GplInfectious)),
                std::string("gpl"));
}

// ---------------------------------------------------------------------------
// 视频编码器选择
// ---------------------------------------------------------------------------

NC_TEST(CodecPolicy, HardwarePreferred) {
    const std::vector<EncoderCandidate> available = {
        sw("libx264", "h264"),
        hw("h264_videotoolbox", "h264"),
        sw("openh264", "h264"),
    };
    EncoderPolicy policy;
    policy.allowGplForDevelopment = false;  // 纯净模式
    const EncoderSelection selection = selectVideoEncoder("h264", available, policy);
    NC_CHECK(selection.valid());
    NC_CHECK_EQ(selection.name, std::string("h264_videotoolbox"));
    NC_CHECK(selection.license == EncoderLicenseClass::Hardware);
    NC_CHECK(!selection.usedDevelopmentException);
}

NC_TEST(CodecPolicy, SoftwareH264NeverPreferredOverGpl) {
    /* 候选只有 libx264（GPL）与 openh264（软件 H.264）时，
       开发构建会选 **libx264**，不是 openh264。
       ------------------------------------------------------------------
       反直觉但正确：两者都要自付专利费（libx264 是 GPL + 专利，
       openh264 是专利），而 GPL 的风险只是「不能闭源分发」——
       在**开发构建**里本来就不分发。软件 H.264 排在 GPL 之后，
       因为它的唯一价值是「本机能跑测试」，而 libx264 同样能跑且更快。

       发布构建里两者都不可用（见下一个测试）。 */
    const std::vector<EncoderCandidate> available = {
        sw("libx264", "h264"),
        sw("openh264", "h264"),
    };
    EncoderPolicy policy;
    policy.allowGplForDevelopment = true;
    policy.allowSoftwareH264ForDevelopment = true;
    const EncoderSelection selection = selectVideoEncoder("h264", available, policy);
    NC_CHECK(selection.valid());
    NC_CHECK_EQ(selection.name, std::string("libx264"));
    NC_CHECK(selection.license == EncoderLicenseClass::GplInfectious);
    NC_CHECK(selection.usedDevelopmentException);
}

NC_TEST(CodecPolicy, SoftwareH264DeniedInRelease) {
    /* 发布构建：即使 allowSoftwareH264ForDevelopment 被误设为 true，
       软件 H.264 也不可用 —— 因为还要过 buildAllowsGplForDevelopment() 这一关。
       本测试在开发构建下无法验证「构建期开关为假」的场景，
       但至少验证「开关为 false 时确实不选」。 */
    const std::vector<EncoderCandidate> available = {sw("openh264", "h264")};
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    release.allowSoftwareH264ForDevelopment = false;
    const EncoderSelection selection = selectVideoEncoder("h264", available, release);
    NC_CHECK(!selection.valid());  // 没有可用编码器 → fail-closed

    // 打开开关但构建期不允许时（发布构建的实际情况）仍然拒绝
    release.allowSoftwareH264ForDevelopment = true;
    const EncoderSelection forced = selectVideoEncoder("h264", available, release);
    if (!buildAllowsGplForDevelopment()) {
        NC_CHECK(!forced.valid());
    }
}

NC_TEST(CodecPolicy, FailClosedWithoutCompliantEncoder) {
    // 发布模式：仅有 libx264 → 拒绝（fail-closed，绝不静默用 GPL）
    const std::vector<EncoderCandidate> available = {sw("libx264", "h264")};
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    const EncoderSelection selection = selectVideoEncoder("h264", available, release);
    NC_CHECK(!selection.valid());
    NC_CHECK(!selection.note.empty());
    // 开发例外：允许选择但必须显著标记
    EncoderPolicy dev;
    dev.allowGplForDevelopment = true;
    const EncoderSelection devSelection = selectVideoEncoder("h264", available, dev);
    NC_CHECK(devSelection.valid());
    NC_CHECK_EQ(devSelection.name, std::string("libx264"));
    NC_CHECK(devSelection.usedDevelopmentException);
    NC_CHECK(devSelection.license == EncoderLicenseClass::GplInfectious);
}

NC_TEST(CodecPolicy, HevcDeniedEntirely) {
    // HEVC 全线禁止（不论软件/硬件/开发例外）
    const std::vector<EncoderCandidate> available = {
        hw("hevc_videotoolbox", "hevc"),
        hw("hevc_nvenc", "hevc"),
        sw("libx265", "hevc"),
    };
    for (bool dev : {false, true}) {
        EncoderPolicy policy;
        policy.allowGplForDevelopment = dev;
        const EncoderSelection selection = selectVideoEncoder("hevc", available, policy);
        NC_CHECK(!selection.valid());  // 任何模式下都拒绝
        NC_CHECK(selection.note.find("H.265") != std::string::npos ||
                 selection.note.find("HEVC") != std::string::npos);
    }
    // 别名同等拒绝
    NC_CHECK(!selectVideoEncoder("h265", available, EncoderPolicy{}).valid());
    NC_CHECK(!selectVideoEncoder("libx265", available, EncoderPolicy{}).valid());
}

NC_TEST(CodecPolicy, RoyaltyFreeCodecsAllowed) {
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    // AV1：SVT-AV1（BSD）
    const auto av1 = selectVideoEncoder("av1", {sw("libsvtav1", "av1"), sw("libaom-av1", "av1")},
                                        release);
    NC_CHECK(av1.valid());
    NC_CHECK_EQ(av1.name, std::string("libsvtav1"));
    // VP9：libvpx（BSD）
    const auto vp9 = selectVideoEncoder("vp9", {sw("libvpx-vp9", "vp9")}, release);
    NC_CHECK(vp9.valid());
    NC_CHECK_EQ(vp9.name, std::string("libvpx-vp9"));
}

NC_TEST(CodecPolicy, UnknownEncoderSkipped) {
    // 未知实现（不在清单）→ 保守跳过，不参与选择
    const std::vector<EncoderCandidate> available = {
        sw("mystery_codec", "h264"),
        hw("h264_mediacodec", "h264"),   // 用硬件项做「可用项」
    };
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    const EncoderSelection selection = selectVideoEncoder("h264", available, release);
    NC_CHECK(selection.valid());
    NC_CHECK_EQ(selection.name, std::string("h264_mediacodec"));
    // 全部未知 → 拒绝
    const EncoderSelection none =
        selectVideoEncoder("h264", {sw("mystery_codec", "h264")}, release);
    NC_CHECK(!none.valid());
}

// ---------------------------------------------------------------------------
// 候选序列（逐个尝试打开的回退顺序）
// ---------------------------------------------------------------------------

NC_TEST(CodecPolicy, CandidateRankingOrder) {
    /* 完整顺序（2026-11-05 策略）：
         硬件 → Permissive(AV1/VP9) → GPL(开发) → 软件H264(开发最后)
       软件 H.264 排**最后**：它是「本机没别的可用时」的应急手段。 */
    const std::vector<EncoderCandidate> available = {
        sw("libx264", "h264"),
        hw("h264_nvenc", "h264"),
        sw("openh264", "h264"),
        hw("h264_videotoolbox", "h264"),
    };
    EncoderPolicy dev;
    dev.allowGplForDevelopment = true;
    dev.allowSoftwareH264ForDevelopment = true;
    const auto ranked = rankVideoEncoderCandidates("h264", available, dev);
    NC_CHECK_EQ(ranked.size(), std::size_t(4));
    NC_CHECK_EQ(ranked[0].name, std::string("h264_videotoolbox"));
    NC_CHECK_EQ(ranked[1].name, std::string("h264_nvenc"));
    NC_CHECK_EQ(ranked[2].name, std::string("libx264"));
    NC_CHECK_EQ(ranked[3].name, std::string("openh264"));
    NC_CHECK(ranked[2].usedDevelopmentException);
    NC_CHECK(ranked[3].usedDevelopmentException);

    // 发布模式：GPL 与软件 H.264 都不出现在候选序列中
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    release.allowSoftwareH264ForDevelopment = false;
    const auto rankedRelease = rankVideoEncoderCandidates("h264", available, release);
    NC_CHECK_EQ(rankedRelease.size(), std::size_t(2));  // 只剩两个硬件
    for (const auto& candidate : rankedRelease) {
        NC_CHECK(candidate.license == EncoderLicenseClass::Hardware);
    }

    /* 软件 H.264 排在 GPL **之后**的验证：
       只给这两个候选，且两者都放开 → 应选 libx264。 */
    const auto twoOnly = rankVideoEncoderCandidates(
        "h264", {sw("openh264", "h264"), sw("libx264", "h264")}, dev);
    NC_CHECK_EQ(twoOnly.size(), std::size_t(2));
    NC_CHECK_EQ(twoOnly[0].name, std::string("libx264"));

    // HEVC → 空序列
    NC_CHECK(rankVideoEncoderCandidates("hevc", available, release).empty());
}

// ---------------------------------------------------------------------------
// 音频编码器
// ---------------------------------------------------------------------------

NC_TEST(CodecPolicy, AudioEncoderSelection) {
    // 内置 AAC（宽松）可用
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    const auto aac = selectAudioEncoder("aac", {sw("aac", "aac")}, release);
    NC_CHECK(aac.valid());
    NC_CHECK_EQ(aac.name, std::string("aac"));
    // libfdk_aac（不可分发）→ 拒绝
    const auto fdk = selectAudioEncoder("aac", {sw("libfdk_aac", "aac")}, release);
    NC_CHECK(!fdk.valid());
    // Opus（BSD）可用
    const auto opus = selectAudioEncoder("opus", {sw("libopus", "opus")}, release);
    NC_CHECK(opus.valid());
    NC_CHECK_EQ(opus.name, std::string("libopus"));
}

// ---------------------------------------------------------------------------
// 代理判定与合规文案
// ---------------------------------------------------------------------------

NC_TEST(CodecPolicy, EncoderAllowedProxy) {
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    NC_CHECK(encoderAllowed("h264_videotoolbox", release));
    /* 软件 H.264 在任何构建下都不允许 —— 这一点与 GPL 不同：
       GPL 在开发构建可放开，AVC 专利费不行。 */
    NC_CHECK(!encoderAllowed("openh264", release));
    NC_CHECK(encoderAllowed("libsvtav1", release));
    NC_CHECK(!encoderAllowed("libx264", release));
    NC_CHECK(!encoderAllowed("libx265", release));
    NC_CHECK(!encoderAllowed("mystery", release));  // 未知保守拒绝
    EncoderPolicy dev;
    dev.allowGplForDevelopment = true;
    NC_CHECK(encoderAllowed("libx264", dev));    // 开发例外放行
    NC_CHECK(!encoderAllowed("openh264", dev));  // 软件 H.264 仍然拒绝
    NC_CHECK(!encoderAllowed("mystery", dev));   // 未知仍然拒绝
}

NC_TEST(CodecPolicy, ComplianceStatementMentionsKeyRules) {
    const std::string statement = codecComplianceStatement();
    NC_CHECK(statement.find("libx264") != std::string::npos);
    /* OpenH264 已不再是兜底（2026-11-05 策略变更）——
       声明里现在是「软件 H.264 一律禁用」，应能找到这个表述。 */
    NC_CHECK(statement.find("OpenH264") != std::string::npos);
    NC_CHECK(statement.find("硬件") != std::string::npos);
    NC_CHECK(statement.find("LGPL") != std::string::npos);
}

NC_TEST(CodecPolicy, BuildFlagDefaults) {
    /* 开关状态**只能**通过库导出的函数查询，不能用宏。
       ----------------------------------------------------------------
       原来这里写的是 `#if defined(NAIROCLIP_DEVELOPMENT_GPL_OK)` +
       `NC_CHECK(kBuildAllowsGplForDevelopment)`。两个问题：
         · 宏只在编译 nairoclip_core 时注入，测试 TU 里没有，
           所以测试永远走 #else 分支 —— 与库的真实行为相反；
         · 这也掩盖了「库和测试对同一个常量取值不同」的 ODR 缺陷。
       现在 buildAllowsGplForDevelopment() 在库里定义，取值唯一。 */
    const bool devOk = buildAllowsGplForDevelopment();
    EncoderPolicy defaultPolicy;
    NC_CHECK_EQ(defaultPolicy.allowGplForDevelopment, devOk);
    /* 软件 H.264 的默认值同样跟随构建开关，且**必须**与 GPL 开关同时为真
       才可能生效（双保险，发布构建下恒不可用）。 */
    NC_CHECK_EQ(defaultPolicy.allowSoftwareH264ForDevelopment, devOk);
}
