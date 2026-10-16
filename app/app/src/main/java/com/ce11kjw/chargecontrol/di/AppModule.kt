package com.ce11kjw.chargecontrol.di

import android.content.Context
import com.ce11kjw.chargecontrol.AppSettings
import com.ce11kjw.chargecontrol.data.repository.DeviceRepository
import com.ce11kjw.chargecontrol.data.source.StepCounterStore
import com.ce11kjw.chargecontrol.ui.AppViewModel
import com.ce11kjw.chargecontrol.ui.battery.BatteryViewModel
import com.ce11kjw.chargecontrol.ui.cpu.CpuViewModel
import com.ce11kjw.chargecontrol.ui.dashboard.DashboardViewModel
import com.ce11kjw.chargecontrol.ui.gpu.GpuViewModel
import com.ce11kjw.chargecontrol.ui.memory.MemoryViewModel
import com.ce11kjw.chargecontrol.ui.oem.OemViewModel
import com.ce11kjw.chargecontrol.ui.settings.SettingsViewModel
import org.koin.android.ext.koin.androidContext
import org.koin.androidx.viewmodel.dsl.viewModel
import org.koin.dsl.module

val appModule = module {
    single { DeviceRepository(androidContext()) }
    single { AppSettings.getInstance(androidContext()) }
    single {
        StepCounterStore.fromPrefs(
            androidContext().getSharedPreferences("step_counter", Context.MODE_PRIVATE)
        )
    }

    viewModel { AppViewModel(get()) }
    viewModel { DashboardViewModel(get()) }
    viewModel { CpuViewModel(get()) }
    viewModel { GpuViewModel(get()) }
    viewModel { MemoryViewModel(get()) }
    viewModel { BatteryViewModel(get()) }
    viewModel { OemViewModel(get()) }
    viewModel { SettingsViewModel(get()) }
}
