#pragma once
// =============================================================================
// 色度抠像（Chroma Key）—— 文档第 2 部分「🤖AI实验室 → 智能抠像 → 人像/物品/
// 色度抠像(绿幕)」+ 权益矩阵「AI智能抠像：基础版85% | 高精度版98%+物品/色度抠像」
// =============================================================================
// 产品对应（原文）：
//   · 智能抠像 → 人像/物品/色度抠像(绿幕)；
//   · 权益：「AI智能抠像 | 基础版(精度85%) | 高精度版(98%)+物品/色度抠像」
//     —— 色度抠像为 Pro 专属；免费用户显示 🔒 并弹付费墙（文档 7.7 边界规则）；
//   · 时间轴「🎭前景轨」上的「[抠像━━]」徽标（前景 = 被抠素材叠加到下层）。
//
// 实现原理（真实、可测，零第三方依赖；不需要推理引擎）：
//   · 色彩空间：RGB → YCbCr（BT.601）；在 Cb/Cr 平面取「键控方向」u（单位向量）
//     与像素色度向量 v（相对中性轴 128）；
//   · 键控相似度（方向投影，亮度自适应）：
//         a = dot(v, u_key) / |k|       （0..1；1 ≈ 与键控色同色相且亮度接近）
//     该度量与「亮度成比例」——浅绿/深绿共线，但阴影（亮度衰减的键控色）
//     相似度按比例下降 → 阴影不会被一刀切掉（保留立体感，可调阈值再抠净）；
//     反向色（红/肤/品红等）得到负值 → 天然保留为前景；
//   · 键控距离 d = 1 - clamp(a, 0, 1)；双阈值软判决：
//       d <= inner → 完全背景（覆盖率 0）；d >= outer → 完全前景（覆盖率 1）；
//       中间为 smoothstep 平滑过渡（边缘自然、无锯齿）；
//   · 溢色抑制（spill suppression）：对判定为前景（覆盖率 > 0.5）且残留键控色
//     倾向的像素，把色度向自身亮度（中性灰）收敛——消除头发/衣物边缘的键控色反光。
//
// 数学性质说明（设计取舍，非近似）：
//   · 亮度自适应投影使"打光不完美的绿幕"（如 (0,200,40)）仍以较高相似度被抠除；
//   · 而暗部前景（深色衣物/头发）的相似度天然很低（色度幅值小）→ 自动保护，
//     因此无需额外的"暗部保护"参数（避免引入无实际效果的旋钮）；
//   · 肤色方向与常用键控色（绿/蓝/洋红）在 Cb/Cr 平面夹角 > 78°，方向投影
//     天然分离 → 无需额外的"肤色保护"参数（同理）。
//
// 与蒙版的关系（可叠加）：
//   · 蒙版（shape）决定「哪些区域参与」；抠像决定「区域内哪些像素透明」；
//   · 两者都产生 alpha：多层轨道时交由既有下层混合路径合成；
//   · 单层轨道时走「背景合成」路径（输出像素有效、不残留键控色）。
//
// 分工（与全项目一致）：
//   · core 负责算法与渲染（纯像素运算，可测试）；
//   · 人像/物品语义分割（MediaPipe 等推理）由平台层实现，本模块不依赖模型。

#include <cstdint>
#include <string>

namespace nairoclip {
namespace core {

/// 色度抠像参数（键控色 + 双阈值 + 溢色抑制）。
struct ChromaKey {
    /// 键控色（0xRRGGBB；默认纯绿 0x00FF00，绿幕标准色）。
    std::uint32_t keyColorRgb = 0x00FF00;
    /// 内部阈值（键控距离 d <= inner → 完全背景；0..1 归一化）。
    /// 默认 0.30：相似度 a >= 0.70 完全抠除（覆盖常见打光不完美的绿幕）。
    double innerThreshold = 0.30;
    /// 外部阈值（d >= outer → 完全前景；0..1 归一化）。
    /// 默认 0.75：a <= 0.25 的像素保留（弱键控色倾向的物体边缘）。
    double outerThreshold = 0.75;
    /// 溢色抑制强度（0..1；0 = 关闭；1 = 强力去溢色）。
    double spillSuppression = 0.6;

    /// 参数是否合法（供命令校验与反序列化防御）。
    bool valid() const {
        return innerThreshold >= 0.0 && innerThreshold <= 1.0 &&
               outerThreshold > innerThreshold && outerThreshold <= 1.0 &&
               spillSuppression >= 0.0 && spillSuppression <= 1.0;
    }
};

/// 抠像结果的合成模式（与 MaskBlendMode 对称的语义）。
enum class ChromaKeyBlendMode {
    /// 多层合成：被抠像素 alpha 缩放为覆盖率（透出下层）。
    Transparent = 0,
    /// 单层输出：被抠像素按 (1 - 覆盖率) 混入背景色（不残留键控色）。
    Background = 1,
};

/// 键控色从字符串解析（"#00FF00" / "00ff00" / "0x00FF00" / "green" / "blue" /
/// "magenta" / 中文「绿」「蓝」「洋红」）。
/// 返回 0xRRGGBB；非法返回 0xFFFFFFFF（调用方判负）。
std::uint32_t chromaKeyColorFromString(const char* text);

/// 键控色名称（"green" / "blue" / "magenta" / "custom"；供 UI 与日志）。
const char* chromaKeyColorName(std::uint32_t rgb);

/// 单像素色度键控覆盖率（0..1；1 = 完全是前景，0 = 完全是背景）。
/// - r/g/b：像素颜色（0..255）；
/// - 返回 < 0 表示参数非法（key 不合法）。
double chromaKeyCoverageAt(std::uint8_t r, std::uint8_t g, std::uint8_t b, const ChromaKey& key);

/// 采样估计整体前景覆盖率（0..1；供 UI 展示「抠像后前景约 X%」）。
/// - step：采样步长（像素；>=1，越大越快越粗）。
/// - 参数非法返回 < 0。
double chromaKeyCoverageRatio(const std::uint8_t* rgba, int width, int height,
                              const ChromaKey& key, int step = 2);

/// 对 RGBA8 缓冲应用色度抠像（原地修改）。
/// - Transparent：alpha *= 覆盖率（RGB 保留但做溢色抑制，供下层混合）；
/// - Background：被抠像素按 (1 - 覆盖率) 混入背景色（0xRRGGBBAA），alpha = 背景 alpha；
/// - 参数非法返回 false 且不改动缓冲；应用成功（含无需改动的帧）返回 true。
bool applyChromaKeyToFrame(std::uint8_t* rgba, int width, int height, const ChromaKey& key,
                           ChromaKeyBlendMode mode = ChromaKeyBlendMode::Transparent,
                           std::uint32_t backgroundRgba = 0x000000FF);

/// 说明文案（供 UI/日志；描述色度抠像用途与权益）。
const char* chromaKeyPurpose();

// =============================================================================
// 质量档位与权益门控（文档权益矩阵「基础版85% / 高精度版98%+物品/色度抠像」）
// =============================================================================

/// 抠像质量档位。
enum class MattingQuality {
    Basic = 0,          ///< 基础版（精度 85% 心智：过渡带更宽、溢色抑制受限）
    HighPrecision = 1,  ///< 高精度版（Pro 专属：全参数逐像素 + 完整溢色抑制）
};

/// 从整数解析质量档位（0 = Basic / 1 = HighPrecision；非法返回 false）。
bool mattingQualityFromInt(int value, MattingQuality& out);
/// 档位名称（"basic" / "high_precision"）。
const char* mattingQualityName(MattingQuality quality);

/// 按质量档位调整处理参数：
/// - Basic（免费心智）：过渡带至少 0.30 宽（处理更粗）、溢色抑制上限 0.4；
/// - HighPrecision：原样（完整溢色抑制与更窄过渡带 → 更干净的边缘）。
ChromaKey chromaKeyForQuality(const ChromaKey& key, MattingQuality quality);

/// 抠像功能门控结果（文档权益矩阵：色度抠像为 Pro 专属 → 免费版付费墙）。
struct MattingAccess {
    bool allowed = false;
    /// 生效档位（不允许时为 Basic）。
    MattingQuality quality = MattingQuality::Basic;
    /// 展示文案（含 🔒 语义，可直接用于 UI）。
    std::string reason;
    /// 是否应弹付费墙。
    bool showPaywall = false;
};

/// 色度抠像门控（fail-closed）：
/// - proActive（由调用方用 proActiveAt(now) 判定，纵深防御）→ 允许，档位 = requested；
/// - 非 Pro → 不允许（showPaywall + 🔒 文案）。
MattingAccess mattingAccessFor(bool proActive, MattingQuality requested);

}  // namespace core
}  // namespace nairoclip
