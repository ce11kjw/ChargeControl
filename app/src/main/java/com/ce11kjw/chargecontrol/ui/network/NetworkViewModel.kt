package com.ce11kjw.chargecontrol.ui.network

import androidx.lifecycle.LiveData
import androidx.lifecycle.ViewModel
import com.ce11kjw.chargecontrol.data.model.MobileNetworkInfo
import com.ce11kjw.chargecontrol.data.model.WifiDetailInfo
import com.ce11kjw.chargecontrol.data.repository.DeviceRepository

class NetworkViewModel(
    private val repo: DeviceRepository
) : ViewModel() {
    val wifiInfo: LiveData<WifiDetailInfo> get() = repo.wifiLiveData
    val mobileNetworkInfo: LiveData<MobileNetworkInfo> get() = repo.mobileNetworkLiveData
    val historyData get() = repo.historyData
}
