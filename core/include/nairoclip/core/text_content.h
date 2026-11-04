#pragma once

/* =============================================================================
   文本素材化（§4.6 + §4.10）
   -----------------------------------------------------------------------------
   目标（用户原话归纳）：「字体 → 新建文本」创建后，文本作为**素材**直接
   显示在轨道上，与视频/图片同等地位 —— 可加 8 项几何变换、可打关键帧、
   可调曲线。字幕轨就是文本轨，二者统一。

   ## 关键设计决策（已定案，见 PERF_XFORM_PLAN.md §4.10）

   **栅格化走平台侧**（Android Canvas / iOS CoreText / Windows DirectWrite），
   内核只认位图，不关心来源。

   为什么不用 stb_truetype 统一软渲染：
     · 系统字体引擎有完整的 hinting / 字距 / 连字 / emoji 回退，
       质量明显高于任何内置软渲染器；
     · 「三端像素一致」这个理由不成立 —— 预览本来就按代理分辨率渲染
       （见 §1），像素级差异是预期行为。
   一致性边界：
     · 几何 / 时序  → ✅ 必须逐位一致（由 xform.cpp 的纯数学保证）
     · 字形抗锯齿   → ❌ 不必一致

   ## 与 Subtitle 的关系

   字幕（SubtitleCue）是**独立集合**，由平台层用 HTML 叠加渲染；
   文本片段（TextContent）是**轨道上的 Clip**，走完整的渲染管线。
   二者最终会合流：字幕轨的 Cue 可以转成文本 Clip（迁移函数见下）。

   为什么现在不直接删掉 Subtitle：
     历史工程的 .nclip 里存着 subtitles 数组，直接删会导致老工程丢内容。
     保留并双向转换是最稳妥的迁移路径。
   ============================================================================= */

#include "nairoclip/core/subtitle.h"
#include "nairoclip/core/time.h"

#include <cstdint>
#include <string>
#include <vector>

namespace nairoclip {
namespace core {

/// 文本片段的完整内容描述。
///
/// 这是「素材化」的核心：它让文本拥有了与视频素材对等的输入 ——
/// 一段可渲染成像素的东西。有了它，XformStack / EffectStack /
/// ClipAnimation 全部可以直接复用，不需要为文本另写一套。
struct TextContent {
    /// 文本正文。支持 \n 换行（换行由平台层布局引擎处理，
    /// 内核不做断行 —— 断行规则依赖字体度量，自己做必然与系统不一致）。
    std::string text;

    /// 样式（复用 SubtitleStyle，避免两套字段漂移）。
    ///
    /// 为什么不新建 TextStyle：字幕和文本片段需要同一套字体参数，
    /// 两份结构会在「改一处忘了另一处」时出现视觉不一致。
    SubtitleStyle style;

    /// 栅格化目标宽度上限（像素，导出分辨率坐标系）。
    /// 0 = 不限制（按文本自然宽）。
    /// 平台层据此调 StaticLayout / CTFramesetter 的约束宽度。
    int maxWidth = 0;

    /// 栅格化缓存键的组成部分（决定是否需要重新栅格化）。
    ///
    /// 为什么把平台标识也算进去：同一份工程在 Android 与 iOS 上
    /// 栅格化的字形边缘不同，缓存不能跨平台共享 ——
    /// 共享会导致「在 A 平台改过文本，到 B 平台显示的还是旧位图」。
    std::string cacheKey(const char* platformTag) const;

    /// 内容是否为空（空文本不应产生片段）。
    bool empty() const { return text.empty(); }
};

/// 平台侧栅格化接口。
///
/// **必须是纯虚 + 工厂注入**：内核不链接任何字体库，
/// 由平台层在启动时注册实现。没注册时文本片段渲染为空 ——
/// 这是显式的降级，不是崩溃。
class TextRasterizer {
public:
    virtual ~TextRasterizer() = default;

    /// 把文本栅格化成 RGBA8。
    ///
    /// @param text        内容与样式
    /// @param maxWidth    最大宽度（像素）；0 = 不限
    /// @param maxHeight   最大高度（像素）；0 = 不限
    /// @param outRgba     输出像素（RGBA8 交错，stride = outW * 4）
    /// @param outW/outH   实际输出尺寸
    ///
    /// 实现要求：
    ///   · 背景必须**全透明**（alpha=0），只有字形不透明 ——
    ///     否则文本片段会盖住下方画面；
    ///   · 尺寸必须 ≤ maxWidth/maxHeight（超出应换行或缩字号）；
    ///   · 失败返回 false，outRgba 不修改。
    virtual bool rasterize(const TextContent& text, int maxWidth, int maxHeight,
                           std::vector<std::uint8_t>& outRgba, int& outW, int& outH) = 0;
};

/// 全局栅格化器注册（平台层启动时调用一次）。
void setTextRasterizer(TextRasterizer* rasterizer);
/// 当前注册的栅格化器（可能为空）。
TextRasterizer* textRasterizer();

/// 把一条字幕转换为文本片段内容（迁移用）。
///
/// 语义：字幕是「时间区间 + 文本 + 样式」，文本片段是「素材 + 时间区间」。
/// 转换时文本与样式原样搬过来，时间区间由调用方设置。
TextContent textContentFromCue(const SubtitleCue& cue);

}  // namespace core
}  // namespace nairoclip
