package com.nairoclip.app

import android.view.View
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView

/**
 * First native editor surface for the S5 migration.
 *
 * This composable deliberately owns presentation only. Editing commands remain
 * routed through the existing editor until their native command adapters and
 * S5 interface contract are accepted. Every not-yet-migrated tool can hand the
 * user back to the complete legacy editor through [onOpenLegacy].
 */
@Composable
fun NativeEditorScreen(
    previewView: View? = null,
    projectName: String = "未命名工程",
    onOpenLegacy: () -> Unit
) {
    var activeTool by remember { mutableStateOf("剪辑") }

    MaterialTheme {
        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(NcColors.background)
        ) {
            EditorTopBar(projectName = projectName, onOpenLegacy = onOpenLegacy)

            PreviewStage(
                previewView = previewView,
                modifier = Modifier.weight(1f).fillMaxWidth()
            )

            QuickActionStrip(onAction = onOpenLegacy)

            NativeTimelinePanel(
                modifier = Modifier
                    .fillMaxWidth()
                    .height(218.dp)
            )

            TransportBar(onAction = onOpenLegacy)

            NativeToolBar(
                activeTool = activeTool,
                onToolSelected = {
                    activeTool = it
                    onOpenLegacy()
                }
            )
        }
    }
}

private object NcColors {
    val background = Color(0xFF101112)
    val topBar = Color(0xFF191A1C)
    val stage = Color(0xFF242526)
    val timeline = Color(0xFF111314)
    val lane = Color(0xFF1A1D1F)
    val laneAlt = Color(0xFF202326)
    val panel = Color(0xFF252729)
    val line = Color(0xFF34373A)
    val text = Color(0xFFF1F1F1)
    val secondary = Color(0xFF9B9B9E)
    val muted = Color(0xFF666B6E)
    val danger = Color(0xFFE56A55)
}

@Composable
private fun EditorTopBar(projectName: String, onOpenLegacy: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(44.dp)
            .background(NcColors.topBar)
            .padding(horizontal = 14.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = "NairoClip",
            color = NcColors.text,
            fontSize = 16.sp,
            fontWeight = FontWeight.Bold
        )
        Spacer(Modifier.width(12.dp))
        Text(
            text = projectName,
            color = NcColors.secondary,
            fontSize = 11.sp,
            maxLines = 1,
            modifier = Modifier.weight(1f)
        )
        Text(
            text = "旧版功能",
            color = NcColors.text,
            fontSize = 11.sp,
            modifier = Modifier
                .clip(RoundedCornerShape(16.dp))
                .border(1.dp, NcColors.secondary, RoundedCornerShape(16.dp))
                .clickable(onClick = onOpenLegacy)
                .padding(horizontal = 12.dp, vertical = 6.dp)
        )
        Spacer(Modifier.width(8.dp))
        Text(
            text = "导出",
            color = NcColors.text,
            fontSize = 12.sp,
            fontWeight = FontWeight.SemiBold,
            modifier = Modifier
                .clip(RoundedCornerShape(16.dp))
                .border(1.dp, NcColors.text, RoundedCornerShape(16.dp))
                .clickable(onClick = onOpenLegacy)
                .padding(horizontal = 14.dp, vertical = 6.dp)
        )
    }
}

@Composable
private fun PreviewStage(previewView: View?, modifier: Modifier = Modifier) {
    Box(
        modifier = modifier
            .background(NcColors.stage)
            .padding(horizontal = 10.dp, vertical = 5.dp),
        contentAlignment = Alignment.Center
    ) {
        BoxWithConstraints(
            modifier = Modifier.fillMaxSize(),
            contentAlignment = Alignment.Center
        ) {
            val maxPreviewHeight = maxHeight
            val maxPreviewWidth = maxWidth
            val previewWidth = with(LocalDensity.current) {
                minOf(maxPreviewWidth, maxPreviewHeight * (9f / 16f))
            }
            Box(
                modifier = Modifier
                    .width(previewWidth)
                    .fillMaxHeight()
                    .clip(RoundedCornerShape(8.dp))
                    .background(Color.Black)
                    .border(1.dp, Color(0x33FFFFFF), RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center
            ) {
                if (previewView != null) {
                    AndroidView(
                        factory = { previewView },
                        modifier = Modifier.fillMaxSize()
                    )
                } else {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        Text("N", color = Color(0xFF777777), fontSize = 34.sp, fontWeight = FontWeight.Light)
                        Text("视频预览", color = NcColors.muted, fontSize = 11.sp)
                    }
                }
            }
        }
    }
}

@Composable
private fun QuickActionStrip(onAction: () -> Unit) {
    val actions = listOf("＋", "⌂", "‹", "◇", "渲染", "◆", "＃", "▧", "AI", "↶", "↷")
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(42.dp)
            .background(NcColors.stage)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        actions.forEachIndexed { index, label ->
            Box(
                modifier = Modifier
                    .size(if (index == 4) 36.dp else 32.dp)
                    .clip(CircleShape)
                    .background(NcColors.panel)
                    .border(1.dp, NcColors.line, CircleShape)
                    .clickable(onClick = onAction),
                contentAlignment = Alignment.Center
            ) {
                Text(
                    text = label,
                    color = NcColors.text,
                    fontSize = if (label == "渲染") 9.sp else 14.sp,
                    fontWeight = FontWeight.Medium
                )
            }
        }
    }
}

@Composable
private fun NativeTimelinePanel(modifier: Modifier = Modifier) {
    AndroidView(
        factory = { context -> NativeTimelineView(context) },
        modifier = modifier
    )
}

@Composable
private fun TimelineRuler() {
    Row(
        modifier = Modifier
            .width(820.dp)
            .height(23.dp)
            .background(NcColors.timeline),
        verticalAlignment = Alignment.CenterVertically
    ) {
        listOf("00:00", "00:05", "00:10", "00:15", "00:20", "00:25", "00:30", "00:35").forEach { time ->
            Box(
                modifier = Modifier.width(102.dp).fillMaxHeight(),
                contentAlignment = Alignment.CenterStart
            ) {
                Text(time, color = NcColors.secondary, fontSize = 8.sp)
            }
        }
    }
}

@Composable
private fun TransportBar(onAction: () -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(46.dp)
            .background(NcColors.timeline),
        horizontalArrangement = Arrangement.Center,
        verticalAlignment = Alignment.CenterVertically
    ) {
        TransportButton("✂", onAction)
        Spacer(Modifier.width(38.dp))
        Box(
            modifier = Modifier
                .size(36.dp)
                .clip(CircleShape)
                .background(Color(0xFF292C2E))
                .clickable(onClick = onAction),
            contentAlignment = Alignment.Center
        ) {
            Text("▶", color = Color.White, fontSize = 14.sp)
        }
        Spacer(Modifier.width(38.dp))
        TransportButton("⌫", onAction, tint = NcColors.danger)
    }
}

@Composable
private fun TransportButton(label: String, onAction: () -> Unit, tint: Color = NcColors.text) {
    Box(
        modifier = Modifier
            .size(34.dp)
            .clip(CircleShape)
            .clickable(onClick = onAction),
        contentAlignment = Alignment.Center
    ) {
        Text(label, color = tint, fontSize = 17.sp)
    }
}

@Composable
private fun NativeToolBar(activeTool: String, onToolSelected: (String) -> Unit) {
    val tools = listOf("剪辑", "音频", "文本", "贴纸", "特效", "画面", "调节", "更多")
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(82.dp)
            .background(NcColors.topBar)
            .horizontalScroll(rememberScrollState())
            .padding(horizontal = 8.dp, vertical = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(7.dp)
    ) {
        tools.forEach { tool ->
            val selected = activeTool == tool
            Column(
                modifier = Modifier
                    .width(54.dp)
                    .clip(RoundedCornerShape(10.dp))
                    .background(if (selected) Color(0xFF303235) else Color.Transparent)
                    .clickable { onToolSelected(tool) }
                    .padding(vertical = 7.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.spacedBy(5.dp)
            ) {
                Box(
                    modifier = Modifier
                        .size(25.dp)
                        .clip(CircleShape)
                        .border(1.dp, if (selected) Color.White else NcColors.line, CircleShape),
                    contentAlignment = Alignment.Center
                ) {
                    Text(tool.take(1), color = NcColors.text, fontSize = 11.sp)
                }
                Text(
                    tool,
                    color = if (selected) NcColors.text else NcColors.secondary,
                    fontSize = 9.sp,
                    textAlign = TextAlign.Center
                )
            }
        }
    }
}
