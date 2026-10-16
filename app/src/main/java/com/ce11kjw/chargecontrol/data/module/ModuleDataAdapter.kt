package com.ce11kjw.chargecontrol.data.module

import android.util.Log
import com.ce11kjw.chargecontrol.data.model.BatteryInfo
import com.ce11kjw.chargecontrol.data.model.CpuCoreInfo
import com.ce11kjw.chargecontrol.data.model.CpuInfo
import com.ce11kjw.chargecontrol.data.model.GpuInfo
import com.ce11kjw.chargecontrol.data.model.MemoryInfo
import com.ce11kjw.chargecontrol.data.model.StorageInfo
import org.json.JSONObject

/**
 * 模块数据适配层 —— 把 /api/status 的 JSON 转成 App 各 model
 *
 * 设计原则：
 *   · 模块有的字段一律用模块的（root 可见性更好）
 *   · 模块没有的字段留默认值，由调用方决定是否回退自读
 *   · 单位统一：模块给的是人类可读单位（MB / mA / mV / °C），
 *     App model 要的是 KB / µA / µV，这里做换算
 *
 * 对应模块版本：v1.8.0
 */
object ModuleDataAdapter {

    private const val TAG = "ModuleDataAdapter"

    /** 模块是否可用（调用方先探测，避免每个字段都判空） */
    fun isUsable(status: JSONObject?): Boolean {
        if (status == null) return false
        // 用几个"模块一定有"的字段做探针
        return status.has("capacity") && status.has("mem_total")
    }

    // ═══════════════════ CPU ═══════════════════

    fun toCpuInfo(o: JSONObject): CpuInfo {
        val info = CpuInfo()
        info.timestamp = System.currentTimeMillis()
        info.architecture = o.optString("cpu_arch", "")
        info.coreCount = o.optInt("cpu_cores", 0)

        // 整机使用率（模块是 ×10 定点）
        val usage = o.optInt("cpu_usage", -1)
        info.cpuUsagePercent = if (usage >= 0) usage / 10f else Float.NaN
        val idle = o.optInt("cpu_idle", -1)
        info.totalIdlePercent = if (idle >= 0) idle / 10f else Float.NaN
        val ds = o.optInt("cpu_deep_sleep", -1)
        info.deepSleepPercent = if (ds >= 0) ds / 10f else Float.NaN

        // 缓存（模块给 "32K" / "1024K" / "28160K" 这类字符串）
        info.cacheL1 = o.optString("cpu_cache_l1", "")
        info.cacheL2 = o.optString("cpu_cache_l2", "")
        info.cacheL3 = o.optString("cpu_cache_l3", "")
        info.cpuUsagePercent.let { /* 保留 */ }
        info.cpuidleSource = "module"

        // ★ v1.9.0: 模块提供 cores 数组（逐核 {f,max,min}）
        val f0 = o.optInt("cpu_freq_khz", -1)
        val fmax = o.optInt("cpu_freq_max_khz", -1)
        val fmin = o.optInt("cpu_freq_min_khz", -1)
        val coresArr = o.optJSONArray("cores")
        if (coresArr != null && coresArr.length() > 0) {
            info.cores = (0 until coresArr.length()).mapNotNull { i ->
                val c = coresArr.optJSONObject(i) ?: return@mapNotNull null
                CpuCoreInfo(
                    coreIndex = i,
                    currentFreqKHz = c.optInt("f", -1).toLong(),
                    maxFreqKHz = c.optInt("max", -1).toLong(),
                    minFreqKHz = c.optInt("min", -1).toLong(),
                    usagePercent = Float.NaN
                )
            }.toMutableList()
        } else if (info.coreCount > 0 && f0 > 0) {
            // 兼容旧版模块（只有 cpu0 单值）
            info.cores = (0 until info.coreCount).map { i ->
                CpuCoreInfo(
                    coreIndex = i,
                    currentFreqKHz = f0.toLong(),
                    minFreqKHz = if (fmin > 0) fmin.toLong() else -1L,
                    maxFreqKHz = if (fmax > 0) fmax.toLong() else -1L,
                    usagePercent = Float.NaN
                )
            }.toMutableList()
        }
        return info
    }

    // ═══════════════════ GPU ═══════════════════

    fun toGpuInfo(o: JSONObject): GpuInfo {
        val info = GpuInfo()
        info.timestamp = System.currentTimeMillis()
        info.model = o.optString("gpu_model", "")
        info.vendor = o.optString("gpu_vendor", "")
        info.renderer = info.model

        val f = o.optInt("gpu_freq_khz", -1)
        val fmax = o.optInt("gpu_freq_max_khz", -1)
        val fmin = o.optInt("gpu_freq_min_khz", -1)
        info.frequencyKHz = if (f > 0) f.toLong() else -1L
        info.maxFreqKHz = if (fmax > 0) fmax.toLong() else -1L
        info.minFreqKHz = if (fmin > 0) fmin.toLong() else -1L

        info.governor = o.optString("gpu_governor", "")
        info.availableGovernors = o.optString("gpu_avail_gov", "")

        val load = o.optInt("gpu_load", -1)
        info.loadPercentage = if (load >= 0) load / 10f else Float.NaN
        info.loadSource = o.optString("gpu_load_src", "module")

        val t = o.optInt("gpu_temp_c", -1)
        info.temperatureCelsius = if (t > 0) t.toFloat() else Float.NaN

        info.isThrottled = o.optInt("gpu_throttled", 0) == 1
        info.vulkanSource = ""
        return info
    }

    // ═══════════════════ 内存 ═══════════════════

    fun toMemoryInfo(o: JSONObject): MemoryInfo {
        val info = MemoryInfo()
        info.timestamp = System.currentTimeMillis()

        // 模块全是 kB，App 也是 kB —— 直接搬
        info.totalKB = o.optInt("mem_total", -1).toLong()
        info.availableKB = o.optInt("mem_avail", -1).toLong()
        info.freeMemoryKB = o.optInt("mem_free", -1).toLong()

        info.buffersKB = o.optInt("mem_buffers", -1).toLong()
        info.cachedKB = o.optInt("mem_cached", -1).toLong()
        info.slabReclaimableKB = o.optInt("mem_sreclaim", -1).toLong()
        info.slabUnreclaimKB = o.optInt("mem_sunreclaim", -1).toLong()
        info.shmemKB = o.optInt("mem_shmem", -1).toLong()
        info.pageTablesKB = o.optInt("mem_pagetables", -1).toLong()
        info.kernelStackKB = o.optInt("mem_kernelstack", -1).toLong()
        info.vmallocUsedKB = o.optInt("mem_vmallocused", -1).toLong()
        info.mappedKB = o.optInt("mem_mapped", -1).toLong()

        info.swapTotalKB = o.optInt("mem_swap_total", -1).toLong()
        val swapTotal = info.swapTotalKB
        val swapFree = o.optInt("mem_swap_free", -1).toLong()
        if (swapTotal > 0 && swapFree >= 0) info.swapUsedKB = swapTotal - swapFree

        info.zramOriginalKB = o.optInt("mem_zram_orig", -1).toLong()
        info.zramCompressedKB = o.optInt("mem_zram_compr", -1).toLong()
        info.zramMemUsedTotalKB = o.optInt("mem_zram_used", -1).toLong()
        if (info.zramOriginalKB > 0 && info.zramCompressedKB > 0)
            info.compressionRatio = info.zramOriginalKB.toFloat() / info.zramCompressedKB.toFloat()

        // used = total - available
        if (info.totalKB > 0 && info.availableKB > 0)
            info.usedKB = info.totalKB - info.availableKB
        // 派生：应用 / 缓存 / 系统 / 其他（App 内存分布饼图用）
        if (info.totalKB > 0) {
            val cached = maxOf(info.cachedKB, 0) + maxOf(info.buffersKB, 0) + maxOf(info.slabReclaimableKB, 0)
            info.cachedMemoryKB = cached
            val sys = maxOf(info.slabUnreclaimKB, 0) + maxOf(info.pageTablesKB, 0) +
                      maxOf(info.kernelStackKB, 0) + maxOf(info.vmallocUsedKB, 0) + maxOf(info.shmemKB, 0)
            info.systemMemoryKB = sys
            val app = info.usedKB - cached - sys
            info.appMemoryKB = if (app > 0) app else 0
            info.otherMemoryKB = info.totalKB - info.usedKB - info.freeMemoryKB
        }
        return info
    }

    // ═══════════════════ 电池 ═══════════════════

    fun toBatteryInfo(o: JSONObject): BatteryInfo {
        val info = BatteryInfo()
        info.timestamp = System.currentTimeMillis()

        val t = o.optInt("temp", -1)
        info.temperatureCelsius = if (t > 0) t.toFloat() else Float.NaN
        info.levelPercent = o.optInt("capacity", -1)
        info.chargeStatus = o.optString("status", "")
        info.isCharging = info.chargeStatus.equals("Charging", true) ||
                          info.chargeStatus.equals("Full", true)

        // 电流：模块 mA → App µA；带符号（正=充电）
        val cur = o.optInt("current", 0)
        info.currentNowUA = cur.toLong() * 1000

        // 电压：模块 mV → App ... App 的 voltage 字段就是 mV
        val volt = o.optInt("voltage", -1)
        info.voltage = volt

        // 功率：mA × mV = mW
        val powerMw = if (cur != 0 && volt > 0) (cur.toLong() * volt / 1000) else -1L
        if (info.isCharging) info.chargingPowerMw = powerMw else info.dischargingPowerMw = powerMw

        info.health = o.optString("bat_health", "")
        info.technology = o.optString("bat_technology", "")
        info.cycleCount = o.optInt("cycle_count", -1)

        // 容量：模块 mAh → App mAh
        info.capacityDesignMAh = o.optInt("design_mah", -1).toLong()
        info.chargeFullMAh = o.optInt("charge_full_mah", -1).toLong()
        if (info.chargeFullMAh < 0) {
            // 兜底：用 SOH 反推
            val soh = o.optInt("soh_native", -1)
            if (soh > 0 && info.capacityDesignMAh > 0)
                info.chargeFullMAh = info.capacityDesignMAh * soh / 100
        }
        info.chargeFullDesignMAh = info.capacityDesignMAh
        info.chargeCounterUAh = o.optInt("charge_counter", -1).toLong()
        info.internalResistanceMOhm = o.optInt("internal_resistance", -1).toFloat()

        info.cycleCountSource = "module"
        info.chargeFullSource = "module"
        info.currentNowSource = "module"

        /* ★ v1.12.0: 补齐此前未接的字段（模块早已采集，App 白采） */

        // ── 电池健康度 ──
        val sohNative = o.optInt("soh_native", -1)
        if (sohNative > 0) {
            info.sohPercent = sohNative.toFloat()
            info.healthPercent = sohNative
            info.capacityFadePercent = 100f - sohNative
        } else if (info.chargeFullMAh > 0 && info.capacityDesignMAh > 0) {
            val s2 = info.chargeFullMAh.toFloat() / info.capacityDesignMAh * 100f
            info.sohPercent = s2
            info.healthPercent = s2.toInt()
            info.capacityFadePercent = 100f - s2
        }
        info.sohFirstPercent = o.optInt("soh_first", -1)
        info.sohDeltaX10 = o.optInt("soh_delta_x10", 0)

        // ── 电池本体 ──
        info.manufacturer = o.optString("bat_manufacturer", "")
        info.modelName = o.optString("bat_model_name", "")
        info.batPresent = o.optInt("bat_present", -1) == 1
        info.dualCell = o.optInt("oem_dual_cell", 0) == 1

        // ── 充电协议 / PD ──
        info.protocolDetected = o.optString("proto_name", "")
        info.pdType = o.optInt("pd_type", -1)
        info.pdState = o.optString("pd_state", "")
        info.pdSvid = o.optString("pd_svid", "")
        info.pdPdoCount = o.optInt("pd_pdo_count", 0)
        val pdoMv = o.optInt("pd_pdo_max_mv", 0)
        val pdoMa = o.optInt("pd_pdo_max_ma", 0)
        if (pdoMv > 0) info.maxChargingVoltageUV = pdoMv.toLong() * 1000
        if (pdoMa > 0) info.maxChargingCurrentUA = pdoMa.toLong() * 1000
        val pwrMax = o.optInt("power_max", 0)
        if (pwrMax > 0) info.chargerMaxPowerMw = pwrMax.toLong()

        // ── 插拔 / 电源来源 ──
        info.isPlugged = o.optInt("usb_online", 0) == 1
        info.chargerType = when (o.optInt("charge_type", -1)) {
            1 -> "AC"; 2 -> "USB"; 4 -> "无线"; 8 -> "底座"; else -> ""
        }
        info.powerSourceLabel = if (info.isPlugged) "已连接电源" else "电池供电"

        // ── 实时功率 ──
        val pmw = o.optInt("power_mw", 0)
        if (pmw != 0) info.wattageNow = kotlin.math.abs(pmw) / 1000.0

        // ── 时间估算 ──
        val ttf = o.optInt("time_to_full", -1)
        val tte = o.optInt("time_to_empty", -1)
        info.chargeRemainingTimeMin = if (ttf > 0) ttf else -1
        info.dischargeRemainingTimeMin = if (tte > 0) tte else -1

        // ── 绝对容量 ──
        info.capacityNowMAh = o.optInt("capacity_disp", -1).toLong()
        info.chargeNowUAh = o.optInt("charge_now", -1).toLong()
        val cRem = o.optInt("charge_counter", -1).toLong()
        if (cRem >= 0) info.capacityRemainingUAh = cRem

        // ── 预估开路电压 ──
        val ocv = o.optInt("voltage_ocv", -1)
        if (ocv > 0) info.estimatedOcvMv = ocv

        // ── 其它 ──
        info.temperatureNormalized = o.optInt("temp_ambient", -1) > 0
        info.isPowerSaveMode = o.optString("oem_power_save", "") == "1"

        /* 当前电量对应的绝对容量：模块给百分比 + charge_counter，
           直接换算成 mAh（比 App 自读 sysfs 更准） */
        if (info.chargeCounterUAh > 0) {
            info.capacityNowMAh = info.chargeCounterUAh / 1000
        }

        /* 未接的 4 个字段（模块确实不采集，留默认值）：
           capacityMaxPct / isCapacityLocked / capacityLockLevel  ← 小米锁容，需读
             /sys/class/power_supply/battery/capacity_max（本机不存在）
           chargerTypeFromPlugged  ← Framework EXTRA_PLUGGED，模块在 C 层拿不到
           currentNormalizedMa     ← BBK 私有归一化，非 BBK 机型无意义
           powerProfileCapacityMAh ← power_profile.xml，非 sysfs */

        return info
    }

    // ═══════════════════ 存储 ═══════════════════

    fun toStorageInfo(o: JSONObject): StorageInfo {
        val info = StorageInfo()
        val totalMb = o.optInt("storage_total", -1).toLong()
        val freeMb = o.optInt("storage_free", -1).toLong()
        if (totalMb > 0) {
            info.internalTotalBytes = totalMb * 1024 * 1024
            info.internalAvailableBytes = freeMb * 1024 * 1024
            info.internalUsedBytes = (totalMb - freeMb) * 1024 * 1024
        }
        // 分区列表
        o.optJSONArray("parts")?.let { arr ->
            for (i in 0 until arr.length()) {
                val p = arr.optJSONObject(i) ?: continue
                val t = p.optLong("t") * 1024 * 1024
                val f = p.optLong("f") * 1024 * 1024
                info.partitions.add(StorageInfo.PartitionInfo(
                    mountPoint = p.optString("m", ""),
                    totalBytes = t,
                    usedBytes = t - f,
                    availableBytes = f
                ))
            }
        }
        return info
    }

    // ═══════════════════ 系统信息（部分字段）═══════════════════

    /** 把模块的若干字段灌进 SystemInfo（只填模块有的） */
    fun fillSystemInfo(info: com.ce11kjw.chargecontrol.data.model.SystemInfo, o: JSONObject) {
        info.kernelVersion = o.optString("kernel_ver", "")
        val up = o.optInt("uptime_secs", -1)
        if (up >= 0) info.uptimeSeconds = up.toLong()
        val ds = o.optInt("cpu_deep_sleep", -1)
        if (ds >= 0) {
            info.deepSleepSeconds = (info.uptimeSeconds * ds / 1000)
            info.sleepSource = "module"
        }
        val av = o.optString("sys_android_ver", "")
        if (av.isNotEmpty()) info.androidVersion = av
    }

    /** 把模块的 OEM/设备字段灌进 OemInfo（只填模块有的） */
    fun fillOemInfo(info: com.ce11kjw.chargecontrol.data.model.OemInfo, o: JSONObject) {
        info.oem = o.optString("oem_brand", "")
        info.osName = o.optString("oem_os_name", "")
        info.osVersion = o.optString("oem_os_version", "")
        info.buildDisplayId = o.optString("sys_build_display", "")
        info.securityPatch = o.optString("sys_security_patch", "")
        info.androidVersion = o.optString("sys_android_ver", "")
        info.sdkLevel = o.optInt("sys_sdk_level", 0)
        info.socModel = o.optString("dev_soc_chipname", "").ifEmpty { o.optString("sys_soc_model", "") }
        info.socManufacturer = o.optString("sys_soc_vendor", "")
        info.chargingProtocol = o.optString("oem_charge_proto", "")
        info.chargingMaxWatt = o.optString("oem_charge_watt", "")
        info.chargingBatteryCapacity = o.optString("oem_bat_capacity", "")
        info.chargingDualCell = o.optInt("oem_dual_cell", 0) == 1
        info.displayPanelType = o.optString("oem_display_panel", "")
        info.displayPanelManufacturer = o.optString("oem_display_vendor", "")
        info.displayMaxRefreshRate = o.optString("oem_display_max_hz", "")
        info.displayPeakBrightness = o.optString("oem_display_nits", "")
        info.cpuGovernor = o.optString("oem_cpu_governor", "")
        info.gpuGovernor = o.optString("oem_gpu_governor", "")
        info.securityChip = o.optString("oem_security_chip", "")
        info.secureBoot = o.optInt("oem_secure_boot", 0) == 1
        info.verifiedBootState = o.optString("oem_verified_boot", "")
        info.gameModeSupported = o.optString("oem_game_mode", "") == "1"
        info.highPerformanceMode = o.optString("oem_perf_mode", "") == "1"
        info.powerSaveMode = o.optString("oem_power_save", "") == "1"
        info.ultraPowerSaveMode = o.optString("oem_ultra_power_save", "") == "1"
        info.aiEngineInfo = o.optString("oem_ai_engine", "")
        info.memoryFusion = o.optString("oem_memory_fusion", "")
        info.thermalSolution = o.optString("oem_thermal", "")
        info.storageBoost = o.optString("oem_storage_boost", "")
    }
}
