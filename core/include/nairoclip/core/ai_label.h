#pragma once

#include "nairoclip/core/media_io.h"
#include "nairoclip/core/time.h"

#include <string>

namespace nairoclip {
namespace core {

// =============================================================================
// AI 内容标识（合规组件）
// =============================================================================
// 法规依据：《互联网信息服务深度合成管理规定》第十七条——
// 深度合成服务提供者提供深度合成服务，可能导致公众混淆或者误认的，
// 应当在生成或者编辑的信息内容的合理位置、区域进行显著标识。
//
// 设计原则：
//  - 标识机制不可被普通用户关闭/删除（UI 层不提供关闭选项）；
//  - 水印渲染由平台层执行（本模块只产出标准化的标识描述）；
//  - 导出帧携带标识描述，未渲染标识的输出视为不合规（平台测试需覆盖）。
//
// 注意：具体技术规格（尺寸/透明度/位置）建议对标头部大厂方案并向
// 合规机构确认；本实现提供可配置基线。

/// AI 内容标识配置。
struct AiLabelConfig {
    /// 标识文案（默认「AI 生成」；可按算法备案要求调整）。
    std::string text = "AI 生成";
    /// 归一化锚点位置（左上角为 0,0）——默认左下角。
    double anchorX = 0.02;
    double anchorY = 0.94;
    /// 字体大小（画布坐标）。
    double fontSize = 28.0;
    /// 文本颜色（#RRGGBBAA）。
    std::string color = "#FFFFFFB3";  // 白色 70% 不透明
    /// 背景色（#RRGGBBAA；与文字对比度需达标）。
    std::string backgroundColor = "#00000066";  // 黑色 40% 不透明
    /// 是否启用（合规要求：AI 功能产出的工程必须启用）。
    bool enabled = true;
};

/// 构建 AI 标识渲染项（供导出帧附加）。
/// 返回值可直接放入 OutputVideoFrame::subtitles 或独立渲染通道。
SubtitleRenderItem buildAiLabelItem(const AiLabelConfig& config);

/// 判断工程是否需要 AI 标识：
/// 触发条件（任一）：
///  - 使用了自动剪辑（参考风格生成）；
///  - 使用了 TTS 配音；
///  - 用户显式标记。
bool projectRequiresAiLabel(bool usedAutoEdit, bool usedTts, bool userMarked);

}  // namespace core
}  // namespace nairoclip
