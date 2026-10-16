package com.ce11kjw.chargecontrol.data.model

data class HistoryDataPoint(
    var timestampMillis: Long = 0L,
    var value: Float = 0f,
    var seriesName: String? = null
)
