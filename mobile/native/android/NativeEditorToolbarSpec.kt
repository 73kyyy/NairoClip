package com.nairoclip.app

/**
 * Source-of-truth presentation inventory for the native editor's first and
 * second toolbar levels. Labels and ordering mirror mobile/assets/editor.js
 * (L1/L2). Keep this as data so parity can be unit-tested without Compose.
 */
internal object NativeEditorToolbarSpec {
    data class Category(
        val id: String,
        val name: String,
        val iconId: String,
        val subTools: List<String>
    )

    val categories: List<Category> = listOf(
        Category("clip", "剪辑", "i-clip", listOf("分割", "变速", "补帧", "音量", "删除", "复制", "倒放", "定格", "降噪", "不透明度", "波纹删除", "启用/停用", "场景检测")),
        Category("tmpl", "模板", "i-auto", listOf("模板库", "一键套用", "AI 剪辑")),
        Category("audio", "音频", "i-audio", listOf("音乐", "音效", "提取音乐", "录音", "淡化", "人声分离")),
        Category("text", "文字", "i-text", listOf("新建文本", "识别字幕", "识别歌词", "镜头追踪", "字幕翻译", "文本转语音")),
        Category("pip", "画中画", "i-pip", listOf("新增画中画", "画面", "混合模式", "蒙版")),
        Category("fx", "特效", "i-fx", listOf("画面特效", "人物特效", "氛围特效", "动感特效", "转换", "超燃预设")),
        Category("trans", "转场", "i-trans", listOf("预设转场", "自定义关键帧")),
        Category("filter", "滤镜", "i-filter", listOf("滤镜库", "LUT", "应用到全部")),
        Category("adjust", "调节", "i-adjust", listOf("参数调节", "重置全部")),
        Category("ratio", "比例", "i-ratio", listOf("9:16", "16:9", "1:1", "3:4", "4:3", "2.35:1", "自定义")),
        Category("bg", "背景", "i-bg", listOf("颜色", "模糊", "画布样式", "填充")),
        Category("mask", "蒙版", "i-mask", listOf("形状", "羽化", "色度键")),
        Category("ai", "更多", "i-ai", listOf("参考仿剪", "深度图"))
    )

    val categoryNames: List<String> = categories.map { it.name }
}
