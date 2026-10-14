package com.ce11kjw.chargecontrol.ui.sensors

import androidx.lifecycle.LiveData
import androidx.lifecycle.ViewModel
import com.ce11kjw.chargecontrol.data.model.SensorItemInfo
import com.ce11kjw.chargecontrol.data.repository.DeviceRepository

class SensorsViewModel(private val repo: DeviceRepository) : ViewModel() {
    val sensors: LiveData<List<SensorItemInfo>> get() = repo.sensorsLiveData

    /** ACTIVITY_RECOGNITION 授权后重采传感器列表 (API 29+ 步数传感器按需回填) */
    fun refreshSensors() = repo.refreshSensors()
}
