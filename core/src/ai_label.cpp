#include "nairoclip/core/ai_label.h"

namespace nairoclip {
namespace core {

namespace {

/// 解析 #RRGGBB / #RRGGBBAA 为 RGBA8 打包（0xRRGGBBAA）。
std::uint32_t parseColor(const std::string& text) {
    if (text.empty() || text[0] != '#') {
        return 0xFFFFFFFF;
    }
    auto hexVal = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    unsigned long value = 0;
    int digits = 0;
    for (std::size_t i = 1; i < text.size() && digits < 8; ++i, ++digits) {
        const int digit = hexVal(text[i]);
        if (digit < 0) return 0xFFFFFFFF;
        value = (value << 4) | static_cast<unsigned long>(digit);
    }
    if (digits == 6) {
        value = (value << 8) | 0xFFu;
    }
    return static_cast<std::uint32_t>(value);
}

}  // namespace

SubtitleRenderItem buildAiLabelItem(const AiLabelConfig& config) {
    SubtitleRenderItem item;
    item.text = config.text;
    item.anchorX = config.anchorX;
    item.anchorY = config.anchorY;
    item.alignment = 0;  // 左对齐
    item.fontSize = config.fontSize;
    item.fillRgba = parseColor(config.color);
    item.outlineRgba = parseColor(config.backgroundColor);
    item.outlineWidth = 0.0;  // 标识用背景块而非描边（由平台层渲染）
    return item;
}

bool projectRequiresAiLabel(bool usedAutoEdit, bool usedTts, bool userMarked) {
    return usedAutoEdit || usedTts || userMarked;
}

}  // namespace core
}  // namespace nairoclip
