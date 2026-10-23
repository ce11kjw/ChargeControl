package com.ce11kjw.chargecontrol.data.module

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import com.ce11kjw.chargecontrol.data.model.BatteryEvent
import com.ce11kjw.chargecontrol.data.model.EventSnapshotPoint
import org.json.JSONObject
import java.io.BufferedReader
import java.io.InputStreamReader
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder

/**
 * ChargeControl 模块 API 客户端
 *
 * 模块 battd 监听 127.0.0.1:8800（仅回环，无鉴权）
 * 端点:
 *   GET  /api/status  — 全量状态（110+ 字段）
 *   POST /api/limit   — 配置修改（form-urlencoded）
 *   POST /api/full    — 手动充满
 *   POST /api/pause   — 暂停/恢复
 *   GET  /api/history — 历史
 *   GET  /api/log     — 日志
 *   GET  /api/curve     — 充电曲线采样（v1.5.0）
 *   GET  /api/sessions  — 历史充电会话索引（v1.5.0）
 *   GET  /api/calibrate — 校准状态（v1.5.0）
 */
object ChargeControlApi {

    private const val TAG = "ChargeControlApi"
    private const val BASE = "http://127.0.0.1:8800"
    private const val CONNECT_TIMEOUT = 2000
    private const val READ_TIMEOUT = 3000

    /* ═══════════════════════════════════════════════════════════
       v1.12.0: API 能力协商

       模块与 App 独立发版，此前版本错配会"静默失败"
       （App 发 x_pp 而模块只认 x_power_path → 开关无效但显示成功）。
       现在模块在 /api/status 最前面返回 api_version 与 features，
       App 据此判断功能可用性。
       ═══════════════════════════════════════════════════════════ */

    /** 本 App 期望的最低 API 版本。模块低于此值 → 提示用户升级模块。 */
    const val MIN_API_VERSION = 1

    /** 最近一次 status 解析出的能力集（由 DeviceRepository 更新） */
    @Volatile
    var apiVersion: Int = -1
        private set

    @Volatile
    private var featureSet: Set<String> = emptySet()

    /** 从 status JSON 更新能力信息（每次拉取 status 后调用） */
    fun updateCapabilities(status: JSONObject?) {
        if (status == null) return
        apiVersion = status.optInt("api_version", -1)
        featureSet = status.optString("features", "")
            .split(',')
            .map { it.trim() }
            .filter { it.isNotEmpty() }
            .toSet()
        if (apiVersion in 1 until MIN_API_VERSION) {
            Log.w(TAG, "模块 API 版本过旧: $apiVersion < $MIN_API_VERSION，部分功能不可用")
        }
    }

    /** 模块是否支持某能力。模块未上报能力信息时返回 true（宽松兜底，避免误伤旧版）。 */
    fun supports(feature: String): Boolean =
        if (apiVersion < 0) true else feature in featureSet

    /** 模块是否过旧（需要提示升级） */
    val isModuleOutdated: Boolean
        get() = apiVersion in 0 until MIN_API_VERSION

    /** 模块是否在线 */
    suspend fun isOnline(): Boolean = withContext(Dispatchers.IO) {
        try {
            val conn = (URL("$BASE/api/status").openConnection() as HttpURLConnection).apply {
                connectTimeout = 800; readTimeout = 1500; requestMethod = "GET"
            }
            val ok = conn.responseCode == 200
            conn.disconnect()
            ok
        } catch (e: Throwable) { false }
    }

    /** 读取全量状态（原始 JSON；解析交给调用方按需取字段） */
    suspend fun fetchStatus(): JSONObject? = withContext(Dispatchers.IO) {
        try {
            val conn = (URL("$BASE/api/status").openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT; readTimeout = READ_TIMEOUT; requestMethod = "GET"
            }
            if (conn.responseCode != 200) { conn.disconnect(); return@withContext null }
            val text = conn.inputStream.bufferedReader().use(BufferedReader::readText)
            conn.disconnect()
            val json = JSONObject(text)
            /* ★ v1.12.0: 每次拉取都更新能力信息（模块可能刚升级） */
            updateCapabilities(json)
            json
        } catch (e: Throwable) {
            Log.w(TAG, "fetchStatus failed: ${e.message}")
            // 拉取失败视为模块离线，清空能力（下次成功会重建）
            apiVersion = -1
            featureSet = emptySet()
            null
        }
    }

    /** 写入配置（key=value 自动编码） */
    suspend fun postLimit(vararg pairs: Pair<String, Any>): Boolean = withContext(Dispatchers.IO) {
        try {
            val body = pairs.joinToString("&") { (k, v) ->
                "${URLEncoder.encode(k, "UTF-8")}=${URLEncoder.encode(v.toString(), "UTF-8")}"
            }
            val conn = (URL("$BASE/api/limit").openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT; readTimeout = READ_TIMEOUT
                requestMethod = "POST"
                doOutput = true
                setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            }
            conn.outputStream.use { it.write(body.toByteArray()) }
            val ok = conn.responseCode == 200
            conn.disconnect()
            ok
        } catch (e: Throwable) {
            Log.w(TAG, "postLimit failed: ${e.message}")
            false
        }
    }

    /** 手动充满（cancel=true 取消） */
    suspend fun postFull(cancel: Boolean = false): Boolean = withContext(Dispatchers.IO) {
        try {
            val conn = (URL("$BASE/api/full").openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT; readTimeout = READ_TIMEOUT
                requestMethod = "POST"; doOutput = true
            }
            conn.outputStream.use { it.write((if (cancel) "cancel=1" else "").toByteArray()) }
            val ok = conn.responseCode == 200
            conn.disconnect(); ok
        } catch (e: Throwable) { false }
    }

    /** 暂停/恢复 */
    suspend fun postPause(pause: Boolean): Boolean = withContext(Dispatchers.IO) {
        try {
            val conn = (URL("$BASE/api/pause").openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT; readTimeout = READ_TIMEOUT
                requestMethod = "POST"; doOutput = true
            }
            conn.outputStream.use { it.write(("pause=" + if (pause) "1" else "0").toByteArray()) }
            val ok = conn.responseCode == 200
            conn.disconnect(); ok
        } catch (e: Throwable) { false }
    }

    // ═══ 便捷读取（带默认值）═══
    /* ═══════════════════════════════════════════════════════════
       v1.14.0: OTA 热更新

       ⚠️ 注意：升级过程会重启 battd 进程，本 App 的 HTTP 连接会断。
       所以「触发升级」之后不能期待立刻拿到完整进度，
       需要 App 侧轮询 /api/ota 直到状态变成 done 或 failed
       （新进程会把状态从 OTA 目录的 state 文件读回来）。
       ═══════════════════════════════════════════════════════════ */

    /** OTA 状态 */
    data class OtaState(
        val state: String = "idle",      // idle/downloading/applying/done/failed
        val progress: Int = 0,
        val message: String = "",
        val from: String = "",
        val to: String = "",
        val current: String = "",
    ) {
        val isBusy get() = state == "downloading" || state == "applying"
        val isDone get() = state == "done"
        val isFailed get() = state == "failed"
        val hasUpdate get() = to.isNotEmpty() && from.isNotEmpty() && to != from
    }

    /** 检查更新（模块去 GitHub 查 latest release） */
    suspend fun checkUpdate(): JSONObject? = withContext(Dispatchers.IO) {
        try {
            val text = getText("/api/update-check", 12000) ?: return@withContext null
            JSONObject(text)
        } catch (e: Throwable) {
            Log.w(TAG, "checkUpdate failed: ${e.message}")
            null
        }
    }

    /** 查询 OTA 进度 */
    suspend fun fetchOtaState(): OtaState? = withContext(Dispatchers.IO) {
        try {
            val text = getText("/api/ota", 5000) ?: return@withContext null
            val o = JSONObject(text)
            OtaState(
                state = o.optString("state", "idle"),
                progress = o.optInt("progress", 0),
                message = o.optString("message", ""),
                from = o.optString("from", ""),
                to = o.optString("to", ""),
                current = o.optString("current", ""),
            )
        } catch (e: Throwable) {
            Log.w(TAG, "fetchOtaState failed: ${e.message}")
            null
        }
    }

    /**
     * 触发升级。
     * @param ver 目标版本（如 "v1.15.0"），来自 checkUpdate 的 latest 字段
     * @return Pair(是否成功, 提示消息)
     */
    suspend fun startOta(ver: String): Pair<Boolean, String> = withContext(Dispatchers.IO) {
        try {
            val body = "ver=" + URLEncoder.encode(ver, "UTF-8")
            val conn = (URL("$BASE/api/ota").openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT
                readTimeout = 10000
                requestMethod = "POST"
                doOutput = true
                setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            }
            conn.outputStream.use { it.write(body.toByteArray()) }
            val ok = conn.responseCode == 200
            val resp = conn.inputStream.bufferedReader().use(BufferedReader::readText)
            conn.disconnect()
            val o = JSONObject(resp)
            val success = o.optBoolean("ok", false)
            val msg = o.optString("message", o.optString("error", ""))
            Pair(success, msg)
        } catch (e: Throwable) {
            Log.w(TAG, "startOta failed: ${e.message}")
            Pair(false, "无法连接模块: ${e.message}")
        }
    }

    fun jint(o: JSONObject?, key: String, def: Int = -1): Int =
        o?.optInt(key, def) ?: def
    fun jbool(o: JSONObject?, key: String, def: Boolean = false): Boolean =
        o?.optBoolean(key, def) ?: def
    fun jstr(o: JSONObject?, key: String, def: String = ""): String =
        o?.optString(key, def) ?: def
    fun jdbl(o: JSONObject?, key: String, def: Double = 0.0): Double =
        o?.optDouble(key, def) ?: def

    /* ═══════════ v1.5.0 充电曲线 / 会话 / 校准 ═══════════ */

    /** 充电曲线采样点 */
    data class CurvePoint(
        val t: Long,      // 采样时间（秒）
        val cap: Int,     // 电量 %
        val mah: Int,     // 本次充入 mAh
        val tmp: Int,     // 温度 °C
        val cur: Int,     // 电流 mA
        val volt: Int,    // 电压 mV
        val stage: Int    // 0未充 1预充 2恒流CC 3恒压CV 4满
    )

    /** 一次历史充电会话 */
    data class ChargeSession(
        val start: Long,  // 起始时间（秒）
        val durSec: Long, // 时长（秒）
        val cap0: Int,    // 起始电量
        val capMax: Int,  // 峰值电量
        val mah: Int,     // 充入 mAh
        val samples: Int  // 采样条数
    )

    /** 校准状态 */
    data class CalState(
        val state: Int,
        val name: String,
        val elapsedSec: Long,
        val capNow: Int,
        val capMax: Int,
        val capMin: Int,
        val fulfilled: Boolean,
        val temp: Int,
        val charging: Boolean
    )

    private fun getText(path: String, timeout: Int = READ_TIMEOUT): String? =
        try {
            val conn = (URL(BASE + path).openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT; readTimeout = timeout; requestMethod = "GET"
            }
            if (conn.responseCode != 200) { conn.disconnect(); null }
            else conn.inputStream.bufferedReader().use(BufferedReader::readText).also { conn.disconnect() }
        } catch (e: Throwable) {
            Log.w(TAG, "GET $path failed: ${e.message}")
            null
        }

    /**
     * 拉取充电曲线。
     * 模块每 30s 写一条采样，最多返回 1440 条（约 12 小时）。
     */
    suspend fun fetchCurve(): List<CurvePoint> = withContext(Dispatchers.IO) {
        val text = getText("/api/curve", 5000) ?: return@withContext emptyList()
        try {
            val arr = org.json.JSONArray(text)
            buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    add(CurvePoint(
                        t = o.optLong("t"),
                        cap = o.optInt("c", -1),
                        mah = o.optInt("mah"),
                        tmp = o.optInt("tmp"),
                        cur = o.optInt("i"),
                        volt = o.optInt("v"),
                        stage = o.optInt("st")
                    ))
                }
            }
        } catch (e: Throwable) {
            Log.w(TAG, "parse curve failed: ${e.message}")
            emptyList()
        }
    }

    /** 拉取历史充电会话索引 */
    suspend fun fetchSessions(): List<ChargeSession> = withContext(Dispatchers.IO) {
        val text = getText("/api/sessions", 5000) ?: return@withContext emptyList()
        try {
            val arr = org.json.JSONArray(text)
            buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    add(ChargeSession(
                        start = o.optLong("t"),
                        durSec = o.optLong("dur"),
                        cap0 = o.optInt("cap0"),
                        capMax = o.optInt("capmax"),
                        mah = o.optInt("mah"),
                        samples = o.optInt("n")
                    ))
                }
            }
        } catch (e: Throwable) {
            Log.w(TAG, "parse sessions failed: ${e.message}")
            emptyList()
        }
    }

    /** 查询校准状态 */
    suspend fun fetchCalibrate(): CalState? = withContext(Dispatchers.IO) {
        val text = getText("/api/calibrate") ?: return@withContext null
        try {
            val o = JSONObject(text)
            CalState(
                state = o.optInt("state"),
                name = o.optString("state_name", "idle"),
                elapsedSec = o.optLong("elapsed_sec"),
                capNow = o.optInt("cap_now", -1),
                capMax = o.optInt("cap_max", -1),
                capMin = o.optInt("cap_min", -1),
                fulfilled = o.optInt("fulfilled") == 1,
                temp = o.optInt("temp", -1),
                charging = o.optInt("charging") == 1
            )
        } catch (e: Throwable) {
            Log.w(TAG, "parse calibrate failed: ${e.message}")
            null
        }
    }

    /** 校准控制：start / abort */
    suspend fun postCalibrate(action: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val conn = (URL("$BASE/api/calibrate").openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT; readTimeout = READ_TIMEOUT
                requestMethod = "POST"; doOutput = true
                setRequestProperty("Content-Type", "application/x-www-form-urlencoded")
            }
            conn.outputStream.use { it.write("action=$action".toByteArray()) }
            val ok = conn.responseCode == 200
            conn.disconnect()
            ok
        } catch (e: Throwable) {
            Log.w(TAG, "postCalibrate failed: ${e.message}")
            false
        }
    }

    /* ═══════════════════════════════════════════════════════════
       v1.17.0 异常事件（温度超限 / 充IC过温 / 恢复 / 拔插）
       ═══════════════════════════════════════════════════════════ */

    /** 事件列表（不含 snapshot，轻量；最多 20 条，新的在前） */
    suspend fun fetchEvents(): List<BatteryEvent> = withContext(Dispatchers.IO) {
        try {
            val txt = getText("/api/events") ?: return@withContext emptyList()
            val root = JSONObject(txt)
            val arr = root.optJSONArray("events") ?: return@withContext emptyList()
            buildList {
                for (i in 0 until arr.length()) {
                    val o = arr.optJSONObject(i) ?: continue
                    add(parseEvent(o))
                }
            }
        } catch (e: Throwable) {
            Log.w(TAG, "fetchEvents failed: ${e.message}")
            emptyList()
        }
    }

    /** 单条事件（含 snapshot 曲线快照） */
    suspend fun fetchEvent(id: String): BatteryEvent? = withContext(Dispatchers.IO) {
        try {
            val enc = URLEncoder.encode(id, "UTF-8")
            val txt = getText("/api/event?id=$enc") ?: return@withContext null
            val root = JSONObject(txt)
            if (!root.optBoolean("ok", true) && root.has("err")) return@withContext null
            parseEvent(root, withSnapshot = true)
        } catch (e: Throwable) {
            Log.w(TAG, "fetchEvent failed: ${e.message}")
            null
        }
    }

    /** 清空全部事件 */
    suspend fun clearEvents(): Boolean = withContext(Dispatchers.IO) {
        try {
            val conn = (URL("$BASE/api/events").openConnection() as HttpURLConnection).apply {
                connectTimeout = CONNECT_TIMEOUT; readTimeout = READ_TIMEOUT
                requestMethod = "DELETE"
            }
            val ok = conn.responseCode == 200
            conn.disconnect()
            ok
        } catch (e: Throwable) {
            Log.w(TAG, "clearEvents failed: ${e.message}")
            false
        }
    }

    private fun parseEvent(o: JSONObject, withSnapshot: Boolean = false): BatteryEvent {
        val detailObj = o.optJSONObject("detail")
        val detail = mutableMapOf<String, Any>()
        if (detailObj != null) {
            detailObj.keys().forEach { k ->
                val v = detailObj.opt(k)
                if (v != null) detail[k] = v
            }
        }
        val snap = mutableListOf<EventSnapshotPoint>()
        if (withSnapshot) {
            val arr = o.optJSONArray("snapshot")
            if (arr != null) {
                for (i in 0 until arr.length()) {
                    val p = arr.optJSONObject(i) ?: continue
                    snap.add(
                        EventSnapshotPoint(
                            t = p.optLong("t", 0L),
                            soc = p.optInt("soc", -1),
                            temp = p.optDouble("temp", 0.0),
                            cur = p.optInt("cur", 0)
                        )
                    )
                }
            }
        }
        return BatteryEvent(
            id = o.optString("id", ""),
            type = o.optString("type", ""),
            at = o.optLong("at", 0L),
            detail = detail,
            snapshot = snap
        )
    }
}
