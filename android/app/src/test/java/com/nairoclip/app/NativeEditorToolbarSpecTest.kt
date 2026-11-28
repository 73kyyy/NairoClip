package com.nairoclip.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeEditorToolbarSpecTest {
    @Test
    fun firstLevelCategoriesMatchLegacyOrder() {
        assertEquals(
            listOf("剪辑", "模板", "音频", "文字", "画中画", "特效", "转场", "滤镜", "调节", "比例", "背景", "蒙版", "更多"),
            NativeEditorToolbarSpec.categoryNames
        )
        assertEquals(13, NativeEditorToolbarSpec.categories.size)
        assertEquals(13, NativeEditorToolbarSpec.categories.map { it.id }.distinct().size)
    }

    @Test
    fun secondLevelActionsMatchLegacyLabelsAndOrder() {
        val expected = linkedMapOf(
            "剪辑" to listOf("分割", "变速", "补帧", "音量", "删除", "复制", "倒放", "定格", "降噪", "不透明度", "波纹删除", "启用/停用", "场景检测"),
            "模板" to listOf("模板库", "一键套用", "AI 剪辑"),
            "音频" to listOf("音乐", "音效", "提取音乐", "录音", "淡化", "人声分离"),
            "文字" to listOf("新建文本", "识别字幕", "识别歌词", "镜头追踪", "字幕翻译", "文本转语音"),
            "画中画" to listOf("新增画中画", "画面", "混合模式", "蒙版"),
            "特效" to listOf("画面特效", "人物特效", "氛围特效", "动感特效", "转换", "超燃预设"),
            "转场" to listOf("预设转场", "自定义关键帧"),
            "滤镜" to listOf("滤镜库", "LUT", "应用到全部"),
            "调节" to listOf("参数调节", "重置全部"),
            "比例" to listOf("9:16", "16:9", "1:1", "3:4", "4:3", "2.35:1", "自定义"),
            "背景" to listOf("颜色", "模糊", "画布样式", "填充"),
            "蒙版" to listOf("形状", "羽化", "色度键"),
            "更多" to listOf("参考仿剪", "深度图")
        )
        val actual = NativeEditorToolbarSpec.categories.associate { it.name to it.subTools }
        assertEquals(expected, actual)
        assertTrue(NativeEditorToolbarSpec.categories.all { it.subTools.isNotEmpty() })
    }

    @Test
    fun firstLevelIconsMatchTheRepositoryIconSprite() {
        assertEquals(
            listOf(
                "i-clip", "i-auto", "i-audio", "i-text", "i-pip", "i-fx", "i-trans",
                "i-filter", "i-adjust", "i-ratio", "i-bg", "i-mask", "i-ai"
            ),
            NativeEditorToolbarSpec.categories.map { it.iconId }
        )
    }
}
