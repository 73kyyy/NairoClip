#include "nairoclip/core/codec_policy.h"

#include <algorithm>
#include <cctype>

namespace nairoclip {
namespace core {

namespace {


std::string toLower(const std::string& s) {
    std::string out = s;
    for (auto& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

/// 硬件编码器名称片段（FFmpeg 约定：<codec>_<backend>）。
bool isSystemEncoderBackendName(const std::string& lower) {
    static const char* kSuffixes[] = {
        "_videotoolbox",  // Apple 平台
        "_mediacodec",    // Android
        "_nvenc",         // NVIDIA
        "_qsv",           // Intel
        "_vaapi",         // Linux
        "_v4l2m2m",       // Linux 嵌入式
        "_amf",           // AMD
        "_mf",            // Windows Media Foundation
        "_d3d12va",       // Windows
    };
    for (const char* suffix : kSuffixes) {
        const std::size_t pos = lower.rfind(suffix);
        if (pos != std::string::npos && pos + std::string(suffix).size() == lower.size()) {
            return true;
        }
    }
    return false;
}

/// 宽松软件编码器白名单（**仅非 H.264**）。
///
/// 2026-11-05 变更：`openh264` 与内置 `h264` 已从此表移除。
/// 它们虽然许可宽松（BSD / LGPL），但**AVC 专利本身仍要授权**——
/// 只有 Cisco 分发的二进制才附带专利覆盖，且条件苛刻
/// （单独下载 / 用户可开关 / 声明文本），与 APK 打包架构冲突，
/// iOS 更是明确禁止安装后下载可执行模块。
///
/// 所以软件 H.264 编码器统一归到 `SoftwareH264` 分级 → 一律禁用。
/// 允许的只剩「非 H.264」的宽松实现：
bool isPermissiveSoftware(const std::string& lower) {
    static const char* kExact[] = {
        "libopus",     // BSD-3
        "libvpx", "libvpx-vp9",  // BSD-3
        "libaom-av1",  // BSD-3
        "libsvtav1",   // BSD-3（SVT-AV1）
        "librav1e",    // BSD-2（rav1e）
    };
    for (const char* name : kExact) {
        if (lower == name) {
            return true;
        }
    }
    // FFmpeg 内置的**非 H.264** 实现：
    //  - "aac"（内置 AAC 编码器，AAC 专利已基本到期）
    //  - "opus"（内置 Opus）
    //  - "av1"/"vp9"（内置，royalty-free）
    //  - "flac"（无损，公有领域）
    // 注意：`h264` 不在此列（已移到 SoftwareH264）。
    static const char* kBuiltin[] = {"aac", "opus", "av1", "vp9", "flac"};
    for (const char* name : kBuiltin) {
        if (lower == name) {
            return true;
        }
    }
    return false;
}

/// 软件 H.264 编码器识别（需要自付 AVC 专利 —— 一律禁用）。
///
/// 判据：名称含 264 但**不是硬件编码器**。
/// 硬件编码器已经在 isHardwareEncoderName 里被提前识别
/// （h264_mediacodec / h264_videotoolbox / h264_nvenc ...），
/// 所以走到这里还含 264 的，必然是软件实现。
bool isSoftwareH264Encoder(const std::string& lower) {
    if (lower.find("264") == std::string::npos && lower.find("avc") == std::string::npos) {
        return false;
    }
    // 显式列举（防止未来出现奇怪命名）
    static const char* kExact[] = {
        "openh264",     // Cisco 源码编译版：专利覆盖不适用
        "h264",         // FFmpeg 内置
        "libopenh264",  // 别名
        "h264_amf",     // ⚠️ AMF 在某些 FFmpeg 版本里是软件路径，保守禁用
    };
    for (const char* name : kExact) {
        if (lower == name) {
            return true;
        }
    }
    // 兜底：含 264/avc 且未被识别为硬件的，全按软件处理
    return true;
}

/// GPL 传染实现识别（文档第七章禁用清单）。
bool isGplEncoder(const std::string& lower) {
    static const char* kGpl[] = {
        "libx264",      // GPL-2.0（文档明确禁止）
        "libx264rgb",   // GPL-2.0
        "libx265",      // GPL-2.0（文档全面禁用）
        "libx262",      // libmpeg2 相关 GPL 组合
        "libxvid",      // GPL-2.0
        "libxvidcore",  // 别名
        "libfdk_aac",   // 非自由（NDA 许可，不可分发）
    };
    for (const char* name : kGpl) {
        if (lower == name) {
            return true;
        }
    }
    // 前缀匹配：libx26x 系列（防御未来新增 GPL 编码器）
    if (lower.rfind("libx26", 0) == 0 || lower.rfind("libxvid", 0) == 0) {
        return true;
    }
    return false;
}

/// 同一优先级内的确定性排序权重（越小越优先）。
int preferenceRank(const std::string& lower) {
        // 硬件编码器：H.264 场景下 MediaCodec/VideoToolbox（移动）排前，
        // 桌面后端（nvenc/qsv/vaapi/mf）随后；名称排序保证确定性。
        static const char* kOrder[] = {"_mediacodec", "_videotoolbox", "_nvenc",
                                       "_qsv",         "_mf",            "_amf",
                                       "_vaapi",       "_d3d12va",       "_v4l2m2m",
                                       "_opencl"};
        for (int i = 0; i < static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0])); ++i) {
            if (lower.find(kOrder[i]) != std::string::npos) {
                return i;
            }
        }
        return 100;
}

/// 通用选择过程（视频/音频共用）。

}  // namespace

// =============================================================================
// 许可分级
// =============================================================================

bool buildAllowsGplForDevelopment() {
    return false;
}

const char* encoderLicenseClassName(EncoderLicenseClass cls) {
    switch (cls) {
        case EncoderLicenseClass::Hardware: return "hardware";
        case EncoderLicenseClass::Permissive: return "permissive";
        case EncoderLicenseClass::GplInfectious: return "gpl";
        case EncoderLicenseClass::SoftwareH264: return "software-h264";
        case EncoderLicenseClass::Unknown: return "unknown";
    }
    return "unknown";
}

EncoderLicenseClass classifyEncoderLicense(const std::string& encoderName) {
    const std::string lower = toLower(encoderName);
    if (lower.empty()) {
        return EncoderLicenseClass::Unknown;
    }
    // 顺序：GPL 优先识别（libx264 等虽含 "264" 也不可误判为宽松）
    if (isGplEncoder(lower)) {
        return EncoderLicenseClass::GplInfectious;
    }
    if (isSystemEncoderBackendName(lower)) {
        return EncoderLicenseClass::Hardware;
    }
    /* 软件 H.264：**必须排在 Permissive 之前**判断。
       顺序错了会把 openh264 归为「宽松可用」，
       于是发布构建里偷偷用软件 H.264 —— 正是要避免的那种情况。 */
    if (isSoftwareH264Encoder(lower)) {
        return EncoderLicenseClass::SoftwareH264;
    }
    if (isPermissiveSoftware(lower)) {
        return EncoderLicenseClass::Permissive;
    }
    return EncoderLicenseClass::Unknown;
}

// =============================================================================
// 选择
// =============================================================================

EncoderSelection selectVideoEncoder(const std::string& codec,
                                    const std::vector<EncoderCandidate>& available,
                                    const EncoderPolicy& policy) {
    const std::string wanted = codec.empty() ? std::string("h264") : codec;
    // 文档禁用清单：H.265/HEVC 全线禁止（不论软件/硬件实现，直接拒绝）
    const std::string lower = toLower(wanted);
    if (lower == "hevc" || lower == "h265" || lower == "libx265") {
        EncoderSelection denied;
        denied.note = "H.265/HEVC 编码按协议清单全线禁止（专利池复杂）";
        return denied;
    }
    /* 取候选序列的第一个即可（rankVideoEncoderCandidates 已按优先级排好）。
       不在本函数里另写一套选择逻辑 —— 两份逻辑必然漂移，见文件末尾的说明。 */
    const std::vector<EncoderSelection> ranked =
        rankVideoEncoderCandidates(wanted, available, policy);
    if (ranked.empty()) {
        EncoderSelection none;
        none.note = "无可用编码器（可用列表中无匹配格式的合规实现）";
        return none;
    }
    return ranked.front();
}

EncoderSelection selectAudioEncoder(const std::string& codec,
                                    const std::vector<EncoderCandidate>& available,
                                    const EncoderPolicy& policy) {
    const std::string wanted = codec.empty() ? std::string("aac") : codec;
    /* 音频走同一个排序函数：它对非视频编解码器同样适用
       软件编码器即使许可宽松也会在候选排序阶段拒绝。 */
    const std::vector<EncoderSelection> ranked =
        rankVideoEncoderCandidates(wanted, available, policy);
    if (ranked.empty()) {
        EncoderSelection none;
        none.note = "无可用音频编码器";
        return none;
    }
    return ranked.front();
}

std::vector<EncoderSelection> rankVideoEncoderCandidates(
    const std::string& codec, const std::vector<EncoderCandidate>& available,
    const EncoderPolicy& policy) {
    (void)policy;  // 旧策略字段保留 ABI；系统-only 模式不允许软件回退。
    const std::string wanted = codec.empty() ? std::string("h264") : codec;
    const std::string lower = toLower(wanted);
    if (lower == "hevc" || lower == "h265" || lower == "libx265") {
        return {};  // H.265 全线禁止
    }
    // 仅平台系统媒体后端可进入候选；所有应用自带软件编码器均被排除。
    struct Scored {
        EncoderSelection selection;
        int rank = 100;
        bool hardware = false;
    };
    std::vector<Scored> hardware;
    for (const auto& candidate : available) {
        if (!candidate.codec.empty() && toLower(candidate.codec) != lower) {
            continue;
        }
        const std::string nameLower = toLower(candidate.name);
        const EncoderLicenseClass cls = classifyEncoderLicense(candidate.name);
        if (cls != EncoderLicenseClass::Hardware) {
            continue;
        }
        const bool isHw = candidate.hardware;
        EncoderSelection selection;
        selection.name = candidate.name;
        selection.license = cls;
        selection.note = "候选：" + candidate.name + "（" + encoderLicenseClassName(cls) + "）";
        Scored scored{selection, preferenceRank(nameLower), isHw};
        if (cls == EncoderLicenseClass::Hardware) {
            hardware.push_back(std::move(scored));
        }
    }
    const auto byRank = [](const Scored& a, const Scored& b) {
        if (a.hardware != b.hardware) return a.hardware;
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.selection.name < b.selection.name;
    };
    std::sort(hardware.begin(), hardware.end(), byRank);

    std::vector<EncoderSelection> result;
    {
        for (auto& s : hardware) {
            result.push_back(std::move(s.selection));
        }
    }
    return result;
}


bool encoderAllowed(const std::string& encoderName, const EncoderPolicy& policy) {
    (void)policy;  // 保留旧接口签名；软件实现不得因策略开关而重新启用。
    const EncoderLicenseClass cls = classifyEncoderLicense(encoderName);
    switch (cls) {
        case EncoderLicenseClass::Hardware:
            return true;
        case EncoderLicenseClass::Permissive:
            return false;  // 应用自带的软件编码器全平台禁用；只能走平台系统媒体 API。
        case EncoderLicenseClass::GplInfectious:
            return false;  // GPL 编码器在开发与发布构建中均禁止。
        case EncoderLicenseClass::SoftwareH264:
            /* **无论如何都不允许**，包括开发构建。
               软件 H.264 的风险是 AVC 专利费（不是 GPL），
               开发示例（allowGplForDevelopment）不该覆盖它。 */
            return false;
        case EncoderLicenseClass::Unknown:
            return false;  // 清单外实现：保守拒绝（文档：清单化管理）
    }
    return false;
}

const char* codecComplianceStatement() {
    return "NairoClip 编解码合规：H.264 仅使用系统媒体 API（硬件优先）"
           "（MediaCodec / VideoToolbox / NVENC / QSV / VAAPI / MF），"
           "软件 H.264 实现（libx264 / OpenH264 / FFmpeg 内置）一律禁用 —— "
           "不包含应用自带软件 H.264 实现；系统兼容路径由平台框架提供；"
           "H.265/HEVC 全线禁用（专利池复杂）；"
           "AV1/VP9/Opus/AAC 不允许应用自带软件编码实现，必须走平台系统媒体 API；"
           "系统无兼容实现时 fail-closed；容器读写与必要格式转换可保留独立实现。";
}

}  // namespace core
}  // namespace nairoclip
