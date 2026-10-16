package com.ce11kjw.chargecontrol.ui.charge

import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import com.ce11kjw.chargecontrol.data.module.ChargeControlApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.json.JSONObject

/**
 * 充电控制 ViewModel
 * 通过 ChargeControlApi 与模块 battd (127.0.0.1:8800) 通信
 */
class ChargeControlViewModel : ViewModel() {

    var online by mutableStateOf(false); private set
    var status by mutableStateOf<JSONObject?>(null); private set
    var busy by mutableStateOf(false); private set
    var lastMsg by mutableStateOf(""); private set

    /* ★ v1.5.0 充电曲线 & 校准 */
    var curve by mutableStateOf<List<ChargeControlApi.CurvePoint>>(emptyList()); private set
    var sessions by mutableStateOf<List<ChargeControlApi.ChargeSession>>(emptyList()); private set
    var cal by mutableStateOf<ChargeControlApi.CalState?>(null); private set
    var curveLoading by mutableStateOf(false); private set

    private var pollJob: Job? = null
    private var curveJob: Job? = null

    init { startPolling(); startCurvePolling() }

    fun startPolling() {
        pollJob?.cancel()
        pollJob = viewModelScope.launch {
            while (true) {
                val s = ChargeControlApi.fetchStatus()
                online = s != null
                if (s != null) status = s
                delay(3000)
            }
        }
    }

    /**
     * 充电曲线轮询（30s）——比状态轮询慢，因为模块本身就是 30s 采样一次。
     * 单独一条协程，避免影响 3s 的状态刷新节奏。
     */
    fun startCurvePolling() {
        curveJob?.cancel()
        curveJob = viewModelScope.launch {
            while (true) {
                refreshCurve()
                delay(30_000)
            }
        }
    }

    suspend fun refreshCurve() {
        curveLoading = true
        val c = ChargeControlApi.fetchCurve()
        if (c.isNotEmpty()) curve = c
        val s = ChargeControlApi.fetchSessions()
        if (s.isNotEmpty()) sessions = s
        cal = ChargeControlApi.fetchCalibrate()
        curveLoading = false
    }

    /** 校准：start / abort */
    fun calibrate(action: String) {
        busy = true
        viewModelScope.launch {
            val ok = ChargeControlApi.postCalibrate(action)
            lastMsg = if (ok) {
                if (action == "start") "校准已启动 ✓" else "校准已中止 ✓"
            } else "操作失败 ✗"
            busy = false
            cal = ChargeControlApi.fetchCalibrate()
        }
    }

    /** 当前阶段（0未充 1预充 2CC 3CV 4满）——取最后一条采样 */
    val currentStage: Int get() = curve.lastOrNull()?.stage ?: 0
    val stageText: String
        get() = when (currentStage) {
            1 -> "预充电"
            2 -> "恒流 CC"
            3 -> "恒压 CV"
            4 -> "已充满"
            else -> "未充电"
        }

    /** 曲线时间跨度（分钟） */
    val curveSpanMin: Int
        get() = if (curve.size < 2) 0
                else ((curve.last().t - curve.first().t) / 60).toInt()

    override fun onCleared() {
        pollJob?.cancel(); curveJob?.cancel(); super.onCleared()
    }

    // ═══ 便捷读取 ═══
    val limitEnabled get() = ChargeControlApi.jbool(status, "limit_enabled")
    val chargeLimit get() = ChargeControlApi.jint(status, "charge_limit", 80)
    val tempLimit get() = ChargeControlApi.jint(status, "temp_limit", 45)
    val chgTempLimit get() = ChargeControlApi.jint(status, "chg_temp_limit", 60)
    val paused get() = ChargeControlApi.jbool(status, "paused")
    val fullOnce get() = ChargeControlApi.jint(status, "full_once")
    val capacity get() = ChargeControlApi.jint(status, "capacity")
    val temp get() = ChargeControlApi.jint(status, "temp")
    val statusText get() = ChargeControlApi.jstr(status, "status")
    val currentMa get() = ChargeControlApi.jint(status, "current")
    val powerMw get() = ChargeControlApi.jdbl(status, "power_mw")
    val health get() = ChargeControlApi.jint(status, "health", -1)
    val cycleCount get() = ChargeControlApi.jint(status, "cycle_count", -1)
    val protoName get() = ChargeControlApi.jstr(status, "proto_name", "Unknown")
    val pdPdoCount get() = ChargeControlApi.jint(status, "pd_pdo_count", 0)
    val pdMaxMv get() = ChargeControlApi.jint(status, "pd_pdo_max_mv", 0)
    val pdMaxMa get() = ChargeControlApi.jint(status, "pd_pdo_max_ma", 0)
    val socTemp get() = ChargeControlApi.jint(status, "soc_temp")
    val gpuTemp get() = ChargeControlApi.jint(status, "gpu_temp")
    val chgTemp get() = ChargeControlApi.jint(status, "chg_temp")
    val caseTemp get() = ChargeControlApi.jint(status, "case_temp")
    // 高级
    /* ═══ v1.12.0: 补齐此前未接的模块字段 ═══ */

    // ── 电池本体 ──
    val batManufacturer get() = ChargeControlApi.jstr(status, "bat_manufacturer")
    val batModelName get() = ChargeControlApi.jstr(status, "bat_model_name")
    val batPresent get() = ChargeControlApi.jbool(status, "bat_present")
    val sohNative get() = ChargeControlApi.jint(status, "soh_native", -1)
    val sohFirst get() = ChargeControlApi.jint(status, "soh_first", -1)
    val sohDeltaX10 get() = ChargeControlApi.jint(status, "soh_delta_x10", 0)
    val healthRating get() = ChargeControlApi.jstr(status, "health_rating")

    // ── 充电协议 / PD ──
    val pdType get() = ChargeControlApi.jint(status, "pd_type", -1)
    val pdState get() = ChargeControlApi.jstr(status, "pd_state")
    val pdSvid get() = ChargeControlApi.jstr(status, "pd_svid")
    val pdPdoMaxMv get() = ChargeControlApi.jint(status, "pd_pdo_max_mv", 0)
    val pdPdoMaxMa get() = ChargeControlApi.jint(status, "pd_pdo_max_ma", 0)
    val powerMaxMw get() = ChargeControlApi.jint(status, "power_max", 0)
    /** PD 档位文本，如 "9V/3A" */
    val pdTierText: String get() {
        val mv = pdPdoMaxMv; val ma = pdPdoMaxMa
        return if (mv > 0 && ma > 0) "%.1fV/%.1fA".format(mv / 1000.0, ma / 1000.0) else ""
    }

    // ── 高级控制的只读状态 ──
    /** 充电 IC 节流档位（MTK charge_control_limit，非电量上限） */
    val ccLimitVal get() = ChargeControlApi.jint(status, "cc_limit_val", -1)
    val ccLimitSupported get() = ChargeControlApi.jbool(status, "cc_limit_supported")
    /** 电源路径是否开启（真旁路前的状态） */
    val powerPathEn get() = ChargeControlApi.jint(status, "power_path_en", -1)
    val jeitaIndex get() = ChargeControlApi.jint(status, "jeita_index", -1)
    val bypassHw get() = ChargeControlApi.jbool(status, "bypass_hw")
    val nativeNight get() = ChargeControlApi.jint(status, "native_night", -1)

    // ── 充电统计（本轮会话 + 累计）──
    val sessActive get() = ChargeControlApi.jint(status, "sess_active", 0)
    val sessMin get() = ChargeControlApi.jint(status, "sess_min", 0)
    val sessMah get() = ChargeControlApi.jint(status, "sess_mah", 0)
    val statsCount get() = ChargeControlApi.jint(status, "stats_count", 0)
    val statsMin get() = ChargeControlApi.jint(status, "stats_min", 0)
    val statsMah get() = ChargeControlApi.jint(status, "stats_mah", 0)

    // ── 运行状态 ──
    val alerted get() = ChargeControlApi.jint(status, "alerted", 0) == 1
    val fullUntil get() = ChargeControlApi.jint(status, "full_until", 0)
    /** 已触发手动充满 → 显示剩余保护时间 */
    val fullUntilText: String get() {
        val t = fullUntil
        if (t <= 0) return ""
        val left = t - (System.currentTimeMillis() / 1000).toInt()
        return if (left > 0) "剩余保护 %d 分钟".format(left / 60) else ""
    }

    val bypassOn get() = ChargeControlApi.jbool(status, "x_pp")
    val bypassOk get() = ChargeControlApi.jbool(status, "x_pp_ok")
    val bypassMin get() = ChargeControlApi.jint(status, "x_bmin", 50)
    val cvOn get() = ChargeControlApi.jbool(status, "x_cv")
    val cvMv get() = ChargeControlApi.jint(status, "x_cv_mv", 0)
    val cvOk get() = ChargeControlApi.jbool(status, "x_cv_ok")
    val lcOn get() = ChargeControlApi.jbool(status, "x_lc")
    val lcMa get() = ChargeControlApi.jint(status, "x_lc_ma", 0)
    val nightOn get() = ChargeControlApi.jbool(status, "x_nn")
    val nightOk get() = ChargeControlApi.jbool(status, "x_nn_ok")

    /* ★ v1.10.2 修复：高级控制键名此前与模块不匹配 ——
       App 发 x_pp / x_cv / x_lc / x_nn 等短名，
       模块 handle_limit 只认 x_power_path / x_set_cv_en / x_limit_en /
       x_native_night 等长名（或 xpp / xcv / xcc / xnight 这套 conf 别名），
       结果开关"发送成功"但被模块静默忽略 ⇒ 高级控制整块空转。
       现在统一用模块的长名（语义最清晰，且模块从 v1.4.0 起兼容长短名）。 */
    fun setBypass(v: Boolean) = write("x_power_path" to if (v) 1 else 0, msg = "真旁路")
    fun setBypassMin(v: Int) = write("x_bypass_min_soc" to v, msg = "旁路最低电量 ${v}%")
    fun setCv(v: Boolean) = write("x_set_cv_en" to if (v) 1 else 0, msg = "恒压点")
    fun setCvMv(v: Int) = write("x_set_cv_mv" to v, msg = "恒压点 ${v}mV")
    fun setLc(v: Boolean) = write("x_limit_en" to if (v) 1 else 0, msg = "电流限流")
    fun setLcMa(v: Int) = write("x_limit_ma" to v, msg = "限流 ${v}mA")
    fun setNight(v: Boolean) = write("x_native_night" to if (v) 1 else 0, msg = "原生夜间充电")
    /**
     * 写入配置 + **回读验证**。
     *
     * ★ v1.11.1 修复：原先只看 postLimit 的返回（HTTP 200），
     * 但模块对非法值（如 charge_limit=5 超出 20-100 范围）也是回 200 +
     * 内部静默忽略 —— 于是 App 会显示「✓ 成功」而实际没生效。
     * 现在：写完 → 重新拉 status → 比对目标字段是否真的变了。
     */
    private fun write(vararg pairs: Pair<String, Any>, msg: String) {
        busy = true
        viewModelScope.launch {
            val posted = ChargeControlApi.postLimit(*pairs)
            val fresh = ChargeControlApi.fetchStatus()
            if (fresh != null) status = fresh

            val ok = if (!posted) {
                false
            } else if (fresh == null) {
                // 配置已发出，但回读失败（模块刚重启等）—— 不算失败，提示不确定
                lastMsg = "$msg… 已发送（未能确认）"
                busy = false
                return@launch
            } else {
                verifyApplied(fresh, pairs)
            }
            lastMsg = if (ok) "$msg ✓" else "$msg ✗ 未生效（模块可能拒绝该值）"
            busy = false
        }
    }

    /** 逐项核对目标值与模块返回是否一致 */
    private fun verifyApplied(fresh: JSONObject, pairs: Array<out Pair<String, Any>>): Boolean {
        for ((k, v) in pairs) {
            // 只校验数值型（字符串等无法可靠比对）
            val expect: Int = if (v is Boolean) {
                if (v) 1 else 0
            } else if (v is Int) {
                v
            } else {
                continue        // 非数值型跳过（continue 在 if 分支里合法）
            }
            // 模块对同一开关可能以长名或短名回传（v1.4.0 起两者都输出），两个都试
            val aliases: List<String> = when (k) {
                "x_power_path"     -> listOf("x_pp")
                "x_set_cv_en"      -> listOf("x_cv")
                "x_set_cv_mv"      -> listOf("x_cv_mv")
                "x_limit_en"       -> listOf("x_lc")
                "x_limit_ma"       -> listOf("x_lc_ma")
                "x_native_night"   -> listOf("x_nn")
                "x_bypass_min_soc" -> listOf("x_bmin")
                else               -> emptyList()
            }
            val matched = (listOf(k) + aliases).any { key ->
                fresh.has(key) && fresh.optInt(key, Int.MIN_VALUE) == expect
            }
            if (!matched) return false
        }
        return true
    }

    fun setLimitEnabled(v: Boolean) = write("limit_enabled" to if (v) 1 else 0, msg = "充电限制")
    fun setChargeLimit(v: Int) = write("charge_limit" to v, msg = "充电上限 ${v}%")
    fun setTempLimit(v: Int) = write("temp_limit" to v, msg = "温度上限 ${v}°C")
    fun setChgTempLimit(v: Int) = write("chg_temp_limit" to v, msg = "充电IC上限 ${v}°C")


    fun togglePause() {
        val target = !paused
        busy = true
        viewModelScope.launch {
            val posted = ChargeControlApi.postPause(target)
            val fresh = ChargeControlApi.fetchStatus()
            if (fresh != null) status = fresh
            // ★ v1.11.1: 回读校验（原只看 HTTP 200）
            val applied = fresh != null && fresh.optInt("paused", -1) == (if (target) 1 else 0)
            lastMsg = when {
                !posted -> "操作失败 ✗"
                fresh == null -> (if (target) "已暂停充电… 已发送（未能确认）" else "已恢复充电… 已发送（未能确认）")
                applied -> (if (target) "已暂停充电 ✓" else "已恢复充电 ✓")
                else -> "操作 ✗ 未生效"
            }
            busy = false
        }
    }

    fun fullOnce() {
        busy = true
        viewModelScope.launch {
            val posted = ChargeControlApi.postFull(false)
            val fresh = ChargeControlApi.fetchStatus()
            if (fresh != null) status = fresh
            // ★ v1.11.1: 回读校验 —— full_once 是模块侧的运行时状态标记
            val applied = fresh != null && fresh.optInt("full_once", 0) == 1
            lastMsg = when {
                !posted -> "操作失败 ✗"
                fresh == null -> "已触发充满… 已发送（未能确认）"
                applied -> "已触发充满 ✓"
                else -> "已触发充满 ✗ 未生效（可能已达 100%）"
            }
            busy = false
        }
    }

    fun clearMsg() { lastMsg = "" }
}
