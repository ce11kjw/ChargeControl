package com.ce11kjw.chargecontrol.ui.oem

import androidx.lifecycle.LiveData
import androidx.lifecycle.ViewModel
import com.ce11kjw.chargecontrol.data.model.OemInfo
import com.ce11kjw.chargecontrol.data.repository.DeviceRepository

class OemViewModel(private val repo: DeviceRepository) : ViewModel() {
    val oemInfo: LiveData<OemInfo> get() = repo.oemLiveData
}
