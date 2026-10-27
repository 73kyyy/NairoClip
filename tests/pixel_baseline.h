#pragma once
// =============================================================================
// 像素基线工具 —— 用于证明「改动没有影响既有渲染输出」
// =============================================================================
//
// 为什么需要：改动合成主链路（如 B2-6 调整图层）时，唯一能证明"没弄坏"的
// 证据是**逐像素一致**。只断言"导出成功""尺寸正确"完全不够——画面错位、
// 颜色偏移、图层顺序反了，这些断言全会通过。
//
// 用法：
//   1. 用同一条导出路径跑两次（改前 / 改后），各收集全部帧的指纹；
//   2. 比对指纹序列；不一致时给出第一处差异的位置与像素差。
//
// 为什么用指纹而不是存整帧：一段 2 秒 160x120 的视频是 16 MB 原始数据，
// 存进仓库不现实。指纹（FNV-1a 64）足以发现任何单像素改动，
// 且冲突概率可忽略（2^-64）。

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace nairoclip {
namespace test {

/// 一帧的指纹与元信息。
struct FrameDigest {
    std::int64_t timestampMicros = 0;
    int width = 0;
    int height = 0;
    std::uint64_t hash = 0;
};

/// FNV-1a 64 位哈希（稳定、无依赖、跨平台一致）。
inline std::uint64_t fnv1a64(const std::uint8_t* data, std::size_t size) {
    std::uint64_t h = 1469598103934665603ull;
    for (std::size_t i = 0; i < size; ++i) {
        h ^= static_cast<std::uint64_t>(data[i]);
        h *= 1099511628211ull;
    }
    return h;
}

/// 比对两组指纹，返回差异描述（空 = 完全一致）。
/// 只报告第一处差异并给出足够定位的信息，避免刷屏。
inline std::string diffDigests(const std::vector<FrameDigest>& a,
                               const std::vector<FrameDigest>& b) {
    if (a.size() != b.size()) {
        return "帧数不同: " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].hash != b[i].hash) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "第 %zu 帧指纹不同（t=%lldus, %dx%d）: %016llx vs %016llx", i,
                          static_cast<long long>(a[i].timestampMicros), a[i].width, a[i].height,
                          static_cast<unsigned long long>(a[i].hash),
                          static_cast<unsigned long long>(b[i].hash));
            return std::string(buf);
        }
        if (a[i].timestampMicros != b[i].timestampMicros || a[i].width != b[i].width ||
            a[i].height != b[i].height) {
            return "第 " + std::to_string(i) + " 帧元信息不同（时间戳或尺寸）";
        }
    }
    return std::string();
}

}  // namespace test
}  // namespace nairoclip
