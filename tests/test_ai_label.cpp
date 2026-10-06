#include "test_framework.h"
#include "nairoclip/core/ai_label.h"
using namespace nairoclip::core;
NC_TEST(AiLabel, BuildsItemWithDefaults) {
    const AiLabelConfig config;
    const SubtitleRenderItem item = buildAiLabelItem(config);
    NC_CHECK_EQ(item.text, std::string("AI 生成"));
    NC_CHECK(item.anchorX > 0.0 && item.anchorX < 0.1);
    NC_CHECK(item.anchorY > 0.9);
    NC_CHECK_EQ(item.alignment, 0);
    NC_CHECK_EQ(item.fillRgba, std::uint32_t(0xFFFFFFB3));
    NC_CHECK_EQ(item.outlineRgba, std::uint32_t(0x00000066));
}
NC_TEST(AiLabel, ParsesHexColors) {
    AiLabelConfig config;
    config.color = "#FF000080";       // 半透明红
    config.backgroundColor = "#00FF00";  // 不透明绿
    const SubtitleRenderItem item = buildAiLabelItem(config);
    NC_CHECK_EQ(item.fillRgba, std::uint32_t(0xFF000080));
    NC_CHECK_EQ(item.outlineRgba, std::uint32_t(0x00FF00FF));
}
NC_TEST(AiLabel, InvalidColorFallsBackToWhite) {
    AiLabelConfig config;
    config.color = "not-a-color";
    const SubtitleRenderItem item = buildAiLabelItem(config);
    NC_CHECK_EQ(item.fillRgba, std::uint32_t(0xFFFFFFFF));
}
NC_TEST(AiLabel, RequiresLabelLogic) {
    // 全部未使用 → 不需要
    NC_CHECK(!projectRequiresAiLabel(false, false, false));
    // 任一使用 → 需要
    NC_CHECK(projectRequiresAiLabel(true, false, false));
    NC_CHECK(projectRequiresAiLabel(false, true, false));
    NC_CHECK(projectRequiresAiLabel(false, false, true));
}
NC_TEST(AiLabel, CustomTextPreserved) {
    AiLabelConfig config;
    config.text = "AI 生成内容";
    config.fontSize = 36.0;
    const SubtitleRenderItem item = buildAiLabelItem(config);
    NC_CHECK_EQ(item.text, std::string("AI 生成内容"));
    NC_CHECK(std::abs(item.fontSize - 36.0) < 1e-9);
}
