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
    // 宽松软件
    NC_CHECK(classifyEncoderLicense("openh264") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("libsvtav1") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("libaom-av1") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("libvpx-vp9") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("libopus") == EncoderLicenseClass::Permissive);
    NC_CHECK(classifyEncoderLicense("aac") == EncoderLicenseClass::Permissive);
    // 未知 → 保守
    NC_CHECK(classifyEncoderLicense("x264_custom") == EncoderLicenseClass::Unknown);
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

NC_TEST(CodecPolicy, PermissivePreferredOverGpl) {
    // 无硬件：OpenH264（宽松）必须优先于 libx264（GPL）
    const std::vector<EncoderCandidate> available = {
        sw("libx264", "h264"),
        sw("openh264", "h264"),
    };
    EncoderPolicy policy;
    policy.allowGplForDevelopment = true;  // 即便是开发模式也不该选 GPL
    const EncoderSelection selection = selectVideoEncoder("h264", available, policy);
    NC_CHECK(selection.valid());
    NC_CHECK_EQ(selection.name, std::string("openh264"));
    NC_CHECK(selection.license == EncoderLicenseClass::Permissive);
    NC_CHECK(!selection.usedDevelopmentException);
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
        sw("openh264", "h264"),
    };
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    const EncoderSelection selection = selectVideoEncoder("h264", available, release);
    NC_CHECK(selection.valid());
    NC_CHECK_EQ(selection.name, std::string("openh264"));
    // 全部未知 → 拒绝
    const EncoderSelection none =
        selectVideoEncoder("h264", {sw("mystery_codec", "h264")}, release);
    NC_CHECK(!none.valid());
}

// ---------------------------------------------------------------------------
// 候选序列（逐个尝试打开的回退顺序）
// ---------------------------------------------------------------------------

NC_TEST(CodecPolicy, CandidateRankingOrder) {
    const std::vector<EncoderCandidate> available = {
        sw("libx264", "h264"),
        hw("h264_nvenc", "h264"),
        sw("openh264", "h264"),
        hw("h264_videotoolbox", "h264"),
    };
    EncoderPolicy dev;
    dev.allowGplForDevelopment = true;
    const auto ranked = rankVideoEncoderCandidates("h264", available, dev);
    NC_CHECK_EQ(ranked.size(), std::size_t(4));
    // 顺序：硬件（移动平台优先）→ 宽松 → GPL
    NC_CHECK_EQ(ranked[0].name, std::string("h264_videotoolbox"));
    NC_CHECK_EQ(ranked[1].name, std::string("h264_nvenc"));
    NC_CHECK_EQ(ranked[2].name, std::string("openh264"));
    NC_CHECK_EQ(ranked[3].name, std::string("libx264"));
    NC_CHECK(ranked[3].usedDevelopmentException);
    // 发布模式：GPL 不出现在候选序列中
    EncoderPolicy release;
    release.allowGplForDevelopment = false;
    const auto rankedRelease = rankVideoEncoderCandidates("h264", available, release);
    NC_CHECK_EQ(rankedRelease.size(), std::size_t(3));
    for (const auto& candidate : rankedRelease) {
        NC_CHECK(candidate.license != EncoderLicenseClass::GplInfectious);
    }
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
    NC_CHECK(encoderAllowed("openh264", release));
    NC_CHECK(encoderAllowed("libsvtav1", release));
    NC_CHECK(!encoderAllowed("libx264", release));
    NC_CHECK(!encoderAllowed("libx265", release));
    NC_CHECK(!encoderAllowed("mystery", release));  // 未知保守拒绝
    EncoderPolicy dev;
    dev.allowGplForDevelopment = true;
    NC_CHECK(encoderAllowed("libx264", dev));  // 开发例外放行
    NC_CHECK(!encoderAllowed("mystery", dev)); // 未知仍然拒绝
}

NC_TEST(CodecPolicy, ComplianceStatementMentionsKeyRules) {
    const std::string statement = codecComplianceStatement();
    NC_CHECK(statement.find("libx264") != std::string::npos);
    NC_CHECK(statement.find("OpenH264") != std::string::npos);
    NC_CHECK(statement.find("LGPL") != std::string::npos);
}

NC_TEST(CodecPolicy, BuildFlagDefaults) {
    // 测试构建默认启用开发例外（沙箱/CI 仅有 libx264）；发布构建必须显式关闭。
    // 本断言记录当前构建的开关状态（防止有人误删 CMake 传递）。
#if defined(NAIROCLIP_DEVELOPMENT_GPL_OK)
    NC_CHECK(kBuildAllowsGplForDevelopment);
    EncoderPolicy defaultPolicy;
    NC_CHECK(defaultPolicy.allowGplForDevelopment);
#else
    // 无开关：必须默认合规（拒绝 GPL）
    NC_CHECK(!kBuildAllowsGplForDevelopment);
    EncoderPolicy defaultPolicy;
    NC_CHECK(!defaultPolicy.allowGplForDevelopment);
#endif
}
