package com.ce11kjw.chargecontrol.ui.curve

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ce11kjw.chargecontrol.HapticUtils
import com.ce11kjw.chargecontrol.ui.charge.ChargeControlViewModel
import com.ce11kjw.chargecontrol.ui.components.charts.LineChart
import com.ce11kjw.chargecontrol.ui.theme.*
import kotlinx.coroutines.launch
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * 充电曲线页 — 展示模块 (v1.5.0) 记录的充电过程
 *
 * 数据来源：模块 /api/curve（每 30s 一条采样，含 电流/电压/电量/温度/阶段）
 * 图形复用项目已有的 LineChart（Compose Canvas 手绘，无第三方依赖）。
 *
 * 三张图：电流 / 电量 / 温度。另有阶段标签与历史会话列表。
 */
@Composable
fun ChargingCurveScreen(
    vm: ChargeControlViewModel = androidx.lifecycle.viewmodel.compose.viewModel()
) {
    val ctx = LocalContext.current
    val scroll = rememberScrollState()
    val scope = rememberCoroutineScope()
    val curve = vm.curve
    val sessions = vm.sessions

    // 下采样到最多 120 点，避免曲线过于密集且降低绘制开销
    val sampled = remember(curve) {
        if (curve.size <= 120) curve
        else {
            val step = curve.size / 120 + 1
            curve.filterIndexed { i, _ -> i % step == 0 }.let {
                // 保证末点一定在（最新状态）
                if (it.lastOrNull() != curve.last()) it + curve.last() else it
            }
        }
    }

    val curData = remember(sampled) { sampled.map { it.cur.toFloat() } }
    val capData = remember(sampled) { sampled.map { it.cap.toFloat() } }
    val tmpData = remember(sampled) { sampled.map { it.tmp.toFloat() } }

    Column(
        Modifier.fillMaxSize()
            .padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 16.dp)
            .verticalScroll(scroll),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // ── 状态横幅 ──
        Card(
            Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(
                containerColor = if (vm.online) SuccessNeon.copy(alpha = 0.12f)
                                 else ErrorNeon.copy(alpha = 0.12f)
            ),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Row(Modifier.fillMaxWidth().padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(10.dp).clip(RoundedCornerShape(50))
                    .background(if (vm.online) SuccessNeon else ErrorNeon))
                Spacer(Modifier.width(10.dp))
                Column(Modifier.weight(1f)) {
                    Text(
                        if (vm.online) "充电阶段：${vm.stageText}" else "模块未连接",
                        fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary
                    )
                    Text(
                        if (vm.online) {
                            val span = vm.curveSpanMin
                            "${curve.size} 条采样" +
                                if (span > 0) " · 跨度 $span 分钟" else ""
                        } else "本页需要 ChargeControl 模块（root 模块）在运行",
                        fontSize = 11.sp, color = TextSecondary
                    )
                }
                if (vm.curveLoading) {
                    Text("…", fontSize = 16.sp, color = TextSecondary)
                }
            }
        }

        if (curve.isEmpty()) {
            Card(
                Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                colors = CardDefaults.cardColors(containerColor = CyberCardStart.copy(alpha = 0.5f)),
                elevation = CardDefaults.cardElevation(0.dp)
            ) {
                Column(Modifier.padding(20.dp)) {
                    Text("暂无充电采样数据", fontSize = 14.sp,
                        fontWeight = FontWeight.SemiBold, color = TextPrimary)
                    Spacer(Modifier.height(6.dp))
                    Text(
                        "模块在充电时每 30 秒记录一条采样。插上充电器等一会儿再看，" +
                            "或下拉刷新。",
                        fontSize = 12.sp, color = TextSecondary, lineHeight = 18.sp
                    )
                    Spacer(Modifier.height(10.dp))
                    TextButton(onClick = {
                        HapticUtils.lightTap(ctx)
                        scope.launch { vm.refreshCurve() }
                    }) { Text("立即刷新", fontSize = 13.sp, color = NeonCyan) }
                }
            }
            return@Column
        }

        // ── 电流曲线 ──
        CurveCard("充电电流", "mA", curData, NeonMagentaPurple,
            latest = "${curve.last().cur}", idx = 0)

        // ── 电量曲线 ──
        CurveCard("电池电量", "%", capData, SuccessNeon,
            latest = "${curve.last().cap}", idx = 1)

        // ── 温度曲线 ──
        CurveCard("电池温度", "°C", tmpData, WarningNeon,
            latest = "${curve.last().tmp}", idx = 2)

        // ── 当前采样详情 ──
        val last = curve.last()
        Card(
            Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = CyberCardStart.copy(alpha = 0.6f)),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text("最新采样", fontSize = 13.sp,
                    fontWeight = FontWeight.SemiBold, color = TextPrimary)
                Spacer(Modifier.height(8.dp))
                InfoRow("时间", fmtTime(last.t))
                InfoRow("阶段", stageName(last.stage))
                InfoRow("电流", "${last.cur} mA")
                InfoRow("电压", "${last.volt / 1000.0} V")
                InfoRow("电量", "${last.cap} %")
                InfoRow("温度", "${last.tmp} °C")
                InfoRow("本次充入", "${last.mah} mAh")
            }
        }

        // ── 历史充电会话 ──
        if (sessions.isNotEmpty()) {
            Card(
                Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                colors = CardDefaults.cardColors(containerColor = CyberCardStart.copy(alpha = 0.6f)),
                elevation = CardDefaults.cardElevation(0.dp)
            ) {
                Column(Modifier.padding(16.dp)) {
                    Text("历史充电会话", fontSize = 13.sp,
                        fontWeight = FontWeight.SemiBold, color = TextPrimary)
                    Spacer(Modifier.height(4.dp))
                    Text("${sessions.size} 次（最近 10 次）", fontSize = 11.sp, color = TextSecondary)
                    Spacer(Modifier.height(8.dp))
                    sessions.takeLast(10).reversed().forEach { s ->
                        Row(
                            Modifier.fillMaxWidth().padding(vertical = 6.dp),
                            verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(Modifier.weight(1f)) {
                                Text(fmtDate(s.start), fontSize = 12.sp, color = TextPrimary)
                                Text(
                                    "${s.cap0}% → ${s.capMax}% · ${s.mah} mAh",
                                    fontSize = 11.sp, color = TextSecondary
                                )
                            }
                            Text(fmtDur(s.durSec), fontSize = 12.sp, color = NeonCyan)
                        }
                    }
                }
            }
        }

        Spacer(Modifier.height(24.dp))
    }
}

/* ═══════ 组件 ═══════ */

@Composable
private fun CurveCard(
    title: String, unit: String, data: List<Float>,
    color: androidx.compose.ui.graphics.Color, latest: String, idx: Int
) {
    Card(
        Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = CyberCardStart.copy(alpha = 0.6f)),
        elevation = CardDefaults.cardElevation(0.dp)
    ) {
        Column(Modifier.padding(16.dp)) {
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(title, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                Text("$latest $unit", fontSize = 13.sp, fontWeight = FontWeight.Bold, color = color)
            }
            Spacer(Modifier.height(8.dp))
            LineChart(
                data = data,
                modifier = Modifier.fillMaxWidth(),
                lineColor = color,
                areaColor = color.copy(alpha = 0.19f),
                hdrKey = "curve.chart.$idx"
            )
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween) {
        Text(label, fontSize = 12.sp, color = TextSecondary)
        Text(value, fontSize = 12.sp, color = TextPrimary)
    }
}

/* ═══════ 工具 ═══════ */

private fun stageName(st: Int) = when (st) {
    1 -> "预充电"
    2 -> "恒流 CC"
    3 -> "恒压 CV"
    4 -> "已充满"
    else -> "未充电"
}

private val timeFmt = SimpleDateFormat("HH:mm:ss", Locale.getDefault())
private val dateFmt = SimpleDateFormat("MM-dd HH:mm", Locale.getDefault())

private fun fmtTime(sec: Long) = timeFmt.format(Date(sec * 1000))
private fun fmtDate(sec: Long) = dateFmt.format(Date(sec * 1000))

private fun fmtDur(sec: Long): String {
    val h = sec / 3600
    val m = (sec % 3600) / 60
    return if (h > 0) "${h}h${m}m" else "${m}m"
}
