package com.nairoclip.app

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Divider
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

data class NativeTemplateItem(
    val id: String,
    val name: String,
    val category: String,
    val slotCount: Int,
    val totalDurationUs: Long,
    val musicHint: String,
    val coverPath: String? = null
)

@Composable
fun NativeTemplateScreen(
    templates: List<NativeTemplateItem>,
    loading: Boolean,
    errorMessage: String?,
    selectedId: String?,
    onBack: () -> Unit,
    onSelect: (String?) -> Unit,
    onContinue: (NativeTemplateItem) -> Unit,
    modifier: Modifier = Modifier
) {
    var query by remember { mutableStateOf("") }
    var category by remember { mutableStateOf("全部") }
    val categories = remember(templates) { listOf("全部") + templates.map { it.category }.filter { it.isNotBlank() }.distinct() }
    val filtered = remember(templates, query, category) {
        templates.filter { template ->
            (category == "全部" || template.category == category) &&
                (query.isBlank() || listOf(template.name, template.category, template.musicHint)
                    .any { it.contains(query.trim(), ignoreCase = true) })
        }
    }
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text("AI 模板剪辑", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
        }
        OutlinedTextField(
            value = query, onValueChange = { query = it },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            singleLine = true, label = { Text("搜索模板") }
        )
        Row(
            modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 6.dp),
            horizontalArrangement = Arrangement.spacedBy(4.dp)
        ) {
            categories.forEach { item ->
                TextButton(onClick = { category = item }) {
                    Text(item, color = if (category == item) Color.White else Color(0xFF858B8F), fontSize = 11.sp)
                }
            }
        }
        when {
            loading -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) { CircularProgressIndicator() }
            errorMessage != null -> Box(Modifier.fillMaxSize().padding(20.dp), contentAlignment = Alignment.Center) {
                Text(errorMessage, color = Color(0xFFFFB4AB), fontSize = 12.sp)
            }
            filtered.isEmpty() -> Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text(if (templates.isEmpty()) "暂无可用模板" else "没有匹配的模板", color = Color(0xFF858B8F))
            }
            else -> LazyColumn(
                modifier = Modifier.weight(1f),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(filtered, key = { it.id }) { template ->
                    Card(
                        modifier = Modifier.fillMaxWidth().clickable {
                            onSelect(if (selectedId == template.id) null else template.id)
                        },
                        shape = RoundedCornerShape(12.dp),
                        colors = CardDefaults.cardColors(
                            containerColor = if (selectedId == template.id) Color(0xFF303538) else Color(0xFF191C1E)
                        )
                    ) {
                        Column(Modifier.fillMaxWidth().padding(14.dp)) {
                            Text(template.name, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Medium)
                            Text(
                                listOfNotNull(
                                    template.category.takeIf { it.isNotBlank() },
                                    "${template.slotCount} 个素材位",
                                    formatNativeDuration(template.totalDurationUs),
                                    template.musicHint.takeIf { it.isNotBlank() }?.let { "♪ $it" }
                                ).joinToString(" · "),
                                color = Color(0xFF858B8F), fontSize = 10.sp, modifier = Modifier.padding(top = 5.dp)
                            )
                        }
                    }
                }
            }
        }
        val selected = templates.firstOrNull { it.id == selectedId }
        Button(
            onClick = { selected?.let(onContinue) },
            enabled = selected != null,
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp)
        ) { Text("下一步") }
    }
}

data class NativeTemplateAsset(val id: Long, val name: String, val durationUs: Long, val mediaType: String)
data class NativeTemplateSlot(val index: Int, val label: String, val assetId: Long?, val targetDurationUs: Long)

@Composable
fun NativeTemplateFillScreen(
    templateName: String,
    slots: List<NativeTemplateSlot>,
    assets: List<NativeTemplateAsset>,
    generating: Boolean,
    errorMessage: String?,
    onBack: () -> Unit,
    onAssetSelected: (Int, Long?) -> Unit,
    onGenerate: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text(templateName, color = Color.White, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        LazyColumn(
            modifier = Modifier.weight(1f),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            items(slots, key = { it.index }) { slot ->
                var expanded by remember(slot.index) { mutableStateOf(false) }
                val selected = assets.firstOrNull { it.id == slot.assetId }
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(12.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                ) {
                    Column(Modifier.fillMaxWidth().padding(14.dp)) {
                        Text(slot.label, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Medium)
                        Text(
                            selected?.name ?: "尚未选择素材",
                            color = if (selected == null) Color(0xFF858B8F) else Color(0xFFE0E3E4),
                            fontSize = 11.sp, modifier = Modifier.padding(top = 5.dp)
                        )
                        TextButton(onClick = { expanded = true }) { Text("选择素材") }
                        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                            DropdownMenuItem(
                                text = { Text("清空素材") },
                                onClick = { expanded = false; onAssetSelected(slot.index, null) }
                            )
                            assets.forEach { asset ->
                                DropdownMenuItem(
                                    text = { Text("${asset.name} · ${formatNativeDuration(asset.durationUs)}") },
                                    onClick = { expanded = false; onAssetSelected(slot.index, asset.id) }
                                )
                            }
                        }
                    }
                }
            }
        }
        errorMessage?.let { Text(it, color = Color(0xFFFFB4AB), fontSize = 12.sp, modifier = Modifier.padding(horizontal = 16.dp)) }
        Button(
            onClick = onGenerate,
            enabled = slots.isNotEmpty() && slots.all { it.assetId != null } && !generating,
            modifier = Modifier.fillMaxWidth().padding(16.dp)
        ) { Text(if (generating) "正在提交…" else "开始生成") }
    }
}

@Composable
fun NativeTemplateProgressScreen(
    status: String,
    progress: Float?,
    completed: Boolean,
    failed: Boolean,
    canCancel: Boolean,
    onCancel: () -> Unit,
    onDone: () -> Unit,
    modifier: Modifier = Modifier
) {
    val validProgress = progress?.takeIf { it.isFinite() }?.coerceIn(0f, 1f)
    Column(
        modifier.fillMaxSize().background(Color(0xFF0D0F10)).padding(24.dp),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(
            when {
                completed -> "生成完成"
                failed -> "生成失败"
                else -> "正在生成"
            },
            color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.SemiBold
        )
        Text(status, color = Color(0xFF9B9FA2), fontSize = 12.sp, modifier = Modifier.padding(top = 12.dp))
        if (!completed && !failed) {
            if (validProgress != null) {
                androidx.compose.material3.LinearProgressIndicator(
                    progress = validProgress,
                    modifier = Modifier.fillMaxWidth().padding(top = 20.dp)
                )
                Text("${(validProgress * 100f).toInt()}%", color = Color(0xFFB9BEC1), fontSize = 11.sp)
            } else {
                CircularProgressIndicator(modifier = Modifier.padding(top = 20.dp))
            }
        }
        if (completed || failed) {
            Button(onClick = onDone, modifier = Modifier.fillMaxWidth().padding(top = 22.dp)) { Text("返回") }
        } else if (canCancel) {
            TextButton(onClick = onCancel, modifier = Modifier.padding(top = 18.dp)) { Text("取消生成") }
        }
    }
}

data class NativeAIReference(val id: Long, val name: String, val durationUs: Long)
data class NativeAIShot(val index: Int, val startUs: Long, val endUs: Long, val confidence: Double, val motionLabel: String)
data class NativeAIMusic(val id: Long, val name: String, val durationUs: Long, val bpm: Double)
data class NativeAISlot(val index: Int, val durationSeconds: Double, val assetId: Long?)

@Composable
fun NativeAIEditWizardScreen(
    step: Int,
    references: List<NativeAIReference>,
    selectedReferenceId: Long?,
    analysisLoading: Boolean,
    shots: List<NativeAIShot>,
    videos: List<NativeTemplateAsset>,
    music: List<NativeAIMusic>,
    selectedMusicId: Long?,
    style: String,
    order: String,
    slots: List<NativeAISlot>,
    intensity: Float,
    overlayAssetId: Long?,
    overlayMode: String,
    overlayBlend: Int,
    generating: Boolean,
    errorMessage: String?,
    onBack: () -> Unit,
    onSelectReference: (Long) -> Unit,
    onAnalyze: () -> Unit,
    onSelectMusic: (Long?) -> Unit,
    onStyleChanged: (String) -> Unit,
    onOrderChanged: (String) -> Unit,
    onSelectSlotAsset: (Int, Long?) -> Unit,
    onIntensityChanged: (Float) -> Unit,
    onSelectOverlay: (Long?) -> Unit,
    onOverlayModeChanged: (String) -> Unit,
    onOverlayBlendChanged: (Int) -> Unit,
    onNext: () -> Unit,
    onGenerate: () -> Unit,
    modifier: Modifier = Modifier
) {
    val safeStep = step.coerceIn(1, 4)
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text("AI 参考创作 · $safeStep/4", color = Color.White, fontSize = 17.sp, fontWeight = FontWeight.SemiBold)
        }
        LazyColumn(
            modifier = Modifier.weight(1f),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(9.dp)
        ) {
            when (safeStep) {
                1 -> {
                    item { Text("选择参考视频", color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.SemiBold) }
                    items(references, key = { it.id }) { ref ->
                        Card(
                            modifier = Modifier.fillMaxWidth().clickable { onSelectReference(ref.id) },
                            colors = CardDefaults.cardColors(containerColor = if (selectedReferenceId == ref.id) Color(0xFF303538) else Color(0xFF191C1E))
                        ) {
                            Column(Modifier.fillMaxWidth().padding(13.dp)) {
                                Text(ref.name, color = Color.White, fontSize = 13.sp)
                                Text(formatNativeDuration(ref.durationUs), color = Color(0xFF858B8F), fontSize = 10.sp)
                            }
                        }
                    }
                    item {
                        Button(onClick = onAnalyze, enabled = selectedReferenceId != null && !analysisLoading, modifier = Modifier.fillMaxWidth()) {
                            Text(if (analysisLoading) "分析中…" else "分析参考视频")
                        }
                    }
                }
                2 -> {
                    item {
                        Text("作品风格", color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                        listOf(
                            "smooth" to "丝滑运镜",
                            "effect" to "画面特效",
                            "curve" to "丝滑变速"
                        ).forEach { (value, label) ->
                            TextButton(onClick = { onStyleChanged(value) }) {
                                Text(if (style == value) "✓ $label" else label, color = if (style == value) Color.White else Color(0xFF9B9FA2))
                            }
                        }
                        Text("超燃强度", color = Color(0xFFB9BEC1), fontSize = 12.sp)
                        androidx.compose.material3.Slider(
                            value = intensity.takeIf { it.isFinite() }?.coerceIn(0.2f, 1f) ?: 0.6f,
                            onValueChange = onIntensityChanged,
                            valueRange = 0.2f..1f
                        )
                        Text("镜头分析结果", color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 8.dp))
                    }
                    if (shots.isEmpty()) item { Text("暂无分析结果，请返回重新分析。", color = Color(0xFF858B8F)) }
                    items(shots, key = { it.index }) { shot ->
                        val confidence = shot.confidence.takeIf { it.isFinite() }?.coerceIn(0.0, 1.0)
                        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))) {
                            Column(Modifier.fillMaxWidth().padding(12.dp)) {
                                Text("镜头 ${shot.index + 1} · ${shot.motionLabel}", color = Color.White, fontSize = 12.sp)
                                Text(
                                    "${formatNativeDuration(shot.startUs)} – ${formatNativeDuration(shot.endUs)} · " +
                                        (confidence?.let { "${(it * 100).toInt()}% 置信度" } ?: "置信度不可用"),
                                    color = Color(0xFF858B8F), fontSize = 10.sp
                                )
                            }
                        }
                    }
                }
                3 -> {
                    item { Text("选择配乐", color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.SemiBold) }
                    item {
                        TextButton(onClick = { onSelectMusic(null) }) {
                            Text(if (selectedMusicId == null) "✓ 不添加配乐" else "不添加配乐")
                        }
                    }
                    items(music, key = { it.id }) { track ->
                        Card(
                            modifier = Modifier.fillMaxWidth().clickable { onSelectMusic(track.id) },
                            colors = CardDefaults.cardColors(containerColor = if (selectedMusicId == track.id) Color(0xFF303538) else Color(0xFF191C1E))
                        ) {
                            Row(Modifier.fillMaxWidth().padding(13.dp), verticalAlignment = Alignment.CenterVertically) {
                                Column(Modifier.weight(1f)) {
                                    Text(track.name, color = Color.White, fontSize = 12.sp)
                                    Text("${formatNativeDuration(track.durationUs)} · ${track.bpm.takeIf { it.isFinite() && it > 0 }?.toInt()?.let { "$it BPM" } ?: "BPM —"}", color = Color(0xFF858B8F), fontSize = 10.sp)
                                }
                                if (selectedMusicId == track.id) Text("✓", color = Color.White)
                            }
                        }
                    }
                }
                4 -> {
                    item {
                        Text("填充镜头素材", color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                            listOf("smart" to "智能排序", "original" to "原有顺序").forEach { (value, label) ->
                                TextButton(onClick = { onOrderChanged(value) }) {
                                    Text(if (order == value) "✓ $label" else label, color = if (order == value) Color.White else Color(0xFF9B9FA2))
                                }
                            }
                        }
                    }
                    items(slots, key = { it.index }) { slot ->
                        var expanded by remember(slot.index) { mutableStateOf(false) }
                        val selected = videos.firstOrNull { it.id == slot.assetId }
                        Card(colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))) {
                            Column(Modifier.fillMaxWidth().padding(12.dp)) {
                                Text("镜头 ${slot.index + 1} · ${slot.durationSeconds.takeIf { it.isFinite() && it >= 0 }?.let { "%.1fs".format(it) } ?: "时长不可用"}", color = Color.White, fontSize = 12.sp)
                                Text(selected?.name ?: "未分配素材", color = Color(0xFF9B9FA2), fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
                                TextButton(onClick = { expanded = true }) { Text("选择素材") }
                                DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
                                    videos.forEach { asset ->
                                        DropdownMenuItem(
                                            text = { Text(asset.name) },
                                            onClick = { expanded = false; onSelectSlotAsset(slot.index, asset.id) }
                                        )
                                    }
                                    DropdownMenuItem(
                                        text = { Text("清空") },
                                        onClick = { expanded = false; onSelectSlotAsset(slot.index, null) }
                                    )
                                }
                            }
                        }
                    }
                    item {
                        var overlayMenu by remember { mutableStateOf(false) }
                        var blendMenu by remember { mutableStateOf(false) }
                        TextButton(onClick = { overlayMenu = true }) {
                            Text("叠加素材：${videos.firstOrNull { it.id == overlayAssetId }?.name ?: "不添加"}")
                        }
                        DropdownMenu(expanded = overlayMenu, onDismissRequest = { overlayMenu = false }) {
                            DropdownMenuItem(text = { Text("不添加叠加素材") }, onClick = { overlayMenu = false; onSelectOverlay(null) })
                            videos.forEach { asset ->
                                DropdownMenuItem(text = { Text(asset.name) }, onClick = { overlayMenu = false; onSelectOverlay(asset.id) })
                            }
                        }
                        Text("叠层铺设范围", color = Color(0xFFB9BEC1), fontSize = 12.sp)
                        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState())) {
                            listOf("few" to "少（约 1/3）", "mid" to "中（约 2/3）", "all" to "全部镜头").forEach { (value, label) ->
                                TextButton(onClick = { onOverlayModeChanged(value) }) {
                                    Text(if (overlayMode == value) "✓ $label" else label, color = if (overlayMode == value) Color.White else Color(0xFF9B9FA2))
                                }
                            }
                        }
                        TextButton(onClick = { blendMenu = true }) {
                            Text("混合模式：${when (overlayBlend) { 0 -> "正常"; 1 -> "滤色"; 2 -> "叠加"; 4 -> "强光"; 5 -> "正片叠底"; 6 -> "变亮"; 11 -> "柔光"; 14 -> "差值"; else -> "未知" }}")
                        }
                        DropdownMenu(expanded = blendMenu, onDismissRequest = { blendMenu = false }) {
                            listOf(0 to "正常", 1 to "滤色（光效常用）", 2 to "叠加", 4 to "强光", 5 to "正片叠底", 6 to "变亮", 11 to "柔光", 14 to "差值").forEach { (value, label) ->
                                DropdownMenuItem(
                                    text = { Text(label) },
                                    onClick = { blendMenu = false; onOverlayBlendChanged(value) }
                                )
                            }
                        }
                    }
                }
            }
        }
        errorMessage?.let { Text(it, color = Color(0xFFFFB4AB), fontSize = 11.sp, modifier = Modifier.padding(horizontal = 16.dp)) }
        Button(
            onClick = if (safeStep == 4) onGenerate else onNext,
            enabled = !generating && when (safeStep) {
                1 -> selectedReferenceId != null && !analysisLoading
                2 -> shots.isNotEmpty()
                3 -> true
                else -> slots.isNotEmpty() && slots.all { it.assetId != null }
            },
            modifier = Modifier.fillMaxWidth().padding(16.dp)
        ) {
            Text(
                when {
                    generating -> "处理中…"
                    safeStep == 4 -> "生成工程"
                    else -> "下一步"
                }
            )
        }
    }
}
