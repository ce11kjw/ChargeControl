package com.ce11kjw.chargecontrol.ui.charge

import androidx.compose.ui.graphics.graphicsLayer
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
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ce11kjw.chargecontrol.HapticUtils
import com.ce11kjw.chargecontrol.ui.components.CyberJoystickSwitch
import com.ce11kjw.chargecontrol.ui.components.FancySlider
import com.ce11kjw.chargecontrol.ui.theme.*

/**
 * 充电控制页 — 与 ChargeControl 模块 (127.0.0.1:8800) 交互
 */
@Composable
fun ChargeControlScreen(vm: ChargeControlViewModel = androidx.lifecycle.viewmodel.compose.viewModel()) {
    val ctx = LocalContext.current
    val scroll = rememberScrollState()

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
                Column {
                    Text(
                        if (vm.online) "模块在线" else "模块未连接",
                        fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary
                    )
                    Text(
                        if (vm.online) "battd · 127.0.0.1:8800"
                        else "请确认 ChargeControl 模块已安装并运行",
                        fontSize = 11.sp, color = TextSecondary
                    )
                }
            }
        }

        if (!vm.online) {
            Text("提示：本页需要 ChargeControl 模块（root 模块）在运行。",
                fontSize = 12.sp, color = TextSecondary)
            return@Column
        }

        // ── 实时状态 ──
        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.08f)),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text("实时状态", fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Stat("电量", "${vm.capacity}%")
                    Stat("温度", "${vm.temp}°C")
                    Stat("电流", "${vm.currentMa}mA")
                    Stat("健康", if (vm.health >= 0) "${vm.health}%" else "--")
                }
                Spacer(Modifier.height(8.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Stat("状态", vm.statusText)
                    Stat("循环", if (vm.cycleCount >= 0) "${vm.cycleCount}" else "--")
                    Stat("功率", "%.1fW".format(vm.powerMw / 1000.0))
                    Stat("协议", vm.protoName)
                }
            }
        }

        // ── 温度 2×2 ──
        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.08f)),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text("温度监控", fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Stat("SoC", "${vm.socTemp}°C"); Stat("GPU", "${vm.gpuTemp}°C")
                    Stat("充电IC", "${vm.chgTemp}°C"); Stat("机身", "${vm.caseTemp}°C")
                }
            }
        }

        // ── 充电统计（v1.12.0: sess_* / stats_*）──
        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.08f)),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Text("充电统计", fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                    if (vm.sessActive == 1) {
                        Text("统计中", fontSize = 11.sp, color = SuccessNeon)
                    }
                }
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Stat("本次时长", if (vm.sessMin > 0) "${vm.sessMin}分" else "--")
                    Stat("本次充入", if (vm.sessMah != 0) "${vm.sessMah}mAh" else "--")
                    Stat("累计次数", if (vm.statsCount > 0) "${vm.statsCount}" else "--")
                }
                Spacer(Modifier.height(6.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Stat("累计时长", if (vm.statsMin > 0) "${vm.statsMin / 60}h${vm.statsMin % 60}m" else "--")
                    Stat("累计充入", if (vm.statsMah > 0) "${vm.statsMah}mAh" else "--")
                    Spacer(Modifier.weight(1f))
                }
            }
        }

        // ── 电池健康（v1.12.0: soh_* / health_rating / 厂商型号）──
        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.08f)),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text("电池健康", fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                Spacer(Modifier.height(10.dp))
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                    Stat("健康度", if (vm.sohNative > 0) "${vm.sohNative}%" else "--")
                    Stat("评级", vm.healthRating.ifEmpty { "--" })
                    Stat("循环", if (vm.cycleCount >= 0) "${vm.cycleCount}" else "--")
                }
                if (vm.sohFirst > 0 || vm.sohDeltaX10 != 0) {
                    Spacer(Modifier.height(6.dp))
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Stat("首次记录", if (vm.sohFirst > 0) "${vm.sohFirst}%" else "--")
                        val d = vm.sohDeltaX10 / 10.0
                        Stat("相对变化", if (vm.sohDeltaX10 != 0) "%+.1f%%".format(d) else "0.0%")
                        Spacer(Modifier.weight(1f))
                    }
                }
                if (vm.batManufacturer.isNotEmpty() || vm.batModelName.isNotEmpty()) {
                    Spacer(Modifier.height(6.dp))
                    Text("${vm.batManufacturer} ${vm.batModelName}".trim(),
                        fontSize = 11.sp, color = TextSecondary)
                }
            }
        }

        // ── PD 协议（v1.12.0: pd_* / power_max）──
        if (vm.pdType >= 0 || vm.pdPdoCount > 0 || vm.powerMaxMw > 0) {
            Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(20.dp),
                colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.08f)),
                elevation = CardDefaults.cardElevation(0.dp)
            ) {
                Column(Modifier.padding(16.dp)) {
                    Text("PD 协议", fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                    Spacer(Modifier.height(10.dp))
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                        Stat("档位", vm.pdTierText.ifEmpty { "--" })
                        Stat("档数", if (vm.pdPdoCount > 0) "${vm.pdPdoCount}" else "--")
                        Stat("上限功率", if (vm.powerMaxMw > 0) "%.0fW".format(vm.powerMaxMw / 1000.0) else "--")
                    }
                    if (vm.pdState.isNotEmpty() || vm.pdSvid.isNotEmpty()) {
                        Spacer(Modifier.height(6.dp))
                        Text(listOf(vm.pdState, vm.pdSvid.takeIf { it.isNotEmpty() }?.let { "SVID $it" } ?: "")
                                .filter { it.isNotEmpty() }.joinToString(" · "),
                            fontSize = 11.sp, color = TextSecondary)
                    }
                }
            }
        }

        // ── 充电控制 ──
        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.08f)),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Column(Modifier.weight(1f)) {
                        Text("充电限制", fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                        Text("达到上限后停止充电", fontSize = 11.sp, color = TextSecondary)
                    }
                    CyberJoystickSwitch(checked = vm.limitEnabled, onCheckedChange = { vm.setLimitEnabled(it) })
                }
                /* ★ v1.15.3: 滑块改为【始终显示】。
                   原先包在 if (limitEnabled) 里 —— 开关没开就完全看不到滑块，
                   用户无从调节（反馈："不给滑条我怎么控制"）。
                   现在开关只决定"是否生效"，滑块随时可拖。
                   未启用时整体降低透明度以示区别。 */
                Spacer(Modifier.height(12.dp))
                val dim = if (vm.limitEnabled) 1f else 0.45f
                Column(Modifier.graphicsLayer(alpha = dim)) {
                    SliderRow("充电上限", vm.chargeLimit, 20..100, "%") { vm.setChargeLimit(it) }
                    SliderRow("电池温度上限", vm.tempLimit, 30..60, "°C") { vm.setTempLimit(it) }
                    SliderRow("充电IC温度上限", vm.chgTempLimit, 40..90, "°C") { vm.setChgTempLimit(it) }
                    SliderRow("回充滞后", vm.resumeDelta, 1..20, "%") { vm.setResumeDelta(it) }
                    SliderRow("采样间隔", vm.intervalSec, 1..60, "s") { vm.setInterval(it) }
                }
                if (!vm.limitEnabled) {
                    Spacer(Modifier.height(4.dp))
                    Text("开关关闭时以上设置不生效", fontSize = 10.sp, color = TextSecondary)
                }
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = { HapticUtils.standardTap(ctx); vm.togglePause() }) {
                        Text(if (vm.paused) "恢复充电" else "暂停充电",
                            fontSize = 13.sp, color = NeonPurpleBright)
                    }
                    TextButton(onClick = { HapticUtils.standardTap(ctx); vm.fullOnce() }) {
                        Text("手动充满", fontSize = 13.sp, color = NeonPurpleBright)
                    }
                }
            }
        }

        // ── 高级控制 ──
        Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(20.dp),
            colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.05f)),
            elevation = CardDefaults.cardElevation(0.dp)
        ) {
            Column(Modifier.padding(16.dp)) {
                Text("高级控制", fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = TextPrimary)
                Text("默认关闭，请确认理解后再开启", fontSize = 11.sp, color = WarningNeon)
                Spacer(Modifier.height(12.dp))

                /* ★ v1.15.3: 高级控制的滑块同样改为始终显示
                   （原来必须开开关才出现，用户看不到可调项） */
                AdvSwitch("真旁路（电源直供）", vm.bypassOn, vm.bypassOk) { vm.setBypass(it) }
                Column(Modifier.graphicsLayer(alpha = if (vm.bypassOn) 1f else 0.45f)) {
                    SliderRow("旁路最低电量", vm.bypassMin, 20..80, "%") { vm.setBypassMin(it) }
                }

                AdvSwitch("恒压点调整", vm.cvOn, vm.cvOk) { vm.setCv(it) }
                Column(Modifier.graphicsLayer(alpha = if (vm.cvOn) 1f else 0.45f)) {
                    SliderRow("恒压点", if (vm.cvMv > 0) vm.cvMv else 4400, 4000..4500, "mV") { vm.setCvMv(it) }
                }

                AdvSwitch("充电电流限流", vm.lcOn, true) { vm.setLc(it) }
                Column(Modifier.graphicsLayer(alpha = if (vm.lcOn) 1f else 0.45f)) {
                    SliderRow("限流值", if (vm.lcMa > 0) vm.lcMa else 3000, 500..6000, "mA") { vm.setLcMa(it) }
                }

                AdvSwitch("系统原生夜间充电", vm.nightOn, vm.nightOk) { vm.setNight(it) }
            }
        }

        // ── 提示消息 ──
        if (vm.lastMsg.isNotEmpty()) {
            Card(Modifier.fillMaxWidth(), shape = RoundedCornerShape(14.dp),
                colors = CardDefaults.cardColors(containerColor = NeonPurple.copy(alpha = 0.15f)),
                elevation = CardDefaults.cardElevation(0.dp)
            ) {
                Row(Modifier.fillMaxWidth().padding(12.dp), horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically) {
                    Text(vm.lastMsg, fontSize = 12.sp, color = TextPrimary, modifier = Modifier.weight(1f))
                    TextButton(onClick = { vm.clearMsg() }) { Text("✕", fontSize = 12.sp) }
                }
            }
        }

        Spacer(Modifier.height(80.dp))
    }
}

@Composable
private fun Stat(label: String, value: String) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(label, fontSize = 10.sp, color = TextSecondary)
        Text(value, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, color = NeonPurpleBright)
    }
}

@Composable
private fun SliderRow(label: String, value: Int, range: IntRange, unit: String, onChange: (Int) -> Unit) {
    var v by remember(value) { mutableFloatStateOf(value.toFloat()) }
    Column(Modifier.padding(vertical = 4.dp)) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            Text(label, fontSize = 12.sp, color = TextSecondary)
            Text("$value$unit", fontSize = 12.sp, color = NeonPurpleBright, fontWeight = FontWeight.SemiBold)
        }
        FancySlider(
            value = v,
            onValueChange = { v = it },
            onValueChangeFinished = { onChange(v.toInt()) },
            valueRange = range.first.toFloat()..range.last.toFloat(),
            modifier = Modifier.fillMaxWidth()
        )
    }
}

@Composable
private fun AdvSwitch(label: String, checked: Boolean, supported: Boolean, onChange: (Boolean) -> Unit) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp),
        horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(label, fontSize = 13.sp, color = if (supported) TextPrimary else TextSecondary)
            if (!supported) Text("本机不支持", fontSize = 10.sp, color = ErrorNeon)
        }
        if (supported) CyberJoystickSwitch(checked = checked, onCheckedChange = onChange)
        else Text("—", fontSize = 13.sp, color = TextSecondary)
    }
}
