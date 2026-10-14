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
}
