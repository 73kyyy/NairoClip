package com.nairoclip.app

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
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

/**
 * Native home/settings presentation surfaces for S5.
 *
 * These screens are deliberately host-driven: they do not invent projects,
 * persist preferences independently, or execute destructive actions. The
 * Activity/repository supplies authoritative data and performs commands.
 */

data class NativeHomeProject(
    val name: String,
    val file: String,
    val durationUs: Long,
    val updatedLabel: String,
    val sizeBytes: Long = 0L
)

data class NativeProjectPreset(
    val id: String,
    val label: String,
    val width: Int,
    val height: Int,
    val frameRateNumerator: Int,
    val frameRateDenominator: Int = 1
) {
    init {
        require(id.isNotBlank())
        require(label.isNotBlank())
        require(width > 0 && height > 0)
        require(frameRateNumerator > 0 && frameRateDenominator > 0)
    }
}

@Composable
fun NativeHomeScreen(
    projects: List<NativeHomeProject>,
    presets: List<NativeProjectPreset>,
    loading: Boolean,
    errorMessage: String?,
    onOpenProject: (NativeHomeProject) -> Unit,
    onCreateProject: (String, NativeProjectPreset) -> Unit,
    onOpenMe: () -> Unit,
    onOpenSettings: () -> Unit,
    onQuickAction: (String) -> Unit,
    onDeleteProjects: (List<NativeHomeProject>) -> Unit,
    modifier: Modifier = Modifier
) {
    var query by remember { mutableStateOf("") }
    var managing by remember { mutableStateOf(false) }
    var selectedFiles by remember { mutableStateOf(setOf<String>()) }
    var showCreateDialog by remember { mutableStateOf(false) }
    var confirmDelete by remember { mutableStateOf(false) }

    LaunchedEffect(projects) {
        val existingFiles = projects.mapTo(mutableSetOf()) { it.file }
        selectedFiles = selectedFiles.intersect(existingFiles)
        if (selectedFiles.isEmpty()) confirmDelete = false
    }

    val filtered = remember(projects, query) {
        if (query.isBlank()) projects
        else projects.filter { it.name.contains(query.trim(), ignoreCase = true) }
    }

    Column(
        modifier = modifier.fillMaxSize().background(Color(0xFF0D0F10))
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 14.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = "N",
                color = Color.White,
                fontSize = 24.sp,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.clickable(onClick = onOpenMe).padding(8.dp)
            )
            Spacer(Modifier.weight(1f))
            TextButton(onClick = onOpenMe) { Text("我的") }
            TextButton(onClick = onOpenSettings) { Text("设置") }
        }

        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(
                start = 18.dp, end = 18.dp, bottom = 28.dp
            ),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                Card(
                    modifier = Modifier.fillMaxWidth().clickable { showCreateDialog = true },
                    shape = RoundedCornerShape(18.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF202326))
                ) {
                    Row(
                        modifier = Modifier.fillMaxWidth().padding(20.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Text("+", color = Color.White, fontSize = 32.sp, fontWeight = FontWeight.Light)
                        Spacer(Modifier.width(16.dp))
                        Column {
                            Text("创建项目", color = Color.White, fontSize = 17.sp, fontWeight = FontWeight.SemiBold)
                            Text("新建剪辑工程", color = Color(0xFF9B9FA2), fontSize = 12.sp)
                        }
                    }
                }
            }
            item {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), modifier = Modifier.fillMaxWidth()) {
                    listOf("自动剪辑" to "auto", "参考创作" to "ref", "节奏卡点" to "beat", "智能抠像" to "cutout")
                        .forEach { (label, action) ->
                            Card(
                                modifier = Modifier.weight(1f).clickable { onQuickAction(action) },
                                shape = RoundedCornerShape(12.dp),
                                colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                            ) {
                                Box(
                                    modifier = Modifier.fillMaxWidth().padding(vertical = 14.dp),
                                    contentAlignment = Alignment.Center
                                ) {
                                    Text(label, color = Color(0xFFE3E5E6), fontSize = 11.sp, maxLines = 1)
                                }
                            }
                        }
                }
            }
            item {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text("我的草稿", color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
                    Spacer(Modifier.weight(1f))
                    TextButton(onClick = {
                        managing = !managing
                        selectedFiles = emptySet()
                    }) { Text(if (managing) "完成" else "管理") }
                }
                OutlinedTextField(
                    value = query,
                    onValueChange = { query = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    label = { Text("搜索草稿名称") }
                )
            }
            if (loading) {
                item {
                    Box(Modifier.fillMaxWidth().padding(28.dp), contentAlignment = Alignment.Center) {
                        CircularProgressIndicator()
                    }
                }
            } else if (errorMessage != null) {
                item {
                    Text(errorMessage, color = Color(0xFFFFB4AB), modifier = Modifier.padding(12.dp))
                }
            } else if (filtered.isEmpty()) {
                item {
                    Column(
                        modifier = Modifier.fillMaxWidth().padding(vertical = 42.dp),
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text(if (query.isBlank()) "还没有草稿" else "没有匹配的草稿", color = Color.White, fontSize = 15.sp)
                        Text(
                            if (query.isBlank()) "创建项目后，工程会显示在这里" else "尝试其他名称",
                            color = Color(0xFF858B8F), fontSize = 12.sp,
                            modifier = Modifier.padding(top = 6.dp)
                        )
                    }
                }
            } else {
                items(filtered, key = { it.file }) { project ->
                    Card(
                        modifier = Modifier.fillMaxWidth().clickable {
                            if (managing) {
                                selectedFiles = if (project.file in selectedFiles) selectedFiles - project.file
                                else selectedFiles + project.file
                            } else onOpenProject(project)
                        },
                        shape = RoundedCornerShape(12.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                    ) {
                        Row(
                            modifier = Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 13.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            if (managing) {
                                Checkbox(
                                    checked = project.file in selectedFiles,
                                    onCheckedChange = { checked ->
                                        selectedFiles = if (checked) selectedFiles + project.file
                                        else selectedFiles - project.file
                                    }
                                )
                            }
                            Column(Modifier.weight(1f)) {
                                Text(project.name, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Medium)
                                Text(
                                    listOfNotNull(
                                        project.durationUs.takeIf { it > 0L }?.let { formatNativeDuration(it) },
                                        project.updatedLabel.takeIf { it.isNotBlank() }
                                    ).joinToString(" · ").ifBlank { "本地工程" },
                                    color = Color(0xFF8F969A), fontSize = 11.sp,
                                    modifier = Modifier.padding(top = 4.dp)
                                )
                            }
                            if (!managing) Text("›", color = Color(0xFF9B9FA2), fontSize = 22.sp)
                        }
                    }
                }
            }
            if (managing && selectedFiles.isNotEmpty()) {
                item {
                    Button(
                        onClick = {
                            confirmDelete = selectedFiles.isNotEmpty()
                        },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text("删除所选（${selectedFiles.size}）")
                    }
                }
            }
        }
    }

    if (confirmDelete) {
        AlertDialog(
            onDismissRequest = { confirmDelete = false },
            title = { Text("删除所选草稿？") },
            text = { Text("将请求平台层删除所选的 ${selectedFiles.size} 个工程。此操作可能无法撤销。") },
            confirmButton = {
                TextButton(onClick = {
                    val selected = projects.filter { it.file in selectedFiles }
                    confirmDelete = false
                    onDeleteProjects(selected)
                }) { Text("确认删除", color = Color(0xFFFFB4AB)) }
            },
            dismissButton = { TextButton(onClick = { confirmDelete = false }) { Text("取消") } }
        )
    }

    if (showCreateDialog) {
        NativeCreateProjectDialog(
            presets = presets,
            onDismiss = { showCreateDialog = false },
            onCreate = { name, preset ->
                showCreateDialog = false
                onCreateProject(name.ifBlank { "未命名工程" }, preset)
            }
        )
    }
}

@Composable
private fun NativeCreateProjectDialog(
    presets: List<NativeProjectPreset>,
    onDismiss: () -> Unit,
    onCreate: (String, NativeProjectPreset) -> Unit
) {
    var name by remember { mutableStateOf("") }
    var selectedPresetId by remember(presets) { mutableStateOf(presets.firstOrNull()?.id.orEmpty()) }
    var menuExpanded by remember { mutableStateOf(false) }
    val selectedPreset = presets.firstOrNull { it.id == selectedPresetId }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("创建项目") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                OutlinedTextField(
                    value = name,
                    onValueChange = { name = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    label = { Text("项目名称") },
                    placeholder = { Text("未命名工程") }
                )
                if (presets.isEmpty()) {
                    Text("当前没有可用的项目规格，请先连接项目配置服务。", color = MaterialTheme.colorScheme.error)
                } else {
                    Box {
                        OutlinedTextField(
                            value = selectedPreset?.label.orEmpty(),
                            onValueChange = {},
                            readOnly = true,
                            modifier = Modifier.fillMaxWidth().clickable { menuExpanded = true },
                            label = { Text("项目规格") }
                        )
                        DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                            presets.forEach { preset ->
                                DropdownMenuItem(
                                    text = { Text("${preset.label} · ${preset.width}×${preset.height} · ${preset.frameRateNumerator}/${preset.frameRateDenominator} fps") },
                                    onClick = {
                                        selectedPresetId = preset.id
                                        menuExpanded = false
                                    }
                                )
                            }
                        }
                    }
                    selectedPreset?.let {
                        Text(
                            "${it.width}×${it.height} · ${it.frameRateNumerator}/${it.frameRateDenominator} fps",
                            color = Color(0xFF858B8F), fontSize = 12.sp
                        )
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                enabled = selectedPreset != null,
                onClick = { selectedPreset?.let { onCreate(name.trim(), it) } }
            ) { Text("创建") }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text("取消") } }
    )
}

private fun formatNativeDuration(durationUs: Long): String {
    val seconds = (durationUs.coerceAtLeast(0L) / 1_000_000L).coerceAtMost(Int.MAX_VALUE.toLong()).toInt()
    val hours = seconds / 3600
    val minutes = (seconds % 3600) / 60
    val remainder = seconds % 60
    return if (hours > 0) "%d:%02d:%02d".format(hours, minutes, remainder)
    else "%02d:%02d".format(minutes, remainder)
}

enum class NativeSettingKind { ACTION, TOGGLE, CHOICE, VALUE }

data class NativeSettingOption(val value: String, val label: String)

data class NativeSettingItem(
    val id: String,
    val title: String,
    val summary: String = "",
    val kind: NativeSettingKind,
    val value: String = "",
    val options: List<NativeSettingOption> = emptyList(),
    val enabled: Boolean = true,
    val destructive: Boolean = false
)

data class NativeSettingGroup(
    val id: String,
    val title: String,
    val items: List<NativeSettingItem>
)

@Composable
fun NativeSettingsScreen(
    groups: List<NativeSettingGroup>,
    appVersion: String,
    onBack: () -> Unit,
    onAction: (String) -> Unit,
    onValueChanged: (String, String) -> Unit,
    modifier: Modifier = Modifier
) {
    var expandedGroup by remember { mutableStateOf<String?>(null) }
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Spacer(Modifier.width(8.dp))
            Text("设置", color = Color.White, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.weight(1f))
            Text(appVersion, color = Color(0xFF858B8F), fontSize = 11.sp)
        }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(start = 16.dp, end = 16.dp, bottom = 28.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            groups.forEach { group ->
                item(key = group.id) {
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(14.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                    ) {
                        Column {
                            Row(
                                modifier = Modifier.fillMaxWidth().clickable {
                                    expandedGroup = if (expandedGroup == group.id) null else group.id
                                }.padding(16.dp),
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text(group.title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Medium)
                                Spacer(Modifier.weight(1f))
                                Text(if (expandedGroup == group.id) "−" else "+", color = Color(0xFF9B9FA2))
                            }
                            if (expandedGroup == group.id) {
                                group.items.forEachIndexed { index, item ->
                                    Divider(color = Color(0xFF2A2E30), thickness = 0.6.dp)
                                    NativeSettingRow(
                                        item = item,
                                        onAction = { onAction(item.id) },
                                        onValueChanged = { value -> onValueChanged(item.id, value) }
                                    )
                                }
                            }
                        }
                    }
                }
            }
            item {
                Text(
                    "© 2026 NairoClip",
                    color = Color(0xFF686F73), fontSize = 11.sp,
                    modifier = Modifier.fillMaxWidth().padding(vertical = 16.dp)
                )
            }
        }
    }
}

@Composable
private fun NativeSettingRow(
    item: NativeSettingItem,
    onAction: () -> Unit,
    onValueChanged: (String) -> Unit
) {
    var menuExpanded by remember(item.id) { mutableStateOf(false) }
    Row(
        modifier = Modifier.fillMaxWidth()
            .then(if (item.kind == NativeSettingKind.ACTION) Modifier.clickable(enabled = item.enabled, onClick = onAction) else Modifier)
            .padding(horizontal = 16.dp, vertical = 12.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(Modifier.weight(1f)) {
            Text(
                item.title,
                color = if (item.destructive) Color(0xFFFFB4AB) else Color(0xFFE6E8E9),
                fontSize = 13.sp
            )
            if (item.summary.isNotBlank()) {
                Text(item.summary, color = Color(0xFF858B8F), fontSize = 10.sp, modifier = Modifier.padding(top = 3.dp))
            }
        }
        when (item.kind) {
            NativeSettingKind.TOGGLE -> Switch(
                checked = item.value == "true",
                onCheckedChange = { onValueChanged(it.toString()) },
                enabled = item.enabled
            )
            NativeSettingKind.CHOICE -> Box {
                TextButton(onClick = { menuExpanded = true }, enabled = item.enabled) {
                    Text(item.options.firstOrNull { it.value == item.value }?.label ?: item.value.ifBlank { "—" })
                }
                DropdownMenu(expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                    item.options.forEach { option ->
                        DropdownMenuItem(
                            text = { Text(option.label) },
                            onClick = {
                                menuExpanded = false
                                onValueChanged(option.value)
                            },
                            enabled = item.enabled
                        )
                    }
                }
            }
            NativeSettingKind.VALUE -> Text(item.value.ifBlank { "—" }, color = Color(0xFF9B9FA2), fontSize = 12.sp)
            NativeSettingKind.ACTION -> Text("›", color = if (item.destructive) Color(0xFFFFB4AB) else Color(0xFF9B9FA2), fontSize = 20.sp)
        }
    }
}


data class NativeAccountSummary(
    val displayName: String,
    val userId: String,
    val phoneLabel: String,
    val loginMethodLabel: String,
    val membershipLabel: String,
    val loggedIn: Boolean
)

@Composable
fun NativeMeScreen(
    account: NativeAccountSummary,
    groups: List<NativeSettingGroup>,
    onBack: () -> Unit,
    onLogin: () -> Unit,
    onBindPhone: () -> Unit,
    onMembership: () -> Unit,
    onLogout: () -> Unit,
    onAction: (String) -> Unit,
    onValueChanged: (String, String) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text("我的", color = Color.White, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
        }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            item {
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(16.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                ) {
                    Column(Modifier.fillMaxWidth().padding(18.dp)) {
                        Text(account.displayName.ifBlank { "未登录" }, color = Color.White, fontSize = 18.sp, fontWeight = FontWeight.SemiBold)
                        Text("ID: ${account.userId.ifBlank { "—" }}", color = Color(0xFF858B8F), fontSize = 11.sp, modifier = Modifier.padding(top = 5.dp))
                        TextButton(onClick = if (account.loggedIn) onBindPhone else onLogin) {
                            Text(if (account.loggedIn) account.phoneLabel else "登录 / 注册")
                        }
                    }
                }
            }
            item {
                Card(
                    modifier = Modifier.fillMaxWidth().clickable(onClick = onMembership),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF242629))
                ) {
                    Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                        Text("会员", color = Color.White, fontWeight = FontWeight.SemiBold)
                        Spacer(Modifier.weight(1f))
                        Text(account.membershipLabel, color = Color(0xFFB9BEC1), fontSize = 12.sp)
                        Text("  ›", color = Color(0xFF9B9FA2), fontSize = 18.sp)
                    }
                }
            }
            items(groups, key = { it.id }) { group ->
                var expanded by remember(group.id) { mutableStateOf(false) }
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(14.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                ) {
                    Column {
                        Row(
                            Modifier.fillMaxWidth().clickable { expanded = !expanded }.padding(16.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text(group.title, color = Color.White, fontSize = 14.sp, fontWeight = FontWeight.Medium)
                            Spacer(Modifier.weight(1f))
                            Text(if (expanded) "−" else "+", color = Color(0xFF9B9FA2))
                        }
                        if (expanded) {
                            group.items.forEach { item ->
                                Divider(color = Color(0xFF2A2E30), thickness = 0.6.dp)
                                NativeSettingRow(
                                    item = item,
                                    onAction = { onAction(item.id) },
                                    onValueChanged = { value -> onValueChanged(item.id, value) }
                                )
                            }
                        }
                    }
                }
            }
            if (account.loggedIn) {
                item {
                    TextButton(onClick = onLogout, modifier = Modifier.fillMaxWidth()) {
                        Text("退出登录", color = Color(0xFFFFB4AB))
                    }
                }
            }
        }
    }
}

data class NativeFeedbackSubmission(
    val type: String,
    val description: String,
    val contact: String
)

@Composable
fun NativeFeedbackScreen(
    submitting: Boolean,
    statusMessage: String?,
    onBack: () -> Unit,
    onSubmit: (NativeFeedbackSubmission) -> Unit,
    onCopy: (NativeFeedbackSubmission) -> Unit,
    modifier: Modifier = Modifier
) {
    var selectedType by remember { mutableStateOf("bug") }
    var description by remember { mutableStateOf("") }
    var contact by remember { mutableStateOf("") }
    val submission = NativeFeedbackSubmission(selectedType, description.trim(), contact.trim())
    val types = listOf("bug" to "功能异常", "idea" to "功能建议", "ui" to "界面问题", "other" to "其他")

    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text("意见反馈", color = Color.White, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
        }
        Column(
            Modifier.fillMaxSize().padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text("反馈类型", color = Color(0xFFB9BEC1), fontSize = 12.sp)
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                types.forEach { (value, label) ->
                    TextButton(onClick = { selectedType = value }) {
                        Text(label, color = if (selectedType == value) Color.White else Color(0xFF858B8F))
                    }
                }
            }
            OutlinedTextField(
                value = description,
                onValueChange = { description = it },
                modifier = Modifier.fillMaxWidth().weight(1f),
                label = { Text("详细描述") },
                placeholder = { Text("请描述遇到的问题或建议") }
            )
            OutlinedTextField(
                value = contact,
                onValueChange = { contact = it },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                label = { Text("联系方式（选填）") }
            )
            statusMessage?.let { Text(it, color = Color(0xFFB9BEC1), fontSize = 12.sp) }
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp), modifier = Modifier.fillMaxWidth()) {
                TextButton(
                    onClick = { onCopy(submission) },
                    enabled = description.isNotBlank() && !submitting,
                    modifier = Modifier.weight(1f)
                ) { Text("复制反馈内容") }
                Button(
                    onClick = { onSubmit(submission) },
                    enabled = description.isNotBlank() && !submitting,
                    modifier = Modifier.weight(1f)
                ) { Text(if (submitting) "提交中…" else "提交反馈") }
            }
            Spacer(Modifier.height(8.dp))
        }
    }
}

data class NativeFaqEntry(val id: String, val question: String, val answer: String)

@Composable
fun NativeFaqScreen(
    entries: List<NativeFaqEntry>,
    onBack: () -> Unit,
    modifier: Modifier = Modifier
) {
    var query by remember { mutableStateOf("") }
    var expandedId by remember { mutableStateOf<String?>(null) }
    val filtered = remember(entries, query) {
        if (query.isBlank()) entries else entries.filter {
            it.question.contains(query.trim(), true) || it.answer.contains(query.trim(), true)
        }
    }
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text("常见问题", color = Color.White, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
        }
        OutlinedTextField(
            value = query, onValueChange = { query = it },
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp),
            singleLine = true, label = { Text("搜索问题") }
        )
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            items(filtered, key = { it.id }) { entry ->
                Card(
                    modifier = Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(12.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                ) {
                    Column(Modifier.fillMaxWidth().clickable {
                        expandedId = if (expandedId == entry.id) null else entry.id
                    }.padding(14.dp)) {
                        Text(entry.question, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Medium)
                        if (expandedId == entry.id) {
                            Text(entry.answer, color = Color(0xFFB9BEC1), fontSize = 12.sp, modifier = Modifier.padding(top = 10.dp))
                        }
                    }
                }
            }
            if (filtered.isEmpty()) item { Text("没有匹配的问题", color = Color(0xFF858B8F)) }
        }
    }
}

data class NativeLegalSection(val id: String, val heading: String, val body: String)

@Composable
fun NativeLegalScreen(
    documentTitle: String,
    sections: List<NativeLegalSection>,
    onBack: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text(documentTitle, color = Color.White, fontSize = 17.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = androidx.compose.foundation.layout.PaddingValues(horizontal = 18.dp, vertical = 12.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            items(sections, key = { it.id }) { section ->
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(section.heading, color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
                    Text(section.body, color = Color(0xFFB9BEC1), fontSize = 12.sp, lineHeight = 19.sp)
                }
            }
        }
    }
}

data class NativeCrashEntry(val id: String, val timestamp: String, val summary: String, val detail: String)

@Composable
fun NativeCrashLogScreen(
    entries: List<NativeCrashEntry>,
    onBack: () -> Unit,
    onCopyEntry: (NativeCrashEntry) -> Unit,
    onExportAll: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier.fillMaxSize().background(Color(0xFF0D0F10))) {
        Row(Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 12.dp), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Text("崩溃日志", color = Color.White, fontSize = 19.sp, fontWeight = FontWeight.SemiBold)
            Spacer(Modifier.weight(1f))
            TextButton(onClick = onExportAll, enabled = entries.isNotEmpty()) { Text("导出") }
        }
        if (entries.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("暂无崩溃日志", color = Color(0xFF858B8F), fontSize = 13.sp)
            }
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxSize(),
                contentPadding = androidx.compose.foundation.layout.PaddingValues(16.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(entries, key = { it.id }) { entry ->
                    Card(
                        modifier = Modifier.fillMaxWidth(),
                        shape = RoundedCornerShape(12.dp),
                        colors = CardDefaults.cardColors(containerColor = Color(0xFF191C1E))
                    ) {
                        Column(Modifier.fillMaxWidth().padding(14.dp)) {
                            Text(entry.summary, color = Color.White, fontSize = 13.sp, fontWeight = FontWeight.Medium)
                            Text(entry.timestamp, color = Color(0xFF858B8F), fontSize = 10.sp, modifier = Modifier.padding(top = 4.dp))
                            Text(entry.detail, color = Color(0xFFB9BEC1), fontSize = 11.sp, modifier = Modifier.padding(top = 8.dp))
                            TextButton(onClick = { onCopyEntry(entry) }) { Text("复制此日志") }
                        }
                    }
                }
            }
        }
    }
}

@Composable
fun NativeAuthScreen(
    availableProviders: List<String>,
    statusMessage: String?,
    busyProvider: String?,
    onAuthorize: (String) -> Unit,
    onPhoneLogin: () -> Unit,
    onBack: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier.fillMaxSize().background(Color(0xFF0D0F10)).padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onBack) { Text("‹ 返回") }
            Spacer(Modifier.weight(1f))
        }
        Spacer(Modifier.height(48.dp))
        Text("N", color = Color.White, fontSize = 42.sp, fontWeight = FontWeight.Bold)
        Text("登录 NairoClip", color = Color.White, fontSize = 19.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 14.dp))
        Text("登录仅用于账号与云服务，本地剪辑不受影响", color = Color(0xFF858B8F), fontSize = 11.sp, modifier = Modifier.padding(top = 8.dp))
        Spacer(Modifier.height(30.dp))
        Button(onClick = onPhoneLogin, modifier = Modifier.fillMaxWidth()) { Text("手机号登录") }
        availableProviders.forEach { provider ->
            TextButton(
                onClick = { onAuthorize(provider) },
                enabled = busyProvider == null,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    when (provider.lowercase()) {
                        "wechat" -> "微信登录"
                        "qq" -> "QQ 登录"
                        "apple" -> "Apple 登录"
                        else -> provider
                    }
                )
            }
        }
        if (busyProvider != null) {
            CircularProgressIndicator(modifier = Modifier.padding(top = 12.dp).size(22.dp))
        }
        statusMessage?.let {
            Text(it, color = Color(0xFFFFB4AB), fontSize = 12.sp, modifier = Modifier.padding(top = 12.dp))
        }
    }
}


@Composable
fun NativePhoneLoginScreen(
    requestInProgress: Boolean,
    submitInProgress: Boolean,
    cooldownSeconds: Int,
    statusMessage: String?,
    onBack: () -> Unit,
    onRequestCode: (String) -> Unit,
    onSubmit: (String, String) -> Unit,
    modifier: Modifier = Modifier
) {
    var phone by remember { mutableStateOf("") }
    var code by remember { mutableStateOf("") }
    Column(
        modifier.fillMaxSize().background(Color(0xFF0D0F10)).padding(22.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp)
    ) {
        TextButton(onClick = onBack) { Text("‹ 返回") }
        Text("手机号登录", color = Color.White, fontSize = 20.sp, fontWeight = FontWeight.SemiBold)
        Text("验证码仅用于本次登录验证", color = Color(0xFF858B8F), fontSize = 11.sp)
        OutlinedTextField(
            value = phone,
            onValueChange = { value -> phone = value.filter { it.isDigit() || it == '+' }.take(16) },
            modifier = Modifier.fillMaxWidth(),
            singleLine = true,
            label = { Text("手机号") }
        )
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedTextField(
                value = code,
                onValueChange = { value -> code = value.filter(Char::isDigit).take(8) },
                modifier = Modifier.weight(1f),
                singleLine = true,
                label = { Text("验证码") }
            )
            TextButton(
                onClick = { onRequestCode(phone.trim()) },
                enabled = phone.length >= 6 && !requestInProgress && cooldownSeconds <= 0
            ) {
                Text(
                    when {
                        requestInProgress -> "发送中…"
                        cooldownSeconds > 0 -> "${cooldownSeconds}s"
                        else -> "获取验证码"
                    }
                )
            }
        }
        statusMessage?.let { Text(it, color = Color(0xFFFFB4AB), fontSize = 12.sp) }
        Button(
            onClick = { onSubmit(phone.trim(), code.trim()) },
            enabled = phone.length >= 6 && code.isNotBlank() && !submitInProgress,
            modifier = Modifier.fillMaxWidth()
        ) { Text(if (submitInProgress) "登录中…" else "登录") }
    }
}
