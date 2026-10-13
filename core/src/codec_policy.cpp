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
bool isHardwareEncoderName(const std::string& lower) {
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
        "_opencl",        // 通用 OpenCL
    };
    for (const char* suffix : kSuffixes) {
        const std::size_t pos = lower.rfind(suffix);
        if (pos != std::string::npos && pos + std::string(suffix).size() == lower.size()) {
            return true;
        }
    }
    return false;
}

/// 宽松软件编码器白名单（精确或前缀匹配）。
bool isPermissiveSoftware(const std::string& lower) {
    // 精确名
    static const char* kExact[] = {
        "openh264",    // Cisco OpenH264（BSD + 专利费覆盖，H.264 兜底）
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
    // FFmpeg 内置实现（LGPL 内置编码器 + 独立 BSD 实现）：
    //  - "aac"（内置 AAC 编码器）；"opus"（内置 Opus 编码器）；
    //  - "av1"/"vp9"/"h264"（内置实现，不含第三方库前缀）
    static const char* kBuiltin[] = {"aac", "opus", "av1", "vp9", "h264", "mpeg4", "flac"};
    for (const char* name : kBuiltin) {
        if (lower == name) {
            return true;
        }
    }
    return false;
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
int preferenceRank(const std::string& lower, bool hardware) {
    if (hardware) {
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
    // 宽松软件：OpenH264（文档指定的 H.264 兜底）最优先，其后 AV1/VP9/Opus。
    static const char* kOrder[] = {"openh264", "libsvtav1", "libaom-av1", "librav1e",
                                   "libvpx-vp9", "libvpx",     "libopus",    "aac",
                                   "opus",       "av1",        "vp9",        "h264"};
    for (int i = 0; i < static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0])); ++i) {
        if (lower == kOrder[i]) {
            return i;
        }
    }
    return 100;
}

/// 通用选择过程（视频/音频共用）。
EncoderSelection selectByCodec(const std::string& codec,
                               const std::vector<EncoderCandidate>& available,
                               const EncoderPolicy& policy) {
    EncoderSelection result;
    const std::string wantedCodec = toLower(codec);

    // 候选过滤 + 分级
    struct Scored {
        std::string name;
        EncoderLicenseClass license;
        int rank;
        bool hardware;
    };
    std::vector<Scored> hardwareList;
    std::vector<Scored> permissiveList;
    std::vector<Scored> gplList;
    for (const auto& candidate : available) {
        if (!candidate.codec.empty() && toLower(candidate.codec) != wantedCodec) {
            continue;  // 格式不匹配
        }
        const std::string lower = toLower(candidate.name);
        const EncoderLicenseClass cls = classifyEncoderLicense(candidate.name);
        if (cls == EncoderLicenseClass::Unknown) {
            continue;  // 未知：保守跳过（文档要求清单化管理）
        }
        const bool hardware =
            candidate.hardware || cls == EncoderLicenseClass::Hardware;
        Scored scored{candidate.name, cls, preferenceRank(lower, hardware), hardware};
        if (cls == EncoderLicenseClass::Hardware) {
            hardwareList.push_back(std::move(scored));
        } else if (cls == EncoderLicenseClass::Permissive) {
            permissiveList.push_back(std::move(scored));
        } else {
            gplList.push_back(std::move(scored));
        }
    }
    const auto byRank = [](const Scored& a, const Scored& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.name < b.name;  // 确定性：同名次按字典序
    };
    std::sort(hardwareList.begin(), hardwareList.end(), byRank);
    std::sort(permissiveList.begin(), permissiveList.end(), byRank);
    std::sort(gplList.begin(), gplList.end(), byRank);

    // 1) 硬件优先
    if (policy.preferHardware && !hardwareList.empty()) {
        result.name = hardwareList.front().name;
        result.license = EncoderLicenseClass::Hardware;
        result.note = "选择硬件编码器（三端统一优先，专利费由设备厂商覆盖）: " + result.name;
        return result;
    }
    // 2) 宽松软件
    if (!permissiveList.empty()) {
        result.name = permissiveList.front().name;
        result.license = EncoderLicenseClass::Permissive;
        result.note = "选择宽松许可软件编码器: " + result.name;
        return result;
    }
    // 3) GPL —— 仅开发例外
    if (policy.allowGplForDevelopment && !gplList.empty()) {
        result.name = gplList.front().name;
        result.license = EncoderLicenseClass::GplInfectious;
        result.usedDevelopmentException = true;
        result.note = "开发例外：使用 GPL 编码器 " + result.name +
                      "（仅限开发/测试构建；发布构建必须改用硬件或 OpenH264）";
        return result;
    }
    // 4) fail-closed
    result.note = gplList.empty()
                      ? "无可用编码器（可用列表中无匹配格式的合规实现）"
                      : "仅有 GPL 编码器可用（如 libx264）且未允许开发例外——"
                        "合规要求拒绝编码（发布构建需链接 OpenH264 或使用硬件编码器）";
    return result;
}

}  // namespace

// =============================================================================
// 许可分级
// =============================================================================

const char* encoderLicenseClassName(EncoderLicenseClass cls) {
    switch (cls) {
        case EncoderLicenseClass::Hardware: return "hardware";
        case EncoderLicenseClass::Permissive: return "permissive";
        case EncoderLicenseClass::GplInfectious: return "gpl";
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
    if (isHardwareEncoderName(lower)) {
        return EncoderLicenseClass::Hardware;
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
    return selectByCodec(wanted, available, policy);
}

EncoderSelection selectAudioEncoder(const std::string& codec,
                                    const std::vector<EncoderCandidate>& available,
                                    const EncoderPolicy& policy) {
    const std::string wanted = codec.empty() ? std::string("aac") : codec;
    return selectByCodec(wanted, available, policy);
}

std::vector<EncoderSelection> rankVideoEncoderCandidates(
    const std::string& codec, const std::vector<EncoderCandidate>& available,
    const EncoderPolicy& policy) {
    const std::string wanted = codec.empty() ? std::string("h264") : codec;
    const std::string lower = toLower(wanted);
    if (lower == "hevc" || lower == "h265" || lower == "libx265") {
        return {};  // H.265 全线禁止
    }
    // 归档：硬件（按优先序）→ 宽松软件（按优先序）→ GPL（仅开发例外）
    struct Scored {
        EncoderSelection selection;
        int rank = 100;
    };
    std::vector<Scored> hardware;
    std::vector<Scored> permissive;
    std::vector<Scored> gpl;
    for (const auto& candidate : available) {
        if (!candidate.codec.empty() && toLower(candidate.codec) != lower) {
            continue;
        }
        const std::string nameLower = toLower(candidate.name);
        const EncoderLicenseClass cls = classifyEncoderLicense(candidate.name);
        if (cls == EncoderLicenseClass::Unknown) {
            continue;
        }
        const bool isHw = candidate.hardware || cls == EncoderLicenseClass::Hardware;
        EncoderSelection selection;
        selection.name = candidate.name;
        selection.license = cls;
        selection.note = "候选：" + candidate.name + "（" + encoderLicenseClassName(cls) + "）";
        Scored scored{selection, preferenceRank(nameLower, isHw)};
        if (cls == EncoderLicenseClass::Hardware) {
            hardware.push_back(std::move(scored));
        } else if (cls == EncoderLicenseClass::Permissive) {
            permissive.push_back(std::move(scored));
        } else {
            gpl.push_back(std::move(scored));
        }
    }
    const auto byRank = [](const Scored& a, const Scored& b) {
        if (a.rank != b.rank) return a.rank < b.rank;
        return a.selection.name < b.selection.name;
    };
    std::sort(hardware.begin(), hardware.end(), byRank);
    std::sort(permissive.begin(), permissive.end(), byRank);
    std::sort(gpl.begin(), gpl.end(), byRank);

    std::vector<EncoderSelection> result;
    if (policy.preferHardware) {
        for (auto& s : hardware) {
            result.push_back(std::move(s.selection));
        }
    }
    for (auto& s : permissive) {
        result.push_back(std::move(s.selection));
    }
    if (policy.allowGplForDevelopment) {
        for (auto& s : gpl) {
            s.selection.usedDevelopmentException = true;
            s.selection.note = "开发例外候选：" + s.selection.name;
            result.push_back(std::move(s.selection));
        }
    }
    return result;
}

bool encoderAllowed(const std::string& encoderName, const EncoderPolicy& policy) {
    const EncoderLicenseClass cls = classifyEncoderLicense(encoderName);
    switch (cls) {
        case EncoderLicenseClass::Hardware:
        case EncoderLicenseClass::Permissive:
            return true;
        case EncoderLicenseClass::GplInfectious:
            return policy.allowGplForDevelopment;
        case EncoderLicenseClass::Unknown:
            return false;  // 清单外实现：保守拒绝（文档：清单化管理）
    }
    return false;
}

const char* codecComplianceStatement() {
    return "NairoClip 编解码合规：禁用 libx264/libx265（GPL）与 --enable-gpl/"
           "--enable-nonfree 编译选项；H.264 硬件编码优先、OpenH264 兜底；"
           "AV1/VP9/Opus 采用 BSD/royalty-free 实现；FFmpeg 核心库动态链接"
           "（LGPL 2.1+），许可清单见「关于-开源软件许可」";
}

}  // namespace core
}  // namespace nairoclip
