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
    private fun write(vararg pairs: Pair<String, Any>, msg: String) {
        busy = true
        viewModelScope.launch {
            val ok = ChargeControlApi.postLimit(*pairs)
            lastMsg = if (ok) "$msg ✓" else "$msg ✗ 失败"
            busy = false
            // 立即刷新一次
            ChargeControlApi.fetchStatus()?.let { status = it }
        }
    }

    fun setLimitEnabled(v: Boolean) = write("limit_enabled" to if (v) 1 else 0, msg = "充电限制")
    fun setChargeLimit(v: Int) = write("charge_limit" to v, msg = "充电上限 ${v}%")
    fun setTempLimit(v: Int) = write("temp_limit" to v, msg = "温度上限 ${v}°C")
    fun setChgTempLimit(v: Int) = write("chg_temp_limit" to v, msg = "充电IC上限 ${v}°C")


    fun togglePause() {
        busy = true
        viewModelScope.launch {
            val ok = ChargeControlApi.postPause(!paused)
            lastMsg = if (ok) (if (!paused) "已暂停充电 ✓" else "已恢复充电 ✓") else "操作失败 ✗"
            busy = false
            ChargeControlApi.fetchStatus()?.let { status = it }
        }
    }

    fun fullOnce() {
        busy = true
        viewModelScope.launch {
            val ok = ChargeControlApi.postFull(false)
            lastMsg = if (ok) "已触发充满 ✓" else "操作失败 ✗"
            busy = false
            ChargeControlApi.fetchStatus()?.let { status = it }
        }
    }

    fun clearMsg() { lastMsg = "" }
}
