#pragma once
// =============================================================================
// 滤镜库（Filter Library）—— 文档 Part 4-B 第 7 节「滤镜」
// =============================================================================
// 产品对应（文档 Part 4-B 第 7.1 节分类 Tab，**强制名称**）：
//   全部 / 电影 / 复古 / 清新 / 黑白 / 赛博 / 日系 / 胶片 / 暖阳 / 冷调 /
//   港风 / 高级灰 / 奶油 / Negative
//
// 设计说明（文档 5.7「滤镜库（14种风格，LUT实现）」）：
//   · 文档原方案为 3D LUT（.cube 文件）；本模块以**程序化色调映射**实现同一
//     语义（零第三方依赖、可测试、可解释），每个风格的参数与视觉意图一一对应；
//   · 后续接入调色师制作的 .cube 素材时，替换 applyFilterToRgba 内部实现即可，
//     对外接口（FilterStyle / 强度）与上层语义不变；
//   · 与「调节」分类严格分离：滤镜只改变色彩/色调，不做画面结构调整
//     （文档 7.4「滤镜和特效不允许功能重叠」）；
//   · 强度 0 = 零改动；强度 100 = 完整风格；中间线性插值（文档「强度 0~100」）。
//
// 与「特效」的关系（文档明确边界）：
//   · 滤镜 = 整体色彩风格（色调映射；本模块）；
//   · 特效 = 叠加视觉元素（故障/马赛克/雪花等；effects 模块）。

#include <cstdint>

namespace nairoclip {
namespace core {

/// 滤镜风格（文档 Part 4-B 7.1 分类清单；名称逐字对应）。
enum class FilterStyle {
    None = 0,     ///< 不应用（默认）
    Movie = 1,    ///< 电影（青橙对比 + 轻微褪色）
    Retro = 2,    ///< 复古（暖黄 + 低饱和 + 暗部抬升）
    Fresh = 3,    ///< 清新（高亮 + 冷绿倾向 + 高饱和）
    BlackWhite = 4, ///< 黑白（去色 + 对比强化）
    Cyber = 5,    ///< 赛博（青紫双色调 + 高对比）
    Japanese = 6, ///< 日系（低对比 + 高明度 + 淡青）
    Film = 7,     ///< 胶片（密度曲线 + 轻微品红）
    WarmSun = 8,  ///< 暖阳（橙黄暖调 + 提亮）
    CoolTone = 9, ///< 冷调（蓝青 + 降饱和）
    HongKong = 10,///< 港风（浓郁红绿 + 高对比）
    HighGray = 11,///< 高级灰（低饱和 + 灰调 + 柔对比）
    Cream = 12,   ///< 奶油（柔光 + 低对比 + 暖白）
    Negative = 13,///< Negative（负片反色）
};

/// 滤镜名称（"none"/"movie"/...；序列化与 C ABI 使用）。
const char* filterStyleName(FilterStyle style);
/// 滤镜中文展示名（文档 7.1 逐字："电影"/"复古"/...）。
const char* filterStyleDisplayName(FilterStyle style);
/// 从字符串解析（大小写不敏感；支持中文名；非法返回 false）。
bool filterStyleFromString(const char* text, FilterStyle& out);
/// 从整数解析（0..13；非法返回 false）。
bool filterStyleFromInt(int value, FilterStyle& out);

/// 应用滤镜到 RGBA8 帧（原地修改 RGB；alpha 不变）。
/// - intensity：强度 0..100（0 = 零改动；100 = 完整风格；中间按比例插值）；
/// - 参数非法（缓冲区不足/尺寸非法）返回 false 且不改动缓冲；
/// - 确定性：同一 (输入, 风格, 强度) → 同一输出（可测）。
bool applyFilterToRgba(std::uint8_t* pixels, int width, int height, FilterStyle style,
                       double intensity);

/// 滤镜说明文案（供 UI/日志）。
const char* filterLibraryPurpose();

}  // namespace core
}  // namespace nairoclip
