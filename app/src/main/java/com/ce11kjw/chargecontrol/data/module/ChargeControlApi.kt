package com.ce11kjw.chargecontrol.data.module

import android.util.Log
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
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
            JSONObject(text)
        } catch (e: Throwable) {
            Log.w(TAG, "fetchStatus failed: ${e.message}")
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
}
