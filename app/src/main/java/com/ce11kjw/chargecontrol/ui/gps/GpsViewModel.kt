package com.ce11kjw.chargecontrol.ui.gps

import androidx.lifecycle.LiveData
import androidx.lifecycle.ViewModel
import com.ce11kjw.chargecontrol.data.model.GpsStatusInfo
import com.ce11kjw.chargecontrol.data.repository.DeviceRepository

class GpsViewModel(private val repo: DeviceRepository) : ViewModel() {
    val gpsInfo: LiveData<GpsStatusInfo> get() = repo.gpsLiveData
}
