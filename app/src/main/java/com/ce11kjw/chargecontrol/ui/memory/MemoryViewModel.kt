package com.ce11kjw.chargecontrol.ui.memory

import androidx.lifecycle.ViewModel
import com.ce11kjw.chargecontrol.data.repository.DeviceRepository

class MemoryViewModel(
    private val repo: DeviceRepository
) : ViewModel() {

    val memoryInfo get() = repo.memoryLiveData
    val historyData get() = repo.historyData
}
