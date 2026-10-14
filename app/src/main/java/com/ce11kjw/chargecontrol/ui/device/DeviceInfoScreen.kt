package com.ce11kjw.chargecontrol.ui.device

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.ce11kjw.chargecontrol.ui.charge.ChargeControlViewModel
import com.ce11kjw.chargecontrol.ui.theme.*

/**
 * 设备详情页 — 纯模块取数版（v1.8.0）
 *
 * 数据全部来自 ChargeControl 模块的 /api/status：
 *   硬件类型 / SoC / USB / 蓝牙 / WiFi / 安全 / 图形 / 显示 / 屏幕 / 热区 / OEM
 *
 * 已移除（按需求）：
 *   相机、音频、SIM、连接详情、编解码器、DRM
 *   —— 这些需要 Android Framework API，模块（C 程序）拿不到
 */
@Composable
fun DeviceInfoScreen(
    vm: ChargeControlViewModel = androidx.lifecycle.viewmodel.compose.viewModel()
) {
    val scroll = rememberScrollState()
    val s = vm.status

    Column(
        Modifier.fillMaxSize()
            .padding(start = 16.dp, end = 16.dp, top = 16.dp, bottom = 16.dp)
            .verticalScroll(scroll),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        // 模块离线提示
        if (!vm.online) {
            Card(
                Modifier.fillMaxWidth(),
                shape = RoundedCornerShape(20.dp),
                colors = CardDefaults.cardColors(containerColor = ErrorNeon.copy(alpha = 0.12f)),
                elevation = CardDefaults.cardElevation(0.dp)
            ) {
                Column(Modifier.padding(16.dp)) {
                    Text("模块未连接", fontSize = 15.sp,
                        fontWeight = FontWeight.SemiBold, color = TextPrimary)
                    Spacer(Modifier.height(6.dp))
                    Text("本页数据由 ChargeControl 模块提供，请确认模块已安装并运行。",
                        fontSize = 12.sp, color = TextSecondary, lineHeight = 18.sp)
                }
            }
            Spacer(Modifier.height(8.dp))
            return@Column
        }

        val g = { k: String -> ChargeControlApiStr(s, k) }
        val gi = { k: String, d: Int -> ChargeControlApiInt(s, k, d) }

        // ═══ SoC / 芯片 ═══
        InfoCard("SoC / 芯片") {
            Row2("芯片型号", g("dev_soc_chipname"), g("cpu_model"))
            Row2("制程工艺", g("dev_soc_process"), "—")
            Row2("CPU 架构", g("cpu_arch"), "—")
            Row2("核心数", "${gi("cpu_cores", -1)}", "—")
        }

        // ═══ CPU 缓存与频率 ═══
        InfoCard("CPU 缓存与频率") {
            Row2("L1 缓存", g("cpu_cache_l1"), "—")
            Row2("L2 缓存", g("cpu_cache_l2"), "—")
            Row2("L3 缓存", g("cpu_cache_l3"), "—")
            val f = gi("cpu_freq_khz", -1)
            val fm = gi("cpu_freq_max_khz", -1)
            Row2("当前频率", if (f > 0) "${f / 1000} MHz" else "—",
                 if (fm > 0) "最高 ${fm / 1000} MHz" else "—")
        }

        // ═══ GPU（模块采集，四平台覆盖）═══
        InfoCard("GPU · ${g("gpu_model").ifEmpty { "未知" }}") {
            Row2("厂商", g("gpu_vendor"), "—")
            val gf = gi("gpu_freq_khz", -1)
            val gmax = gi("gpu_freq_max_khz", -1)
            Row2("当前频率", if (gf > 0) "${gf / 1000} MHz" else "—",
                 if (gmax > 0) "最高 ${gmax / 1000} MHz" else "—")
            Row2("调速器", g("gpu_governor"), "—")
            val load = gi("gpu_load", -1)
            Row2("当前负载", if (load >= 0) "${load / 10.0}%" else "—", "—")
            Row2("DVFS 状态", if (gi("gpu_throttled", 0) == 1) "已降频" else "正常", "—")
            val gt = gi("gpu_temp_c", -1)
            Row2("GPU 温度", if (gt > 0) "$gt °C" else "—", "—")
        }

        // ═══ 显示 ═══
        InfoCard("显示") {
            Row2("面板类型", g("dev_display_type"), "—")
            Row2("色深", g("dev_display_bit"), "—")
            Row2("色域", g("dev_display_gamut"), "—")
            val w = gi("dev_screen_w", -1), h = gi("dev_screen_h", -1)
            Row2("分辨率", if (w > 0 && h > 0) "${w} × ${h}" else "—", "—")
            val dpi = gi("dev_screen_density", -1)
            Row2("屏幕密度", if (dpi > 0) "$dpi dpi" else "—", "—")
            Row2("峰值亮度", g("oem_display_nits"), "—")
            Row2("最高刷新率", g("oem_display_max_hz"), "—")
        }

        // ═══ 内存规格 ═══
        InfoCard("内存规格") {
            Row2("内存类型", g("dev_memory_type"), "—")
            val spd = gi("dev_mem_speed_mhz", -1)
            Row2("内存频率", if (spd > 0) "$spd MHz" else "—", "—")
            val tot = gi("mem_total", -1)
            Row2("总容量", if (tot > 0) fmtMb(tot) else "—", "—")
            val av = gi("mem_avail", -1)
            Row2("可用", if (av > 0) fmtMb(av) else "—", "—")
            val swap = gi("mem_swap_total", -1)
            Row2("Swap", if (swap > 0) fmtMb(swap) else "无", "—")
        }

        // ═══ 存储规格 ═══
        InfoCard("存储规格") {
            Row2("闪存类型", g("dev_storage_type"), "—")
            Row2("加速特性", g("oem_storage_boost"), "—")
            val st = gi("storage_total", -1), sf = gi("storage_free", -1)
            Row2("/data", if (st > 0) "${fmtMb(st)}（可用 ${fmtMb(sf)}）" else "—", "—")
            // 分区列表
            s?.optJSONArray("parts")?.let { arr ->
                for (i in 0 until arr.length()) {
                    val p = arr.optJSONObject(i) ?: continue
                    val m = p.optString("m")
                    if (m == "/data") continue
                    Row2(m, "${fmtMb(p.optLong("t"))}（可用 ${fmtMb(p.optLong("f"))}）", "—")
                }
            }
        }

        // ═══ 连接 ═══
        InfoCard("连接") {
            Row2("USB", listOf(
                g("dev_usb_version").takeIf { it.isNotEmpty() },
                if (gi("dev_usb_typec", -1) == 1) "Type-C" else null,
                if (gi("dev_usb_host", -1) == 1) "Host" else null
            ).filterNotNull().joinToString(" · ").ifEmpty { "—" }, "—")
            Row2("蓝牙", g("dev_bt_ver"), "—")
            Row2("WiFi", g("dev_wifi_standard"), "—")
            Row2("NFC", if (gi("dev_has_nfc", -1) == 1) "支持" else "不支持", "—")
            Row2("红外", if (gi("dev_has_ir", -1) == 1) "支持" else "不支持", "—")
            Row2("UWB", if (gi("dev_has_uwb", -1) == 1) "支持" else "不支持", "—")
            Row2("无线充电", if (gi("dev_has_wireless_chg", -1) == 1) "支持" else "不支持", "—")
        }

        // ═══ 图形接口 ═══
        InfoCard("图形接口") {
            Row2("OpenGL ES", g("dev_gles"), "—")
            Row2("Vulkan", g("dev_vulkan_level"), "—")
            Row2("EGL 驱动", g("dev_hw_egl"), "—")
        }

        // ═══ 安全 ═══
        InfoCard("安全") {
            Row2("SELinux", g("dev_selinux").ifEmpty { "—" }, "—")
            Row2("加密状态", g("dev_crypto_state"), "—")
            Row2("加密类型", g("dev_crypto_type"), "—")
            Row2("TEE", if (gi("dev_tee_supported", -1) == 1)
                "支持${if (g("dev_tee_ver").isNotEmpty()) " (${g("dev_tee_ver")})" else ""}"
                else "不支持", "—")
            Row2("Bootloader", when (gi("dev_bootloader_locked", -1)) {
                1 -> "已锁定"; 0 -> "已解锁"; else -> "—"
            }, g("oem_verified_boot").takeIf { it.isNotEmpty() }?.let { "验证启动: $it" } ?: "—")
            Row2("OpenSSL", g("dev_openssl"), "—")
        }

        // ═══ 热区 ═══
        InfoCard("温度传感器") {
            Row2("热区数量", "${gi("dev_thermal_zones", 0)} 个", "—")
            val types = g("dev_thermal_types")
            if (types.isNotEmpty()) {
                Text(types, fontSize = 11.sp, color = TextSecondary,
                    lineHeight = 16.sp, modifier = Modifier.padding(top = 4.dp))
            }
        }

        // ═══ 标识 ═══
        InfoCard("设备标识") {
            Row2("机型", g("device_model"), "—")
            Row2("序列号", g("dev_serial"), "—")
            Row2("品牌", g("oem_brand"), "—")
            Row2("指纹", g("oem_fingerprint"), "—")
        }

        // ═══ OEM / 系统 ═══
        InfoCard("系统 · ${g("oem_brand")} ${g("oem_os_name")}".trim()) {
            Row2("Android", g("sys_android_ver"), "—")
            Row2("SDK", "${gi("sys_sdk_level", -1)}", "—")
            Row2("ROM 版本", g("oem_os_version"), "—")
            Row2("安全补丁", g("sys_security_patch"), "—")
            Row2("内核", g("kernel_ver").take(40), "—")
            Row2("Bootloader", g("sys_bootloader"), "—")
            Row2("CPU 调度器", g("oem_cpu_governor"), "—")
            Row2("GPU 调度器", g("oem_gpu_governor"), "—")
        }

        // ═══ 厂商特性 ═══
        InfoCard("厂商特性") {
            Row2("充电协议", g("oem_charge_proto"), "—")
            Row2("最大功率", g("oem_charge_watt"), "—")
            Row2("电池容量", g("oem_bat_capacity"), g("design_mah").takeIf { it != "0" && it.isNotEmpty() }?.let { "$it mAh" } ?: "—")
            Row2("双电芯", if (gi("oem_dual_cell", 0) == 1) "是" else "否", "—")
            Row2("内存融合", g("oem_memory_fusion"), "—")
            Row2("AI 引擎", g("oem_ai_engine"), "—")
            Row2("散热方案", g("oem_thermal"), "—")
            Row2("游戏模式", if (g("oem_game_mode") == "1") "开启" else "关闭", "—")
            Row2("高性能模式", if (g("oem_perf_mode") == "1") "开启" else "关闭", "—")
            Row2("省电模式", if (g("oem_power_save") == "1") "开启" else "关闭", "—")
        }

        Spacer(Modifier.height(24.dp))
    }
}

/* ═══════ 组件 ═══════ */

@Composable
private fun InfoCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(
        Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(20.dp),
        colors = CardDefaults.cardColors(containerColor = CyberCardStart.copy(alpha = 0.6f)),
        elevation = CardDefaults.cardElevation(0.dp)
    ) {
        Column(Modifier.padding(16.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(Modifier.size(3.dp, 14.dp).clip(RoundedCornerShape(2.dp))
                    .background(NeonPurple))
                Spacer(Modifier.width(8.dp))
                Text(title, fontSize = 13.sp,
                    fontWeight = FontWeight.SemiBold, color = TextPrimary)
            }
            Spacer(Modifier.height(8.dp))
            content()
        }
    }
}

/** 左标签 / 右值 的一行；右侧副标题可为空 */
@Composable
private fun Row2(label: String, value: String, sub: String) {
    if (value.isEmpty() || value == "—") return
    Row(
        Modifier.fillMaxWidth().padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.Top
    ) {
        Text(label, fontSize = 12.sp, color = TextSecondary,
            modifier = Modifier.weight(0.4f))
        Column(Modifier.weight(0.6f), horizontalAlignment = Alignment.End) {
            Text(value, fontSize = 12.sp, color = TextPrimary, lineHeight = 17.sp)
            if (sub.isNotEmpty() && sub != "—") {
                Text(sub, fontSize = 10.sp, color = TextSecondary.copy(alpha = 0.7f))
            }
        }
    }
}

/* ═══════ 工具 ═══════ */

private fun fmtMb(mb: Long): String = when {
    mb <= 0 -> "—"
    mb >= 1024 -> "%.1f GB".format(mb / 1024.0)
    else -> "$mb MB"
}

private fun fmtMb(mb: Int): String = fmtMb(mb.toLong())

/** 从 status JSON 安全取字符串 */
private fun ChargeControlApiStr(obj: org.json.JSONObject?, key: String): String =
    obj?.optString(key, "") ?: ""

/** 从 status JSON 安全取整数 */
private fun ChargeControlApiInt(obj: org.json.JSONObject?, key: String, def: Int): Int =
    obj?.optInt(key, def) ?: def
