package com.ce11kjw.chargecontrol.data.model

/**
 * v1.17.0 异常事件（模块侧 /api/events）
 *
 * 事件类型：
 *   temp_exceed      电池过温
 *   chg_temp_exceed  充电IC过温
 *   recovered        已恢复充电
 *   replug_reset     重新插电（清空停充状态）
 */
data class BatteryEvent(
    val id: String = "",
    val type: String = "",
    val at: Long = 0L,
    val detail: Map<String, Any> = emptyMap(),
    /** 仅单条查询时返回；列表接口不含此字段（省流量） */
    val snapshot: List<EventSnapshotPoint> = emptyList()
) {
    /** 中文类型名 */
    val typeText: String
        get() = when (type) {
            "temp_exceed" -> "电池过温"
            "chg_temp_exceed" -> "充电IC过温"
            "recovered" -> "已恢复充电"
            "replug_reset" -> "重新插电"
            else -> type
        }

    /** 详情描述，如 "46.0°C（上限 45°C）" */
    val detailText: String
        get() {
            fun d(k: String): Double = (detail[k] as? Number)?.toDouble() ?: 0.0
            fun i(k: String): Int = (detail[k] as? Number)?.toInt() ?: 0
            return when (type) {
                "temp_exceed" -> "${d("temp")}°C（上限 ${i("limit")}°C）"
                "chg_temp_exceed" -> "${d("chg_temp")}°C（上限 ${i("limit")}°C）"
                "recovered" -> "原因「${reasonText(detail["prev_reason"] as? String)}」持续 ${i("duration_s")} 秒"
                "replug_reset" -> "原停充原因「${reasonText(detail["prev_reason"] as? String)}」"
                else -> ""
            }
        }

    private fun reasonText(r: String?): String = when (r) {
        "temp" -> "电池过温"
        "chg_temp" -> "充电IC过温"
        "limit" -> "已达充电上限"
        "paused" -> "手动暂停"
        "full" -> "手动充满中"
        "bypass" -> "软旁路"
        "manual" -> "手动停充"
        else -> r ?: "无"
    }
}

/** 事件快照单点（10 秒一个点，覆盖触发前 5 分钟） */
data class EventSnapshotPoint(
    /** 相对触发时刻的秒数（负数=触发前） */
    val t: Long = 0L,
    val soc: Int = -1,
    val temp: Double = 0.0,
    val cur: Int = 0
)
