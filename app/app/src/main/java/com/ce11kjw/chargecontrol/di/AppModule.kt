package com.ce11kjw.chargecontrol.di

import com.ce11kjw.chargecontrol.AppSettings
import com.ce11kjw.chargecontrol.data.repository.DeviceRepository
import com.ce11kjw.chargecontrol.ui.AppViewModel
import com.ce11kjw.chargecontrol.ui.battery.BatteryViewModel
import com.ce11kjw.chargecontrol.ui.settings.SettingsViewModel
import org.koin.android.ext.koin.androidContext
import org.koin.androidx.viewmodel.dsl.viewModel
import org.koin.dsl.module

val appModule = module {
    single { DeviceRepository(androidContext()) }
    single { AppSettings.getInstance(androidContext()) }

    viewModel { AppViewModel(get()) }
    viewModel { BatteryViewModel(get()) }
    viewModel { SettingsViewModel(get()) }
}
