package com.ce11kjw.chargecontrol.service

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import com.ce11kjw.chargecontrol.MainActivity
import com.ce11kjw.chargecontrol.data.module.ChargeControlApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import org.json.JSONObject

/**
 * v1.17.0 电池告警服务（替代原 TG 推送）
 *
 * 设计取舍（用户 2026-10-24 拍板）：
 *   - App 有实时曲线 + 本地通知，TG 推送重复且多一条故障链路 → 全砍
 *   - 四个通道：chrgc(保养) / chrgw(异常) / chrgp(阈值) / chrgr(恢复)
 *   - 恢复类走低优先级 + 同原因 10 分钟内只提示一次
 *   - 停充后自动恢复由模块状态机负责，App 只负责"告知用户已恢复"
 *
 * 轮询：5 秒（模块 /api/status）
 */
class BatteryAlertService : Service() {

    companion object {
        private const val TAG = "BatteryAlert"

        const val CH_MAINTENANCE = "chrgc"   // 电池保养
        const val CH_WARNING = "chrgw"       // 充电异常
        const val CH_PERCENT = "chrgp"       // 到 X% 提醒
        const val CH_RECOVER = "chrgr"       // 恢复通知
        private const val CH_ONGOING = "chrg_service"  // 前台服务常驻

        private const val NOTIFY_ID_ONGOING = 100
        private const val NOTIFY_ID_MAINT = 101
        private const val NOTIFY_ID_WARN = 102
        private const val NOTIFY_ID_PCT = 103
        private const val NOTIFY_ID_RECOVER = 104

        private const val POLL_MS = 5_000L
        private const val DEDUP_MS = 10 * 60 * 1000L   // 恢复通知去重 10 分钟

        const val ACTION_START = "start"
        const val ACTION_STOP = "stop"

        fun start(ctx: Context) {
            val i = Intent(ctx, BatteryAlertService::class.java).setAction(ACTION_START)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) ctx.startForegroundService(i)
            else ctx.startService(i)
        }

        fun stop(ctx: Context) {
            ctx.startService(Intent(ctx, BatteryAlertService::class.java).setAction(ACTION_STOP))
        }
    }

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var pollJob: Job? = null
    private lateinit var nm: NotificationManager

    /* ── 上一轮状态（用于检测变化） ── */
    private var prevSuspendActive = false
    private var prevSuspendReason = "none"
    private var prevCapacity = -1
    private var prevCharging = false

    /* ── 去重时间戳 ── */
    private var lastRecoverNotifyAt = 0L
    private var lastRecoverReason = ""
    private var lastMaintenanceAt = 0L
    private val firedPercents = mutableSetOf<Int>()

    /* ── 用户配置（从 SharedPreferences 读） ── */
    private var maintenanceLimit = 80
    private var alertMaintenance = true
    private var alertWarning = true
    private var alertPercent = true
    private var alertRecover = true

    override fun onCreate() {
        super.onCreate()
        nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        createChannels()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_STOP -> {
                stopSelf()
                return START_NOT_STICKY
            }
            else -> {
                startForeground(NOTIFY_ID_ONGOING, buildOngoing())
                loadPrefs()
                if (pollJob == null) {
                    pollJob = scope.launch { pollLoop() }
                }
            }
        }
        return START_STICKY
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onDestroy() {
        pollJob?.cancel()
        scope.cancel()
        super.onDestroy()
    }

    // ───────────────────────────────────────────── 通道

    private fun createChannels() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return
        fun ch(id: String, name: String, importance: Int, desc: String) {
            if (nm.getNotificationChannel(id) != null) return
            nm.createNotificationChannel(
                NotificationChannel(id, name, importance).apply { description = desc }
            )
        }
        ch(CH_ONGOING, "电池守护", NotificationManager.IMPORTANCE_MIN, "常驻通知，表示守护服务运行中")
        ch(CH_MAINTENANCE, "电池保养", NotificationManager.IMPORTANCE_DEFAULT, "到达充电上限时提醒")
        ch(CH_WARNING, "充电异常", NotificationManager.IMPORTANCE_HIGH, "电池/充电IC 过温等异常")
        ch(CH_PERCENT, "电量提醒", NotificationManager.IMPORTANCE_DEFAULT, "到达指定电量时提醒")
        ch(CH_RECOVER, "恢复通知", NotificationManager.IMPORTANCE_LOW, "停充后自动恢复充电")
    }

    // ───────────────────────────────────────────── 配置

    private fun loadPrefs() {
        val sp = getSharedPreferences("charge_alert", Context.MODE_PRIVATE)
        maintenanceLimit = sp.getInt("limit", 80)
        alertMaintenance = sp.getBoolean("alert_maintenance", true)
        alertWarning = sp.getBoolean("alert_warning", true)
        alertPercent = sp.getBoolean("alert_percent", true)
        alertRecover = sp.getBoolean("alert_recover", true)
        sp.getInt("percent_threshold", 0).takeIf { it > 0 }?.let { firedPercents.add(-it) }
    }

    // ───────────────────────────────────────────── 主循环

    private suspend fun pollLoop() {
        while (scope.isActive) {
            try {
                val st = ChargeControlApi.fetchStatus()
                if (st != null) handleStatus(st)
            } catch (e: Throwable) {
                Log.w(TAG, "poll: ${e.message}")
            }
            delay(POLL_MS)
        }
    }

    private fun handleStatus(st: JSONObject) {
        val capacity = st.optInt("capacity", -1)
        val charging = st.optString("status", "").contains("Charging", true)
        val suspendActive = st.optInt("suspend_active", 0) == 1
        val suspendReason = st.optString("suspend_reason", "none")
        val temp = st.optDouble("temp", 0.0) / 10.0     // 模块发的是 x10
        val chgTemp = st.optDouble("chg_temp", 0.0)
        val limit = st.optInt("charge_limit", maintenanceLimit)
        val recIn = st.optInt("suspend_recover_in", -1)

        /* 1) 到达上限 → 保养提醒 */
        if (alertMaintenance && limit > 0 && capacity >= limit &&
            (prevCapacity < limit || (!prevSuspendActive && suspendActive && suspendReason == "limit"))
        ) {
            val now = System.currentTimeMillis()
            if (now - lastMaintenanceAt > 60_000) {   // 1 分钟去重（防止阈值抖动）
                lastMaintenanceAt = now
                notify(CH_MAINTENANCE, NOTIFY_ID_MAINT, "电池保养",
                    "已充至 $capacity%，建议拔掉充电器", high = false)
            }
        }

        /* 2) 停充状态切换 → 异常/恢复通知 */
        if (suspendActive && !prevSuspendActive) {
            if (alertWarning) {
                val (title, text) = when (suspendReason) {
                    "temp" -> "电池过温" to "电池温度 ${"%.1f".format(temp)}°C，已停止充电"
                    "chg_temp" -> "充电IC过温" to "充电IC ${"%.1f".format(chgTemp)}°C，已停止充电"
                    "limit" -> "已达充电上限" to "电量 $capacity%，已停止充电"
                    "paused" -> "手动暂停" to "已暂停充电"
                    else -> null to null
                }
                if (title != null && text != null) {
                    // 异常类：带上最近事件 id，点击可看曲线快照
                    scope.launch { attachAndNotify(title, text, suspendReason) }
                }
            }
        } else if (!suspendActive && prevSuspendActive) {
            /* 恢复：同原因 10 分钟内只提示一次 */
            if (alertRecover) {
                val now = System.currentTimeMillis()
                val same = (prevSuspendReason == lastRecoverReason) && (now - lastRecoverNotifyAt < DEDUP_MS)
                if (!same) {
                    lastRecoverNotifyAt = now
                    lastRecoverReason = prevSuspendReason
                    val why = when (prevSuspendReason) {
                        "temp" -> "温度已回落"
                        "chg_temp" -> "充电IC 温度已回落"
                        "limit" -> "电量已低于上限"
                        "paused" -> "已恢复"
                        else -> "条件已满足"
                    }
                    notify(CH_RECOVER, NOTIFY_ID_RECOVER, "已恢复充电",
                        "$why，充电已自动继续", high = false)
                }
            }
        }

        /* 3) 到 X% 提醒（每个阈值只响一次，拔电重置） */
        if (alertPercent && capacity > 0) {
            val sp = getSharedPreferences("charge_alert", Context.MODE_PRIVATE)
            val threshold = sp.getInt("percent_threshold", 0)
            if (threshold in 1..100 && capacity >= threshold && !firedPercents.contains(threshold)) {
                firedPercents.add(threshold)
                notify(CH_PERCENT, NOTIFY_ID_PCT, "电量提醒",
                    "已充至 $capacity%（设定 $threshold%）", high = false)
            }
            if (!charging && capacity < threshold - 5) firedPercents.remove(threshold)
        }

        prevCapacity = capacity
        prevCharging = charging
        prevSuspendActive = suspendActive
        prevSuspendReason = suspendReason

        updateOngoing(capacity, suspendActive, suspendReason, recIn)
    }

    /** 异常通知：拉最近一条事件，绑定 id 供点击跳曲线 */
    private suspend fun attachAndNotify(title: String, text: String, reason: String) {
        var eventId = ""
        try {
            val list = ChargeControlApi.fetchEvents()
            eventId = list.firstOrNull()?.id ?: ""
        } catch (_: Throwable) { }
        notify(CH_WARNING, NOTIFY_ID_WARN, title, text, high = true, eventId = eventId)
    }

    // ───────────────────────────────────────────── 通知

    private fun contentIntent(eventId: String = ""): PendingIntent {
        val i = Intent(this, MainActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_SINGLE_TOP or Intent.FLAG_ACTIVITY_CLEAR_TOP
            if (eventId.isNotEmpty()) putExtra("event_id", eventId)
        }
        val flags = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M)
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
        else PendingIntent.FLAG_UPDATE_CURRENT
        return PendingIntent.getActivity(this, eventId.hashCode(), i, flags)
    }

    private fun notify(
        channel: String, id: Int, title: String, text: String,
        high: Boolean, eventId: String = ""
    ) {
        val n = NotificationCompat.Builder(this, channel)
            .setSmallIcon(android.R.drawable.ic_dialog_info)
            .setContentTitle(title)
            .setContentText(text)
            .setStyle(NotificationCompat.BigTextStyle().bigText(text))
            .setPriority(if (high) NotificationCompat.PRIORITY_HIGH else NotificationCompat.PRIORITY_DEFAULT)
            .setAutoCancel(true)
            .setContentIntent(contentIntent(eventId))
            .build()
        runCatching { nm.notify(id, n) }
    }

    private fun buildOngoing(): Notification =
        NotificationCompat.Builder(this, CH_ONGOING)
            .setSmallIcon(android.R.drawable.ic_lock_idle_charging)
            .setContentTitle("电池守护中")
            .setContentText("监控充电状态")
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_MIN)
            .setContentIntent(contentIntent())
            .build()

    private fun updateOngoing(capacity: Int, suspend: Boolean, reason: String, recIn: Int) {
        val txt = when {
            suspend && recIn > 0 -> "已停充 · ${reasonText(reason)} · ${recIn}s 后恢复"
            suspend -> "已停充 · ${reasonText(reason)}"
            capacity >= 0 -> "充电中 $capacity%"
            else -> "监控中"
        }
        val n = NotificationCompat.Builder(this, CH_ONGOING)
            .setSmallIcon(android.R.drawable.ic_lock_idle_charging)
            .setContentTitle("电池守护中")
            .setContentText(txt)
            .setOngoing(true)
            .setPriority(NotificationCompat.PRIORITY_MIN)
            .setContentIntent(contentIntent())
            .build()
        runCatching { nm.notify(NOTIFY_ID_ONGOING, n) }
    }

    private fun reasonText(r: String): String = when (r) {
        "temp" -> "电池过温"
        "chg_temp" -> "充电IC过温"
        "limit" -> "已达上限"
        "paused" -> "手动暂停"
        "full" -> "手动充满"
        "bypass" -> "软旁路"
        else -> r
    }
}
