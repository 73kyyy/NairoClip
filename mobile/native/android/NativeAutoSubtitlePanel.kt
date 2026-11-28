package com.nairoclip.app

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Host-owned status for the native automatic-subtitle workflow.
 *
 * The UI never loads models, reads media, or writes subtitle cues itself. The
 * host supplies the selected media/offset, runs the speech worker and publishes
 * the worker result after the engine's atomic subtitle transaction succeeds.
 */
data class NativeAutoSubtitleUiState(
    val available: Boolean = false,
    val running: Boolean = false,
    val progress: Double? = null,
    val statusMessage: String = "自动字幕服务尚未连接",
    val errorMessage: String? = null,
    val canCancel: Boolean = false
)

@Composable
fun NativeAutoSubtitlePanel(
    state: NativeAutoSubtitleUiState,
    onStart: (() -> Unit)?,
    onCancel: (() -> Unit)?,
    modifier: Modifier = Modifier
) {
    val shape = RoundedCornerShape(12.dp)
    val progress = state.progress?.takeIf { it.isFinite() }?.coerceIn(0.0, 1.0)
    Column(
        modifier = modifier
            .background(Color(0xFF202225), shape)
            .border(1.dp, Color(0xFF3A3D40), shape)
            .padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(9.dp)
    ) {
        Text(
            "自动字幕",
            color = Color.White,
            fontSize = 12.sp,
            fontWeight = FontWeight.SemiBold
        )
        Text(
            "识别当前选定素材中的中文、英文语音，并将字幕写入工程时间轴。",
            color = Color(0xFFB8BDC2),
            fontSize = 10.sp
        )
        val visibleMessage = state.errorMessage?.takeIf { it.isNotBlank() }
            ?: state.statusMessage.takeIf { it.isNotBlank() }
            ?: "等待自动字幕服务"
        Text(
            visibleMessage,
            color = if (state.errorMessage.isNullOrBlank()) Color(0xFFB8BDC2) else Color(0xFFFF8A80),
            fontSize = 10.sp
        )
        if (state.running) {
            if (progress != null) {
                LinearProgressIndicator(
                    progress = { progress.toFloat() },
                    modifier = Modifier.fillMaxWidth(),
                    color = Color.White,
                    trackColor = Color(0xFF44484C)
                )
                Text(
                    "${(progress * 100.0).toInt()}%",
                    color = Color(0xFFB8BDC2),
                    fontSize = 9.sp
                )
            } else {
                LinearProgressIndicator(
                    modifier = Modifier.fillMaxWidth(),
                    color = Color.White,
                    trackColor = Color(0xFF44484C)
                )
            }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(
                onClick = { onStart?.invoke() },
                enabled = state.available && !state.running && onStart != null
            ) {
                Text(if (state.running) "识别中…" else "开始识别")
            }
            if (state.running && state.canCancel && onCancel != null) {
                Button(onClick = onCancel) {
                    Text("取消")
                }
            }
        }
        if ((!state.available || onStart == null) && !state.running) {
            Text(
                "需要宿主提供已选素材、模型会话和字幕写回服务；当前不会伪造识别结果。",
                color = Color(0xFF8D949B),
                fontSize = 9.sp
            )
        }
    }
}
