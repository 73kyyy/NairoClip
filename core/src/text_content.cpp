#include "nairoclip/core/text_content.h"

#include <cstdio>

namespace nairoclip {
namespace core {

namespace {
TextRasterizer* g_rasterizer = nullptr;

/// 把 double 格式化成固定精度字符串（用于缓存键）。
/// 不用 std::to_string —— 它会输出 6 位小数，导致
/// 「48.0 与 48.000001」被当成不同键，缓存命中率暴跌。
std::string fmt2(double v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", v);
    return std::string(buf);
}
}  // namespace

std::string TextContent::cacheKey(const char* platformTag) const {
    /* 缓存键必须覆盖**所有影响像素的参数**。
       漏掉任何一个都会出现「改了参数但画面不变」——
       这类 bug 用户会以为是「功能坏了」。

       覆盖项：文本 / 字体 / 字号 / 颜色 / 描边 / 对齐 / 宽度约束 /
               锚点位置 / 平台标识 */
    std::string k;
    k.reserve(128);
    k += text;
    k += '|';
    k += style.fontFamily;
    k += '|';
    k += fmt2(style.fontSize);
    k += '|';
    k += style.color;
    k += '|';
    k += style.outlineColor;
    k += '|';
    k += fmt2(style.outlineWidth);
    k += '|';
    k += style.backgroundColor;
    k += '|';
    k += fmt2(style.backgroundPadding);
    k += '|';
    k += std::to_string(style.alignment);
    k += '|';
    k += fmt2(style.anchorX);
    k += ',';
    k += fmt2(style.anchorY);
    k += '|';
    k += std::to_string(maxWidth);
    k += '|';
    k += (platformTag != nullptr ? platformTag : "unknown");
    return k;
}

void setTextRasterizer(TextRasterizer* rasterizer) { g_rasterizer = rasterizer; }

TextRasterizer* textRasterizer() { return g_rasterizer; }

TextContent textContentFromCue(const SubtitleCue& cue) {
    TextContent out;
    out.text = cue.text;
    out.style = cue.style;
    /* maxWidth 保持 0（不限）：
       字幕原本由平台层按画布宽度自行断行，转成片段后若强加一个宽度，
       会改变原有的换行位置，视觉上「迁移后排版变了」。
       需要约束时由调用方显式设置。 */
    out.maxWidth = 0;
    return out;
}

}  // namespace core
}  // namespace nairoclip
